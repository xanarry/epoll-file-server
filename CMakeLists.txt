cmake_minimum_required(VERSION 3.20)
project(epoll C)

set(CMAKE_C_STANDARD 99)

add_executable(epoll main.c socket_buffer.c socket_buffer.h request_handler.c request_handler.h event_handler.c event_handler.h)
