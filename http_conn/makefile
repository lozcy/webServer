INC = -I ../lock/ -I ../DB/
# 指令编译器和选项
CFLAGS= -g -std=c++11 -Wall
SQLLINK = -lmysqlclient
THREADLINK = -lpthread
LINK = $(SQLLINK) $(THREADLINK) 
clean:
	rm *.o

all:
	g++ $(CFLAGS) $(INC) -c http_conn.cpp -o http_conn.o $(LINK)