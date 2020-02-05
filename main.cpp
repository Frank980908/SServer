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

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "sock_conn.h"
#include "epoll.h"
#include "log.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
/*
extern int addfd( int epollfd , int fd , bool one_shot );
extern int removefd( int epollfd , int fd );
*/


int main( int argc , char* argv[] ){
	/*if( argc <= 2 ){
		printf("usage: %s ip_address port_number\n",basename(argv[0]) );
	}
	const char* ip = argv[1] ; 
	int port = atoi( argv[2] );*/
	conn_t connect;
    const char* connect_path = "Sever_config.conf";
    if( read_con_info( &connect , connect_path ) == CONNECT_ERR ) {
        fprintf(stderr, "Read information Failure!");
        return -1;
    }
	
	/*设置信号的处理函数*/
	/* 忽略SIGPIPE信号（SIGPIPE：往一个读端关闭的管道或者socket连接中写数据将引发，
	默认行为是结束进程） */
	/*为了避免进程退出, 可以捕获SIGPIPE信号, 或者忽略它, 给它设置SIG_IGN信号处理函数
	(#define SIG_IGN ((void (*) (int))  1)对捕获的信号采取忽略操作或者默认操作。):
	addsig(SIGPIPE, SIG_IGN);
	这样, 第二次调用write方法时, 会返回-1, 同时errno置为SIGPIPE. 程序便能知道对端已经关闭.*/
	addsig( SIGPIPE, SIG_IGN ); 
	
	/*创建线程池*/
    threadpool< http_conn >* pool = NULL;
    try{
        pool = new threadpool< http_conn >;
    }
    catch( ... ){
        return 1;
    }
	/*预先为每个可能的用户分配一个http_conn对象*/
	http_conn* users = new http_conn[ MAX_FD ] ;
	assert( users ) ;
	int user_count = 0 ; 
	
	int listenfd = socket_conn( &connect ) ;
	
	epoll_event events[ MAX_EVENT_NUMBER ] ;
	int epollfd = createfd( 5 ) ;
	addfd( epollfd , listenfd , false ) ; //对listenfd禁用ET模式
	http_conn::m_epollfd = epollfd ; 
	
	while( true ){
		int number = waitfd( epollfd , events , MAX_EVENT_NUMBER , -1 ) ;
		if( number < 0 && errno != EINTR ){
			printf("epoll failure!\n") ;
			break ;
		}
		handle_event( epollfd , listenfd , pool , users , events , number , -1 );
		
	}

	
	close( epollfd ) ;
	close( listenfd ) ;
	delete [] users ;
	delete pool ; 
	return 0 ; 
}




