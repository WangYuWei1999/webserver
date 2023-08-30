#ifndef _HTTP_CONNECTION_H
#define _HTTP_CONNECTION_H
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<thread>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/wait.h>
#include<sys/uio.h>
#include<map>
#include<unordered_map>

#include"../lock/locker.h"
#include"../CGImysql/sql_connection_pool.h"
#include"../timer/lst_timer.h"
#include"../log/log.h"
//http连接处理类
/*
   根据状态转移,通过主从状态机封装了http连接类。其中,主状态机在内部调用从状态机,从状态机将处理状态和数据传给主状态机
> * 客户端发出http连接请求
> * 从状态机读取数据,更新自身状态和接收数据,传给主状态机
> * 主状态机根据从状态机状态,更新自身状态,决定响应请求还是继续读取
*/

class http_conn{
public:
    //读取文件长度上限
    static const int FILENAME_LEN=200;  //静态常量 可以在类内提供初始值
    //读缓存大小
    static const int READ_BUFFER_SIZE=2048;
    //写缓存大小
    static const int WRITE_BUFFER_SIZE=1024;

    //HTTP方法名 枚举常量 第一个设置为0,后面挨个自动+1
    enum METHOD{
        GET = 0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATH
    };         //枚举不要忘了加分号 ;

    //主状态机状态，检查请求报文中的元素
    enum CHECK_STATE{
        CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT
    };

    //HTTP状态码
    enum HTTP_CODE{
        NO_REQUEST, 
        GET_REQUEST, 
        BAD_REQUEST, 
        NO_RESOURCE, 
        FORBIDDEN_REQUEST, 
        FILE_REQUEST, 
        INTERNAL_ERROR,    //内部错误
        CLOSED_CONNECTION
    };

    //从状态机状态，文本解析状态
    enum LINE_STATUS{
        LINE_OK=0,
        LINE_BAD,
        LINE_OPEN
    };
    
public:
    http_conn(){}
    ~http_conn(){}
    //初始化套接字
    void init(int sockfd,const sockaddr_in& addr,char* ,int ,int, std::string user,std::string passwd,std::string sqlname);
    //关闭http连接
    void close_conn(bool real_close=true);
    //http处理函数
    void process();
    //读取游览器发送的数据
    bool read_once();
    //给相应的报文写入数据
    bool write();
    //得到客户端ip地址
    sockaddr_in* get_address();
    //初始化数据库连接池
    void initmysql_result(connection_pool* connPool);

    int timer_flag; //是否关闭连接
    int improv;    //是否正在处理数据中

private:
    void init();
    //从m_read_buf读取数据，处理请求报文
    HTTP_CODE process_read();

    //向m_write_buf写入响应报文
    bool process_write(HTTP_CODE ret);

    //主状态机解析报文中的请求行数据
    HTTP_CODE parse_request_line(char* text);

    //主状态机解析报文中的请求头部数据
    HTTP_CODE parse_headers(char* text);

    //主状态机解析报文中的请求内容
    HTTP_CODE parse_content(char* text);

    //生成响应报文
    HTTP_CODE do_request();

    //将指针向后偏移，指向未处理的字符
    char* get_line();

    //从状态机读取一行，分析是请求报文的那一部分
    LINE_STATUS parse_line();

    //取消内存映射
    void unmap();

    //根据 相应报文格式，生产对应的八个部分，以下函数均由do_request调用
    bool add_response(const char* format, ...);
    bool add_content(const char* content);
    bool add_status_line(int status,const char* titled);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;  //记录用户数量
    MYSQL* mysql;
    int m_state; // io事件类别：读取0,写入1

private:
    int m_sockfd;                      //连接套接字
    sockaddr_in m_address;
    char m_read_buf[READ_BUFFER_SIZE];//存储读取的请求报文数据
    int m_read_idx;                   //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    int m_checked_idx;               //m_read_buf读取的位置m_checked_idx
    int m_start_line;                 //m_read_buf中已经解析的字符个数

    char m_write_buf[WRITE_BUFFER_SIZE];//存储发出的响应报文数据
    int m_write_idx;                    //指示buff中的长度

    CHECK_STATE m_check_state;       //主状态机的状态
    METHOD m_method;                 //请求方法

    //解析报文中的六个变量
    char m_real_file[FILENAME_LEN];  //存储读取文件的名称
    char* m_url;            //请求资源
    char* m_version;        //http版本号
    char* m_host;           
    int m_content_length;  //报文中content-length的值即POST报文内容的字符串长度
    bool m_linger;         //长链接 keep-alive

    //读取服务器上的文件地址
    char* m_file_address;
    struct stat m_file_stat;   //请求资源文件信息

    //io向量机iovec
    struct iovec m_iv[2];     //缓冲区是存放的是readv所接收的数据或是writev将要发送的数据
    int m_iv_count;
    int cgi;                    //是否启用post
    char* m_string;             //存储请求头文件
    int bytes_to_send;           //剩余发送字节数
    int bytes_have_send;        // 已发送字节数
    char* doc_root;            //根目录名称
    
    std::map<std::string,std::string> m_usrs; //用户名密码；
    int m_TRIGMode;  //触发模式
    int m_close_log; //是否开启日志

    char sql_usr[100];
    char sql_passwd[100];
    char sql_name[100];
};

#endif