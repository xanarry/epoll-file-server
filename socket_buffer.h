//
// Created by xanarry on 2021/8/22.
//

#ifndef EPOLL_SOCKET_BUFFER_H
#define EPOLL_SOCKET_BUFFER_H

#define BUF_SEG_SIZE 4096

typedef struct BufNode {
    ssize_t seq;
    char buf[BUF_SEG_SIZE];
    struct BufNode *next;
} BufNode;

typedef struct socketBuffer {
    ssize_t readOffset;
    BufNode *writeNode;
    ssize_t writeOffset;
    BufNode *bufHead;
} SocketBuffer;

SocketBuffer initBuffer();

BufNode *newNode(ssize_t seq);

//自动扩容
ssize_t bufWrite(SocketBuffer *sktBuffer, char *buf, ssize_t size);

ssize_t copyToBuf(SocketBuffer *sktBuffer, int fd);

ssize_t copyNByteToBuf(SocketBuffer *sktBuffer, int fd, ssize_t bytesToRead, ssize_t *);

//自动清出
ssize_t bufRead(SocketBuffer *sktBuffer, char *buf, ssize_t size);

ssize_t bufReadToFile(SocketBuffer *netBuffer, int fd, ssize_t size);


ssize_t bufReadline(SocketBuffer *sktBuffer, char *buf, ssize_t size);

int hasLine(SocketBuffer *sktBuffer, int maxLineSize);

void clear(SocketBuffer *sktBuffer);

ssize_t getBufSize(SocketBuffer *netBuffer);


#endif //EPOLL_SOCKET_BUFFER_H
