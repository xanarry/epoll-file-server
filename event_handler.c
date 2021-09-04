//
// Created by xanarry on 2021/8/29.
//

#include <unistd.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "event_handler.h"
#include "request_handler.h"


ssize_t writeNBytes(int outFd, char *buf, int n) {
    ssize_t hasWrite = 0;
    while (hasWrite < n) {
        ssize_t len = write(outFd, buf + hasWrite, n - hasWrite);
        if (len > 0) {
            hasWrite += len;
        }
        if (len == -1 && errno != EAGAIN) {
            return -1;
        }
    }
    return 0;
}

int parseHead(ConnectCtx *connCtx) {
    if (hasLine(&connCtx->req.readBuf, 1024) == -1) {
        return -1;
    }

    char line[1024] = {0};
    ssize_t len = bufReadline(&connCtx->req.readBuf, line, 1024);
    int spaceIdx = 0;
    for (; spaceIdx < len; spaceIdx++) {
        if (line[spaceIdx] == ' ') {
            break;
        }
    }


    /**
     * POST fileName
     * [fileSize] byteSeqs
     */
    if (strncmp(line, "POST", spaceIdx) == 0) {
        connCtx->req.method = POST;
        strcpy(connCtx->req.fileName, line + spaceIdx + 1);
        fprintf(stderr, "POST %s\n", connCtx->req.fileName);
        connCtx->req.reqProcState = PARSE_CONTENT_LEN;
    }

    /**
     * GET fileName
     */
    if (strncmp(line, "GET", spaceIdx) == 0) {
        connCtx->req.method = GET;
        strcpy(connCtx->req.fileName, line + spaceIdx + 1);
        fprintf(stderr, "GET %s\n", connCtx->req.fileName);
        connCtx->req.reqProcState = DONE;
    }

    /**
     * DELETE fileName
     */
    if (strncmp(line, "DELETE", spaceIdx) == 0) {
        connCtx->req.method = DELETE;
        strcpy(connCtx->req.fileName, line + spaceIdx + 1);
        fprintf(stderr, "DELETE %s\n", connCtx->req.fileName);
        connCtx->req.reqProcState = DONE;
    }

    /**
     * LIST dirpath
     */
    if (strncmp(line, "LIST", spaceIdx) == 0) {
        connCtx->req.method = LIST;
        strcpy(connCtx->req.fileName, line + spaceIdx + 1);
        fprintf(stderr, "LIST %s\n", connCtx->req.fileName);
        connCtx->req.reqProcState = DONE;
    }

    return 0;
}


int addEvent(ConnectCtx *connectCtx, uint32_t events) {
    struct epoll_event evt = connectCtx->event;
    evt.events = events;
    return epoll_ctl(connectCtx->epollFd, EPOLL_CTL_ADD, connectCtx->socketFd, &evt);
}


int modEvent(ConnectCtx *connectCtx, uint32_t events) {
    struct epoll_event evt = connectCtx->event;
    evt.events = events;
    return epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &evt);
}

int delEvent(ConnectCtx *connectCtx) {
    struct epoll_event evt = connectCtx->event;
    return epoll_ctl(connectCtx->epollFd, EPOLL_CTL_DEL, connectCtx->socketFd, &evt);
}


void handleRead(int efd, struct epoll_event event) {
    ConnectCtx *connCtx = (ConnectCtx *) event.data.ptr;
    ssize_t n = copyToBuf(&connCtx->req.readBuf, connCtx->socketFd);
    if (n == -2) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, "memery error");
        clear(&(connCtx->req.readBuf));
        exit(-1);
    }

    if (n == -1 && errno != EAGAIN) {
        //移出监听套接字，并且关闭
        return;
    }

    //EOF读完所有数据
    if (n == 0) {
        event.events |= EPOLLOUT;
        if (epoll_ctl(efd, EPOLL_CTL_MOD, connCtx->socketFd, &event) == -1) {
            perror("epoll_ctl");
            abort();
        }
    }

    //先解析请求头
    if (connCtx->req.reqProcState == PARSE_HEAD) {
        if (parseHead(connCtx) == -1) {
            return;
        }
    }

    switch (connCtx->req.method) {
        case POST:
            handlePost(connCtx);
            break;
        case GET:
            handleGet(connCtx);
            break;
        case DELETE:
            handleDelete(connCtx);
            break;
        case LIST:
            handleList(connCtx);
            break;
        default:
            //todo
            //handleError(connCtx);
            break;
    }
}

