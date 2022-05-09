#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list> //双向链表容器
#include <cstdio> //C库执行输入/输出操作
#include <exception> //标准异常库
#include <pthread.h> //提供多线程操作的函数

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template <typename T> //C++中的模板，T代表一种类型，实例化的时候才知道的具体类型（int，char）
class threadpool
{
public:
    /*构造函数的声明，thread_number是线程池中线程的数量，max_requests是请求队列中最多允许的、等待处理的请求的数量*/
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_request = 10000);
    ~threadpool();
    /*请求队列中插入任务请求*/
    bool append(T *request, int state);
    bool append_p(T *request);

private:
    /*工作线程运行的函数，它不断从工作队列中取出任务并执行之*/
    static void *worker(void *arg); //pthread_create函数中的第三个变量为指向处理线程函数的地址，要求为静态函数
    void run();

private:
    int m_thread_number;        //线程池中的线程数
    int m_max_requests;         //请求队列中允许的最大请求数
    pthread_t *m_threads;       //描述线程池的数组，其大小为m_thread_number
    std::list<T *> m_workqueue; //请求队列
    locker m_queuelocker;       //保护请求队列的互斥锁，声明互斥锁封装类locker
    sem m_queuestat;            //是否有任务需要处理，声明信号量封装类sem
    connection_pool *m_connPool;  //数据库连接池
    int m_actor_model;          //模型切换
};
template <typename T>
/*构造函数的定义*/
threadpool<T>::threadpool( int actor_model, connection_pool *connPool, int thread_number, int max_requests) : m_actor_model(actor_model),m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL),m_connPool(connPool)
{
    if (thread_number <= 0 || max_requests <= 0)
        throw std::exception();
    /*线程id初始化*/
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    for (int i = 0; i < thread_number; ++i)
    {
        /*循环创建线程，并将工作线程按要求进行*/
        if (pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete[] m_threads; //创建过程出错的线程，进行回收，并返回错误代码
            throw std::exception();
        }
        /*线程分离后，不用单独对工作线程进行回收*/
        if (pthread_detach(m_threads[i]))
        {
            delete[] m_threads;//分离过程出错的线程，进行回收，并返回错误代码
            throw std::exception();
        }
    }
}
template <typename T>
/*析构函数的定义*/
threadpool<T>::~threadpool()
{
    delete[] m_threads;
}
template <typename T>
bool threadpool<T>::append(T *request, int state)
{
    m_queuelocker.lock();//请求队列上锁，防止资源竞争
    /*根据硬件设置请求队列的最大值*/
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;//超过请求队列最大值，写入请求队列失败，返回false
    }
    //提取指针中的成员用->，若为对象或者结构本身则用‘.'提取
    request->m_state = state;
    m_workqueue.push_back(request);//添加任务
    m_queuelocker.unlock();//解锁，并唤醒等待的进行
    m_queuestat.post();//信号量提醒有任务要处理，唤醒工作线程，信号量v操作，封装在locker.h中
    return true;
}
template <typename T>
bool threadpool<T>::append_p(T *request)
{
    m_queuelocker.lock();
    if (m_workqueue.size() >= m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}
template <typename T>
void *threadpool<T>::worker(void *arg)
{
    /*将参数转化为线程池类，调用成员方法*/
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}
template <typename T>
void threadpool<T>::run()
{
    while (true)
    {
        m_queuestat.wait();//信号量等待
        m_queuelocker.lock();//被唤醒后，先对请求队列加互斥锁
        if (m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T *request = m_workqueue.front();//从请求队列中取出第一个任务
        m_workqueue.pop_front();//将取出的任务从请求队列中删除
        m_queuelocker.unlock();//完成任务的读取，解锁，唤醒等待的其他线程
        if (!request)
            continue;//若取出的任务为0，则忽略此请求，结束任务
        if (1 == m_actor_model)//判断模型，LT，ET
        {
            if (0 == request->m_state)//判断任务的state，默认值为0
            {
                if (request->read_once())
                {
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connPool);//连接池中取出一个数据库连接
                    request->process();//调用process方法处理任务
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else
            {
                if (request->write())
                {
                    request->improv = 1;
                }
                else
                {
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif
