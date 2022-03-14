GFALGS= -g -std=c++11 -Wall

# 引入头文件
INC = -I ../lock/
SQLLINK = -lmysqlclient
THREADLINK = -lpthread
LINK = $(SQLLINK) $(THREADLINK)

clean:
	rm *.o

all:
	g++ $(GFALGS) $(INC) mysql.cpp -o mysql.o $(LINK)