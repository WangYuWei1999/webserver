#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_last::sort_timer_last(){
    head=NULL;
    tail=NULL;
}

sort_timer_last::~sort_timer_last(){
    util_timer* temp = head;
    while(temp){
        head = temp->next;
        delete temp;
        temp = head;
    }
}

void sort_timer_last::add_timer(util_timer* timer){
    if(!timer) return;
    if(!head){
        head=tail=timer;
        return;
    }
    //定时器按照超时时间从小到大排序，如果新的超时时间小于当前头节点，直接将其设置为头节点
    if(timer->expire<head->expire){
        timer->next=head;
        head->prev=timer;
        head=timer;
        return;
    }
    add_timer(timer,head);  //调用私有方法添加定时器
}

//调整定时器，任务变化时，调整定时器在链表中的位置
void sort_timer_last::adjust_timer(util_timer* timer){
    if(!timer) return;
    util_timer* temp=timer->next;
    //被调整的定时器在链表尾部或者调整后超时时间仍小于下个定时器，不调整
    if(!temp||(timer->expire<temp->expire)) return;
    //被调整定时器是链表头节点，将定时器取出，重新插入
    if(timer==head){
        head=head->next;
        head->prev=NULL;
        timer->next=NULL;
        add_timer(timer,head);
    }
    //被调整定时器是链表头中部，将定时器取出，重新插入
    else{
        timer->prev->next=timer->next;
        timer->next->prev=timer->prev;
        add_timer(timer,timer->next);
    }
}

//删除定时器
void sort_timer_last::del_timer(util_timer* timer){
    if(!timer) return;
    if((timer==head)&&(timer==tail)){
        delete timer;
        head=NULL;
        tail=NULL;
        return;
    }
    //被删除的链表节点在头部
    if(timer==head){
        head=head->next;
        head->prev=NULL;
        delete timer;
        return;
    }
    //被删除的链表节点在尾部
    if(timer==tail){
        tail=tail->prev;
        tail->prev=NULL;
        delete timer;
        return;
    }
    //被删除的链表节点在链表中部
    timer->prev->next=timer->next;
    timer->next->prev=timer->prev;
    delete timer;
}

//定时任务处理函数
void sort_timer_last::tick(){
    if(!head) return;
    time_t cur=time(NULL);  //获取的当前时间 time_t long int
    util_timer* temp=head;

    while(temp){
        if(cur<temp->expire) break;  //当前时间小于头节点超时时间，所有定时器都没有到期
        temp->cb_func(temp->user_data);  //当前定时器到时，调用回调函数

        head=temp->next;
        if(head) head->prev=NULL;
        delete temp;
        temp=head;
    }
}

//加入新的定时器
void sort_timer_last::add_timer(util_timer* timer,util_timer* lst_head){
    util_timer* prev = lst_head;
    util_timer* temp = prev->next;
    //遍历双向链表找到定时器位置
    while(temp){
        if(timer->expire<temp->expire){
            prev->next=timer;
            timer->next=temp;
            temp->prev=timer;
            timer->prev=prev;
            break;
        }
        prev=temp;
        temp=temp->next;
    }
    if(!temp){
        prev->next=timer;
        timer->prev=prev;
        timer->next=NULL;
        tail=timer;
    }
}

void Utils::init(int timeslot){
    m_TIMESLOT=timeslot;
}

//文件描述符设置非阻塞
int Utils::setnonblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//向内核注册读事件，边沿触发模式（ET），选择开启oneshot
void Utils::addfd(int epollfd,int fd,bool one_shot,int TRIGMode){
    epoll_event ev;
    ev.data.fd=fd;
    if(TRIGMode==1){              //EPOLLRDHUP:对端断开连接
        ev.events=EPOLLIN | EPOLLET | EPOLLRDHUP;
    }else{
        ev.events=EPOLLIN | EPOLLRDHUP;
    }
    //如果对描述符socket注册了EPOLLONESHOT事件，
    //那么操作系统最多触发其上注册的一个可读、可写或者异常事件，且只触发一次。
    if(one_shot) ev.events = EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);
    setnonblocking(fd);
}

//信号处理函数
void Utils::sig_handler(int sig){
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1],(char*)&msg,1,0);
    errno=save_errno;
}

//设置信号函数
void Utils::addsig(int sig,void(handler)(int),bool restart){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    if(restart) sa.sa_flags |=SA_RESTART;
    //sigfillset()函数用于初始化一个自定义信号集，将其所有信号都填充满，也就是将信号集中的所有的标志位置为1
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);        
}

//利用alarm函数周期性地触发SIGALRM信号，信号处理函数利用管道通知主循环，主循环接收到该信号后对升序链表上所有定时器进行处理
void Utils::timer_handler(){
    m_timer_lst.tick();
    alarm(m_TIMESLOT);
}

void Utils::show_error(int connfd,const char* info){
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int* Utils::u_pipefd=0;
int Utils::u_epollfd=0;

class Utils;
//void util_timer::cb_func(client_data* user_data){
void cb_func(client_data* user_data){
    //删除非活动连接在socket上的注册事件
    epoll_ctl(Utils::u_epollfd,EPOLL_CTL_DEL,user_data->sockfd,0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--; //http连接数-1
}