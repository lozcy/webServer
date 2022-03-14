//
// Created by LOZCY on 2021/4/20.
//

#ifndef WEB_HTTP_CONN_H
#define WEB_HTTP_CONN_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>

#include "../lock/locker.h"
#include "../DB/mysql.h"
class http_conn {
public:
    /*文件名最大长度*/
    static const int FILENAME_LEN = 200;
    /*读/写缓冲区大小*/
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;
    /*http请求方式*/
    enum METHOD {
        GET = 0, POST, HEAD, PUT, DELETE,
        TRACE, OPTIONS, CONNECT, PATCH
    };
    /*解析请求时，主状态机所处状态*/
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    /*服务器处理HTTP请求的可能结果*/
    enum HTTP_CODE {
        NO_REQUEST, GET_REQUEST, BAD_REQUEST,
        NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST,
        INTERNSL_ERROR, CLOSED_CONNECTION, DIR_REQUEST
    };
    /*行读取状态*/
    enum LINE_STATUS {
        LINE_OK = 0, LINE_BAD, LINE_OPEN
    };
public:
    http_conn(){};
    ~http_conn(){};
public:
    /*初始化新接受的连接*/
    void init(int sockfd, const sockaddr_in & addr, mysql_pool * sql_pool);
    /*关闭连接*/
    void close_conn(bool real_close = true);
    /*处理客户请求*/
    void process();
    /*非阻塞读\写操作*/
    bool read();
    bool write();

private:
    /*初始化连接*/
    void init();
    /*解析 HTTP 请求*/
    HTTP_CODE process_read();
    /*填充 HTTP 应答*/
    bool process_write(HTTP_CODE ret);

    /* 被precess_read调用 分析HTTP请求*/
    HTTP_CODE parse_request_line(char * text);
    HTTP_CODE parse_headers(char * text);
    HTTP_CODE parse_content(char * text);
    HTTP_CODE do_request();
    char * get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    /* 被precess_write 调用 填充HTTP应答*/
    void unmap();
    bool add_response(char* buffer, int & idx, const char * format, ...);
    bool add_content(const char * content);
    bool add_status_line(int status, const char * title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_dir(char * path);
public:
    /*所有 socket 上的事件都被注册到同一个 epoll 内核事件表中， 所以将 epoll 文件描述符都设定为静态*/
    static int m_epolled;
    /*统计用户数量*/
    static int m_user_count;
    mysql_pool * connect_pool;
private:
    /*该 HTTP 连接的 socket 和对方的 socket 地址*/
    int m_sockfd;
    sockaddr_in m_address;
    /*读缓冲区*/
    char m_read_buf[ READ_BUFFER_SIZE ];
    /*标识读缓冲区中已经被读入的客户数据的最后一个字节的下一个位置*/
    int m_read_idx;
    /*当前正在分析的字符在读缓冲区的位置*/
    int m_check_idx;
    /*当前正在解析行的起始位置*/
    int m_start_line;
    /*写缓冲区*/
    char m_write_buf[ WRITE_BUFFER_SIZE];
    /*写缓冲区中待发送的字节数*/
    int m_write_idx;

    /*主状态机当前所处状态*/
    CHECK_STATE m_check_state;
    /*请求方法*/
    METHOD m_method;
    /*客户请求的目标文件的完整路径， 其内容等于 doc_root + m_url, doc_root 是网站根目录*/
    char m_real_file[ FILENAME_LEN];
    /*客户请求的目标文件的文件名*/
    char * m_url;
    /*HTTP 版本协议号 ps：当前仅支持HTTP/1.1*/
    char * m_version;
    /*主机名*/
    char * m_host;
    /* HTTP 请求的消息体长度*/
    int m_content_length;
    /* 是否保持连接*/
    bool m_linger;

    /*客户请求的目标文件被 mmap 到内存中的起始位置*/
    char * m_file_address;
    /*目标问价的状态。可用于判断文件是否存在，是否为目录、是否可读，并获取文件大小等信息*/
    struct stat m_file_stat;
    /*采用 writev 来执行写操作， 所以定义下面两个成员，其中 m_iv_count 表示被写内存块的数量*/
    struct iovec m_iv[2];
    int m_iv_count;
    //文件的缓存区
    char m_file_buffer[ WRITE_BUFFER_SIZE];
    //传输文件大小
    int m_file_idx;
    //要写入的字节数
    int bytes_to_send;
    //已经写入的字节
    int bytes_have_send;

    char user_name[ 30 ];

    char user_password[ 30 ];

    int m_flag;
};
#endif //WEB_HTTP_CONN_H
