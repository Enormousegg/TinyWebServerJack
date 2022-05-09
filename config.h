//防止头文件的重复包含和编译
#ifndef CONFIG_H  
#define CONFIG_H

#include "webserver.h"

//使用std名称空间，一般在 头文件中声明
using namespace std;

class Config
{
public:
    Config();
    //表示析构函数，对象所在函数调用完毕，系统自动执行，用于清理善后工作
    ~Config(){};
//下面是公有函数和公有变量
    void parse_arg(int argc, char*argv[]);

    //端口号
    int PORT;

    //日志写入方式
    int LOGWrite;

    //触发组合模式
    int TRIGMode;

    //listenfd触发模式
    int LISTENTrigmode;

    //connfd触发模式
    int CONNTrigmode;

    //优雅关闭链接
    int OPT_LINGER;

    //数据库连接池数量
    int sql_num;

    //线程池内的线程数量
    int thread_num;

    //是否关闭日志
    int close_log;

    //并发模型选择
    int actor_model;
};

#endif