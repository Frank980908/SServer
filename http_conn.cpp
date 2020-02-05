#include "http_conn.h"
#include "epoll.h"
/*定义HTTP响应的一些状态信息*/
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "404 Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
/*网站根目录*/
const char* doc_root = "/var/www/html" ;
/*设置非阻塞*/


int http_conn::m_user_count = 0 ;
int http_conn::m_epollfd = -1 ;

void http_conn::close_conn( bool real_close ){
	if( real_close & m_sockfd != -1 ){
		removefd( m_epollfd , m_sockfd ) ;
		m_sockfd = -1 ; 
		/*关闭一个连接，客户总量-1*/
		m_user_count -- ; 
	}
}

void http_conn::init( int sockfd , const sockaddr_in& addr ){
	m_sockfd = sockfd ; 
	m_address = addr ; 
	int error = 0 ; 
	socklen_t len = sizeof(error) ; 
	
	getsockopt( m_sockfd , SOL_SOCKET , SO_ERROR , &error , &len ) ;
	addfd( m_epollfd , sockfd , true ) ;
}

void http_conn::init(){
	m_check_state = CHECK_STATE_REQUESTLINE ; 
	m_linger = false ; 
	
	m_method = GET ; 
	m_url = 0 ; 
	m_version = 0 ;
	m_content_length = 0 ; 
	m_host = 0 ;
	m_start_line = 0 ;
	m_checked_idx = 0 ;
	m_read_idx = 0 ;
	m_write_idx = 0 ; 
	memset( m_read_buf , '\0' , READ_BUFFER_SIZE );
	memset( m_write_buf , '\0' , WRITE_BUFFER_SIZE );
	memset( m_real_file , '\0' , FILENAME_LEN );
}
/*从状态机*/
http_conn::LINE_STATUS http_conn::parse_line(){
	char ch ;
	for( ; m_checked_idx < m_read_idx ; m_checked_idx ++ ){
		ch = m_read_buf[ m_checked_idx ];
		if( ch == '\r' ){
			if( ( m_checked_idx + 1 ) == m_read_idx ){
				return LINE_OPEN ; 
			}else if( m_read_buf[ m_checked_idx + 1 ] == '\n' ){
				m_read_buf[ m_checked_idx++ ] = '\0' ;
				m_read_buf[ m_checked_idx++ ] = '\0' ;
				return LINE_OK ; 
			}
			return LINE_BAD ; 
		}else if( ch == '\n' ){
			if( m_checked_idx > 1 && m_read_buf[m_checked_idx+1] == '\r' ){
				m_read_buf[ m_checked_idx - 1 ] = '\0' ;
				m_read_buf[ m_checked_idx ++ ] = '\0' ;
				return LINE_OK ; 
			}
			return LINE_BAD ; 
		}
	}
	return LINE_OPEN ; 
}
/*循环读取客户数据，直到无数据可读或者对方关闭连接*/
bool http_conn::read(){
	if( m_read_idx >= READ_BUFFER_SIZE ){
		return false ; 
	}
	
	int bytes_read = 0 ; 
	while( true ){
		bytes_read = recv( m_sockfd , m_read_buf + m_read_idx , READ_BUFFER_SIZE - m_read_idx , 0 ) ;
		if( bytes_read == -1 ){
			if( errno == EAGAIN || errno == EWOULDBLOCK ){
				/*	EAGAIN:
					表明在非阻塞模式下调用了阻塞操作，在该操作没有完成就返回这个错误，
					这个错误不会破坏socket的同步，下次循环接着recv就可以。
					对非阻塞socket而言，EAGAIN不是一种错误。在VxWorks和Windows上，EAGAIN的名字叫做EWOULDBLOCK。
					
					EWOULDBLOCK:
					用于非阻塞模式，不需要重新读或者写
				*/
				break ; 
			}
			return false ; 
		}else if( !bytes_read ){
			/*recv函数在等待协议接收数据时网络中断了，返回0*/
			return false ; 
		}
		//无错误发生，recv()返回读入的字节数
		m_read_idx += bytes_read ; 
	}
	return true ; 
}

