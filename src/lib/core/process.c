#define _POSIX_C_SOURCE 200112L

#include <poser/core/process.h>

#include "connection.h"

#include <poser/core/event.h>
#include <poser/core/log.h>
#include <poser/core/service.h>
#include <poser/core/timer.h>
#include <poser/core/util.h>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

typedef enum ChildStatus
{
    CS_NONE,
    CS_RUNNING,
    CS_EXITED,
    CS_SIGNALED
} ChildStatus;

struct PSC_EAProcessDone
{
    int exitval;
    ChildStatus status;
};

struct PSC_ProcessOpts
{
    const char **args;
    int argc;
    int argscapa;
    int execError;
    PSC_StreamAction actions[3];
};

struct PSC_Process
{
    PSC_Event *done;
    PSC_Timer *killtimer;
    PSC_Connection *streams[3];
    int execError;
    int argc;
    int thrno;
    pid_t pid;
    PSC_EAProcessDone doneArgs;
    PSC_StreamAction actions[3];
    char *args[];
};

SOEXPORT PSC_ProcessOpts *PSC_ProcessOpts_create(void)
{
    PSC_ProcessOpts *self = PSC_malloc(sizeof *self);
    memset(self, 0, sizeof *self);
    self->argc = 0;
    self->execError = PSC_ERR_EXEC;
    return self;
}

static void addArg(PSC_ProcessOpts *self, const char *arg)
{
    if (self->argc == self->argscapa)
    {
	self->argscapa += 16;
	self->args = PSC_realloc(self->args, self->argscapa *
		sizeof *self->args);
    }
    self->args[self->argc++] = arg;
}

SOEXPORT void PSC_ProcessOpts_setName(PSC_ProcessOpts *self, const char *name)
{
    if (self->argc == 0) addArg(self, name);
    else self->args[0] = name;
}

SOEXPORT void PSC_ProcessOpts_addArg(PSC_ProcessOpts *self, const char *arg)
{
    if (self->argc == 0) addArg(self, 0);
    addArg(self, arg);
}

SOEXPORT int PSC_ProcessOpts_streamAction(PSC_ProcessOpts *self,
	PSC_StreamType stream, PSC_StreamAction action)
{
    if (stream < PSC_ST_STDIN || stream > PSC_ST_STDERR) return -1;
    if (action < PSC_SA_LEAVE || action > PSC_SA_PIPE) return -1;
    self->actions[stream] = action;
    return 0;
}

SOEXPORT int PSC_ProcessOpts_setExecError(PSC_ProcessOpts *self,
	int execError)
{
    if (execError < -128 || execError > 127) return -1;
    self->execError = execError;
    return 0;
}

SOEXPORT void PSC_ProcessOpts_destroy(PSC_ProcessOpts *self)
{
    if (!self) return;
    free(self->args);
    free(self);
}

SOEXPORT PSC_Process *PSC_Process_create(const PSC_ProcessOpts *opts)
{
    int argc = opts->argc;
    if (argc == 0) argc = 1;
    PSC_Process *self = PSC_malloc(sizeof *self +
	    (argc + 1) * sizeof *self->args);
    memset(self, 0, sizeof *self);
    self->done = PSC_Event_create(self);
    self->execError = opts->execError;
    self->argc = argc;
    memcpy(self->actions, opts->actions, sizeof self->actions);
    if (opts->argc == 0) self->args[0] = 0;
    else for (int i = 0; i < argc; ++i)
    {
	self->args[i] = opts->args[i] ? PSC_copystr(opts->args[i]) : 0;
    }
    self->args[argc] = 0;
    return self;
}

static int createPipe(int *fds, int check)
{
    if (pipe(fds) < 0)
    {
	PSC_Log_err(PSC_L_ERROR, "process: Error creating pipe");
	return -1;
    }
    if (!PSC_Service_isValidFd(fds[check], "process")) return -1;
    fcntl(fds[check], F_SETFL, fcntl(fds[check], F_GETFL) | O_NONBLOCK);
    return 0;
}

