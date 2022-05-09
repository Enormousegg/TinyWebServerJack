#ifndef LOCKER_H//防止头文件的重复包含和编译
#define LOCKER_H

#include <exception>//异常处理机制库
#include <pthread.h>//linux多线程POSIX标准库
#include <semaphore.h>//linux下POSIX标准的信号量库

class sem//信号量
{
public:
    sem()//构造函数，初始化信号量
    {
        if (sem_init(&m_sem, 0, 0) != 0)
        {
            throw std::exception();
        }
    }
    sem(int num)//构造函数重载
    {
        if (sem_init(&m_sem, 0, num) != 0)
        {
            throw std::exception();
        }
    }
    ~sem()//析构函数。删除信号量
    {
        sem_destroy(&m_sem);
    }
    bool wait()//信号量p操作，若m_sem为0，wait阻塞，否则信号量减1，为原子操作
    {
        return sem_wait(&m_sem) == 0;
    }
    bool post()//信号量v操作，信号量加1，若m_sem大于0，则唤醒调用post的线程，为原子操作
    {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};
class locker//互斥锁
{
public:
    locker()//构造函数，初始化锁
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
        {
            throw std::exception();
        }
    }
    ~locker()//析构函数。删除锁
    {
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock()//原子操作给互斥锁加锁
    {
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    bool unlock()//原子操作给互斥锁解锁
    {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    pthread_mutex_t *get()//返回互斥对象指针
    {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};
class cond//条件变量
{
public:
    cond()//构造函数，初始化条件变量
    {
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            //pthread_mutex_destroy(&m_mutex);
            throw std::exception();
        }
    }
    ~cond()//析构函数，销毁条件变量
    {
        pthread_cond_destroy(&m_cond);
    }
    bool wait(pthread_mutex_t *m_mutex)//等待目标条件变量
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        /*传入加锁的互斥锁m_mutex，函数执行把调用线程放入条件变量的请求队列，然后解锁互斥锁，成功时返回0，互斥锁会再次被锁上，存在一次解锁和加锁操作*/
        ret = pthread_cond_wait(&m_cond, m_mutex);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool timewait(pthread_mutex_t *m_mutex, struct timespec t)//等待时间限制的等待条件变量
    {
        int ret = 0;
        //pthread_mutex_lock(&m_mutex);
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &t);
        //pthread_mutex_unlock(&m_mutex);
        return ret == 0;
    }
    bool signal()//信号方式唤醒等待目标条件变量的线程
    {
        return pthread_cond_signal(&m_cond) == 0;
    }
    bool broadcast()//广播方式唤醒所有等待目标条件变量的线程
    {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond;//定义条件变量函数
};
#endif
