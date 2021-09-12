//
// Created by xanarry on 2021/8/29.
//

#ifndef EPOLL_REQUEST_HANDLER_H
#define EPOLL_REQUEST_HANDLER_H

#include "socket_buffer.h"

#define OPEN_ERROR 0x7FFFFFFF

enum ReqProcState {
    PARSE_HEAD,
    PARSE_CONTENT_LEN,
    RECV_POST,
    DONE
};

enum Method {
    BAD_REQ,
    GET,
    POST,
    LIST,
    DELETE
};

enum RespStatus {
    OK,
    ERROR
};

typedef struct Request {
    enum Method method;
    LinkedBuffer readBuf;
    ssize_t hasRecvd;
    ssize_t contentLength;
    char fileName[256];

    enum ReqProcState reqProcState;
} Request;

enum RespProcState {
    SEND_HEADER,
    SEND_CONTENT,
    FINISH
};

typedef struct Response {
    enum RespStatus status;
    LinkedBuffer writeBuf;
    ssize_t contentLength;

    enum RespProcState procState;
} Response;

typedef struct connect {
    int epollFd;
    struct epoll_event event;
    int socketFd;
    int openedFiles[128];
    Request req;
    Response resp;
} ConnectCtx;


void handleList(ConnectCtx *connectCtx);


void handleDelete(ConnectCtx *connectCtx);


void handleGet(ConnectCtx *connectCtx);


void handlePost(ConnectCtx *connectCtx);


void handleError(ConnectCtx *connCtx);

#endif //EPOLL_REQUEST_HANDLER_H
