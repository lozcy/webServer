//
// Created by LOZCY on 2021/5/12.
//

#ifndef WEB_MYSQL_H
#define WEB_MYSQL_H

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <error.h>
#include <string>

#include "locker.h"
#include <pthread.h>

class mysql_pool{
public:
    mysql_pool() {};
    ~mysql_pool() {};

    void init(const char * url, const char *user, const char * password,
              const char * dbname, const int port, const int max_conn);
    void destroy_pool();
    MYSQL * get_connection();
    locker table_lock;
    void release_connection(MYSQL *_sql);

private:
    locker sql_lock;
    sem sum;
    std::list<MYSQL*> connList;
    int m_max_conn;
    int m_free_conn;
};
#endif //WEB_MYSQL_H
