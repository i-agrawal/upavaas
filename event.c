#include "event.h"
#include "config.h"
#include <sys/socket.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>


#if defined(__linuxos__)
#include <sys/epoll.h>
// for epoll
int epollfd;
struct epoll_event* evs;
int evsz;
#elif defined(__bsdos__)
#include <sys/event.h>
// for kqueue
int kq;
struct kevent* evs;
int evsz;
#elif defined(__unixos__)
#include <sys/select.h>
// for select
fd_set fdset;
#endif

// for file descriptors
int sz;
int cap;
int* fds;

// for reading messages
#define buflen 65536
char* buffer;

int
push_back(int fd)
{
  if (sz == cap) {
    if (cap == 0) {
      cap = 1;
      fds = (int*)malloc(sizeof(int));
      if (fds == 0) {
        perror("malloc");
        return -1;
      }
    }
    else {
      cap <<= 1;
      int* oldfds = fds;
      fds = (int*)realloc(fds, cap * sizeof(int));
      if (fds == 0) {
        perror("realloc");
        fds = oldfds;
        return -1;
      }
    }
  }
  fds[sz++] = fd;
  return 0;
}

int
delete(int fd)
{
  int i;
  for (i = 0; i != sz; i++) {
    if (fds[i] == fd) {
      for (; i != sz-1; i++) {
        fds[i] = fds[i+1];
      }
      sz--;
      close(fd);
      return 0;
    }
  }
  return -1;
}

int
startsvr(uint32_t addr, uint16_t port)
{
  int svrfd = socket(AF_INET, SOCK_STREAM, 0);
  if (svrfd == -1) {
    perror("socket");
    return -1;
  }

  int ret = fcntl(svrfd, F_SETFL, O_NONBLOCK);
  if (ret == -1) {
    perror("fcntl");
    close(svrfd);
    return -1;
  }

  struct sockaddr_in address;
  memset(&address, 0, sizeof(struct sockaddr_in));
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = addr;

  ret = bind(svrfd, (struct sockaddr*)&address, sizeof(struct sockaddr_in));
  if (ret == -1) {
    perror("bind");
    close(svrfd);
    return -1;
  }

  ret = listen(svrfd, 10);
  if (ret == -1) {
    perror("listen");
    close(svrfd);
    return -1;
  }

  ret = push_back(svrfd);
  if (ret == -1) {
    close(svrfd);
    return -1;
  }

  return svrfd;
}

void
evclean(int sig)
{
  if (sig == SIGINT) {
    printf("interrupt signal received: cleaning up\n");
  }
  else {
    printf("termination signal received: cleaning up\n");
  }
  
  if (fds != 0) {
    int i;
    for (i = 0; i != sz; i++) {
      close(fds[i]);
    }
    free(fds);
  }
  #if defined(__linuxos__)
  close(epollfd);
  #elif defined(__bsdos__)
  close(kq);
  #else
  FD_ZERO(&fdset);
  #endif
  if (buffer != 0) {
    free(buffer);
  }
  #if defined(__linuxos__) || defined(__bsdos__)
  if (evs != 0) {
    free(evs);
  }
  #endif
  exit(0);
}

#if defined(__linuxos__)
int
initevl(void)
{
  epollfd = epoll_create1(0);
  if (epollfd == -1) {
    perror("epoll_create1");
    close(fds[0]);
    free(fds);
    return -1;
  }

  struct epoll_event svrev;
  svrev.events = EPOLLIN | EPOLLET;
  svrev.data.fd = fds[0];
  int ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fds[0], &svrev);
  if (ret == -1) {
    perror("epoll_ctl");
    close(epollfd);
    close(fds[0]);
    free(fds);
    return -1;
  }

  return epollfd;
}

