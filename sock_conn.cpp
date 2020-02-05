#include "sock_conn.h"

CONNECT_STATUS read_con_info( conn_t* connect , const char* filename ){
	FILE* fp = fopen(filename,"r");
	if( !fp ){
		return CONNECT_ERR ; 
	}
	char buf[BUFFERSIZE];
	int pos = 0; 
	while( fgets( buf + pos , BUFFERSIZE - pos , fp ) ){
		if( !strncasecmp( buf + pos ,"threadNumber" , 12 ) ){
			connect -> thread_number = atoi( buf + pos + 13 ) ;
		}
		if( !strncasecmp( buf + pos , "ip" , 2 ) ){
			strncpy( connect -> ip , buf+pos+3 , strlen(buf+pos+3)-1 );
		}
		if( !strncasecmp( buf + pos , "port" , 4 ) ){
			connect -> port = atoi( buf + pos + 5 );
		}
		if( !strncasecmp( buf + pos , "root" , 4 ) ){
			strncpy( connect -> root , buf+pos+5 , strlen(buf+pos+5)-1 );
		}
		pos += strlen( buf + pos ) ;		
	}
}

int socket_conn( conn_t* connect ){
	int listenfd = socket( PF_INET , SOCK_STREAM , 0 );
	assert( listenfd >= 0 ) ;
	/* <arpa/inet.h>
	struct linger {
　　		int l_onoff;
　　		int l_linger;
	};                
	l_onoff = 0, l_linger忽略:closesocket的时候立刻返回，底层会将未发送完的数据发送完成后再释放资源
	l_onoff != 0 , l_linger = 0 ：调用closesocket的时候同样会立刻返回，但不会发送未发送完成的数据，而是通过一个RST包强制的关闭socket描述符，也就是强制的退出
	l_onoff != 0, l_linger > 0 :，在调用closesocket的时候不会立刻返回，内核会延迟一段时间
	*/
	struct linger tmp = { 1 , 0 } ;//调用closesocket的时候立刻返回,强制关闭socket描述符
	/* setsockopt( int sockfd , int level , int option_name , const void* option_value , socklen_t option_len) ：设置文件描述符属性
		sockfd:被操作的目标sockfd
		level：操作哪个协议（通用socket选项：SOL_SOCKET ,ipv4：IPPROTO_IP ,ipv6:IPPROTO_IPv6, TCP:IPPROTO_TCP）
		option_name: 指定选项名字（SO_LINGER:linger类型，若有数据待发送，则延迟关闭）
	*/
	setsockopt( listenfd , SOL_SOCKET , SO_LINGER , &tmp , sizeof(tmp) ) ; 
	
	int ret = 0 ; 
	struct sockaddr_in address ; 
	bzero( &address , sizeof(address) ) ;
	address.sin_family = AF_INET ; 
	inet_pton( AF_INET , connect -> ip , &address.sin_addr ) ;
	address.sin_port = htons( connect -> port ) ;
	
	ret = bind( listenfd , (struct sockaddr* )&address , sizeof(address) ) ;
	assert( ret >= 0 ) ;
	
	ret = listen( listenfd , 5 ) ;
	assert( ret >= 0 ) ;
	return listenfd ; 
}

/*信号处理函数*/
void addsig( int sig , void( handler )(int) , bool restart ){
	struct sigaction sa ; 
	memset( &sa , '\0' , sizeof(sa) ) ;
	sa.sa_handler = handler ; 
	if( restart ){
		/* SA_RESTART : 重新调用被该信号终止的系统调用*/
		sa.sa_flags |= SA_RESTART ;
	}
	sigfillset( &sa.sa_mask ) ;/*在信号集中设置所有信号*/
	/* sigaction（ int sig , const struct sigaction* act , struct sigaction* oact ）：
	设置信号处理函数  sig: 要捕获的信号类型  act:指定新的信号处理方式 */
	assert( sigaction( sig , &sa , NULL ) != -1 ) ;
}
void show_error( int connfd , const char* info ){
	printf("%s",info) ;
	send( connfd , info , strlen(info) , 0 ) ;
	close( connfd ) ;
}

















