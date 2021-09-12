//
// Created by xanarry on 2021/8/22.
//


#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include "socket_buffer.h"

LinkedBuffer createBuffer() {
    LinkedBuffer buffer;
    buffer.readOffset = 0;
    buffer.writeOffset = 0;
    buffer.bufHead = NULL;
    buffer.writeNode = buffer.bufHead;
    return buffer;
}


void initBuffer(LinkedBuffer *bufferObj) {
    bufferObj->readOffset = 0;
    bufferObj->writeOffset = 0;
    bufferObj->bufHead = NULL;
    bufferObj->writeNode = bufferObj->bufHead;
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
 * 从缓冲区对象中读取字节到指定数组
 *
 * 读的时候先读取第一个第一个节点的数据，因为第一个节点可能不是满的，或者第一个节点的数据量就满足需求.
 * 读完一个节点的字节时候，自动释放掉该节点的内存。
 *
 * @param bufferObj
 * @param buf
 * @param sz
 * @return 返回实际读取到的字节数，实际返回值可能小于预期值
 */
ssize_t bufRead(LinkedBuffer *bufferObj, char *buf, const ssize_t sz) {
    ssize_t availSize = getBufSize(bufferObj);
    if (availSize <= 0) {
        return 0; //没有数据可以读
    }

    ssize_t size = sz;
    if (availSize < sz) {
        size = availSize; //实际可读数据小于预期的数据，设置读取量为实际可用量
    }


    ssize_t readCnt = size; //当前链表节点可以读的字节数
    ssize_t hasRead = 0; //全局已读取字节数

    //如果链表的第一个节点有数据要读，特殊处理
    ssize_t readOffset = bufferObj->readOffset % BUF_SEG_SIZE;
    if (readOffset != 0) {
        ssize_t nodeRemain = BUF_SEG_SIZE - readOffset;
        if (nodeRemain < readCnt) {
            readCnt = nodeRemain; //如果链表节点内字节数不足预期，那么最多只能读取本节点剩下的字节数
        }
        memcpy(buf + hasRead, bufferObj->bufHead->buf + readOffset, readCnt);
        bufferObj->readOffset += readCnt;
        hasRead += readCnt;

        //正好读完一个节点, 或者读完全部数据
        if (bufferObj->readOffset % BUF_SEG_SIZE == 0 || getBufSize(bufferObj) == 0) {
            BufNode *old = bufferObj->bufHead;
            bufferObj->bufHead = bufferObj->bufHead->next;
            free(old);
        }
    }

    //循环读取后面的链表
    while (hasRead < size) {
        readCnt = size - hasRead; //计算还要读取的量
        if (readCnt >= BUF_SEG_SIZE) {
            readCnt = BUF_SEG_SIZE; //每次读取的最大量为BUF_SEG_SIZE
        }
        memcpy(buf + hasRead, bufferObj->bufHead->buf + (bufferObj->readOffset % BUF_SEG_SIZE), readCnt);
        bufferObj->readOffset += readCnt;
        hasRead += readCnt;

        if (readCnt == BUF_SEG_SIZE || getBufSize(bufferObj) == 0) {
            BufNode *old = bufferObj->bufHead;
            bufferObj->bufHead = bufferObj->bufHead->next;
            free(old);
        }
    }

    return hasRead;
}

/**
 * 将字节写入缓冲区，自动扩容，容量没有限制
 *
 * 首先判断第一个节点是否读完，没有则特殊处理。后续节点放到循环中
 *
 * @param bufferObj
 * @param buf
 * @param size
 * @return 返回实际写入的字节数，如果申请内存失败，返回-1
 */
ssize_t bufWrite(LinkedBuffer *bufferObj, char *buf, const ssize_t size) {
    ssize_t writeCnt = 0;
    ssize_t inputOffset = 0;
    ssize_t wrOffsetOfLastNode = bufferObj->writeOffset % BUF_SEG_SIZE;
    if (getBufSize(bufferObj) != 0 && wrOffsetOfLastNode != 0) { //末尾节点没有写满
        writeCnt = BUF_SEG_SIZE - wrOffsetOfLastNode; //写入量为剩下的空间
        if (size < writeCnt) {
            writeCnt = size;
        }
        memcpy(bufferObj->writeNode->buf + wrOffsetOfLastNode, buf + inputOffset, writeCnt);
        bufferObj->writeOffset += writeCnt;
        inputOffset += writeCnt;
    }

    //如果写入节点为一个新节点的起点
    ssize_t byteRemain = size - writeCnt;
    while (byteRemain > 0) {
        ssize_t seq = 0;
        if (getBufSize(bufferObj) == 0) { //如果是数据被读完/初始化的情况，重置缓冲对象
            initBuffer(bufferObj);
        } else {
            seq = bufferObj->writeNode->seq + 1;
        }

        BufNode *node = newNode(seq);
        if (node == NULL) {
            return -1;
        }

        if (bufferObj->bufHead == NULL) {
            bufferObj->bufHead = node; //初始化状态
        } else { //末尾节点写满状态
            bufferObj->writeNode->next = node;
        }
        bufferObj->writeNode = node;

        writeCnt = BUF_SEG_SIZE;
        if (byteRemain < BUF_SEG_SIZE) {
            writeCnt = byteRemain;
        }

        memcpy(bufferObj->writeNode->buf + (bufferObj->writeOffset % BUF_SEG_SIZE), buf + inputOffset, writeCnt);
        bufferObj->writeOffset += writeCnt;
        inputOffset += writeCnt;
        byteRemain -= writeCnt;
    }
    //fprintf(stderr, "%s %d write to buffer %ld\n", __FILE__, __LINE__, bufferObj->writeOffset);
    return bufferObj->writeOffset;
}

/**
 * 从套接字中读取数据，直到读到EOF，或者读完缓冲区，或者遇到错误。读过的数据会被自动释放
 * @param sktBuffer
 * @param fd
 * @return 返回值与read一样，返回-2表示缓冲区内存错误。
 */
ssize_t copyToBuf(LinkedBuffer *sktBuffer, int fd) {
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
 * @param bufferObj
 * @param fd
 * @param n 想要读取的字节数
 * @param copiedSize 实际读取的字节数
 * @return 如果顺利读满指定字节数，返回值为最后一次read返回的值，实际读取的字节数保存到copiedSize中
 */
ssize_t copyNByteToBuf(LinkedBuffer *bufferObj, int fd, const ssize_t n, ssize_t *copiedSize) {
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
            if (bufWrite(bufferObj, buf, rdRet) == -1) {
                return -2;//mem error
            }
            copiedCnt += rdRet;
        } else  {
            break;
        }
    }
    *copiedSize = copiedCnt;

    if (rdRet == -1 && errno == EAGAIN) {
        return -EAGAIN;
    }
    return rdRet;
}


