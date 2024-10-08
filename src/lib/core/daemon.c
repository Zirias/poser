#define _DEFAULT_SOURCE

#include "runopts.h"

#include <poser/core/daemon.h>
#include <poser/core/log.h>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static FILE *openpidfile(const char *pidfile) ATTR_NONNULL((1));
static int waitpflock(FILE *pf, const char *pidfile)
    ATTR_NONNULL((1)) ATTR_NONNULL((2));

static FILE *openpidfile(const char *pidfile)
{
    struct flock pflock;
    pid_t pid;

    FILE *pf = fopen(pidfile, "r");
    if (pf)
    {
	memset(&pflock, 0, sizeof pflock);
	pflock.l_type = F_RDLCK;
	pflock.l_whence = SEEK_SET;
	if (fcntl(fileno(pf), F_GETLK, &pflock) < 0)
	{
	    PSC_Log_fmt(PSC_L_ERROR, "error getting lock info on `%s'",
		    pidfile);
	    return 0;
	}
	int locked = (pflock.l_type != F_UNLCK);
	if (!locked)
	{
	    struct flock pfrdlock;
	    memset(&pfrdlock, 0, sizeof pfrdlock);
	    pfrdlock.l_type = F_RDLCK;
	    pfrdlock.l_whence = SEEK_SET;
	    if (fcntl(fileno(pf), F_SETLK, &pfrdlock) < 0)
	    {
		locked = 1;
		memset(&pflock, 0, sizeof pflock);
		pflock.l_type = F_RDLCK;
		pflock.l_whence = SEEK_SET;
		fcntl(fileno(pf), F_GETLK, &pflock);
	    }
	    else
	    {
		PSC_Log_fmt(PSC_L_WARNING, "removing stale pidfile `%s'",
			pidfile);
		if (unlink(pidfile) < 0)
		{
		    PSC_Log_fmt(PSC_L_ERROR, "cannot remove `%s'", pidfile);
		    return 0;
		}
		fclose(pf);
	    }
	}
	if (locked)
	{
	    int prc = fscanf(pf, "%d", &pid);
	    fclose(pf);
	    if (prc < 1 || pid != pflock.l_pid)
	    {
		if (prc < 1) pid = -1;
		PSC_Log_fmt(PSC_L_ERROR, "pidfile `%s' content (pid %d) and "
			"lock owner (pid %d) disagree! This should never "
			"happen, giving up!", pidfile, pid, pflock.l_pid);
	    }
	    else
	    {
		PSC_Log_fmt(PSC_L_ERROR, "daemon already running with pid %d",
			pid);
	    }
	    return 0;
	}
    }
    pf = fopen(pidfile, "w");
    if (!pf)
    {
	PSC_Log_fmt(PSC_L_ERROR, "cannot open pidfile `%s' for writing",
		pidfile);
	return 0;
    }
    memset(&pflock, 0, sizeof pflock);
    pflock.l_type = F_WRLCK;
    pflock.l_whence = SEEK_SET;
    if (fcntl(fileno(pf), F_SETLK, &pflock) < 0)
    {
	fclose(pf);
	PSC_Log_fmt(PSC_L_ERROR, "locking own pidfile `%s' failed", pidfile);
	return 0;
    }

    return pf;
}

static int waitpflock(FILE *pf, const char *pidfile)
{
    struct flock pflock;
    int lrc;

    memset(&pflock, 0, sizeof pflock);
    pflock.l_type = F_WRLCK;
    pflock.l_whence = SEEK_SET;
    do
    {
	errno = 0;
	lrc = fcntl(fileno(pf), F_SETLKW, &pflock);
    } while (lrc < 0 && errno == EINTR);
    if (lrc < 0)
    {
	PSC_Log_fmt(PSC_L_ERROR, "locking own pidfile `%s' failed", pidfile);
	return -1;
    }
    return 0;
}

