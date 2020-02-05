#ifndef EPOLL_H
#define EPOLL_H 

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>


#include "http_conn.h"
#include "sock_conn.h"
#include "threadpool.h"

int setnonblocking( int fd );
int waitfd( int epollfd , epoll_event* event , int max_events , int timeout );
int createfd( int size );
void addfd( int epollfd , int fd , bool one_shot );
void removefd( int epollfd , int fd );
void modfd( int epollfd , int fd , int ev );
void handle_event( int epollfd , int listenfd , threadpool<http_conn>* pool , http_conn* users,
					epoll_event* events , int number , int timeout );

#endif