/**
 * 从缓冲区对象中读取字节写入到指定文件描述符
 * @param netBuffer
 * @param fd
 * @param sz
 * @return 返回实际写入成功的数据量，出错时返回write的返回值
 */
ssize_t bufReadToFile(LinkedBuffer *bufferObj, int fd, const ssize_t sz) {
    ssize_t availSize = getBufSize(bufferObj);
    if (availSize <= 0) {
        return 0;
    }

    ssize_t size = sz;
    if (availSize < sz) {
        size = availSize;
    }

    ssize_t readCnt = size; //当前链表节点可以读的字节数
    ssize_t hasRead = 0; //全局已读取字节数

    //如果链表的第一个节点有数据要读，特殊处理
    ssize_t readOffset = bufferObj->readOffset % BUF_SEG_SIZE;
    if (readOffset != 0) {
        ssize_t nodeRemain = BUF_SEG_SIZE - readOffset;
        if (nodeRemain < readCnt) {
            readCnt = nodeRemain;
        }

        ssize_t wrRet = write(fd, bufferObj->bufHead->buf + readOffset, readCnt);
        if (wrRet == -1) {
            return -1;
        } else {
            bufferObj->readOffset += wrRet; //成功写入到fd中的字节才能算是已经从缓冲区中读走
            hasRead += wrRet;
        }


        if (bufferObj->readOffset % BUF_SEG_SIZE == 0 || getBufSize(bufferObj) == 0) {
            BufNode *old = bufferObj->bufHead;
            bufferObj->bufHead = bufferObj->bufHead->next;
            free(old);
        }
    }

    //循环读取后面的链表
    while (hasRead < size) {
        readCnt = size - hasRead;
        if (readCnt >= BUF_SEG_SIZE) {
            readCnt = BUF_SEG_SIZE;
        }

        ssize_t wrRet =  write(fd, bufferObj->bufHead->buf + (bufferObj->readOffset % BUF_SEG_SIZE), readCnt);
        if (wrRet == -1) {
            return -1;
        } else {
            bufferObj->readOffset += wrRet;
            hasRead += wrRet;
        }

        if (readCnt == BUF_SEG_SIZE || getBufSize(bufferObj) == 0) {
            BufNode *old = bufferObj->bufHead;
            bufferObj->bufHead = bufferObj->bufHead->next;
            free(old);
        }
    }

    return hasRead;
}


