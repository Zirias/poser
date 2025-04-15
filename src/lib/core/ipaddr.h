#ifndef POSER_CORE_INT_IPADDR_H
#define POSER_CORE_INT_IPADDR_H

#include <poser/core/ipaddr.h>

#include <sys/socket.h>

PSC_IpAddr *PSC_IpAddr_fromSockAddr(const struct sockaddr *addr);
int PSC_IpAddr_port(const PSC_IpAddr *self);

#endif
