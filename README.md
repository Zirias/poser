# poser – a C framework for POsix SERvices

This is a lightweight framework to easily implement a typical (networking)
service in C, using an event-driven API.

It only uses standard POSIX APIs, therefore it is not suitable for services
that must scale to thousands of concurrent clients.

Currently, only the `core` part is implemented.

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
#include <stddef.h>

static PSC_Server *server;
static const uint8_t hellomsg[] = "Hello!\n";

static void sent(void *receiver, void *sender, void *args) {
    // Close the connection after it sent our message:
    Connection_close(sender, 0);
}

static void connected(void *receiver, void *sender, void *args) {
    // PSC_Server_clientConnected() gives us the new client connection in
    // the event args:
    PSC_Connection *client = args;

    // We want to know when data to the client was sent:
    PSC_Event_register(PSC_Connection_dataSent(client), 0, sent, 0);

    // Send "hello" to the client, also pass some id object, so we get a
    // "dataSent" event for it:
    PSC_Connection_sendAsync(client, hellomsg, sizeof hellomsg - 1, client);
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

