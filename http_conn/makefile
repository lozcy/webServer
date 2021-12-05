INC = -I ../lock -I ../DB
# 指令编译器和选项
CFLAGS=-Wall

clean:
	rm *.o

all:
	g++ $(CFLAGS) $(INC) http_conn.h http_conn.cpp -o http_conn.so