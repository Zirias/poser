#ifndef POSER_CORE_INT_IPADDR_H
#define POSER_CORE_INT_IPADDR_H

#include <poser/core/ipaddr.h>

#include <sys/socket.h>

PSC_IpAddr *PSC_IpAddr_fromSockAddr(const struct sockaddr *addr);

#endif
