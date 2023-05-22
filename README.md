# poser – a C framework for POsix SERvices

This is a lightweight framework to easily implement a typical (networking)
service in C, using an event-driven API.

It only uses standard POSIX APIs, therefore it is not suitable for services
that must scale to thousands of concurrent clients.

Currently, only the `core` part is implemented.

## libposercore

This is the core part offering basic functionality needed for every service:

* A simple event mechanism
* A simple logging abstraction
* Generic code to automatically deamonize and correctly handle a pidfile
* A generic service main loop built around `pselect()`
* An abstraction for a (socket) connection, with optional TLS client support
* An abstraction for a server listening on a socket and accepting connections
* A few utility functions

