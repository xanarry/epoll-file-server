//
// Created by xanarry on 2021/8/29.
//

#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include "request_handler.h"
#include "socket_buffer.h"
#include "event_handler.h"


void setErrMsg(LinkedBuffer *buffer, char *errMsg) {
    char errs[4096];
    sprintf(errs, "ERROR\n%s\n", errMsg);
    bufWrite(buffer, errs, strlen(errs));
}


/**
 *
 * @param absPath
 * @param resultBuffer
 * @return
 */
int createFileList(char *absPath, LinkedBuffer *resultBuffer) {
    struct stat st;
    if (stat(absPath, &st) == -1 || !S_ISDIR(st.st_mode)) {
        char *errMsg = "No Such Directory";
        bufWrite(resultBuffer, errMsg, (ssize_t) strlen(errMsg));
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        return -1;
    }

    DIR *dirPtr = NULL;
    if (!(dirPtr = opendir(absPath))) {
        char *errMsg = "Failed to open folder";
        bufWrite(resultBuffer, errMsg, (ssize_t) strlen(errMsg));
        fprintf(stdout, "failed to open %s\n", absPath);
        return -1;
    }

    LinkedBuffer tmpBuffer;
    initBuffer(&tmpBuffer);

    size_t maxSizeWidth = 1;
    struct dirent *dirEntry = NULL; /* readdir函数的返回值就存放在这个结构体中 */
    while ((dirEntry = readdir(dirPtr)) != NULL) {
        /* 把当前目录.，上一级目录..及隐藏文件都去掉，避免死循环遍历目录 */
        if ((!strncmp(dirEntry->d_name, ".", 1)) || (!strncmp(dirEntry->d_name, "..", 2))) {
            continue;
        }

        char fileItem[2048] = {0};
        char fileRecord[1024] = {0};
        sprintf(fileItem, "%s/%s", absPath, dirEntry->d_name);
        stat(fileItem, &st);
        if (!S_ISDIR(st.st_mode)) {
            sprintf(fileRecord, "-\n%ld\n%s\n", st.st_size, dirEntry->d_name);
        } else {
            sprintf(fileRecord, "d\n%ld\n%s\n", st.st_size, dirEntry->d_name);
        }

        char strSize[32] = {0};
        sprintf(strSize, "%ld", st.st_size);
        size_t szWidth = strlen(strSize);
        if (szWidth > maxSizeWidth) {
            maxSizeWidth = szWidth;
        }

        bufWrite(&tmpBuffer, fileRecord, (ssize_t) strlen(fileRecord));
    }
    closedir(dirPtr);

    char line[1024] = {0};
    char formatLine[1050] = {0};
    int seq = 0;

    while (hasLine(&tmpBuffer, 1024) != -1) {
        memset(line, 0, 1024);
        memset(formatLine, 0, 1050);
        bufReadline(&tmpBuffer, line, 1024);
        switch (seq % 3) {
            case 2:
                sprintf(formatLine, "%s\n", line);
                break;
            case 1:
                sprintf(formatLine, "%*s ", (int) maxSizeWidth, line);
                break;
            default:
                sprintf(formatLine, "%s ", line);
        }
        bufWrite(resultBuffer, formatLine, (ssize_t) strlen(formatLine));
        seq++;
    }            //  LIST ../../../../../bin
    return 0;
}

/**
 *
 * @param connectCtx
 */
void handleList(ConnectCtx *connectCtx) {
    char absBase[1024] = {0};
    char absPath[1500] = {0};
    getcwd(absBase, 1024);
    sprintf(absPath, "%s/%s", absBase, connectCtx->req.fileName);

    LinkedBuffer contentBuffer = createBuffer();
    if (createFileList(absPath, &contentBuffer) == -1) {
        connectCtx->resp.status = ERROR;
        bufWrite(&connectCtx->resp.writeBuf, "ERROR\n", 6);
    } else {
        connectCtx->resp.status = OK;
        bufWrite(&connectCtx->resp.writeBuf, "OK\n", 3);
        connectCtx->resp.contentLength = getBufSize(&contentBuffer);
        bufWrite(&connectCtx->resp.writeBuf, (char *) &(connectCtx->resp.contentLength), sizeof(ssize_t));
    }

    bufCopy(&connectCtx->resp.writeBuf, &contentBuffer);
    clearBuf(&contentBuffer);

    connectCtx->resp.procState = SEND_HEADER;

    connectCtx->event.events = EPOLLET | EPOLLOUT;
    if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
        perror("epoll_ctl");
        abort();
    }
}

/**
 *
 * @param connectCtx
 */
void handleDelete(ConnectCtx *connectCtx) {
    char absBase[1024] = {0};
    char absPath[1500] = {0};
    getcwd(absBase, 1024);
    sprintf(absPath, "%s/%s", absBase, connectCtx->req.fileName);

    printf("DELETE '%s'\n", absPath);

    if (access(absPath, F_OK) == 0 && remove(absPath) == 0) {
        connectCtx->resp.status = OK;
        bufWrite(&connectCtx->resp.writeBuf, "OK\n", 3);
    } else {
        connectCtx->resp.status = ERROR;
        setErrMsg(&(connectCtx->resp.writeBuf), strerror(errno));
    }
    connectCtx->resp.procState = SEND_HEADER;

    connectCtx->event.events = EPOLLET | EPOLLOUT;
    if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        abort();
    }
}


/**
 *
 * @param connectCtx
 */
void handleGet(ConnectCtx *connectCtx) {
    char absBase[1024] = {0};
    char absPath[1500] = {0};
    getcwd(absBase, 1024);
    sprintf(absPath, "%s/%s", absBase, connectCtx->req.fileName);

    printf("GET [%s]\n", absPath);

    int fd = open(absPath, O_RDONLY);
    struct stat st;
    char errMsg[1024] = {0};
    connectCtx->openedFiles[0] = -1;
    if (fd == -1 || fstat(fd, &st) == -1) {
        connectCtx->resp.status = ERROR;
        char *strerr = strerror(errno);
        sprintf(errMsg, "%s: %s", connectCtx->req.fileName, strerr);
    } else if (!S_ISREG(st.st_mode)) {
        connectCtx->resp.status = ERROR;
        sprintf(errMsg, "%s is not a regular file", connectCtx->req.fileName);
    }

    if (connectCtx->resp.status == ERROR) {
        setErrMsg(&(connectCtx->resp.writeBuf), errMsg);
    } else {
        connectCtx->resp.status = OK;
        connectCtx->openedFiles[0] = fd;
        connectCtx->resp.contentLength = st.st_size;

        bufWrite(&(connectCtx->resp.writeBuf), "OK\n", 3);
        bufWrite(&connectCtx->resp.writeBuf, (char *) &(connectCtx->resp.contentLength), sizeof(ssize_t));
    }

    connectCtx->resp.procState = SEND_HEADER;

    connectCtx->event.events = EPOLLET | EPOLLOUT;
    if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        abort();
    }
}


int openLocalFile(char *fileName) {
    char absBase[1024] = {0};
    char absPath[1500] = {0};
    getcwd(absBase, 1024);
    sprintf(absPath, "%s/%s", absBase, fileName);

    fprintf(stderr, "POST [%s]\n", absPath);

    int outFd = open(absPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG);
    if (outFd == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        return OPEN_ERROR;
    } else {
        return outFd;
    }
}


/**
 * 处理POST请求
 * @param connectCtx
 */
