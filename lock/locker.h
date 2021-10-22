//
// Created by LOZCY on 2021/4/16.
//

#ifndef WEB_LOCKER_H
#define WEB_LOCKER_H
#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 封装信号量的类
class sem {
public:
//    创建并初始化信号量
    sem() {
        if (sem_init(&m_sem, 0, 0) != 0) {
            // 无返回值，所以通过抛出异常来报告错误
            throw std::exception();
        }
    }
    sem( int num ) {
        if ( sem_init(&m_sem, 0, num) != 0) {
            throw std::exception();
        }
    }
//    销毁信号量
    ~sem() {
        sem_destroy(&m_sem);
    }
//    等待信号量
    bool wait() {return sem_wait(&m_sem) == 0;}
//    增加信号量
    bool post() {return sem_post(&m_sem) == 0;}

private:
    sem_t m_sem;
};
//创建互斥锁的类
class locker {
public:
    locker() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
    }
    ~locker() {
        pthread_mutex_destroy(&m_mutex);
    }
//    获取互斥锁
    bool lock() {return pthread_mutex_lock(&m_mutex) == 0;}
//    释放互斥锁
    bool unlock() {return pthread_mutex_unlock(&m_mutex) == 0;}

private:
    pthread_mutex_t m_mutex;
};

// 封装条件变量的类
class cond {
public:
    cond() {
        if (pthread_mutex_init(&m_mutex, NULL) != 0) {
            throw std::exception();
        }
        if (pthread_cond_init(&m_cond, NULL) != 0) {
//            构造函数出现问题时立即释放已成功分配的资源
            pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_cond);
    }
//    等待条件变量
    bool wait() {
        int ret = 0;
        pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_wait(&m_cond, &m_mutex);
        pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
//    唤醒等待条件变量的线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

private:
    pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;
};
#endif //WEB_LOCKER_H
