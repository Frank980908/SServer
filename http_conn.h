/*线程池模板参数类，封装对逻辑任务的处理*/
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/uio.h>

#include "locker.h"
#include "log.h"

class http_conn
{
public:
	/*文件名最大长度*/
    static const int FILENAME_LEN = 200;
	/*读写缓冲区大小*/
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
	/*HTTP请求方法（暂时只写GET）*/
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };
    /*解析客户请求时，主状态机处的状态-
		*CHECK_STATE_REQUESTLINE:正在分析请求行
		*CHECK_STATE_HEADER：正分析头部字段
		*CHECK_STATE_CONTENT：正分析请求正文
	*/
	enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    /*服务器处理HTTP请求的可能结果*/
	enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };
    /*从状态机的状态，即行的读取状态-
		 *LINE_OK:完整行
		 *LINE_BAD:行出错
		 *LINE_OPEN：行数据不完整
	*/
	enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_conn(){}
    ~http_conn(){}

public:
	/*初始化新接受的连接*/
    void init( int sockfd, const sockaddr_in& addr );
    /*关闭连接*/
	void close_conn( bool real_close = true );
    /*处理客户请求*/
	void process();
	/*非阻塞读操作*/
    bool read();
	/*非阻塞写*/
    bool write();

private:
	/*初始化连接*/
    void init();
	/*解析HTTP请求*/
    HTTP_CODE process_read();
	/*填充HTTP应答*/
    bool process_write( HTTP_CODE ret );
	
	/*被process_read调用以分析http请求*/
    HTTP_CODE parse_request_line( char* text );
    HTTP_CODE parse_headers( char* text );
    HTTP_CODE parse_content( char* text );
    HTTP_CODE do_request();
    char* get_line() { return m_read_buf + m_start_line; }
    LINE_STATUS parse_line();

	/*被process_write调用以填充http应答*/
    void unmap();
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

public:
	/*所有socket上的事件都被注册到同一个epoll内核事件表
	故将epoll文件描述符设置为静态的*/
    static int m_epollfd;
    static int m_user_count; //统计用户数量

private:
	/*读HTTP连接的socket和对方的socket地址*/
    int m_sockfd;
    sockaddr_in m_address;
	
	/*读缓冲区*/
    char m_read_buf[ READ_BUFFER_SIZE ];
	/*标识读缓冲中已经读入的客户数据最后一个字节的下一个位置*/
	int m_read_idx;
	/*当前正在分析的字符在读缓冲区中的位置*/
    int m_checked_idx;
	/*当前正在解析的行的起始位置*/
    int m_start_line;
	/*写缓冲区*/
    char m_write_buf[ WRITE_BUFFER_SIZE ];
	/*写缓冲区中待发送的字节数*/
    int m_write_idx;
	
	/*主状态机当前所处的状态*/
    CHECK_STATE m_check_state;
	/*请求方法*/
    METHOD m_method;

	/*客户请求的目标文件完整路径（doc_root+m_url）,doc_root是网站根目录*/
    char m_real_file[ FILENAME_LEN ];
	/*客户请求的目标文件文件名*/
    char* m_url;
	/*HTTP协议版本号，暂仅支持HTTP/1.1*/
    char* m_version;
	/*主机名*/
    char* m_host;
	/*HTTP请求消息长度*/
    int m_content_length;
	/*HTTP请求是否要求保持连接*/
    bool m_linger;

	/*客户请求的目标文件被mmap到内存中的起始位置*/
    char* m_file_address;
	/*目标文件状态，判断文件是否存在，是否为目录，是否可读并获取文件大小等信息*/
    struct stat m_file_stat;
	/*采用writev执行写操作,避免多次系统调用*/
	/*
		#include <sys/uio.h>
		ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
		ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
		struct iovec {
			void  *iov_base;    // Starting address 
			size_t iov_len;     // Number of bytes to transfer 
		};
	*/
	struct iovec m_iv[2];
	/*被写内存块的数量*/
    int m_iv_count;
};

#endif
/*
防止重复定义
#define 条件编译
　　头文件(.h)可以被头文件或C文件包含；
　　重复包含（重复定义）
　　由于头文件包含可以嵌套，那么C文件就有可能包含多次同一个头文件，就可能出现重复定义的问题的。
　　通过条件编译开关来避免重复包含（重复定义）
　　例如
　　#ifndef __headerfileXXX__
　　＃define __headerfileXXX__
　　…
　　文件内容
　　…
　　#endif
 */