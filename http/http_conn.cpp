#include "http_conn.h"

#include <mysql/mysql.h> //mysql类库
#include <fstream> //文件流库

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users;//用户名和密码

void http_conn::initmysql_result(connection_pool *connPool)
{
    //先从连接池中取一个连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    //在user表中检索username，passwd数据，浏览器端输入
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error:%s\n", mysql_error(mysql));
    }

    //从表中检索完整的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    //返回结果集中的列数
    int num_fields = mysql_num_fields(result);

    //返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    //从结果集中获取下一行，将对应的用户名和密码，存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string temp1(row[0]);
        string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    //对文件描述符fd采用F_GETFL（获取文件打开方式的标志，标志值含义与open调用一致）,得到描述符
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);//对文件描述符指定O_NONBLOCK标志（非阻塞I/O）
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;//结构体epoll_event被用于注册所感兴趣的事件和回传所发生待处理的事件
    event.data.fd = fd;

    if (1 == TRIGMode)//ET模式
        //事件含义分别为,fd可读，设置ET，代表对端断开连接（读关闭）
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;//fd可读，读关闭

    if (one_shot)//是否one_shot
        event.events |= EPOLLONESHOT;//只监听一次事件
    /*epollfd为epoll_create句柄，注册新的fd到epollfd，文件描述符，内核需要监听的事件*/
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);//文件描述符设置为非阻塞
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);//EPOLL_CTL_DEL从epollfd删除fd
    close(fd);//关闭文件描述符对应的连接
}

//将事件重置为EPOLLONESHOT，当次socket有新请求时，其他线程能处理
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;//直接将监听的文件描述符赋值给event.data.fd

    if (1 == TRIGMode)//ET模式
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;//ev为输入事件
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;//全局静态变量，初始化用户数量
int http_conn::m_epollfd = -1;//全局静态变量，epoll事件符号数量初始化

//关闭连接，关闭一个连接，客户总量减一
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);//删除epollfd中事件符号，以及关闭m_sockfd对应连接
        m_sockfd = -1;
        m_user_count--;
    }
}

//初始化连接,外部调用初始化套接字地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);//注册事件到epollfd，True表示开启oneshot
    m_user_count++;

    //当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或者访问的文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_user, user.c_str());//c_str()返回指向user的指针，strcpy将user指针所指复制给sql_user
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());

    init();
}

//初始化新接受的连接
//check_state默认为分析请求行状态，init()函数重载
void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

/*从状态机，用于分析出一行内容，在HTTP报文中，每一行的数据由\r\n作为结束字符，
空行则是仅仅是字符\r\n。因此，可以通过查找\r\n将报文拆解成单独的行进行解析
返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
m_read_idx指向缓冲区m_read_buf的数据末尾的下一个字节
m_checked_idx指向从状态机当前正在分析的字节*/
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];//字符串，储存读取的请求报文数据
        if (temp == '\r')//tmp为回车符号，回到当前行首位，此情况可能读取到完整行
        {
            //下一个字符达到了buffer结尾，则接收不完整，需要继续接收
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            //下一个字符是\n，将\r\n改为\0\0，将m_checked_idx指向下一行的开头
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //都不符合返回语法错误
            return LINE_BAD;
        }
        //如果当前字符是\n，也有可能读取到完整行
        //一般是上次读取到\r就到buffer末尾了，没有接收完整，再次接收时会出现这种情况
        else if (temp == '\n')
        {   
            //前一个字符是\r，则接收完整
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    //并没有找到\r\n，需要继续接收
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once()
{
    //缓冲区中m_read_buf中数据的最后一个字节的下一个位置是否大于读缓冲区设置值
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;

    //LT读取数据
    if (0 == m_TRIGMode)//判断触发模式LT或者ET
    {
        //从套接字接收数据，存储在m_read_buf缓冲区
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;//更新m_read_idx的读取字节数

        if (bytes_read <= 0)
        {
            return false;
        }

        return true;
    }
    //ET读数据，阻塞I/O若read_size大于可用读缓冲区，则会出现读取不完全
    else
    {
        while (true)//非阻塞I/O，防止最后一次读取阻塞，而是返回EGAIN
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                if (errno == EAGAIN || errno == EWOULDBLOCK)//判断读取结束
                    break;
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;//更新m_read_idx的读取字节数
        }
        return true;
    }
}

