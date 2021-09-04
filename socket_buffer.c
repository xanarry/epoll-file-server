//
// Created by xanarry on 2021/8/22.
//


#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "socket_buffer.h"


SocketBuffer initBuffer() {
    SocketBuffer buffer;
    buffer.readOffset = 0;
    buffer.writeOffset = 0;
    buffer.bufHead = NULL;
    buffer.writeNode = buffer.bufHead;
    return buffer;
}


BufNode *newNode(ssize_t seq) {
    BufNode *node = (BufNode *) malloc(sizeof(BufNode));
    if (node == NULL) {
        fprintf(stderr, "%s %d malloc %s", __FILE__, __LINE__, strerror(errno));
        return NULL;
    }
    memset(node, 0, sizeof(BufNode));
    node->seq = seq;
    return node;
}

/**
 * 将字节写入缓冲区，自动扩容，容量没有限制
 * @param sktBuffer
 * @param buf
 * @param size
 * @return 返回实际写入的字节数
 */
ssize_t bufWrite(SocketBuffer *sktBuffer, char *buf, const ssize_t size) {
    if (sktBuffer->writeOffset == 0 || sktBuffer->writeOffset % BUF_SEG_SIZE == 0 /*|| getBufSize(sktBuffer) == 0*/) { //last node full
        BufNode *node = newNode(0);
        if (node == NULL) {
            return -1;
        }

//        if (getBufSize(sktBuffer) == 0) {
//            sktBuffer->readOffset = 0;
//            sktBuffer->writeOffset = 0;
//        }

        if (sktBuffer->bufHead == NULL) {
            sktBuffer->bufHead = node;
        } else {
            node->seq = sktBuffer->writeNode->seq + 1;
            sktBuffer->writeNode->next = node;
        }
        sktBuffer->writeNode = node;
    }
    ssize_t nodeRemain = BUF_SEG_SIZE - sktBuffer->writeOffset % BUF_SEG_SIZE;
    ssize_t writeCnt = nodeRemain;
    ssize_t inputOffset = 0;
    if (nodeRemain > 0) {
        if (size < nodeRemain) {
            writeCnt = size;
        }
        memcpy(sktBuffer->writeNode->buf + (sktBuffer->writeOffset % BUF_SEG_SIZE), buf + inputOffset, writeCnt);
        sktBuffer->writeOffset += writeCnt;
        inputOffset += writeCnt;

        if (size < nodeRemain) {
            return sktBuffer->writeOffset;
        }
    }
    ssize_t byteRemain = size - writeCnt;
    while (byteRemain > 0) {
        BufNode *node = newNode(sktBuffer->writeNode->seq + 1);
        if (node == NULL) {
            return -1;
        }
        sktBuffer->writeNode->next = node;
        sktBuffer->writeNode = sktBuffer->writeNode->next;
        writeCnt = BUF_SEG_SIZE;
        if (byteRemain < BUF_SEG_SIZE) {
            writeCnt = byteRemain;
        }

        memcpy(sktBuffer->writeNode->buf + (sktBuffer->writeOffset % BUF_SEG_SIZE), buf + inputOffset, writeCnt);
        sktBuffer->writeOffset += writeCnt;
        inputOffset += writeCnt;
        byteRemain -= writeCnt;
    }
    //fprintf(stderr, "%s %d write to buffer %ld\n", __FILE__, __LINE__, sktBuffer->writeOffset);
    return sktBuffer->writeOffset;
}

/**
 * 从套接字中读取数据，直到读到EOF，或者读完缓冲区，或者遇到错误。读过的数据会被自动释放
 * @param sktBuffer
 * @param fd
 * @return 返回值与read一样，返回-2表示缓冲区内存错误。
 */
ssize_t copyToBuf(SocketBuffer *sktBuffer, int fd) {
    char buf[BUF_SEG_SIZE];
    ssize_t len = 0;
    while (1) {
        len = read(fd, buf, BUF_SEG_SIZE);
        //fprintf(stderr, "%s %d read retrun %ld\n", __FILE__, __LINE__, len);
        if (len > 0) { //读取到数据，将数据写入缓冲区
            if (bufWrite(sktBuffer, buf, len) == -1) {
                return -2; //缓冲区分配内存错误，使用-2区别与read本身返回的-1
            }
        } else {
            break;
        }
    }
    return len; //原样返回read的返回值
}

