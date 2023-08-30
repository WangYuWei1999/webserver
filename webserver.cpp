#include"webserver.h"
#include<memory>

//完成服务器初始化，http连接，设置根目录，开启定时器
WebServer::WebServer(){
    //http_conn最大连接数
    users = new http_conn[MAX_FD];
    //root文件夹路径
    char server_path[200];
    getcwd(server_path,200);  //getcwd()会将当前工作目录的绝对路径复制到参数buffer所指的内存空间中
    char root[6]="/root";
    m_root = (char*)malloc(strlen(server_path)+strlen(root)+1);
    strcpy(m_root,server_path);
    strcat(m_root,root);

    //定时器
    users_timer = new client_data[MAX_FD];
}

//服务器资源释放
WebServer::~WebServer(){
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);
    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

//初始化用户名、数据库等信息
void WebServer::init(int port,std::string user,std::string password,std::string databasename,int log_write,int opt_linger,int trigmode,int sql_num,int thread_num,int close_log,int actor_model){
    m_port=port;
    m_user=user;
    m_passWord = password;
    m_databaseName = databasename;
    m_sql_num = sql_num;
    m_thread_num = thread_num;
    m_log_write = log_write;
    m_OPT_LINGER = opt_linger;
    m_TRIGMode = trigmode;
    m_close_log = close_log;
    m_actormodel = actor_model;
}

//设置epoll触发模式：ET/LT
void WebServer::trig_mode(){
    //lt+lt
    if(m_TRIGMode==0){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 0;
    }
    else if(m_TRIGMode==1){
        m_LISTENTrigmode = 0;
        m_CONNTrigmode = 1;
    }
    else if(m_TRIGMode==2){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 0;
    }
    else if(m_TRIGMode==3){
        m_LISTENTrigmode = 1;
        m_CONNTrigmode = 1;
    }
}

//初始化日志系统
void WebServer::log_write(){
    if(m_close_log==0){
        //确定日志类型：同步/异步
        if(m_log_write==1)
            //Log::get_instance()->init("./ServerLog",m_close_log,2000,800000,800);
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else 
            //Log::get_instance()->init("./ServerLog",m_close_log,2000,800000,0);
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

//初始化数据库连接池
void WebServer::sql_pool(){
    //单例模式
    m_connPool=connection_pool::GetInstance();
    m_connPool->init("localhost",m_user,m_passWord,m_databaseName,3306,m_sql_num,m_close_log);
    //初始化数据库读取表
    users->initmysql_result(m_connPool);
}

//创建线程池
void WebServer::thread_pool(){
    m_pool = new threadpool<http_conn>(m_actormodel,m_connPool,m_thread_num);
}

//创建网络编程
void WebServer::eventListen(){
    m_listenfd = socket(PF_INET,SOCK_STREAM,0);


    assert(m_listenfd>=0);
    //优雅关闭
    if(m_OPT_LINGER==0){
        struct linger tmp={0,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));        
    }
    else if(m_OPT_LINGER==1){
        struct linger tmp={1,1};
        setsockopt(m_listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp)); 
    }
    int ret =0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag=1;
                                    //地址复用
    setsockopt(m_listenfd,SOL_SOCKET,SO_REUSEADDR,&flag,sizeof(flag));
    ret = bind(m_listenfd,(struct sockaddr*)&address,sizeof(address));
    assert(ret>=0);
    //表示已连接和为连接最大队列数总和为5
    ret = listen(m_listenfd,5);
    assert(ret>=0);
    //设置服务区最小时间间隙
    utils.init(TIMESLOT);
    //epoll创建内核事件表
    epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);
    assert(m_epollfd!=-1);
    //向内核注册监听套接字事件
    utils.addfd(m_epollfd,m_listenfd,false,m_LISTENTrigmode);
    http_conn::m_epollfd=m_epollfd;

    //socketpair()用于创建一对无名，互相连接的套接字
    ret = socketpair(PF_UNIX,SOCK_STREAM,0,m_pipefd);
    assert(ret!=-1);
    utils.setnonblocking(m_pipefd[1]);
    utils.addfd(m_epollfd,m_pipefd[0],false,0);

    utils.addsig(SIGPIPE,SIG_IGN);
    utils.addsig(SIGALRM,utils.sig_handler,false);
    utils.addsig(SIGTERM,utils.sig_handler,false);

    alarm(TIMESLOT);
    //工具类
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

