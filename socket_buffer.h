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
} LinkedBuffer;

LinkedBuffer createBuffer();
void initBuffer(LinkedBuffer *buffer);

BufNode *newNode(ssize_t seq);

//自动扩容
ssize_t bufWrite(LinkedBuffer *sktBuffer, char *buf, ssize_t size);

ssize_t copyToBuf(LinkedBuffer *sktBuffer, int fd);

ssize_t copyNByteToBuf(LinkedBuffer *sktBuffer, int fd, ssize_t bytesToRead, ssize_t *);

//自动清出
ssize_t bufRead(LinkedBuffer *bufObj, char *buf, ssize_t size);

ssize_t bufReadToFile(LinkedBuffer *netBuffer, int fd, ssize_t size);


ssize_t bufReadline(LinkedBuffer *sktBuffer, char *buf, ssize_t size);

int hasLine(LinkedBuffer *sktBuffer, int maxLineSize);

void clearBuf(LinkedBuffer *sktBuffer);

ssize_t getBufSize(LinkedBuffer *);

size_t bufCopy(LinkedBuffer *desc, LinkedBuffer *src);


#endif //EPOLL_SOCKET_BUFFER_H
