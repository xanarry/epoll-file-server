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


/**
 * 解析出请求的头部信息
 *
 * @param connCtx
 * @return
 */
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

    connCtx->req.method = BAD_REQ;

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


/**
 * 处理读事件
 * @param efd
 * @param event
 */
void handleRead(int efd, struct epoll_event event) {
    ConnectCtx *connCtx = (ConnectCtx *) event.data.ptr;
    ssize_t n = copyToBuf(&connCtx->req.readBuf, connCtx->socketFd);
    if (n == -2) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, "memery error");
        clearBuf(&(connCtx->req.readBuf));
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
            fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
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
            handleError(connCtx);
            break;
    }
}


/**
 * 处理写事件
 * @param efd
 * @param event
 */
void handleWrite(int efd, struct epoll_event event) {
    ConnectCtx *connCtx = (ConnectCtx *) event.data.ptr;

    //请求处理完毕, 从epoll中移除
    if (connCtx->resp.procState == FINISH) {
        if (epoll_ctl(efd, EPOLL_CTL_DEL, connCtx->socketFd, &event) == -1) {
            fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
            abort();
        }
        close(connCtx->socketFd);
        return;
    }


    //限制每次发送的数据量，防止一次发送占用过多时间, 数据没有写完时，将事件重新添加到epoll，方便下一次通知
    ssize_t writeCnt = 0;
    while (writeCnt < BYTES_HANDLE_ONE_TIME) {

        if (connCtx->resp.procState == SEND_HEADER) {
            //发送响应头部
            //默认如果发送完头部（实际的头部状态表示写缓冲区中存在内容）就结束
            ssize_t needToWrite = getBufSize(&connCtx->resp.writeBuf);
            ssize_t n = bufReadToFile(&connCtx->resp.writeBuf, connCtx->socketFd, needToWrite);
            if (n > 0) {
                writeCnt += n;
            } else if (n == 0) {
                connCtx->resp.procState = FINISH; // 数据发送完毕
            } else if (n == -1) {
                if (errno == EAGAIN) {
                    //套接字阻塞
                    connCtx->resp.procState = SEND_HEADER;
                    return;
                } else {
                    //发生错误
                    connCtx->resp.procState = FINISH;
                }
            }

            //假如请求为GET，修改下一步状态为发送文件内容
            if (connCtx->openedFiles[0] > 0 && connCtx->req.method == GET) {
                connCtx->resp.procState = SEND_CONTENT;
            }
        }

        if (connCtx->resp.procState == SEND_CONTENT) {
            //将文件读取到缓冲区中再发送
            ssize_t bufSize = getBufSize(&connCtx->resp.writeBuf);
            if (bufSize == 0) {
                ssize_t copiedSize = 0; //读文件到缓冲区
                ssize_t n = copyNByteToBuf(&connCtx->resp.writeBuf, connCtx->openedFiles[0], BYTES_HANDLE_ONE_TIME,
                                           &copiedSize);
                if (n >= 0) {
                    //更新缓冲区大小
                    bufSize = getBufSize(&connCtx->resp.writeBuf);
                } else {
                    //文件已经加载到缓冲区完毕，关闭本地文件
                    close(connCtx->openedFiles[0]);
                }
            }

            //printf("bufsize %ld, file %d\n", bufSize, connCtx->openedFiles[0]);
            ssize_t n = bufReadToFile(&connCtx->resp.writeBuf, connCtx->socketFd, bufSize);
            if (n > 0) {
                writeCnt += n;
            } else if (n == 0) {
                connCtx->resp.procState = FINISH; // 数据发送完毕
            } else if (n == -1) {
                if (errno == EAGAIN) {
                    //套接字阻塞
                    connCtx->resp.procState = SEND_CONTENT;
                    return;
                } else {
                    //发生错误
                    connCtx->resp.procState = FINISH;
                }
            }

        }

        //添加写事件监听
        event.events = EPOLLET | EPOLLOUT;
        if (epoll_ctl(efd, EPOLL_CTL_MOD, connCtx->socketFd, &event) == -1) {
            abort();
        }

        if (connCtx->resp.procState == FINISH) {
            return;
        }
    }
}
