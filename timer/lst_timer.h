#ifndef LST_TIMER//一种宏定义判断，作用是防止多重定义
#define LST_TIMER

#include <unistd.h>//POSIX标准定义的unix类系统定义符号常量的头文件，提供通用的文件、目录、程序及进程操作的函数 
#include <signal.h>//信号处理标准库，提供对信号操作的函数
#include <sys/types.h>//基本系统数据类型
#include <sys/epoll.h>//epoll头文件
#include <fcntl.h>//fcntl.h定义了很多宏和open,fcntl函数原型，unistd.h定义了更多的函数原型
#include <sys/socket.h>//提供socket函数及数据结构
#include <netinet/in.h>//定义数据结构sockaddr_in
#include <arpa/inet.h>//提供IP地址转换函数
#include <assert.h>//提供了一个名为 assert 的宏，它可用于验证程序做出的假设，并在假设为假时输出诊断消息。设定插入点
#include <sys/stat.h>//用来获取文件属性，返回值：成功返回0，失败返回-1
#include <string.h>//字符串处理
#include <pthread.h>//提供多线程操作的函数
#include <stdio.h>//定义输入／输出函数
#include <stdlib.h>//定义杂项函数及内存分配函数
#include <sys/mman.h>//一种内存映射文件的方法，mmap
#include <stdarg.h>//头文件定义了一个变量类型 va_list 和三个宏，这三个宏可用于在参数个数未知（即参数个数可变）时获取函数中的参数
#include <errno.h>//提供错误号errno的定义，用于错误处理
#include <sys/wait.h>//提供进程等待(sys/wait)、进程间通讯（sys/ipc）及共享内存的函数(sys/shm)
#include <sys/uio.h>//向量I/O操作的定义

#include <time.h>//定义了四个变量类型、两个宏和各种操作日期和时间的函数。
#include "../log/log.h"

//连接资源结构体成员需要用到定时器类，需要前向声明
class util_timer;

//连接资源
struct client_data
{
    sockaddr_in address;//客户端socket地址
    int sockfd;//socket文件描述符
    util_timer *timer;//定时器，已经前向声明util_timer类
};

//定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}//构造函数对链表进行初始化

public:
    time_t expire;//超时时间
    
    /*回调函数，函数（入口地址）作为参数传入别人（或系统）的函数中，实现代码解耦
    https://www.cnblogs.com/jontian/p/5619641.html*/
    void (* cb_func)(client_data *);
    client_data *user_data;//连接资源
    util_timer *prev;//前向定时器
    util_timer *next;//后继定时器
};

//定时器容器类
class sort_timer_lst
{
public:
    sort_timer_lst();
    ~sort_timer_lst();

    void add_timer(util_timer *timer);//添加定时器，内部调用私有成员add_timer
    void adjust_timer(util_timer *timer);//调整定时器，任务发生变化时，调整定时器在链表中的位置
    void del_timer(util_timer *timer);//删除定时器
    void tick();//定时任务处理函数

private:
    //私有成员，被公有成员add_timer和adjust_time调用
    void add_timer(util_timer *timer, util_timer *lst_head);//函数重载，主要用于调整链表内部结点

    util_timer *head;
    util_timer *tail;
};

class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);//初始化时间阈值

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);

    //将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    //信号处理函数
    static void sig_handler(int sig);

    //设置信号函数
    void addsig(int sig, void(handler)(int), bool restart = true);

    //定时处理任务，重新定时以不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int *u_pipefd;//管道描述符
    sort_timer_lst m_timer_lst;
    static int u_epollfd;//事件描述符
    int m_TIMESLOT;
};

void cb_func(client_data *user_data);

#endif