/*解析HTTP请求行，获得 请求方法，目标URL，HTTP版本号*/
http_conn::HTTP_CODE http_conn::parse_request_line( char* text ){
	m_url = strpbrk( text , " \t" ) ;
	if( !m_url ){
		return BAD_REQUEST ; 
	}
	*m_url ++ = '\0' ; 
	
	char* method = text ; 
	if( !strcasecmp( method , "GET" ) ){
		m_method = GET ; 
	}else{
		return BAD_REQUEST ; 
	}
	
	m_url += strspn( m_url , " \t" ) ;
	m_version = strpbrk( m_url , " \t" ) ;
	if( !m_version ){
		return BAD_REQUEST ; 
	}
	*m_version++ = '\0' ; 
	m_version += strspn( m_version , " \t" );
	if( strcasecmp( m_version , "HTTP/1.1" ) ){
		return BAD_REQUEST ; 
	}
	if( !strncasecmp( m_url , "http://" , 7 ) ){
		m_url += 7 ; 
		m_url = strchr( m_url , '/' ) ;
	}
	if( !m_url || m_url[0] != '/' ){
		return BAD_REQUEST ; 
	}
	m_check_state = CHECK_STATE_HEADER ; //状态转移到检查头部信息
	return NO_REQUEST ; 
}
/*解析HTTP请求的头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers( char* text ){
	/*遇到空行，表示头部字段解析完毕*/
	if( text[0] == '\0' ){
		/*若HTTP请求有消息体，则还需读取m_content_length字节的消息体，状态机转移到CHECK_STATE_CONNECT状态*/
		if( m_content_length ){
			m_check_state = CHECK_STATE_CONTENT ; 
			return NO_REQUEST ; 
		}
		/*否则说明得到一个完整的HTTP对象*/
		return GET_REQUEST ; 
	}else if( !strncasecmp( text , "Connection" , 11 ) ){
		text += 11 ;
		text += strspn( text , " \t" ) ;
		if( !strcasecmp( text , "keep-alive" ) ){
			m_linger = true ; 
		}
	}else if( !strncasecmp( text, "Content-Length:", 15 ) ){
		/*处理Content-Length头部字段*/
		text += 15 ;
		text += strspn( text , " \t" ) ;
		m_content_length = atol( text );
	}else if( !strncasecmp( text, "Host:", 5 ) ){
		//处理HOST头部字段
		text += 5 ;
		text += strspn( text , " \t" );
		m_host = text ; 
	}else{
		printf("oop! unknown header %s\n",text);
	}
}

/*这里没有真正解析HTTP请求的消息体，只是判断是否被完整读入*/
http_conn::HTTP_CODE http_conn::parse_content( char* text ){
	if( m_read_idx >= ( m_content_length + m_checked_idx ) ){
		text[ m_content_length ] = '\0' ; 
		return GET_REQUEST ; 
	}
	return NO_REQUEST ; 
}