static const int stdfds[] = { STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO };
static const int stdfddir[] = { 1, 0, 0 };

static void prepareChild(const PSC_StreamAction *actions, int (*pipefd)[2],
	int execError)
{
    int nullfd = -1;

    for (int i = PSC_ST_STDIN; i <= PSC_ST_STDERR; ++i)
    {
	switch (actions[i])
	{
	    case PSC_SA_CLOSE:
		close(stdfds[i]);
		break;

	    case PSC_SA_NULL:
		if (nullfd < 0 && (nullfd = open("/dev/null", O_RDWR) < 0))
		{
		    goto fail;
		}
		if (dup2(nullfd, stdfds[i]) < 0) goto fail;
		break;

	    case PSC_SA_PIPE:
		if (dup2(pipefd[i][!stdfddir[i]], stdfds[i]) < 0) goto fail;
		close(pipefd[i][stdfddir[i]]);
		break;

	    default:
		break;
	}
    }
    if (nullfd >= 0) close(nullfd);

    sigset_t allsigs;
    sigfillset(&allsigs);
    sigprocmask(SIG_UNBLOCK, &allsigs, 0);

    return;

fail:
    exit(execError);
}

static void tryDestroy(PSC_Process *self)
{
    if (self->doneArgs.status == CS_RUNNING) return;
    if (self->streams[PSC_ST_STDIN]) return;
    if (self->streams[PSC_ST_STDOUT]) return;
    if (self->streams[PSC_ST_STDERR]) return;

    PSC_Event_raise(self->done, 0, &self->doneArgs);

    for (int i = 0; i < self->argc; ++i) free(self->args[i]);
    PSC_Event_destroy(self->done);
    free(self);
}

static void streamClosed(void *receiver, void *sender, void *args)
{
    (void)args;

    PSC_Process *self = receiver;
    PSC_Connection *conn = sender;

    for (int i = PSC_ST_STDIN; i <= PSC_ST_STDERR; ++i)
    {
	if (self->streams[i] == conn) self->streams[i] = 0;
    }

    tryDestroy(self);
}

struct endprocargs
{
    PSC_Process *proc;
    pid_t pid;
    int signo;
    int status;
};

static void endprocess(void *arg)
{
    struct endprocargs *epa = arg;

    if (epa->pid != epa->proc->pid) goto done;

    PSC_Timer_destroy(epa->proc->killtimer);
    epa->proc->killtimer = 0;

    if (epa->proc->streams[PSC_ST_STDIN])
    {
	PSC_Connection_close(epa->proc->streams[PSC_ST_STDIN], 0);
    }

    if (epa->signo)
    {
	epa->proc->doneArgs.status = CS_SIGNALED;
	epa->proc->doneArgs.exitval = epa->signo;
    }
    else
    {
	epa->proc->doneArgs.status = CS_EXITED;
	epa->proc->doneArgs.exitval = epa->status;
    }

    tryDestroy(epa->proc);
done:
    free(epa);
}

static void childExited(void *receiver, void *sender, void *args)
{
    (void)sender;

    PSC_EAChildExited *ea = args;
    struct endprocargs *epa = PSC_malloc(sizeof *epa);
    epa->proc = receiver;
    epa->pid = PSC_EAChildExited_pid(ea);
    epa->signo = PSC_EAChildExited_signal(ea);
    epa->status = PSC_EAChildExited_status(ea);

    PSC_Service_runOnThread(epa->proc->thrno, endprocess, epa);
}

