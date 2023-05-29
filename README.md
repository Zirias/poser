# poser – a C framework for POsix SERvices

This is a lightweight framework to easily implement a typical (networking)
service in C, using an event-driven API.

It only uses standard POSIX APIs, therefore it is not suitable for services
that must scale to thousands of concurrent clients.

Currently, only the `core` part is implemented.

## Dynamic library versioning

Dynamic libraries in poser will follow a simple versioning scheme:

* A full library version consists of `major.minor.revision`, e.g.
  `libposercore.so.1.0.0` has major version 1, minor version 0 and revision 0.
* The major version is the *ABI* version. It will only be bumped when a change
  breaks the ABI (introduces changes that are not backwards-compatible).
* The minor version is the *Feature* version. It will be bumped whenever new
  features are added in a backwards-compatible way.
* The revision will be bumped when a change fixes some issue, not touching
  API/ABI at all.
* The `SONAME` property of a dynamic library will only contain the major/ABI
  version (e.g. `libposercore.so.1`).

So, updating a library with the same major/ABI version will never break
consumers. Using consumers built against a newer minor/Feature version with an
older version of a library *might* result in unresolved symbols.

## libposercore

This is the core part offering basic functionality needed for every service:

* `PSC_Event`: a simple event mechanism
* `PSC_Log`: a simple logging abstraction
* `PSC_Daemon`: generic code to automatically deamonize and correctly handle a
                pidfile
* `PSC_Service`: a generic service main loop built around `pselect()`
* `PSC_ThreadPool`: a thread pool used to offload jobs that might block,
                    available to schedule own jobs on
* `PSC_Connection`: an abstraction for a (socket) connection
* `PSC_Server`: an abstraction for a server listening on a socket and
                accepting connections
* Optional TLS support
* A few utility functions and classes

### Quick start

A typical structure for a service looks like this minimal example implementing
a TCP server that just writes "Hello!" to every client:

```c
#include <poser/core.h>
#include <stdlib.h>

static PSC_Server *server;

static void sent(void *receiver, void *sender, void *args) {
    // Close the connection after it sent our message:
    PSC_Connection_close(sender, 0);
}

static void connected(void *receiver, void *sender, void *args) {
    // PSC_Server_clientConnected() gives us the new client connection in
    // the event args:
    PSC_Connection *client = args;

    // We want to know when data to the client was sent:
    PSC_Event_register(PSC_Connection_dataSent(client), 0, sent, 0);

    // Send "hello" to the client, also pass some id object, so we get a
    // "dataSent" event for it:
    PSC_Connection_sendTextAsync(client, "Hello!\n", client);
}

static void startup(void *receiver, void *sender, void *args) {
    // initialization, for example create a PSC_Server

    // Create TCP server options to listen on some port:
    PSC_TcpServerOpts *opts = PSC_TcpServerOpts_create(10000);

    // Create server using these options:
    server = PSC_Server_createTcp(opts);
    PSC_TcpServerOpts_destroy(opts);

    // In case of error, let PSC_Service know via event arguments:
    if (!server) {
        PSC_EAStartup_return(args, EXIT_FAILURE);
        return;
    }

    // We want to know when a client connects:
    PSC_Event_register(PSC_Server_clientConnected(server), 0, connected, 0);
}

static void shutdown(void *receiver, void *sender, void *args) {
    // cleanup, e.g. destroy objects created in startup()

    PSC_Server_destroy(server);
}

int main(void) {
    PSC_RunOpts_init("/tmp/example.pid");         // configure pidfile
    PSC_RunOpts_enableDefaultLogging("example");  // ident for syslog

    // Execute startup() early during service startup
    PSC_Event_register(PSC_Service_prestartup(), 0, startup, 0);

    // Execute shutdown() when service is about to stop
    PSC_Event_register(PSC_Service_shutdown(), 0, shutdown, 0);

    // Run the service
    return PSC_Service_run();
}
```

Reference documentation is available at https://zirias.github.io/poser/ and
can be built from the source using `doxygen`.

### Threading model

`libposercore` implements a service loop doing I/O multiplexing on a single
thread. Different components are mostly "wired" using `PSC_Event`, which is
not aware of threads and just calls any registered event handler directly.

A thread pool is offered (and used internally as well) to offload either jobs
that might block, or jobs that could be CPU intensive, because the main loop
should always iterate quickly to serve all peers in a timely manner.

For a thread job, you can pass some data which will be properly locked
automatically at job start and end. But if you pass any "poser" objects there,
be aware that executing any actions on them outside the main thread is **not**
a safe thing to do! As a rule of thumb, treat them as read-only (`const`)
inside your thread job.

A notable exception is scheduling another thread job, this **is** a safe thing
to do from any thread.

Whether logging is safe depends on the log writer you use. The standard log
writers offered either log to a file (which *should* be thread-safe on almost
any platform) or to syslog (which is thread-safe). When logging is set to
asynchronous mode (which is recommended to do after daemonizing and handled
automatically when you use `PSC_RunOpts_enableDefaultLogging()`), any log
writer is always called on some worker thread, so if you use your custom log
writer, make sure it is thread-safe!

