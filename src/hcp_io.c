/*
 * $Id: hcp_io.c $
 *
 * Author: Markus Stenberg <mstenber@cisco.com>
 *
 * Copyright (c) 2013 cisco Systems, Inc.
 *
 * Created:       Mon Nov 25 14:00:10 2013 mstenber
 * Last modified: Fri Nov 29 11:35:40 2013 mstenber
 * Edit time:     136 min
 *
 */

/* This module implements I/O needs of hcp. Notably, it has both
 * functionality that deals with sockets, and bit more abstract ones
 * that just deal with buffers for input and output (thereby
 * facilitating unit testing without using real sockets). */

#include "hcp_i.h"
#undef __unused
/* In linux, fcntl.h includes something with __unused. Argh. */
#include <fcntl.h>
#define __unused __attribute__((unused))
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/ethernet.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <libubox/usock.h>
#include <ifaddrs.h>

int
hcp_io_get_hwaddrs(unsigned char *buf, int buf_left)
{
  struct ifaddrs *ia, *p;
  int r = getifaddrs(&ia);
  bool first = true;
  void *a1 = buf, *a2 = buf + ETHER_ADDR_LEN;

  if (buf_left < ETHER_ADDR_LEN * 2)
    return 0;
  memset(buf, ETHER_ADDR_LEN * 2, 0);
  if (r)
    return 0;
  for (p = ia ; p ; p = p->ifa_next)
    if (p->ifa_addr->sa_family == AF_LINK)
    {
      void *a = &p->ifa_addr->sa_data[0];
      if (first || memcmp(a1, a, ETHER_ADDR_LEN) < 0)
        memcpy(a1, a, ETHER_ADDR_LEN);
      if (first || memcmp(a2, a, ETHER_ADDR_LEN) > 0)
        memcpy(a2, a, ETHER_ADDR_LEN);
      first = false;
    }
  freeifaddrs(ia);
  if (first)
    return 0;
  return ETHER_ADDR_LEN * 2;
}

static void _timeout(struct uloop_timeout *t)
{
  hcp o = container_of(t, hcp_s, timeout);
  hcp_run(o);
}

bool hcp_io_init(hcp o)
{
  int s;
  int on = 1;
#if 0
  /* Could also use usock here; however, it uses getaddrinfo, which
   * doesn't seem to work when e.g. default routes aren't currently
   * set up. Too bad. */
  char buf[6];

  sprintf(buf, "%d", HCP_PORT);
  s = usock(USOCK_IPV6ONLY|USOCK_UDP|USOCK_SERVER|USOCK_NONBLOCK, NULL, buf);
  if (s < 0)
    return false;
#else
  struct sockaddr_in6 addr;

  s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (s<0)
    return false;
  fcntl(s, F_SETFL, O_NONBLOCK);
  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port = HCP_PORT;
  if (bind(s, (struct sockaddr *)&addr, sizeof(addr))<0)
    return false;
#endif
  if (setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on)) < 0)
    return false;
  o->udp_socket = s;
  o->timeout.cb = _timeout;
  return true;
}

void hcp_io_uninit(hcp o)
{
  close(o->udp_socket);
  /* clear the timer from uloop. */
  uloop_timeout_cancel(&o->timeout);
}

bool hcp_io_set_ifname_enabled(hcp o,
                               const char *ifname,
                               bool enabled)
{
  struct ipv6_mreq val;

  val.ipv6mr_multiaddr = o->multicast_address;
  if (!(val.ipv6mr_interface = if_nametoindex(ifname)))
    goto fail;
  if (setsockopt(o->udp_socket,
                 IPPROTO_IPV6,
                 enabled ? IPV6_ADD_MEMBERSHIP : IPV6_DROP_MEMBERSHIP,
                 (char *) &val, sizeof(val)) < 0)
    goto fail;
  /* Yay. It succeeded(?). */
  return true;

 fail:
  return false;
}

void hcp_io_schedule(hcp o, int msecs)
{
  uloop_timeout_set(&o->timeout, msecs);
}

ssize_t hcp_io_recvfrom(hcp o, void *buf, size_t len,
                        char *ifname,
                        struct in6_addr *src,
                        struct in6_addr *dst)
{
  struct sockaddr_in6 srcsa;
  struct iovec iov = {buf, len};
  unsigned char cmsg_buf[256];
  struct msghdr msg = {&srcsa, sizeof(srcsa), &iov, 1,
                       cmsg_buf, sizeof(cmsg_buf), 0};
  ssize_t l;
  struct cmsghdr *h;
  struct in6_pktinfo *ipi6;

  l = recvmsg(o->udp_socket, &msg, MSG_DONTWAIT);
  if (l > 0)
    {
      *ifname = 0;
      *src = srcsa.sin6_addr;
      for (h = CMSG_FIRSTHDR(&msg); h ;
           h = CMSG_NXTHDR(&msg, h))
        if (h->cmsg_level == IPPROTO_IPV6
            && h->cmsg_type == IPV6_PKTINFO)
          {
            ipi6 = (struct in6_pktinfo *)CMSG_DATA(h);
            if (!if_indextoname(ipi6->ipi6_ifindex, ifname))
              *ifname = 0;
            *dst = ipi6->ipi6_addr;
          }
    }
  else
    {
      *ifname = 0;
    }
  if (!*ifname)
    return -1;
  return l;
}

ssize_t hcp_io_sendto(hcp o, void *buf, size_t len,
                      const char *ifname,
                      const struct in6_addr *to)
{
  int flags = 0;
  struct sockaddr_in6 dst;

  memset(&dst, 0, sizeof(dst));
  if (!(dst.sin6_scope_id = if_nametoindex(ifname)))
    return -1;
  dst.sin6_addr = *to;
  return sendto(o->udp_socket, buf, len, flags,
                (struct sockaddr *)&dst, sizeof(dst));
}

hnetd_time_t hcp_io_time(hcp o __unused)
{
  return hnetd_time();
}