SOEXPORT int PSC_Daemon_run(PSC_Daemon_main dmain, void *data)
{
    pid_t pid, sid;
    int rc = EXIT_FAILURE;
    FILE *pf = 0;

    PSC_RunOpts *opts = runOpts();
    if (!opts->daemonize) return dmain(data);

    if (opts->pidfile && !(pf = openpidfile(opts->pidfile))) goto done;

    int pfd[2];
    if (!opts->waitLaunched || pipe(pfd) < 0)
    {
	pfd[0] = -1;
	pfd[1] = -1;
    }

    pid = fork();

    if (pid < 0)
    {
	if (pfd[0] >= 0) close(pfd[0]);
	if (pfd[1] >= 0) close(pfd[1]);
	PSC_Log_msg(PSC_L_ERROR, "failed to fork (1)");
	goto done;
    }

    if (pid > 0)
    {
	if (pfd[1] >= 0) close(pfd[1]);
	if (pf) fclose(pf);
	int drc = EXIT_SUCCESS;
	if (pfd[0] >= 0)
	{
	    char buf[256];
	    ssize_t sz;
	    ssize_t wrc;
	    size_t bp;
	    while ((sz = read(pfd[0], buf, sizeof buf)) > 0)
	    {
		bp = 0;
		if (!buf[sz-1])
		{
		    drc = EXIT_FAILURE;
		    if (sz > 1) do
		    {
			wrc = write(STDERR_FILENO, buf+bp, sz-bp-1);
		    } while (wrc >= 0 && (bp += wrc) < (size_t)sz - 1);
		    break;
		}
		do
		{
		    wrc = write(STDERR_FILENO, buf+bp, sz-bp);
		} while (wrc >= 0 && (bp += wrc) < (size_t)sz);
	    }
	    close(pfd[0]);
	}
	return drc;
    }

    if (pf && waitpflock(pf, opts->pidfile) < 0) goto done;

    if (pfd[0] >= 0) close(pfd[0]);

    sid = setsid();
    if (sid < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "setsid() failed");
	goto done;
    }

    struct sigaction handler;
    memset(&handler, 0, sizeof handler);
    handler.sa_handler = SIG_IGN;
    sigemptyset(&handler.sa_mask);
    sigaction(SIGTERM, &handler, 0);
    sigaction(SIGINT, &handler, 0);
    sigaction(SIGHUP, &handler, 0);
    sigaction(SIGPIPE, &handler, 0);
    sigaction(SIGUSR1, &handler, 0);

    pid = fork();

    if (pid < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "failed to fork (2)");
	goto done;
    }

    if (pid > 0)
    {
	if (pf)
	{
	    fprintf(pf, "%d\n", pid);
	    fclose(pf);
	}
	return EXIT_SUCCESS;
    }

    if (pf && waitpflock(pf, opts->pidfile) < 0) goto done;

    if (chdir("/") < 0)
    {
	PSC_Log_msg(PSC_L_ERROR, "chdir(\"/\") failed");
	goto done;
    }

    umask(0);

    int nullfd = open("/dev/null", O_RDWR);
    if (nullfd < 0)
    {
	close(STDOUT_FILENO);
	if (pfd[1] < 0) close(STDERR_FILENO);
    }
    else
    {
	dup2(nullfd, STDIN_FILENO);
	dup2(nullfd, STDOUT_FILENO);
	if (pfd[1] < 0) dup2(nullfd, STDERR_FILENO);
	close(nullfd);
    }
    if (pfd[1] >= 0)
    {
	dup2(pfd[1], STDERR_FILENO);
	close(pfd[1]);
    }

    PSC_Log_msg(PSC_L_INFO, "forked into background");
    rc = dmain(data);
    if (rc != EXIT_SUCCESS && write(STDERR_FILENO, "\0", 1) < 1)
    {
	PSC_Log_msg(PSC_L_WARNING, "daemon: cannot notify parent process");
    }
    if (pf)
    {
	fclose(pf);
	pf = 0;
    }
    if (opts->pidfile) unlink(opts->pidfile);

done:
    if (pf) fclose(pf);
    return rc;
}

SOEXPORT void PSC_Daemon_launched(void)
{
    dup2(STDIN_FILENO, STDERR_FILENO);
}

