#ifndef SOCK_CONN
#define SOCK_CONN

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>

#define PATHLEN 256 
#define BUFFERSIZE 1024
#define ADDRESSNUMBER 64
#define MAXLISTEN 1024

enum CONNECT_STATUS{ CONNECT_OK = 0 , CONNECT_ERR } ;

typedef struct CONN{
	char root[PATHLEN];
	char ip[ADDRESSNUMBER];
	int port ; 
	int thread_number ; 
}conn_t ; 

CONNECT_STATUS read_con_info( conn_t* connect , const char* filename );
int socket_conn( conn_t* connect );
void addsig( int sig , void( handler )(int) , bool restart = true );
void show_error( int connfd , const char* info );

#endif