/**
 * 从文件描述符中读取指定字节数到缓冲区，实际读取的字节数<=指定的字节数。
 * @param sktBuffer
 * @param fd
 * @param n 想要读取的字节数
 * @param copiedSize 实际读取的字节数
 * @return 如果顺利读满指定字节数，返回值为最后一次read返回的值，实际读取的字节数保存到copiedSize中
 */
ssize_t copyNByteToBuf(SocketBuffer *sktBuffer, int fd, const ssize_t n, ssize_t *copiedSize) {
    char buf[BUF_SEG_SIZE];
    ssize_t copiedCnt = 0;
    ssize_t rdRet = 0;
    while (copiedCnt < n) {
        ssize_t remain = n - copiedCnt;
        ssize_t readLen = BUF_SEG_SIZE;
        if (remain < BUF_SEG_SIZE) {
            readLen = remain;
        }

        rdRet = read(fd, buf, readLen);
        if (rdRet > 0) {
            if (bufWrite(sktBuffer, buf, rdRet) == -1) {
                return -2;//mem error
            }
            copiedCnt += rdRet;
        } else  {
            break;
        }
    }
    *copiedSize = copiedCnt;
    return rdRet;
}

/**
 * 从缓冲区对象中读取字节到指定数组
 * @param sktBuffer
 * @param buf
 * @param sz
 * @return 返回实际读取到的字节数
 */
ssize_t bufRead(SocketBuffer *sktBuffer, char *buf, const ssize_t sz) {
    ssize_t availSize = getBufSize(sktBuffer);
    if (availSize <= 0) {
        return 0; //没有数据可以读
    }

    ssize_t size = sz;
    if (availSize < sz) {
        size = availSize; //实际可读数据小于预期的数据，设置读取量为实际可用量
    }


    ssize_t readCnt = size; //当前链表节点可以读的字节数
    ssize_t hasRead = 0; //全局已读取字节数

    //对于链表的第一个节点特殊处理
    if (sktBuffer->readOffset != 0 && sktBuffer->readOffset % BUF_SEG_SIZE != 0) {
        ssize_t nodeRemain = BUF_SEG_SIZE - (sktBuffer->readOffset % BUF_SEG_SIZE);
        if (nodeRemain < readCnt) {
            readCnt = nodeRemain; //如果链表节点内字节数不足预期，那么最多只能读取本节点剩下的字节数
        }
        memcpy(buf + hasRead, sktBuffer->bufHead->buf + (sktBuffer->readOffset % BUF_SEG_SIZE), readCnt);
        sktBuffer->readOffset += readCnt;
        hasRead += readCnt;

        //正好读完一个节点
        if (sktBuffer->readOffset % BUF_SEG_SIZE == 0 /*|| getBufSize(sktBuffer) == 0*/) {
            BufNode *old = sktBuffer->bufHead;
            sktBuffer->bufHead = sktBuffer->bufHead->next;
            free(old);
        }

        if (hasRead == size) {
            return size;
        }
    }

    //循环读取后面的链表
    while (hasRead < size) {
        readCnt = size - hasRead; //计算还要读取的量
        if (readCnt >= BUF_SEG_SIZE) {
            readCnt = BUF_SEG_SIZE; //每次读取的最大量为BUF_SEG_SIZE
        }
        memcpy(buf + hasRead, sktBuffer->bufHead->buf + (sktBuffer->readOffset % BUF_SEG_SIZE), readCnt);
        sktBuffer->readOffset += readCnt;
        hasRead += readCnt;

        if (readCnt == BUF_SEG_SIZE/* || getBufSize(sktBuffer) == 0*/) {
            BufNode *old = sktBuffer->bufHead;
            sktBuffer->bufHead = sktBuffer->bufHead->next;
            free(old);
        }
    }

    return hasRead;
}

/**
 * 从缓冲区对象中读取字节写入到指定文件描述符
 * @param netBuffer
 * @param fd
 * @param sz
 * @return 返回实际写入成功的数据量
 */
