#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>//定义输入／输出函数
#include <list> //双向链表容器
#include <mysql/mysql.h> ////mysql类库
#include <error.h> //提供错误号errno的定义，用于错误处理
// #include <string.h> //字符串处理
#include <iostream> //输入/输出流
#include <string> //字符串处理，包含<string.h> 
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
	MYSQL *GetConnection();				 //获取数据库连接
	bool ReleaseConnection(MYSQL *conn); //释放连接
	int GetFreeConn();					 //获取连接
	void DestroyPool();					 //销毁所有连接

	//单例模式，采用局部静态变量懒汉单例模式（C++11多线程安全）
	static connection_pool *GetInstance();
	//构造初始化函数
	void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log); 

private:
	connection_pool();
	~connection_pool();

	int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
	locker lock;
	list<MYSQL *> connList; //连接池，元素为mysql指针
	sem reserve;

public:
	string m_url;			 //主机地址
	string m_Port;		 //数据库端口号
	string m_User;		 //登陆数据库用户名
	string m_PassWord;	 //登陆数据库密码
	string m_DatabaseName; //使用数据库名
	int m_close_log;	//日志开关
};

class connectionRAII{

public:
	//双指针对MYSQL *con修改，中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改。
	connectionRAII(MYSQL **con, connection_pool *connPool);
	~connectionRAII();
	
private:
	MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif
