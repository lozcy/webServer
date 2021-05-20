//
// Created by LOZCY on 2021/5/12.
//

#include "mysql.h"
void mysql_pool::init(const char *url, const char *usr, const char *password,
                      const char *dbname, const int port, const int max_conn) {
    m_max_conn = max_conn;
    m_free_conn = 0;
    for( int i = 0; i < max_conn; ++i ){
        MYSQL *con = NULL;
        con = mysql_init( con );

        if( con == NULL ){
            printf( "init fail\n" );
            exit( 1 );
        }
        con = mysql_real_connect( con, url, usr, password, dbname, port, NULL, 0 );

        if( con == NULL ){
            printf( "connect fail\n" );
            exit(1);
        }
        connList.push_back( con );
        ++m_free_conn;
    }
    sum = sem( m_free_conn );
}

void mysql_pool::destroy_pool(){
    sql_lock.lock();
    for( auto i:connList ){
        MYSQL *con = i;
        mysql_close( con );
    }
    //m_curconn = 0;
    m_free_conn = 0;
    connList.clear();
    sql_lock.unlock();
}
MYSQL *mysql_pool::get_connection(){
    sql_lock.lock();
    sum.wait();
    MYSQL *con = connList.front();
    connList.pop_front();
    m_free_conn--;
    sql_lock.unlock();
    return con;
}
void mysql_pool::release_connection( MYSQL *_sql ){
    sql_lock.lock();
    connList.push_back( _sql );
    ++m_free_conn;
    sql_lock.unlock();
    sum.post();
    return;
}