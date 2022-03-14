# web服务器的makefile文件，完成整个项目的编译
# 指令编译器和选项
CPPFLAGS= -g -std=c++11 -Wall
INC = -I ./lock -I ./http_conn -I ./threadpool -I ./DB -I ./timer
SQLLINK = -lmysqlclient
REDISLINK = -lhiredis
THREADLINK = -lpthread
LINK = $(SQLLINK) $(THREADLINK) $(REDISLINK)
clean:
	rm *.o
server : http_conn.o mysql.o main.cpp ./lock/*.h ./threadpool/*.h
	g++ $(CFLAGS) $(INC) http_conn.o mysql.o main.cpp -o server $(LINK) && make clean

http_conn.o : ./http_conn/http_conn.cpp
	g++ $(CPPFLAGS) $(INC) -c ./http_conn/http_conn.cpp

mysql.o : ./DB/mysql.cpp
	g++ $(CPPFLAGS) $(INC) -c ./DB/mysql.cpp $(SQLLINK)