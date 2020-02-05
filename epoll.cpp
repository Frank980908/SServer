#include "epoll.h"
#define MAX_FD 65536
int createfd( int size ){
	int epollfd = epoll_create( size );
	assert( epollfd != -1 ) ;
	return epollfd ; 
}

int setnonblocking( int fd ){
	int flag = fcntl( fd , F_GETFL ) ;
	int flag_new = flag | O_NONBLOCK ;
	fcntl( fd , F_SETFL , flag_new ) ;
	return flag ; 
}

void addfd( int epollfd , int fd , bool one_shot ){
	epoll_event event ; 
	event.data.fd = fd ; 
	event.events = EPOLLIN | EPOLLET | EPOLLRDHUP ; 
	/*如果对描述符socket注册了EPOLLONESHOT事件，
	那么操作系统最多触发其上注册的一个可读、可写或者异常事件，且只触发一次。
	想要下次再触发则必须使用epoll_ctl重置该描述符上注册的事件，包括EPOLLONESHOT 事件。*/
	/*只监听一次事件，当监听完这次事件之后，
	如果还需要继续监听这个socket的话，需要再次把这个socket加入到EPOLL队列里 */
	if( one_shot ){
		event.events |= EPOLLONESHOT ; 
	}
	epoll_ctl( epollfd , EPOLL_CTL_ADD , fd , &event ) ;
	setnonblocking( fd ) ;
}

void removefd( int epollfd , int fd ){
	epoll_ctl( epollfd , EPOLL_CTL_DEL , fd , 0 ) ;
	close( fd );
}

void modfd( int epollfd , int fd , int ev ){
	epoll_event event ; 
	event.data.fd = fd ; 
	event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP ;
	epoll_ctl( epollfd , EPOLL_CTL_MOD , fd , &event ) ;
}

int waitfd( int epollfd , epoll_event* event , int max_events , int timeout ){
	int number = epoll_wait( epollfd , event , max_events , timeout );
	return number ; 
}

void handle_event( int epollfd , int listenfd , threadpool<http_conn>* pool , http_conn* users,
					epoll_event* events , int number , int timeout ){
	log(LOG_INFO, __FILE__, __LINE__, "number %d",number);
	for( int i = 0 ; i < number ; i++ ){
		int sockfd = events[i].data.fd ;
		log(LOG_INFO, __FILE__, __LINE__, "%d %d",sockfd, events[i].events);
		if( sockfd == listenfd ){
			/*若就绪文件描述符是listenfd，处理新的连接*/
			struct sockaddr_in client_address ;
			socklen_t client_addrlength = sizeof( client_address ) ;
			int connfd = accept( listenfd , (struct sockaddr*)&client_address , &client_addrlength) ;
			log(LOG_INFO, __FILE__, __LINE__, "new client %d", connfd);
			if( connfd < 0 ){
				printf("errno is: %d\n",errno) ;
				continue ; 
			}
			if( http_conn::m_user_count >= MAX_FD ){
				show_error( connfd , "Internal server busy!") ;
				continue ;
			}
			/*初始化客户连接*/
			users[connfd].init( connfd , client_address ) ;
		}else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) ){
			/*有异常，直接关闭客户连接*/
			users[sockfd].close_conn() ; 
			/*EPOLLRDHUP：TCP连接被对方关闭，或对方关闭了写操作
			  EPOLLHUP：挂起（管道写端被关闭，读端则会接收到此事件）
			  EPOLLERR：错误*/
		}else if( events[i].events & EPOLLIN ){
			/*根据读的结果，选择将任务添加到线程池里，还是关闭连接*/
			if( users[sockfd].read() ){
				pool -> append( users + sockfd ) ;
			}else{
				users[sockfd].close_conn() ; 
			}
		}else if( events[i].events & EPOLLOUT ){
			/*根据写的结果，决定是否关闭连接*/
			if( !users[sockfd].write() ){
				log(LOG_INFO, __FILE__, __LINE__, "close 3");
				users[sockfd].close_conn() ; 
			}
		}else{}
	}	
}