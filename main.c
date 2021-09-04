#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include "socket_buffer.h"
#include "request_handler.h"
#include "event_handler.h"

#define MAXEVENTS 64

static int setNonBlocking(int sfd) {
    int flags = fcntl(sfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    if (fcntl(sfd, F_SETFL, flags) == -1) {
        perror("fcntl");
        return -1;
    }
    return 0;
}

static int createServerSocket(int port) {
    struct sockaddr_in serv_addr;
    char sendBuff[1025];
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(sendBuff, 0, sizeof(sendBuff));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if (bind(sfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
        perror("bind");
        return -1;
    }
    return sfd;
}

void acceptNew(int listenFd, int epollFd) {
    for (;;) {
        struct sockaddr in_addr;
        socklen_t in_len = sizeof in_addr;
        int infd = accept(listenFd, &in_addr, &in_len);
        if (infd == -1) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("accept in acceptNew");
                exit(-1);
            } else {
                return;
            }
        }

        char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
        if (getnameinfo(&in_addr, in_len, hbuf, sizeof hbuf, sbuf, sizeof sbuf, NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
            printf("Accepted connection on descriptor %d (host=%s, port=%s)\n", infd, hbuf, sbuf);
        }

        /* Make the incoming socket non-blocking and add it to the list of fds to monitor. */
        if (setNonBlocking(infd) == -1) {
            abort();
        }


        ConnectCtx *connectCtx = (ConnectCtx *) malloc(sizeof(ConnectCtx));
        memset(connectCtx, 0, sizeof(ConnectCtx));
        connectCtx->epollFd = epollFd;
        connectCtx->socketFd = infd;
        connectCtx->req.reqProcState = PARSE_HEAD;
        connectCtx->req.readBuf = initBuffer(0);
        connectCtx->resp.writeBuf = initBuffer(0);
        memset(connectCtx->openedFiles, -1, 128);

        struct epoll_event evt;
        evt.data.ptr = connectCtx;
        evt.events = EPOLLIN | EPOLLET;

        connectCtx->event = evt;

        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, infd, &evt) == -1) {
            perror("epoll_ctl");
            abort();
        }

    }
}



int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sfd = createServerSocket(atoi(argv[1]));
    if (sfd == -1 || setNonBlocking(sfd) == -1) {
        return 0;
    }

    //SOMAXCONN
    if (listen(sfd, SOMAXCONN) == -1) {
        perror("listen");
        return 0;
    }

    int efd = epoll_create1(0);
    if (efd == -1) {
        perror("epoll_create");
        abort();
    }

    struct epoll_event event;
    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) == -1) {
        perror("epoll_ctl");
        abort();
    }

    /* Buffer where events are returned */
    struct epoll_event *events = calloc(MAXEVENTS, sizeof(event));

    /* The event loop */
    while (1) {
        int n = epoll_wait(efd, events, MAXEVENTS, -1);
        for (int i = 0; i < n; i++) {
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)) {
                /* An error has occured on this socketFd, or the socket is not ready for reading (why were we notified then?) */
                fprintf(stderr, "%d:", events[i].data.fd);
                if (epoll_ctl(efd, EPOLL_CTL_DEL, events[i].data.fd, &event) == -1) {
                    perror("epoll_ctl");
                    abort();
                }
                close(events[i].data.fd);
                continue;
            }

            if (events[i].events & EPOLLIN) {
                if (events[i].data.fd == sfd) {
                    acceptNew(sfd, efd);
                } else {
                    handleRead(efd, events[i]);
                }
            }

            if (events[i].events & EPOLLOUT) {
                handleWrite(efd, events[i]);
            }
        }
    }
}