static int runprocess(PSC_Process *self,
	void *obj, PSC_StreamCallback cb,
	const char *path, PSC_ProcessMain main)
{
    if (self->pid) return -1;
    self->thrno = PSC_Service_threadNo();

    int rc = -1;
    int pipefd[][2] = {{-1, -1}, {-1, -1}, {-1, -1}};

    for (int i = PSC_ST_STDIN; i <= PSC_ST_STDERR; ++i)
    {
	if (self->actions[i] == PSC_SA_PIPE
		&& (!cb || createPipe(pipefd[i], stdfddir[i]) < 0)) goto done;
    }

    PSC_Service_lockChildren();
    pid_t pid = fork();
    if (pid < 0)
    {
	PSC_Log_err(PSC_L_ERROR, "process: cannot fork()");
	PSC_Service_unlockChildren();
	goto done;
    }

    if (pid == 0)
    {
	prepareChild(self->actions, pipefd, self->execError);
	if (path)
	{
	    if (self->args[0] == 0) self->args[0] = (char *)path;
	    execv(path, self->args);
	    exit(self->execError);
	}
	else
	{
	    exit(main(self->argc, self->args));
	}
    }

    self->pid = pid;
    self->doneArgs.status = CS_RUNNING;
    PSC_Event_register(PSC_Service_childExited(), self, childExited, pid);
    PSC_Service_unlockChildren();

    ConnOpts opts;
    memset(&opts, 0, sizeof opts);
    opts.rdbufsz = DEFRDBUFSZ;

    for (int i = PSC_ST_STDIN; i <= PSC_ST_STDERR; ++i)
    {
	if (self->actions[i] == PSC_SA_PIPE)
	{
	    opts.createmode = stdfddir[i] ? CCM_PIPEWR : CCM_PIPERD;
	    self->streams[i] = PSC_Connection_create(
		    pipefd[i][stdfddir[i]], &opts);
	    pipefd[i][stdfddir[i]] = -1;
	    PSC_Event_register(PSC_Connection_closed(self->streams[i]),
		    self, streamClosed, 0);
	    cb(obj, i, self->streams[i]);
	}
    }

    rc = 0;

done:
    for (int i = PSC_ST_STDIN; i <= PSC_ST_STDERR; ++i)
    {
	if (pipefd[i][0] >= 0) close(pipefd[i][0]);
	if (pipefd[i][1] >= 0) close(pipefd[i][1]);
    }
    return rc;
}

SOEXPORT int PSC_Process_exec(PSC_Process *self, void *obj,
	PSC_StreamCallback cb, const char *path)
{
    return runprocess(self, obj, cb, path, 0);
}

SOEXPORT int PSC_Process_run(PSC_Process *self, void *obj,
	PSC_StreamCallback cb, PSC_ProcessMain main)
{
    return runprocess(self, obj, cb, 0, main);
}

static void forcekill(void *receiver, void *sender, void *args)
{
    (void)sender;
    (void)args;

    PSC_Process *self = receiver;
    kill(self->pid, SIGKILL);
}

SOEXPORT int PSC_Process_stop(PSC_Process *self, unsigned forceMs)
{
    if (self->doneArgs.status != CS_RUNNING || self->killtimer) return -1;
    if (kill(self->pid, SIGTERM) < 0) return -1;
    if (forceMs)
    {
	self->killtimer = PSC_Timer_create();
	if (!self->killtimer)
	{
	    PSC_Log_msg(PSC_L_ERROR, "process: cannot create timer for "
		    "force-stopping the child process");
	    return -1;
	}
	PSC_Timer_setMs(self->killtimer, forceMs);
	PSC_Event_register(PSC_Timer_expired(self->killtimer), self,
		forcekill, 0);
	PSC_Timer_start(self->killtimer, 0);
    }
    return 0;
}

SOEXPORT PSC_Event *PSC_Process_done(PSC_Process *self)
{
    return self->done;
}

SOEXPORT pid_t PSC_Process_pid(const PSC_Process *self)
{
    return self->doneArgs.status == CS_RUNNING ? self->pid : (pid_t)-1;
}

SOEXPORT int PSC_EAProcessDone_status(const PSC_EAProcessDone *self)
{
    if (self->status != CS_EXITED) return PSC_CHILD_SIGNALED;
    return self->exitval;
}

SOEXPORT int PSC_EAProcessDone_signal(const PSC_EAProcessDone *self)
{
    if (self->status != CS_SIGNALED) return 0;
    return self->exitval;
}

