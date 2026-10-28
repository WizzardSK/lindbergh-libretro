#pragma once

#include <stdio.h>
#include <unistd.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#endif

void hummerSetIPs(size_t firstAddress);
int rtunedGetCabs();
void rtunedGetLinkIdandCabs();
void rtunedSetIPs();
int rtunedBind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int getHostByName(struct in_addr *param_1, const char *param_2);
void setVf5BroadcastAddr(int a, int b, char* c);