void handleReadBAK(int efd, struct epoll_event event) {
    ConnectCtx *connCtx = (ConnectCtx *) event.data.ptr;
    ssize_t n = copyToBuf(&connCtx->req.readBuf, connCtx->socketFd);
    if (n == -2) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, "memery error");
        clear(&(connCtx->req.readBuf));
        exit(-1);
    }

    if (n == -1 && errno != EAGAIN) {
        //移出监听套接字，并且关闭
        return;
    }

    //EOF读完所有数据
    if (n == 0) { //移出监听
        if (epoll_ctl(efd, EPOLL_CTL_DEL, connCtx->socketFd, &event) == -1) {
            perror("epoll_ctl");
            abort();
        }
    }


    if (connCtx->req.reqProcState == PARSE_HEAD) {

        if (hasLine(&connCtx->req.readBuf, 1024) == -1) {
            return;
        }

        char line[1024] = {0};
        bufReadline(&connCtx->req.readBuf, line, 1024);

        if (strncmp(line, "POST", 4) == 0) {
            connCtx->req.method = POST;
            strcpy(connCtx->req.fileName, line + 5);
            fprintf(stderr, "POST %s\n", connCtx->req.fileName);

            char absBase[1024] = {0};
            char absPath[1500] = {0};
            getcwd(absBase, 1024);
            sprintf(absPath, "%s/%s", absBase, connCtx->req.fileName);

            fprintf(stderr, "POST [%s]\n", absPath);

            int outFd = open(absPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG);
            if (outFd == -1) {
                perror("create file");
            }
            connCtx->openedFiles[0] = outFd;
            connCtx->req.reqProcState = RECV_POST;
        }

        if (strncmp(line, "GET", 3) == 0) {
            connCtx->req.method = GET;
            strcpy(connCtx->req.fileName, line + 4);
            handleGet(connCtx);

            connCtx->resp.procState = SEND_HEADER;
            connCtx->req.reqProcState = DONE;

            event.events = EPOLLET | EPOLLOUT;
            if (epoll_ctl(efd, EPOLL_CTL_MOD, connCtx->socketFd, &event) == -1) {
                perror("epoll_ctl");
                abort();
            }
        }

        if (strncmp(line, "LIST", 4) == 0) {
            connCtx->req.method = LIST;
            strcpy(connCtx->req.fileName, line + 5);

            handleList(connCtx);
            connCtx->resp.procState = SEND_HEADER;
            connCtx->req.reqProcState = DONE;

            event.events = EPOLLET | EPOLLOUT;
            if (epoll_ctl(efd, EPOLL_CTL_MOD, connCtx->socketFd, &event) == -1) {
                perror("epoll_ctl");
                abort();
            }
        }

        if (strncmp(line, "DELETE", 6) == 0) {
            connCtx->req.method = DELETE;
            strcpy(connCtx->req.fileName, line + 7);
            handleDelete(connCtx);
            connCtx->resp.procState = SEND_HEADER;
            connCtx->req.reqProcState = DONE;

            event.events = EPOLLET | EPOLLOUT;
            if (epoll_ctl(efd, EPOLL_CTL_MOD, connCtx->socketFd, &event) == -1) {
                perror("epoll_ctl");
                abort();
            }

            close(connCtx->socketFd);
        }
    }

    if (connCtx->req.reqProcState == RECV_POST) {
        handlePost(connCtx);
    }
}


void handleWrite(int efd, struct epoll_event event) {
    ConnectCtx *connState = (ConnectCtx *) event.data.ptr;
    ssize_t writeCnt = 0;
    if (connState->resp.procState == FINISH) {
        if (epoll_ctl(efd, EPOLL_CTL_DEL, connState->socketFd, &event) == -1) {
            perror("epoll_ctl in handle write");
            abort();
        }
        close(connState->socketFd);
        return;
    }

    while (writeCnt < BYTES_HANDLE_ONE_TIME) {
        if (connState->resp.procState == SEND_HEADER) {
            if (connState->resp.status == OK) {
                writeNBytes(connState->socketFd, "OK\n", 3);
                writeCnt += 3;
            } else {
                writeNBytes(connState->socketFd, "ERROR\n", 6);
                bufReadToFile(&connState->resp.writeBuf, connState->socketFd, getBufSize(&connState->resp.writeBuf));
                connState->resp.procState = FINISH;
            }

            if (connState->resp.contentLength > 0) {
                ssize_t n = writeNBytes(connState->socketFd, (char *) &(connState->resp.contentLength),
                                        sizeof(ssize_t));
                if (n == -1) {
                    fprintf(stderr, "%s %d writeNBytes error\n", __FILE__, __LINE__);
                    abort();
                }
                writeCnt += n;
                connState->resp.procState = SEND_CONTENT;
            } else {
                connState->resp.procState = FINISH;
            }
        }

        if (connState->resp.procState == SEND_CONTENT) {
            ssize_t bufSize = getBufSize(&connState->resp.writeBuf);
            if (bufSize == 0) {
                if (connState->openedFiles[0] > 0 && connState->req.method == GET) {
                    ssize_t copiedSize = 0;
                    ssize_t n = copyNByteToBuf(&connState->resp.writeBuf, connState->openedFiles[0],
                                               BYTES_HANDLE_ONE_TIME, &copiedSize);
                    if (n >= 0) {
                        bufSize = getBufSize(&connState->resp.writeBuf);
                    } else {
                        if (epoll_ctl(efd, EPOLL_CTL_DEL, connState->socketFd, &event) == -1) {
                            perror("epoll_ctl in handle write");
                            abort();
                        }
                        shutdown(connState->socketFd, SHUT_WR);
                        close(connState->socketFd);
                        break;
                    }
                } else {
                    if (epoll_ctl(efd, EPOLL_CTL_DEL, connState->socketFd, &event) == -1) {
                        perror("epoll_ctl in handle write");
                        abort();
                    }
                    shutdown(connState->socketFd, SHUT_WR);
                    close(connState->socketFd);
                    break;
                }
            }

            //printf("bufsize %ld, file %d\n", bufSize, connState->openedFiles[0]);
            if (bufSize > 0) {
                ssize_t n = bufReadToFile(&connState->resp.writeBuf, connState->socketFd, bufSize);
                if (n == -1) {
                    return;
                } else {
                    writeCnt += n;
                }
            } else {
                connState->resp.procState = FINISH;
                return;
            }
        }

        event.events = EPOLLET | EPOLLOUT;
        if (epoll_ctl(efd, EPOLL_CTL_MOD, connState->socketFd, &event) == -1) {
            abort();
        }

        if (connState->resp.procState == FINISH) {
            return;
        }
    }
}
