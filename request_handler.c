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
        bufWrite(&connectCtx->resp.writeBuf, "ERROR\n", 6);
        char *strErr = strerror(errno);
        bufWrite(&(connectCtx->resp.writeBuf), strErr, (ssize_t) strlen(strErr));
    }
    connectCtx->resp.procState = SEND_HEADER;

    connectCtx->event.events = EPOLLET | EPOLLOUT;
    if (epoll_ctl(connectCtx->epollFd, EPOLL_CTL_MOD, connectCtx->socketFd, &connectCtx->event) == -1) {
        fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
        abort();
    }
}


uint64_t ntoh64(const uint64_t *input)
{
    uint64_t rval;
    uint8_t *data = (uint8_t *)&rval;

    data[0] = *input >> 56;
    data[1] = *input >> 48;
    data[2] = *input >> 40;
    data[3] = *input >> 32;
    data[4] = *input >> 24;
    data[5] = *input >> 16;
    data[6] = *input >> 8;
    data[7] = *input >> 0;

    return rval;
}

uint64_t hton64(const uint64_t *input)
{
    return (ntoh64(input));
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
        bufWrite(&(connectCtx->resp.writeBuf), "ERROR\n", 6);
        bufWrite(&(connectCtx->resp.writeBuf), errMsg, strlen(errMsg));
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


/**
 *
 * @param connectCtx
 */
void handlePost(ConnectCtx *connCtx) {
    if (connCtx->req.reqProcState == PARSE_CONTENT_LEN) {
        //获取文件长度8个字节
        const int bitWidth = sizeof(uint64_t);
        uint64_t contentLength = 0;
        if (getBufSize(&(connCtx->req.readBuf)) >= bitWidth) {
            bufRead(&(connCtx->req.readBuf), (char *) &contentLength, bitWidth);
            connCtx->req.contentLength = ntoh64(&contentLength);
            fprintf(stderr, "contentLength: %ld\n", connCtx->req.contentLength);
            connCtx->req.reqProcState = RECV_POST;
        } else {
            return;
        }
    }

    if (connCtx->req.reqProcState != RECV_POST) {
        return;
    }

    if (connCtx->openedFiles[0] == -1) {
        char absBase[1024] = {0};
        char absPath[1500] = {0};
        getcwd(absBase, 1024);
        sprintf(absPath, "%s/%s", absBase, connCtx->req.fileName);

        fprintf(stderr, "POST [%s]\n", absPath);

        int outFd = open(absPath, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IRWXG);
        if (outFd == -1) {
            fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
            //todo need to handle error
        }
        connCtx->openedFiles[0] = outFd;
    }


    ssize_t hasGot = 0;
    while (hasGot < BYTES_HANDLE_ONE_TIME) {
        ssize_t bufSize = getBufSize(&(connCtx->req.readBuf));
        if (bufSize == 0) {
            ssize_t len = copyToBuf(&connCtx->req.readBuf, connCtx->socketFd);
            if (len == 0) {
                close(connCtx->openedFiles[0]);
                connCtx->resp.status = OK;
                connCtx->resp.procState = SEND_HEADER;
                connCtx->event.events = EPOLLET | EPOLLOUT;
                if (epoll_ctl(connCtx->epollFd, EPOLL_CTL_MOD, connCtx->socketFd, &connCtx->event) == -1) {
                    fprintf(stderr, "%s %d %s\n", __FILE__, __LINE__, strerror(errno));
                    abort();
                }
                return;
            } else if (len == -1) {
                //fprintf(stdout, "%s %d %s\n", __FILE__, __LINE__, "read get -1");
                if (errno == EAGAIN) {
                    connCtx->resp.procState = SEND_HEADER;
                    return;
                } else {
                    abort();
                }
            } else if (len == -2) {
                fprintf(stdout, "%s %d malloc %s", __FILE__, __LINE__, strerror(errno));
                abort();
            }
        }

        bufSize = getBufSize(&connCtx->req.readBuf);
        if (bufSize > 0) {
            ssize_t n = bufReadToFile(&connCtx->req.readBuf, connCtx->openedFiles[0], bufSize);
            if (n > 0) {
                hasGot += n;
            } else {
                return;
            }
        }
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