//创建定时器节点，将连接信息挂载
void WebServer::timer(int connfd,struct sockaddr_in client_address){
    //http_conn初始化
    users[connfd].init(connfd,client_address,m_root,m_CONNTrigmode,m_close_log,m_user,m_passWord,m_databaseName);
    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据将定时器添加到链表
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;

    util_timer* timer = new util_timer;
    //std::shared_ptr<util_timer>timer = std::make_shared<util_timer>();  //使用智能指针
    timer->user_data = &users_timer[connfd];
    //timer->call_back = call_back;    //不需要maybe
    timer->cb_func = cb_func;     //函数指针指向回调函数
    time_t cur = time(NULL);
    timer->expire = cur+3*TIMESLOT;//设置过期时间
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若数据活跃，则将定时器节点往后延迟三个时间单位 并对新的定时器在链表位置调整
void WebServer::adjust_timer(util_timer* timer){
    time_t cur = time(NULL);
    timer->expire = cur + 3*TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);
    LOG_INFO("%s","adjust timer once");
}

//删除定时器节点，关闭连接
void WebServer::deal_timer(util_timer* timer, int sockfd){
    //timer->call_back(&users_timer[sockfd]);
    timer->cb_func(&users_timer[sockfd]);
    if(timer){
        utils.m_timer_lst.del_timer(timer);
    }
    LOG_INFO("close fd %d",users_timer[sockfd].sockfd);
}

//处理用户数据
bool WebServer::dealclientdata(){  
    struct sockaddr_in client_address;
    socklen_t client_addrelength = sizeof(client_address);
    //LT
    if(m_LISTENTrigmode==0){
        int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrelength);
        if(connfd<0){
            LOG_ERROR("%s:errno is:%d", "accept error", errno);
            return false;
        }
        //while (connfd == -1 && errno == EAGAIN){
        //connfd = accept(m_listenfd,(sockaddr*)&client_address,&client_addrelength);
        
        // if (connfd == -1 && errno != EAGAIN){
        //     LOG_ERROR("%s:errno is:%d", "accept error", errno);
        //     return false;
        // }
        if(http_conn::m_user_count>=MAX_FD){
            utils.show_error(connfd,"Internal server busy");
            LOG_ERROR("%s","Internal server busy");
            return false;
        }
        timer(connfd,client_address);  //创建定时器节点，将连接信息挂载
    }
    //ET
    else{
        //ET需要循环接受
        while(true){
            int connfd = accept(m_listenfd,(struct sockaddr*)&client_address,&client_addrelength);
            if(connfd<0){
                LOG_ERROR("%s:errno is:%d", "accept error", errno);
                break;
            }
            if(http_conn::m_user_count>=MAX_FD){
                utils.show_error(connfd,"Internal server busy");
                LOG_ERROR("%s","Internal server busy");
                break;
            }
            timer(connfd,client_address);  //创建定时器节点，将连接信息挂载
        }
        return false;
    }
    return true;
}

//处理定时器信号，set the timeout true;
bool WebServer::dealwithsignal(bool& timeout,bool& stop_server){
    int ret = 0;
    int sig;
    char signals[1024];
    //从管道读取信号值，成功返回字节数量，失败返回-1
    //正常情况下总是返回1,只有14和15两个ASCII码对应的字符
    ret = recv(m_pipefd[0],signals,sizeof(signals),0);
    if(ret==-1){
        return false;
    }
    else if(ret==0){
        return false;
    }
    else{
        //处理信号对应的逻辑
        for(int i=0;i<ret;++i){
            switch(signals[i]){
                case SIGALRM:{
                    timeout = true;
                    break;
                }
                //关闭服务器
                case SIGTERM:{
                    stop_server = true;
                    break;
                }
            }
        }
    }
    return true;
}