//解析http请求行，获得请求方法，目标url及http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    m_url = strpbrk(text, " \t");//找到第一个制表符的位置并返回给m_url，未找到，返回null
    if (!m_url)//判断是否为NULL，建议使用if(m_url==NULL),增加可读性
    {
        return BAD_REQUEST;
    }
    /*运算符++大于*，但++是后置的，故(*m_url='\0')++，将m_url指针指向url，将前面数据取出
    并将\t(制表符)替换为\0(结束符NULL）*/
    *m_url++ = '\0';
    char *method = text;//将指向请求方法的指针text传给method，text由于结束符，只指向方法
    if (strcasecmp(method, "GET") == 0)//比较两字符串，忽略大小写，完全匹配返回0，前者大返回正
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;//是否启用post的flag
    }
    else
        return BAD_REQUEST;
    m_url += strspn(m_url, " \t");//返回str1中第一个不在字符串str2中出现的字符下标，跨过空格（url与HTTP版本）
    m_version = strpbrk(m_url, " \t");//找到第一个制表符的位置并返回给m_url，未找到，返回null
    if (!m_version)
        return BAD_REQUEST;
    *m_version++ = '\0';//同上
    m_version += strspn(m_version, " \t");//m_version跳过HTTP版本之前的所有空格，指向HTTP版本
    if (strcasecmp(m_version, "HTTP/1.1") != 0)//仅支持HTTP/1.1
        return BAD_REQUEST;
    if (strncasecmp(m_url, "http://", 7) == 0)//匹配m_url前七个字符
    {
        m_url += 7;
        m_url = strchr(m_url, '/');//char *strchr(const char *str, int c)，返回在字符串str中第一次出现字符c的位置
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')//一般的不会带有上述两种符号，直接是单独的/或/后面带访问资源
        return BAD_REQUEST;
    //当url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");
    m_check_state = CHECK_STATE_HEADER;//请求行处理完毕，将主状态机转移处理请求头
    return NO_REQUEST;
}
/*请求头部示例
GET
 1    GET /562f25980001b1b106000338.jpg HTTP/1.1
 2    Host:img.mukewang.com
 3    User-Agent:Mozilla/5.0 (Windows NT 10.0; WOW64)
 4    AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.106 Safari/537.36
 5    Accept:image/webp,image/*,/*;q=0.8
 6    Referer:http://www.imooc.com/
 7    Accept-Encoding:gzip, deflate, sdch
 8    Accept-Language:zh-CN,zh;q=0.8
 9    空行
10    请求数据为空
POST
1    POST / HTTP1.1
2    Host:www.wrox.com
3    User-Agent:Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1; .NET CLR 2.0.50727; .NET CLR 3.0.04506.648; .NET CLR 3.5.21022)
4    Content-Type:application/x-www-form-urlencoded
5    Content-Length:40
6    Connection: Keep-Alive
7    空行
8    name=Professional%20Ajax&publisher=Wiley
*/
//解析http请求的一个头部信息
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')//判断是空行还是请求头
    {
        //判断是GET还是POST请求
        if (m_content_length != 0)
        {
            //POST跳转到消息体处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        //否则说明是GET请求，则报文解析结束。
        return GET_REQUEST;
    }
    //解析请求头部连接字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        //跳过空格和\t字符（制表符）
        text += strspn(text, " \t");
        //如果是长连接，则将linger标志设置为true
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    //解析请求头部内容长度字段
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);//字符串转换为长整型数
    }
    //解析请求头部HOST字段
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入，仅用与解析POST请求
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    //判断buffer中是否读取了消息体
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        //POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;//初始化从状态机状态、HTTP请求解析结果
    HTTP_CODE ret = NO_REQUEST;//报文解析结果定义为无请求
    char *text = 0;//定义char指针text并初始化为0，表示空指针

    //前部分判断条件用于判断POST请求报文是否解析完成，后部分判断GET请求报文解析是否完成
    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();//指针向后偏移，指向未处理的字符
        //m_start_line是每一个数据行在m_read_buf中的起始位置，m_checked_idx表示从状态机在m_read_buf中读取的位置
        m_start_line = m_checked_idx;
        LOG_INFO("%s", text);
        switch (m_check_state)//主状态机的三种状态转移逻辑
        {
        case CHECK_STATE_REQUESTLINE://解析请求行
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            break;
        }
        case CHECK_STATE_HEADER://解析请求头
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT://解析消息体，仅用于解析POST请求
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    //将初始化的m_real_file赋值为网站根目录
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    //printf("m_url:%s\n", m_url);
    const char *p = strrchr(m_url, '/');//找到m_url中/的位置，进而判断/后第一个字符

    //处理cgi,实现登录和注册校验
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {

        //根据标志判断是登录检测还是注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2);
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        //将用户名和密码提取出来
        //user=123&passwd=123
        char name[100], password[100];
        int i;
        //以&为分隔符，前面的为用户名
        for (i = 5; m_string[i] != '&'; ++i)
            name[i - 5] = m_string[i];
        name[i - 5] = '\0';

        //以&为分隔符，后面的是密码
        int j = 0;
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
            password[j] = m_string[i];
        password[j] = '\0';

        if (*(p + 1) == '3')
        {
            //如果是注册，先检测数据库中是否有重名的
            //没有重名的，进行增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            //判断map中能否找到重复的用户名
            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                //校验成功，跳转登录页面
                if (!res)
                    strcpy(m_url, "/log.html");
                //校验失败，跳转注册失败页面
                else
                    strcpy(m_url, "/registerError.html");
            }
            else
                strcpy(m_url, "/registerError.html");
        }
        //如果是登录，直接判断
        //若浏览器端输入的用户名和密码在表中可以查找到，返回1，否则返回0
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
                strcpy(m_url, "/welcome.html");
            else
                strcpy(m_url, "/logError.html");
        }
    }

    //如果请求资源为/0，表示跳转注册界面
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/1，表示跳转登录界面
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/5，表示跳转图片页面
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/6，表示跳转视频页面
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    //如果请求资源为/7，表示跳转关注页面
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
        //如果以上均不符合，即不是登录和注册，直接将url与网站目录拼接
        //这里的情况是welcome界面，请求服务器上的一个图片
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //通过stat获取请求资源文件信息，成功则将信息更新到m_file_stat结构体
    //失败返回NO_RESOURCE状态，表示资源不存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return NO_RESOURCE;
    //判断文件的权限，是否可读，不可读则返回FORBIDDEN_REQUEST状态
    if (!(m_file_stat.st_mode & S_IROTH))
        return FORBIDDEN_REQUEST;
    //判断文件类型，如果是目录，则返回BAD_REQUEST，表示请求报文有误
    if (S_ISDIR(m_file_stat.st_mode))
        return BAD_REQUEST;
    
    //以只读方式获取文件描述符，通过mmap将该文件映射到内存中
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);//避免文件描述符的浪费和占用
    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

/*对数据传输过程分析后，定位到writev的m_iv结构体成员有问题，
每次传输后不会自动偏移文件指针和传输长度，还会按照原有指针和原有长度发送数据。
改动：
第一次传输后，需要更新m_iv[1].iov_base和iov_len，m_iv[0].iov_len置成0，只传输文件，不用传输响应消息头
每次传输后都要更新下次传输的文件起始位置和长度*/
bool http_conn::write()
{
    int temp = 0;

    //若要发送的数据长度为0，表示响应报文为空，一般不会出现这种情况
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }

    while (1)
    {
        //将响应报文的状态行、消息头、空行和响应正文发送给浏览器端
        /*参数：readv和writev的第一个参数fd是个文件描述符，
        第二个参数是指向iovec数据结构的一个指针，其中iov_base为缓冲区首地址，
        iov_len为缓冲区长度，参数iovcnt指定了iovec的个数。*/
        temp = writev(m_sockfd, m_iv, m_iv_count);

        //判断条件，数据已全部发送完
        if (temp < 0)
        {
            //判断缓冲区是否满了
            if (errno == EAGAIN)
            {
                //重新注册写事件
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            //如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            return false;
        }

        //此时表示正常发送，temp为发送字节数
        bytes_have_send += temp;//更新已经发送字节数
        bytes_to_send -= temp;//偏移文件iovec指针
        //第一个iovec头部信息的数据已发送完，发送第二个iovec数据
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        //继续发送第一个iovec头部信息的数据
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        //判断条件，数据已全部发送完
        if (bytes_to_send <= 0)
        {
            unmap();//取消映射
            //在epoll树上重置EPOLLONESHOT事件
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);

            //浏览器的请求为长连接
            if (m_linger)
            {
                //重新初始化HTTP对象
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
//更新m_write_idx指针和缓冲区m_write_buf中的内容
bool http_conn::add_response(const char *format, ...)
{
    //如果写入内容超出m_write_buf大小则报错
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;//定义可变参数列表
    va_start(arg_list, format);//将变量arg_list初始化为传入参数
    //将数据format从可变参数列表写入缓冲区写，返回写入数据的长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))//如果写入的数据长度超过缓冲区剩余空间，则报错
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;//更新m_write_idx位置
    va_end(arg_list);//清空可变参列表

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
//添加状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
//添加消息报头，具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
//添加Content-Length，表示响应报文的长度
bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
//添加文本类型，这里是html
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
//添加空行
bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
//添加文本content
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
     //内部错误500   
    case INTERNAL_ERROR:
    {
        //状态行
        add_status_line(500, error_500_title);
        //消息报头
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    //报文语法错误404
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    //资源没有访问权限403
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    //文件存在200
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        //如果请求的资源存在
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            //第一个iovec指针指向响应报文缓冲区，长度指向m_write_idx
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //第二个iovec指针指向mmap返回的文件指针，长度指向文件大小
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            //发送的全部数据为响应报文头部信息和文件大小
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            //如果请求的资源大小为0，则返回空白html文件
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
        return false;
    }
    //除FILE_REQUEST状态外，其余状态只申请一个iovec，指向响应报文缓冲区
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
//子线程通过process函数对任务进行处理
void http_conn::process()
{
    HTTP_CODE read_ret = process_read();//完成报文解析任务
    if (read_ret == NO_REQUEST)//NO_REQUEST表示请求不完整，需要继续接收请求数据
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);//注册并监听读事件，重置oneshot
        return;
    }
    bool write_ret = process_write(read_ret);//完成报文响应
    if (!write_ret)
    {
        close_conn();//事件移除epollfd，关闭fd对应连接
    }
    //注册并监听写事件，重置oneshot
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}
