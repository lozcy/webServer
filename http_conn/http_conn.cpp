//
// Created by LOZCY on 2021/4/21.
//
#include <dirent.h>
#include "http_conn.h"

/* 定义HTTP 响应的一些状态信息*/
const char * ok_200_title = "OK";
const char * error_400_title = "Bad Request";
const char * error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char * error_403_title = "Forbidden";
const char * error_403_form = "You do not have permission to get file from this server.\n";
const char * error_404_title = "Not Found";
const char * error_404_from = "The requested file was not found on this server.\n";
const char * error_500_title = "Internal Error";
const char * error_500_from = "There was an unusual problem serving the requested file.\n";

/*网站根目录*/
const char * doc_root = "/var/www/html";

/*非阻塞*/
int setnonblocking(int fd) {
    /*参数 fd 代表打算设置的文件描述符， fcntl 针对描述符进行控制*/
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
/*EPOLLIN 表示数据可读， EPOLLET 表示ET模式， EPOLLRDHUP 表示读关闭*/
void addfd(int epollfd, int fd, bool one_shot) {
    epoll_event event; // epoll 注册的事件
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP; // 注册的事件类型
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event); // 注册事件
    setnonblocking(fd);
}

void removefd(int epollfd, int fd) {
    // 删除事件
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
//    修改事件
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epolled = -1;

void http_conn::close_conn(bool real_close) {
    if (real_close && (m_sockfd != -1)) {
        /*关闭连接，关闭后将客户总量减一*/
        removefd(m_epolled, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init(int sockfd, const sockaddr_in &addr, mysql_pool * sql_pool) {
    m_sockfd = sockfd;
    m_address = addr;
    connect_pool = sql_pool;
    /*后两行主要为调试作用，正常运行时应删除*/
//    int reuse = 1;
//    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    addfd(m_epolled, sockfd, true);
    m_user_count++;

    init();
}

void http_conn::init() {
//    初始化主状态机当前状态, 是否保持连接
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_check_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_file_idx = 0;
    m_flag = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*从状态机:解析出一行的内容， 三种状态为：一个完整的行、行出错、行数据暂不完整*/
http_conn::LINE_STATUS http_conn::parse_line() {
    char tmp;
    for (; m_check_idx < m_read_idx; ++m_check_idx) {
        tmp = m_read_buf[m_check_idx];
        if (tmp == '\r') {
            if ( (m_check_idx + 1 ) == m_read_idx) {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_check_idx + 1] == '\n') {
                m_read_buf[m_check_idx++] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (tmp == '\n') {
            if ((m_check_idx > 1) && (m_read_buf[m_check_idx - 1] == '\r')) {
                m_read_buf[m_check_idx - 1] = '\0';
                m_read_buf[m_check_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/* 循环读取客户数据， 直到无数据可读取或者对方关闭连接
 * 非阻塞ET工作模式，需要一次性读完所有数据
 * */
bool http_conn::read() {
    if (m_read_idx >= READ_BUFFER_SIZE) {
        return  false;
    }
    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        }
        else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
}

/*解析 HTTP 请求行， 获得请求方法、目标URL， 以及 HTTP 版本号 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    // 检索 text 中第一个匹配 \t 的字符
    m_url = strpbrk(text, "\t");
    if (! m_url) {
        return  BAD_REQUEST;
    }
    *m_url++ = '\0';

    char * method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    }else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    }else {
        return BAD_REQUEST;
    }
//    表示 url中头位置不为str2中字符
    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return  BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }
    if ( strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }
    if (!m_url || m_url[0] != '/') {
        return  BAD_REQUEST;
    }
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/*解析 HTTP 请求的一个头部信息*/
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
//    遇到空行表示头部字段解析完毕
    if (text[0] == '\0') {
        // 如果有消息体，则需要读取length字节的消息体，状态转移
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        // 否则说明获得完整的 HTTP 请求
        return GET_REQUEST;
    }
    // 处理Connection 头部字段
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    }
    // 处理Content-Length 头部字段
    else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text); // 转换成长整型
    }
    // 处理 HOST 头部字段
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        printf("oop! unknow header %s \n", text);
    }
    return NO_REQUEST;
}

/* 不真正解析 HTTP 请求消息体，只判断是否被完整读入*/
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_check_idx)) {
        int i;
        for (i = 5;; ++i) {
            if (text[i] == '&') {
                user_name[i] = '\0';
                break;
            }
            user_name[i - 5] = text[i];
        }
        i += 10;
        for (int j = 0; ; ++j, ++i) {
            if (text[i] == '\0') {
                user_password[j] = '\0';
                break;
            }
            user_password[j] = text[i];
        }
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/*主状态机*/
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK; // 记录当前状态
    HTTP_CODE ret = NO_REQUEST; // 记录处理结果
    char * text = 0;
    while ( ((m_check_state == CHECK_STATE_CONTENT) && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK) ) {
        text = get_line();
        m_start_line = m_check_idx;
        printf("got 1 http line : %s \n", text);
        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE : // 分析请求行
            {
                ret = parse_request_line(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER : // 分析头部字段
            {
                ret = parse_headers(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }else if (ret == GET_REQUEST) {
                    return do_request();
                }
                break;
            }
            case CHECK_STATE_CONTENT :
            {
                ret = parse_content(text);
                if (ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
                return INTERNSL_ERROR;
        }
    }
    return NO_REQUEST;
}

/* 得到一个完整、正确的 HTTP 请求时，我们分析目标文件的属性。
 * 如果文件存在、对所有用户可读且不是目录，则使用mmap
 * 将其映射在内存地址 m_file_address 处，并告诉调用者获取文件成功
 * */
http_conn::HTTP_CODE http_conn::do_request() {
    if( strlen( m_url ) == 1 && m_url[ 0 ] == '/' ){
        strcpy( m_real_file, "./files/init.html" );
        if( stat( m_real_file, &m_file_stat ) < 0 ){
            return NO_RESOURCE;
        }
        int fd = open("./files/init.html", O_RDONLY);
        m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        return FILE_REQUEST;
    }
    if( m_method == POST ){
        m_flag = 1;
        MYSQL *con = connect_pool->get_connection();
        switch( m_url[ 1 ] ){
            case '1':
            {
                puts( user_name );
                puts( user_password );
                char tmp[100];
                int tmp_idx = 0;
                if( !add_response( tmp, tmp_idx, "SELECT password FROM user where name = '%s';",user_name ) ){
                    strcpy( m_url, "/enter_error.html" );
                }else{
                    //puts( tmp );
                    //printf( "tmp_idx = %d\n",tmp_idx );
                    mysql_query( con, tmp );
                    MYSQL_RES *result = mysql_store_result( con );
                    MYSQL_ROW row = mysql_fetch_row( result );
                    if( row == NULL || strcmp( user_password, std::string( row[ 0 ] ).c_str() ) != 0 ){
                        strcpy( m_url, "/enter_error.html" );
                    }else{
                        m_flag = 2;
                        strcpy( m_url, "/" );
                    }
                }
                break;
            }
            case '0':
            {
                //puts( user_name );
                //puts( user_password );
                char tmp[100];
                int tmp_idx = 0;
                connect_pool->table_lock.lock();
                if( !add_response( tmp, tmp_idx, "insert into user(name,password) values('%s','%s');"
                        ,user_name, user_password ) ){
                    strcpy( m_url, "/register_error.html" );
                }else{
                    if( 0==mysql_query( con, tmp ) ){
                        strcpy( m_url, "/init.html" );
                    }else{
                        strcpy( m_url, "/register_error.html" );
                    }
                }
                connect_pool->table_lock.unlock();
                break;
            }
            case '2':
            {
                strcpy( m_url,"/register.html" );
                break;
            }
            default:
            {
                break;
            }
        }
        connect_pool->release_connection( con );
    }
    if( m_flag == 0  ){
        strcpy( m_url, "/init.html" );
    }
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    // 获取文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }
    if ( !(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }
    if ( S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ,
                                 MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

/* 对内存映射区执行munmap 操作*/
void http_conn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*写 HTTP 响应*/
bool http_conn::write() {
    int tmp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        modfd(m_epolled, m_sockfd, EPOLLIN);
        init();
        return true;
    }
    while (true) {
        tmp = writev(m_sockfd, m_iv, m_iv_count);
        if (tmp <= -1) {
            /*如果 TCP 写缓存没有空间，则等待下一轮 EPOLLOUT 事件。
             * （在此期间服务器无法立即接受同一客户的下一个请求，但能够保证连接完整）*/
            if (errno == EAGAIN) {
                modfd(m_epolled, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        bytes_to_send -= tmp;
        bytes_have_send += tmp;
        if( bytes_have_send >=m_iv[0].iov_len ){
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + ( bytes_have_send - m_write_idx );
            m_iv[1].iov_len = bytes_to_send;
        }else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_write_idx - bytes_have_send;
        }
        if (bytes_to_send <= 0) {
            /* 发送 HTTP 响应成功，根据请求中的Connection 字段决定是否立即关闭连接*/
            unmap();
            if ( m_linger) {
                init();
                modfd(m_epolled, m_sockfd, EPOLLIN);
                return true;
            }
            else {
                modfd(m_epolled, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

/* 往缓冲区写入待发送的数据*/
bool http_conn::add_response(char * buffer, int & idx, const char *format, ...) {
    if (m_write_idx >= WRITE_BUFFER_SIZE) {
        return false;
    }
    va_list arg_list; // char *
    va_start(arg_list, format);
    int len = vsnprintf(buffer + idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list); // 向字符串中打印数据
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title) {
    return add_response(m_write_buf, m_write_idx, "%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool http_conn::add_headers(int content_len) {
//    add_content_length(content_len);
//    add_linger();
//
//    add_blank_line();
    if( m_flag == 2 ){
        return add_content_length(content_len) && add_linger() &&
               add_blank_line();
    }
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool http_conn::add_content_length(int content_len) {
    return add_response(m_write_buf, m_write_idx, "Content-Length: %d\r\n", content_len);
}
bool http_conn::add_linger() {
    return add_response(m_write_buf, m_write_idx, "Connection: %s\r\n", ( m_linger == true) ? "keep-alive" : "close");
}
bool http_conn::add_blank_line() {
    return add_response(m_write_buf, m_write_idx, "%s", "\r\n");
}
bool http_conn::add_content(const char *content) {
    return add_response(m_write_buf, m_write_idx, "%s", content);
}
bool http_conn::add_dir( char *path ){
    //puts( path );
    DIR *dir_ptr;
    struct dirent *file_info;
    dir_ptr = opendir( path );
    add_response( m_file_buffer, m_file_idx, "<html><body>" );
    while( file_info = readdir( dir_ptr ) ){
        add_response(m_file_buffer, m_file_idx,
                     "<br><td><a href=\"./%s\">%s</a></td></br>",file_info->d_name, file_info->d_name );
    }
    add_response( m_file_buffer, m_file_idx, "</body></html>" );
    closedir( dir_ptr );
    return true;
}
/* 根据服务器处理 HTTP 请求的结果决定返回给客户端的内容*/
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNSL_ERROR :
        {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_400_form));
            if ( !add_content(error_500_from)) {
                return false;
            }
            break;
        }
        case BAD_REQUEST :
        {
            add_status_line(400, error_400_title);
            add_headers(strlen(error_400_form));
            if (!add_content(error_400_form)) {
                return false;
            }
            break;
        }
        case NO_RESOURCE :
        {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_from));
            if (!add_content(error_404_from)) {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST :
        {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form)) {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0)
            {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                //printf( "m_write_idx:%d m_file_size:%d\n", m_write_idx, m_file_stat.st_size );
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string)){
                    printf( "ok_string\n" );
                    return false;
                }
            }
        }

        case DIR_REQUEST:
        {
            add_status_line( 200, ok_200_title );
            //puts( m_real_file );
            add_dir( m_real_file );
            add_headers( m_file_idx );
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_buffer;
            m_iv[1].iov_len = m_file_idx;
            m_iv_count = 2;
            printf( "m_write_idx:%d m_file_idx:%d\n", m_write_idx, m_file_idx );
            bytes_to_send = m_write_idx + m_file_idx;
            return true;
        }
        default:
        {
            return false;
        }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

/* 由线程池的工作线程调动， 处理 HTTP 请求的入口函数*/
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        modfd(m_epolled, m_sockfd, EPOLLIN);
        return ;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    modfd(m_epolled, m_sockfd, EPOLLOUT);
}