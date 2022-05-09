#include <mysql/mysql.h> //mysql类库
#include <stdio.h> //标准输入/输出
#include <string> //字符串处理
// #include <string.h>
#include <stdlib.h> //定义杂项函数及内存分配函数
#include <list> //双向链表容器
#include <pthread.h> //多线程类库
#include <iostream> //输入/输出流
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()//当前已使用的连接数，当前空闲的连接数
{
	m_CurConn = 0;//当前已使用的连接数
	m_FreeConn = 0;//当前空闲的连接数
}

connection_pool *connection_pool::GetInstance()//单例静态函数实现
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化函数实现
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	m_url = url; //主机地址
	m_Port = Port; //数据库端口
	m_User = User; //登陆数据库用户名
	m_PassWord = PassWord; //登陆数据库密码
	m_DatabaseName = DBName; //所使用数据库名
	m_close_log = close_log; //日志开关

	for (int i = 0; i < MaxConn; i++)//创建MaxConn条数据库连接
	{
		MYSQL *con = NULL;
		/*如果mysql是NULL指针，该函数将分配、初始化、并返回新对象。
    	否则，将初始化对象，并返回对象的地址。
    	如果mysql_init()分配了新的对象，应当在程序中调用mysql_close() 来关闭连接，以释放对象*/
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		/*数据库引擎建立连接，如果连接成功，返回MYSQL*连接句柄如果连接失败，返回NULL。对于成功的连接，返回值与第1个参数的值相同*/
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		//更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}

	reserve = sem(m_FreeConn);//信号量初始化为最大连接次数

	m_MaxConn = m_FreeConn;//可用连接数量更新
}


//当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;//初始化连接指针con

	if (0 == connList.size())
		return NULL;

	reserve.wait();//取出连接，信号量原子减1，为0则等待，p操作
	
	lock.lock();//对连接池加锁

	con = connList.front();//连接池队列中取出第一个连接
	connList.pop_front();//将取出的连接从连接池中删除

	--m_FreeConn;//更新可用连接数量
	++m_CurConn;//更新已使用连接数量

	lock.unlock();//解锁并唤醒其他等待线程
	return con;//返回可用连接
}

//释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();//对连接池加锁

	connList.push_back(con);//释放出的连接重新 放入连接池
	++m_FreeConn;//更新可用连接数量
	--m_CurConn;//更新已使用连接数量

	lock.unlock();//解锁，并唤醒等待线程

	reserve.post();//放入连接，信号原子量加1，唤醒等待程序，v操作
	return true;
}

//销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();//对连接池加锁
	if (connList.size() > 0)
	{
		//通过迭代器遍历，关闭数据库连接
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);//关闭数据库连接
		}
		m_CurConn = 0;//更新已使用连接数量
		m_FreeConn = 0;//更新可使用连接数量
		connList.clear();//清空list
	}

	lock.unlock();//解锁
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()//析构函数具体实现，关闭连接，清空连接池
{
	DestroyPool();
}

//不直接调用获取和释放连接的接口，将其封装起来，通过RAII机制进行获取和释放。
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool){
	*SQL = connPool->GetConnection();
	
	conRAII = *SQL;
	poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}
