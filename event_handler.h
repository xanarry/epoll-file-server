//
// Created by xanarry on 2021/8/29.
//

#ifndef EPOLL_EVENT_HANDLER_H
#define EPOLL_EVENT_HANDLER_H

#define BYTES_HANDLE_ONE_TIME 32768

void handleWrite(int efd, struct epoll_event event);
void handleRead(int efd, struct epoll_event event);


#endif //EPOLL_EVENT_HANDLER_H