ssize_t bufReadToFile(SocketBuffer *sktBuffer, int fd, const ssize_t sz) {
    ssize_t availSize = getBufSize(sktBuffer);
    if (availSize <= 0) {
        return 0;
    }

    ssize_t size = sz;
    if (availSize < sz) {
        size = availSize;
    }

    ssize_t readCnt = size; //当前链表节点可以读的字节数
    ssize_t hasRead = 0; //全局已读取字节数

    //对于链表的第一个节点特殊处理
    if (sktBuffer->readOffset != 0 && sktBuffer->readOffset % BUF_SEG_SIZE != 0) {
        ssize_t nodeRemain = BUF_SEG_SIZE - (sktBuffer->readOffset % BUF_SEG_SIZE);
        if (nodeRemain < readCnt) {
            readCnt = nodeRemain;
        }

        ssize_t wrRet = write(fd, sktBuffer->bufHead->buf + (sktBuffer->readOffset % BUF_SEG_SIZE), readCnt);
        if (wrRet == -1) {
            return -1;
        } else {
            sktBuffer->readOffset += wrRet; //成功写入到fd中的字节才能算是已经从缓冲区中读走
            hasRead += wrRet;
        }


        if (sktBuffer->readOffset % BUF_SEG_SIZE == 0) {
            BufNode *old = sktBuffer->bufHead;
            sktBuffer->bufHead = sktBuffer->bufHead->next;
            free(old);
        }

        if (hasRead == size) {
            return size;
        }
    }

    //循环读取后面的链表
    while (hasRead < size) {
        readCnt = size - hasRead;
        if (readCnt >= BUF_SEG_SIZE) {
            readCnt = BUF_SEG_SIZE;
        }

        ssize_t wrRet =  write(fd, sktBuffer->bufHead->buf + (sktBuffer->readOffset % BUF_SEG_SIZE), readCnt);
        if (wrRet == -1) {
            return -1;
        } else {
            sktBuffer->readOffset += wrRet;
            hasRead += wrRet;
        }

        if (readCnt == BUF_SEG_SIZE) {
            BufNode *old = sktBuffer->bufHead;
            sktBuffer->bufHead = sktBuffer->bufHead->next;
            free(old);
        }
    }

    return hasRead;
}


/**
 * 冲缓冲区中读取一行，不包括'\n'
 * @param sktBuffer
 * @param line 行缓冲区
 * @param size 一行的最大长度限制
 * @return 返回一行数的字符数
 */
ssize_t bufReadline(SocketBuffer *sktBuffer, char *line, const ssize_t size) {
    ssize_t idx = 0;
    while (idx < size) {
        char buf[1];
        if (bufRead(sktBuffer, buf, 1) == 0) { //没有数据可以读则返回
            break;
        }

        if (buf[0] == '\n' || buf[0] == '\r') { //读到行末尾，返回
            break;
        }

        line[idx++] = buf[0];
    }
    line[idx] = '\0'; //添加字符串结束符
    return idx;
}

/**
 * 检查缓冲区中是否存在行
 * @param sktBuffer
 * @param maxLineSize
 * @return -1 means has no line
 */
int hasLine(SocketBuffer *sktBuffer, int maxLineSize) {
    BufNode *cur = sktBuffer->bufHead;
    for (int i = 0; i < maxLineSize; i++) {
        ssize_t readIdx = sktBuffer->readOffset + i;
        if (readIdx >= sktBuffer->writeOffset) {
            return -1;
        }

        if (readIdx != 0 && readIdx % BUF_SEG_SIZE == 0) {
            cur = cur->next;
        }

        if (cur->buf[readIdx % BUF_SEG_SIZE] == '\n') {
            return i;
        }
    }
    return -1;
}


/**
 * 清空缓冲区
 * @param sktBuffer
 */
void clear(SocketBuffer *sktBuffer) {
    if (sktBuffer->bufHead == NULL) {
        return;
    }

    BufNode *cur = sktBuffer->bufHead;
    while (cur) {
        BufNode *next = sktBuffer->bufHead->next;
        free(cur);
        cur = next;
    }
}


/**
 * 获取缓冲区的可用大小
 * @param buffer
 * @return 可读大小
 */
ssize_t getBufSize(SocketBuffer *buffer) {
    if (buffer == NULL) {
        return 0;
    }
    return buffer->writeOffset - buffer->readOffset;
}