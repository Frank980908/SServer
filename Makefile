CXX = g++

Server:http_conn.o main.o sock_conn.o epoll.o log.o
	$(CXX) *.o -o SServer -lpthread
http_conn.o:http_conn.cpp
	$(CXX) -c http_conn.cpp
main.o:main.cpp
	$(CXX) -c main.cpp
sock_conn.o:sock_conn.cpp
	$(CXX) -c sock_conn.cpp
epoll.o:epoll.cpp
	$(CXX) -c epoll.cpp
log.o:log.cpp
	$(CXX) -c log.cpp	

clean:
	rm *.o SServer
