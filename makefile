# web服务器的makefile文件，完成整个项目的编译
# 指令编译器和选项
CFLAGS=-Wall
INC = -I ./lock -I ./http_conn -I ./threadpool -I ./DB -I ./timer

clean:
	rm *.o
all:
	g++ $(CFLAGS) $(INC) main.cpp -o webserver