/**
 * 冲缓冲区中读取一行，不包括'\n'
 * @param bufferObj
 * @param line 行缓冲区
 * @param size 一行的最大长度限制
 * @return 返回一行数的字符数
 */
ssize_t bufReadline(LinkedBuffer *bufferObj, char *line, const ssize_t size) {
    ssize_t idx = 0;
    while (idx < size) {
        char buf[1];
        if (bufRead(bufferObj, buf, 1) == 0) { //没有数据可以读则返回
            break;
        }

        if (buf[0] == '\n' || buf[0] == '\r' || getBufSize(bufferObj) == 0) { //读到行末尾，返回
            break;
        }

        line[idx++] = buf[0];
    }
    line[idx] = '\0'; //添加字符串结束符
    return idx;
}

/**
 * 检查缓冲区中是否存在行
 * @param bufferObj
 * @param maxLineSize
 * @return -1 means has no line
 */
int hasLine(LinkedBuffer *bufferObj, int maxLineSize) {
    BufNode *cur = bufferObj->bufHead;
    for (int i = 0; i < maxLineSize; i++) {
        ssize_t readIdx = bufferObj->readOffset + i;
        if (readIdx >= bufferObj->writeOffset) {
            return -1;
        }

        if (readIdx != bufferObj->readOffset && readIdx % BUF_SEG_SIZE == 0) {
            cur = cur->next;
        }

        if (cur != NULL && (cur->buf[readIdx % BUF_SEG_SIZE] == '\n' || bufferObj->writeOffset == readIdx)) {
            return i;
        }
    }
    return -1;
}


/**
 * 清空缓冲区
 * @param bufferObj
 */
void clearBuf(LinkedBuffer *bufferObj) {
    if (bufferObj->bufHead == NULL) {
        return;
    }

    BufNode *cur = bufferObj->bufHead;
    while (cur != NULL) {
        BufNode *next = cur->next;
        free(cur);
        cur = next;
    }

    initBuffer(bufferObj);
}


/**
 * 获取缓冲区的可用大小
 * @param bufferObj
 * @return 可读大小
 */
ssize_t getBufSize(LinkedBuffer *bufferObj) {
    if (bufferObj == NULL) {
        return 0;
    }
    return bufferObj->writeOffset - bufferObj->readOffset;
}


/**
 * 复制一个缓冲对象的数据到另一个缓冲对象，且不清除原始对象的数据
 * @param desc
 * @param src
 * @return 成功复制返回0，发生错误返回-1
 */
size_t bufCopy(LinkedBuffer *desc, LinkedBuffer *src) {
    ssize_t offset = src->readOffset % BUF_SEG_SIZE;
    BufNode *cur = src->bufHead;
    ssize_t len = 0;
    //先处理第一个节点
    if (offset != 0) {
        len = bufWrite(desc, cur->buf + offset, BUF_SEG_SIZE - offset);
        if (len < 0) {
            return -1;
        }
        cur = cur->next;
    }

    //处理后续节点

    while (cur != NULL) {
        if (cur == src->writeNode) {
            ssize_t cnt = src->writeOffset % BUF_SEG_SIZE;
            if (cnt == 0) {
                cnt = BUF_SEG_SIZE;
            }

            if (bufWrite(desc, cur->buf, cnt) == -1) {
                return -1;
            }
            break;
        } else {
            if (bufWrite(desc, cur->buf, BUF_SEG_SIZE) == -1) {
                return -1;
            }
        }
        cur = cur->next;
    }
    return 0;
}