void handlePost(ConnectCtx *connectCtx) {
    //获取文件长度8个字节, 然后把打开本地文件存储数据
    if (connectCtx->req.reqProcState == PARSE_CONTENT_LEN) {
        const int bitWidth = sizeof(ssize_t);
        if (getBufSize(&(connectCtx->req.readBuf)) >= bitWidth) {
            bufRead(&(connectCtx->req.readBuf), (char *) &(connectCtx->req.contentLength), bitWidth);
            fprintf(stderr, "contentLength: %ld\n", connectCtx->req.contentLength);

            //打开本地文件，接受客户端发送的数据
            connectCtx->openedFiles[0] = openLocalFile(connectCtx->req.fileName);
            if (connectCtx->openedFiles[0] == OPEN_ERROR) {
                connectCtx->resp.status = ERROR;
                setErrMsg(&(connectCtx->resp.writeBuf), strerror(errno));
            }
            connectCtx->req.reqProcState = RECV_POST;
        } else {
            return;
        }
    }

    //接受上传的数据
    if (connectCtx->req.reqProcState == RECV_POST) {
        ssize_t hasGot = 0;
        //由于进入该函数前，数据在hanleRead函数执行了一次，读取量一般会超过BYTES_HANDLE_ONE_TIME，
        //所以一般不会进入第一个if分支，while循环也只执行一次。
        while (hasGot < BYTES_HANDLE_ONE_TIME) {
            ssize_t bufSize = getBufSize(&(connectCtx->req.readBuf));
            //如果需要被写入文件的缓冲区已经没有数据，那么从套接字中读取一次
            if (bufSize == 0) {
                ssize_t expectedByteCnt = connectCtx->req.contentLength - connectCtx->req.hasRecvd;
                if (expectedByteCnt > BYTES_HANDLE_ONE_TIME) {
                    expectedByteCnt = BYTES_HANDLE_ONE_TIME;
                }

                ssize_t realCnt = 0;
                ssize_t retVal = copyNByteToBuf(&connectCtx->req.readBuf, connectCtx->socketFd, expectedByteCnt, &realCnt);

                if (retVal > 0) { //还有数据要读

                } else if (retVal == 0) {//数据读完
                    connectCtx->req.reqProcState = DONE;
                } else if (retVal == -EAGAIN) { //读阻塞, 直接返回
                    return;
                } else {//读出错
                    connectCtx->resp.status = ERROR;
                    setErrMsg(&connectCtx->resp.writeBuf, strerror(errno));
                    connectCtx->resp.procState = SEND_HEADER;
                }
            }

            bufSize = getBufSize(&connectCtx->req.readBuf);
            if (bufSize == 0) {
                break;
            }

            hasGot += bufSize;
            connectCtx->req.hasRecvd += bufSize;//更新已读数据量

            ssize_t n = bufSize;
            if (connectCtx->openedFiles[0] != OPEN_ERROR) {
                n = bufReadToFile(&connectCtx->req.readBuf, connectCtx->openedFiles[0], bufSize);
            } else {
                clearBuf(&connectCtx->req.readBuf);
            }

            if (n != bufSize) {
                connectCtx->openedFiles[0] = OPEN_ERROR;
                connectCtx->resp.status = ERROR;
                setErrMsg(&(connectCtx->resp.writeBuf), "Failed to write byte to remote file");
            }
        }

        connectCtx->event.events = EPOLLET | EPOLLIN; //继续监听
        if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
            fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
            abort();
        }

        //printf("%ld %d\n", hasGot, BYTES_HANDLE_ONE_TIME);
    }


    if (connectCtx->req.reqProcState == DONE) {
        connectCtx->event.events = EPOLLET | EPOLLOUT;
        if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
            fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
            abort();
        }

        if (connectCtx->openedFiles[0] != OPEN_ERROR && connectCtx->req.contentLength != connectCtx->req.hasRecvd) {
            connectCtx->resp.status = ERROR;
            setErrMsg(&(connectCtx->resp.writeBuf),
                      connectCtx->req.contentLength < connectCtx->req.hasRecvd ? "Send too many bytes to server" : "Send too less bytes to server" );
        }

        //没有发生错误，就是OK
        if (connectCtx->resp.status != ERROR) {
            connectCtx->resp.status = OK;
            bufWrite(&connectCtx->resp.writeBuf, "OK\n", 3);
        }
        connectCtx->resp.procState = SEND_HEADER;
    }
}


void handleError(ConnectCtx *connectCtx) {
    connectCtx->resp.status = ERROR;
    char *head = "ERROR\nBad Request";
    bufWrite(&connectCtx->resp.writeBuf, head, (ssize_t) strlen(head));
    connectCtx->resp.procState = SEND_HEADER;

    connectCtx->event.events = EPOLLET | EPOLLOUT;
    if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        abort();
    }
}