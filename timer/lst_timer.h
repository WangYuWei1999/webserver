#ifndef _LST_TIMER_H
#define _LST_TIMER_H
#include<unistd.h>
#include<signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include<time.h>
#include"../log/log.h"

//连接资源结构体需要定时器 前向声明
class util_timer;

//连接资源
struct client_data{
    sockaddr_in address;
    int sockfd;
    util_timer* timer;
};

//定时器类：双向链表
class util_timer{
public:
    util_timer():prev(NULL),next(NULL){}
public:
    //static void cb_func(client_data* user_data); //回调函数
    time_t expire;  //超时时间
    void (*cb_func)(client_data*); //回调函数：从内核删除文件描述符，释放连接资源  函数指针
    client_data* user_data;         //连接资源
    util_timer* prev;      //前向定时器
    util_timer* next;      //后继定时器
};

//定时器容器类
class sort_timer_last{
public:
    sort_timer_last();
    ~sort_timer_last();
    void add_timer(util_timer* timer);
    void adjust_timer(util_timer* timer); //调整定时器，任务发生变化时，调整定时器在链表中的位置
    void del_timer(util_timer* timer);    //删除定时器
    void tick();   //定时器任务处理函数
private:
    void add_timer(util_timer* timer,util_timer* lst_head);
    util_timer* head;
    util_timer* tail;
};

class Utils{
public:
    Utils(){}
    ~Utils(){}
    void init(int timeslot);

    //对文件描述符设置非阻塞
    int setnonblocking(int fd);
    //向内核注册读事件，边沿触发模式（ET），选择开启oneshot
    void addfd(int epollfd,int fd,bool one_shot,int TRIGMode);
    //信号处理函数（固定格式）
    static void sig_handler(int sig);
    //设置信号函数
    void addsig(int sig,void(handler)(int),bool restart=true);
    //定时处理任务
    void timer_handler();

    void show_error(int connfd,const char* info);
public:
    static int* u_pipefd;  //管道id
    sort_timer_last m_timer_lst;  //升序链表定时器
    static int u_epollfd;
    int m_TIMESLOT;    //最小时间间隔
};

void cb_func(client_data *user_data);
#endif