int
startevl(void)
{
  evs = 0;
  buffer = 0;
  struct epoll_event ftmp;
  evs = (struct epoll_event*)malloc(sz * sizeof(struct epoll_event));
  if (evs == 0) {
    perror("malloc");
    goto failure;
  }
  evsz = 1;
  buffer = (char*)malloc(buflen);
  if (buffer == 0) {
    perror("malloc");
    goto failure;
  }

  struct sockaddr_in address;
  socklen_t len;
  int i, nevs, fd, ret;
  uint8_t* addrptr = (uint8_t*)&(address.sin_addr.s_addr);
  while (1) {
    nevs = epoll_wait(epollfd, evs, sz, -1);
    if (nevs == -1) {
      perror("epoll_wait");
      goto failure;
    }
    for (i = 0; i != nevs; i++) {
      if (evs[i].data.fd == fds[0]) {
        len = sizeof(struct sockaddr_in);
        fd = accept(fds[0], (struct sockaddr*)&address, &len);
        if (fd == -1) {
          perror("accept");
          goto failure;
        }
        ret = fcntl(fd, F_SETFL, O_NONBLOCK);
        if (ret == -1) {
          perror("fcntl");
          close(fd);
          goto failure;
        }

        ftmp.events = EPOLLIN | EPOLLET;
        ftmp.data.fd = fd;
        ret = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ftmp);
        if (ret == -1) {
          perror("epoll_ctl");
          goto failure;
        }
        ret = push_back(fd);
        if (ret == -1) {
          goto failure;
        }
        if (evsz < cap) {
          evs = realloc(evs, cap * sizeof(struct epoll_event));
          if (evs == 0) {
            perror("realloc");
            goto failure;
          }
          evsz = cap;
        }
        printf("new connection %hhu.%hhu.%hhu.%hhu:%hu\n", addrptr[0], addrptr[1], addrptr[2], addrptr[3], ntohs(address.sin_port));
      }
      else {
        ret = read(evs[i].data.fd, buffer, buflen);
        if (ret == -1) {
          perror("read");
          goto failure;
        }
        if (ret == 0) {
          printf("disconnect\n");
          fd = evs[i].data.fd;
          close(fd);
          ret = delete(fd);
          if (ret == -1) {
            goto failure;
          }
        }
        else {
          printf("connection said:\n%s\n", buffer);
        }
      }
    }
  }

  // should never get here
  return 0;

  failure:
  if (fds != 0) {
    for (i = 0; i != sz; i++) {
      close(fds[i]);
    }
    free(fds);
  }
  close(epollfd);
  if (buffer != 0) {
    free(buffer);
  }
  if (evs != 0) {
    free(evs);
  }
  return -1;
}
#elif defined(__bsdos__)
int
initevl(void)
{
  kq = kqueue();
  if (kq == -1) {
    perror("kqueue");
    close(fds[0]);
    free(fds);
    return -1;
  }

  struct kevent svrev;
  EV_SET(&svrev, fds[0], EVFILT_READ, EV_ADD, 0, 0, 0);
  int ret = kevent(kq, &svrev, 1, 0, 0, 0);
  if (ret == -1) {
    perror("kevent");
    close(kq);
    close(fds[0]);
    free(fds);
    return -1;
  }

  return kq;
}