/*主状态机*/
http_conn::HTTP_CODE http_conn::process_read(){
	LINE_STATUS line_status = LINE_OK ; 
	HTTP_CODE ret = NO_REQUEST ; 
	char* text = 0 ; 
	while( ( m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK  )
			||	( line_status = parse_line() ) == LINE_OK	){
		text = get_line() ; 
		m_start_line = m_checked_idx ; 
		printf("got one http line: %s\n",text );
		switch( m_check_state ){
			case CHECK_STATE_REQUESTLINE:{
				ret = parse_request_line( text );
				if( ret == BAD_REQUEST ){
					return BAD_REQUEST ; 
				}
				break ; 
			}
			case CHECK_STATE_HEADER:{
				ret = parse_headers( text );
				if( ret == BAD_REQUEST ){
					return BAD_REQUEST ; 
				}else if( ret == GET_REQUEST ){
					return do_request() ; 
				}
				break;  
			}
			case CHECK_STATE_CONTENT:{
				ret = parse_content( text ) ; 
				if( ret == GET_REQUEST ){
					return do_request() ; 
				}
				line_status = LINE_OPEN ; 
				break ; 
			}
			default:{
				return INTERNAL_ERROR ; 
			}
		}
	}
	return NO_REQUEST ; 
}
/*得到一个完整，正确的HTTP请求时，分析目标文件的属性
若目标文件存在，且对所有用户可读，并且不是目录，则使用mmap将其映射到内存地址
m_file_address,并告诉调用者获取文件成功*/
http_conn::HTTP_CODE http_conn::do_request(){
	strcpy( m_real_file , doc_root ) ;
	int len =  strlen( doc_root ) ;
	strncpy( m_real_file + len , m_url , FILENAME_LEN - len - 1 ) ;
	if( stat( m_real_file , &m_file_stat ) < 0 ){
		return NO_RESOURCE ; 
	}
	if( !( m_file_stat.st_mode & S_IROTH ) ){
		return FORBIDDEN_REQUEST ; 
	}
	if ( S_ISDIR( m_file_stat.st_mode ) ){
        return BAD_REQUEST;
    }
	
	int fd = open( m_real_file , O_RDONLY ) ;
	m_file_address = (char*)mmap( 0 , m_file_stat.st_size , PROT_READ , MAP_PRIVATE, fd , 0 ) ; 
	close( fd );
	return FILE_REQUEST ; 
}
/*对内存映射去执行munmap操作,取消参数所指的映射内存起始地址*/
void http_conn::unmap(){
	if( m_file_address ){
		munmap( m_file_address , m_file_stat.st_size ) ;
		m_file_address = 0 ; 
	}
}
/*写HTTP响应*/
bool http_conn::write(){
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
	if( !bytes_to_send ){
		modfd( m_epollfd , m_sockfd , EPOLLIN ) ;
		init() ; 
		return true ; 
	}
	
	while( true ){
		temp = writev( m_sockfd , m_iv , m_iv_count ) ;
		if( temp <= -1 ){
			/*如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件
			虽在此期间，服务器无法立即接受到同一客户的下一个请求，但这可以
			保证连接的完整性*/
			if( errno == EAGAIN ){
				modfd( m_epollfd , m_sockfd , EPOLLOUT );
				return true; 
			}
			unmap();
			return false ;
		}
		bytes_to_send -= temp ; 
		bytes_have_send += temp ; 
		if( bytes_to_send <= bytes_have_send ){
			unmap() ; 
			/*发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接*/
			if( m_linger ){
				init() ; 
				modfd( m_epollfd , m_sockfd , EPOLLIN );
				return true ; 
			}else{
				modfd( m_epollfd , m_sockfd , EPOLLIN );
				return false ; 
			}
		}
	}
}
/*往写缓冲中写入待发送的数据*/
bool http_conn::add_response( const char* format , ... ){
	if( m_write_idx >= WRITE_BUFFER_SIZE ){
		return false ;
	}
	va_list arg_list ; 
	/*C 库宏 void va_start(va_list ap, last_arg) 
	初始化 ap 变量，它与 va_arg 和 va_end 宏是一起使用的。
	last_arg 是最后一个传递给函数的已知的固定参数，即省略号之前的参数。*/
	va_start( arg_list , format );
	/*
		头文件:
		#include <stdarg.h>
		函数声明:
		int _vsnprintf(char* str, size_t size, const char* format, va_list ap);
		参数说明:
		char *str [out],把生成的格式化的字符串存放在这里.
		size_t size [in], str可接受的最大字符数[1]  (非字节数，UNICODE一个字符两个字节),防止产生数组越界.
		const char *format [in], 指定输出格式的字符串，它决定了你需要提供的可变参数的类型、个数和顺序。
		va_list ap [in], va_list变量. va:variable-argument:可变参数
		函数功能：将可变参数格式化输出到一个字符数组。
	*/
	int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
	if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ){
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status , const char* title ){
	return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_headers( int content_len ){
	add_content_length( content_len );
	add_linger() ; 
	add_blank_line() ;
}

bool http_conn::add_content_length( int content_len ){
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_conn::add_linger(){
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_conn::add_blank_line(){
    return add_response( "%s", "\r\n" );
}

bool http_conn::add_content( const char* content ){
    return add_response( "%s", content );
}

/*根据服务器处理HTTP请求的结果，决定返回给客户端的内容*/
bool http_conn::process_write( HTTP_CODE ret ){
    switch ( ret ){
        case INTERNAL_ERROR:{
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ){
                return false;
            }
            break;
        }
        case NO_RESOURCE:{
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ){
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) ){
                return false;
            }
            break;
        }
        case FILE_REQUEST:{
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 ){
                add_headers( m_file_stat.st_size );
                m_iv[ 0 ].iov_base = m_write_buf;
                m_iv[ 0 ].iov_len = m_write_idx;
                m_iv[ 1 ].iov_base = m_file_address;
                m_iv[ 1 ].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                return true;
            }
            else{
                const char* ok_string = "<html><body></body></html>";
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) ){
                    return false;
                }
            }
        }
        default:{
            return false;
        }
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}
/*由线程池中的工作线程调用，处理HTTP请求的入口函数*/
void http_conn::process(){
    HTTP_CODE read_ret = process_read();
	log(LOG_INFO, __FILE__, __LINE__, "read HTTP CODE %d", read_ret);
    if ( read_ret == NO_REQUEST ){
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret );
	log(LOG_INFO, __FILE__, __LINE__, "write HTTP CODE %d", write_ret);
    if ( ! write_ret ){
        close_conn();
    }
    modfd( m_epollfd, m_sockfd, EPOLLOUT );
}