//处理客户连接受到的数据
void WebServer::dealwithread(int sockfd){
    //创建定时器临时变量，将连接对应的定时器取出来
    util_timer* timer = users_timer[sockfd].timer;

    //reactor 主线程(I/O处理单元)只负责监听文件描述符上是否有事件发生，有的话立即通知工作线程(逻辑单元 )，读写数据、接受新连接及处理客户请求均在工作线程中完成
    if(m_actormodel==1){
        if(timer) adjust_timer(timer);
        //若检测到读事件，放入请求队列
        m_pool->append(users+sockfd,0);
        while(true){
            //是否处理中
            if(users[sockfd].improv==1){
                //事件类型关闭连接
                if(users[sockfd].timer_flag==1){
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }        
    }
    //proactor主线程和内核负责处理读写数据、接受新连接等I/O操作，工作线程仅负责业务逻辑，如处理客户请求
    else{
        //先读取数据，再放入请求队列
        if(users[sockfd].read_once()){
            LOG_INFO("deal_with the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
            //将该事件放入请求队列
            m_pool->append_p(users+sockfd);
            if(timer){
                adjust_timer(timer);
            }
        }
        else{
            deal_timer(timer,sockfd);
        }
    }
}

//写操作
void WebServer::dealwithwrite(int sockfd){
    util_timer* timer = users_timer[sockfd].timer;
    //reactor
    if(m_actormodel == 1){
        if(timer) adjust_timer(timer);
        m_pool->append(users+sockfd,1);
        while(true){
            if(users[sockfd].improv==1){
                if(users[sockfd].timer_flag==1){
                    deal_timer(timer,sockfd);
                    users[sockfd].timer_flag = 0;
                }
                users[sockfd].improv = 0;
                break;
            }
        }
    }
    //proactor
    else{
        if(users[sockfd].write()){   //inet_ntoa函数将 (Ipv4) Internet 网络地址转换为采用 Internet 标准点十进制格式的 ASCII 字符串。
            LOG_INFO("send data to the client(%s)",inet_ntoa(users[sockfd].get_address()->sin_addr));
            if(timer) adjust_timer(timer);
        }
        else{
            deal_timer(timer,sockfd);
        }
    }
}

//事件回环（服务器主线程）
void WebServer::eventLoop(){
    bool timeout = false;
    bool stop_server = false;
    while(!stop_server){
        
        int n = epoll_wait(m_epollfd,m_events,MAX_EVENT_NUMBER,-1);
        if(n<0 && errno!=EINTR){
            LOG_ERROR("%s","epoll failure");
            break;
        }
        //对就绪事件进行处理
        for(int i=0;i<n;++i){
            int sockfd = m_events[i].data.fd;
            //处理新客户连接
            if(sockfd == m_listenfd){
                bool flag = dealclientdata();
                if(flag == false) continue;
            }
            //处理异常事件
            else if(m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //服务器关闭，移除定时器
                util_timer* timer = users_timer[sockfd].timer;
                deal_timer(timer,sockfd);
            }
            //处理定时信号
            else if((sockfd == m_pipefd[0]) && (m_events[i].events & EPOLLIN)){
                //接受SIGALRM信号，timeout设置为true
                bool flag = dealwithsignal(timeout,stop_server);
                if(flag == false) LOG_ERROR("%s","dealclientdata failure");
            }
            //处理客户连接上受到的数据
            else if(m_events[i].events&EPOLLIN){
                dealwithread(sockfd);
            }
            else if(m_events[i].events & EPOLLOUT){
                dealwithwrite(sockfd);
            }
        }
        //处理定时器事件，完成读写事件后再处理
        if(timeout){
            utils.timer_handler(); //处理定时任务
            LOG_INFO("%s","timer_tick");
            timeout = false;
        }
    }
}