int
startevl(void)
{
  evs = 0;
  buffer = 0;
  struct kevent ftmp;
  evs = (struct kevent*)malloc(sz * sizeof(struct kevent));
  if (evs == 0) {
    perror("malloc");
    goto failure;
  }
  evsz = 1;
  buffer = (char*)malloc(buflen);
  if (buffer == 0) {
    perror("malloc");
    goto failure;
  }

  struct sockaddr_in address;
  socklen_t len;
  int i, nevs, fd, ret;
  uint8_t* addrptr = (uint8_t*)&(address.sin_addr.s_addr);
  while (1) {
    nevs = kevent(kq, 0, 0, evs, sz, 0);
    if (nevs == -1) {
      perror("kevent");
      goto failure;
    }
    for (i = 0; i != nevs; i++) {
      if (evs[i].flags & EV_EOF) {
        printf("disconnect\n");
        fd = evs[i].ident;
        EV_SET(&ftmp, fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
        ret = kevent(kq, &ftmp, 1, 0, 0, 0);
        if (ret == -1) {
          perror("kevent");
          goto failure;
        }
        ret = delete(fd);
        if (ret == -1) {
          goto failure;
        }
      }
      else if (evs[i].ident == fds[0]) {
        len = sizeof(struct sockaddr_in);
        fd = accept(fds[0], (struct sockaddr*)&address, &len);
        if (fd == -1) {
          perror("accept");
          goto failure;
        }
        EV_SET(&ftmp, fd, EVFILT_READ, EV_ADD, 0, 0, 0);
        ret = kevent(kq, &ftmp, 1, 0, 0, 0);
        if (ret == -1) {
          perror("kevent");
          goto failure;
        }
        ret = push_back(fd);
        if (ret == -1) {
          goto failure;
        }
        if (evsz < cap) {
          evs = realloc(evs, cap * sizeof(struct kevent));
          if (evs == 0) {
            perror("realloc");
            goto failure;
          }
          evsz = cap;
        }
        printf("new connection %hhu.%hhu.%hhu.%hhu:%hu\n", addrptr[0], addrptr[1], addrptr[2], addrptr[3], ntohs(address.sin_port));
      }
      else {
        ret = read(evs[i].ident, buffer, buflen);
        if (ret == -1) {
          perror("read");
          goto failure;
        }
        printf("connection said:\n%s\n", buffer);
      }
    }
  }

  failure:
  close(kq);
  if (buffer != 0) {
    free(buffer);
  }
  if (evs != 0) {
    free(evs);
  }
  if (fds != 0) {
    for (i = 0; i != sz; i++) {
      close(fds[i]);
    }
    free(fds);
  }
  return -1;
}
#elif defined(__unixos__)
int
initevl(void)
{
  FD_ZERO(&fdset);
  FD_SET(fds[0], &fdset);
  return 0;
}

int
startevl(void)
{
  buffer = 0;
  buffer = (char*)malloc(buflen);
  if (buffer == 0) {
    perror("malloc");
    goto failure;
  }

  struct sockaddr_in address;
  socklen_t len;
  int i, nevs, fd, ret, tsz;
  fd_set readfds;
  uint8_t* addrptr = (uint8_t*)&(address.sin_addr.s_addr);
  FD_ZERO(&readfds);
  while (1) {
    tsz = sz;
    readfds = fdset;
    ret = select(FD_SETSIZE, &readfds, 0, 0, 0);
    if (ret == -1) {
      perror("select");
      goto failure;
    }
    if (FD_ISSET(fds[0], &readfds)) {
      len = sizeof(struct sockaddr_in);
      fd = accept(fds[0], (struct sockaddr*)&address, &len);
      if (fd == -1) {
        perror("accept");
        goto failure;
      }
      ret = fcntl(fd, F_SETFL, O_NONBLOCK);
      if (ret == -1) {
        perror("fcntl");
        close(fd);
        goto failure;
      }
      FD_SET(fd, &fdset);
      ret = push_back(fd);
      if (ret == -1) {
        close(fd);
        goto failure;
      }
      printf("new connection %hhu.%hhu.%hhu.%hhu:%hu\n", addrptr[0], addrptr[1], addrptr[2], addrptr[3], ntohs(address.sin_port));
    }
    for (i = 1; i != tsz; i++) {
      if (FD_ISSET(fds[i], &readfds)) {
        ret = read(fds[i], buffer, buflen);
        if (ret == -1) {
          perror("read");
          goto failure;
        }
        if (ret == 0) {
          fd = fds[i];
          close(fd);
          FD_CLR(fd, &fdset);
          ret = delete(fd);
          if (ret == -1) {
            goto failure;
          }
          printf("diconnect\n");
        }
        else {
          printf("connection said:\n%s\n", buffer);
        }
      }
    }
  }

  failure:
  printf("shutting down\n");
  if (buffer != 0) {
    free(buffer);
  }
  FD_ZERO(&fdset);
  FD_ZERO(&readfds);
  if (fds != 0) {
    for (i = 0; i != sz; i++) {
      close(fds[i]);
    }
    free(fds);
  }
  return -1;
}
#endif
