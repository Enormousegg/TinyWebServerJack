#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h> //POSIX标准定义的unix类系统定义符号常量的头文件，提供通用的文件、目录、程序及进程操作的函数 
#include <signal.h> //信号处理标准库，提供对信号操作的函数
#include <sys/types.h> //基本系统数据类型
#include <sys/epoll.h> //epoll头文件
#include <fcntl.h> //fcntl.h定义了很多宏和open,fcntl函数原型，unistd.h定义了更多的函数原型
#include <sys/socket.h> //提供socket函数及数据结构
#include <netinet/in.h> //定义数据结构sockaddr_in
#include <arpa/inet.h>  //提供IP地址转换函数
#include <assert.h> //提供了一个名为 assert 的宏，它可用于验证程序做出的假设，并在假设为假时输出诊断消息。设定插入点
#include <sys/stat.h> //用来获取文件属性，返回值：成功返回0，失败返回-1
#include <string.h> //字符串处理
#include <pthread.h> //提供多线程操作的函数
#include <stdio.h> //定义输入／输出函数
#include <stdlib.h> //定义杂项函数及内存分配函数
#include <sys/mman.h> //一种内存映射文件的方法，mmap
#include <stdarg.h> //头文件定义了一个变量类型 va_list 和三个宏，这三个宏可用于在参数个数未知（即参数个数可变）时获取函数中的参数
#include <errno.h> //提供错误号errno的定义，用于错误处理
#include <sys/wait.h> //提供进程等待(sys/wait)、进程间通讯（sys/ipc）及共享内存的函数(sys/shm)
#include <sys/uio.h> //向量I/O操作的定义
#include <map> //STL 映射容器

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
    static const int FILENAME_LEN = 200;//设置 读取文件的名称m_real_file大小
    static const int READ_BUFFER_SIZE = 2048;//设置读缓冲区m_read_buf大小
    static const int WRITE_BUFFER_SIZE = 1024;//设置写缓冲区m_write_buf大小
    enum METHOD //报文的请求方法，本项目只用到GET和POST，enum表示枚举类型
    {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum CHECK_STATE //主状态机的状态
    {
        CHECK_STATE_REQUESTLINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum HTTP_CODE //报文解析的结果
    {
        NO_REQUEST,
        GET_REQUEST,
        BAD_REQUEST,
        NO_RESOURCE,
        FORBIDDEN_REQUEST,
        FILE_REQUEST,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum LINE_STATUS //从状态机的状态
    {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    http_conn() {}
    ~http_conn() {}

public:
    /*初始化套接字地址，函数内部会调用私有方法init*/
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    void close_conn(bool real_close = true); //关闭http连接
    void process(); 
    bool read_once(); //读取浏览器端发来的全部数据
    bool write(); //响应报文写入函数
    sockaddr_in *get_address() //此结构体用于处理网络通信地址，为何这样定义
    {
        return &m_address;
    }
    void initmysql_result(connection_pool *connPool);//同步线程初始化数据库读取表
    int timer_flag;
    int improv;


private:
    void init();
    HTTP_CODE process_read();//从m_read_buf读取，并处理请求报文
    bool process_write(HTTP_CODE ret);//向m_write_buf写入响应报文数据
    HTTP_CODE parse_request_line(char *text);//主状态机解析报文中的请求行数据
    HTTP_CODE parse_headers(char *text);//主状态机解析报文中的请求头数据
    HTTP_CODE parse_content(char *text);//主状态机解析报文中的请求内容
    HTTP_CODE do_request();//生成响应报文
    //m_start_line是已经解析的字符，get_line用于将指针向后偏移，指向未处理的字符
    char *get_line() { return m_read_buf + m_start_line; };
    LINE_STATUS parse_line();//从状态机读取一行，分析是请求报文的哪一部分
    void unmap();
    //根据响应报文格式，生成对应8个部分，以下函数均由do_request调用
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;//epoll事件符号数量
    static int m_user_count;//用户数量，活跃？
    MYSQL *mysql;//定义指向数据库的指针
    int m_state;  //读为0, 写为1

private:
    int m_sockfd;
    sockaddr_in m_address;//处理网络通信地址的结构

    char m_read_buf[READ_BUFFER_SIZE];//存储读取的请求报文数据
    int m_read_idx;//缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;//m_read_buf读取的位置m_checked_idx
    int m_start_line;//m_read_buf中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];//存储发出的响应报文数据
    int m_write_idx;//指示buffer中的长度
    CHECK_STATE m_check_state;//主状态机的状态
    METHOD m_method;//请求方法

    //以下为解析请求报文中对应的6个变量，存储读取文件的名称
    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    char *m_file_address;//读取服务器上的文件地址
    struct stat m_file_stat;
    struct iovec m_iv[2];//io向量机制iovec
    int m_iv_count;
    int cgi;        //是否启用的POST
    char *m_string; //存储请求头数据
    int bytes_to_send;//剩余发送字节数
    int bytes_have_send;//已发送字节数
    char *doc_root;

    map<string, string> m_users;
    int m_TRIGMode; //socket I/O模式，epoll中的LT（select，poll）与ET（epoll独有）选择，0为LT，1为ET
    int m_close_log;

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif
