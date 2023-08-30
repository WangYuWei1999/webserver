#include"http_conn.h"
#include<mysql/mysql.h>
#include<fstream>
//使用主从状态机解析报文  第一行：请求行+请求头部
//从状态机负责读取报文的一行数据，主状态机负责对该行数据进行解析，主状态机内部调用从状态机，从状态机驱动主状态机

//定义http响应状态信息
const char* ok_200_title="OK";
const char* error_400_title="Bad Request";
const char* error_400_form="Your request has bad syntax or is inherentyl impossible to satisfy.\n"; //语法错误
const char* error_403_title="Forbidden";
const char* error_403_form="You do not have permission to get file from this server.\n";
const char* error_404_title="Not Found";
const char* error_404_form="The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

Locker m_lock;
std::map<std::string,std::string> users;
//std::unordered_map<std::string,std::string>users;   //存储用户名和密码的哈希表
int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

 //得到客户端ip地址
sockaddr_in* http_conn::get_address(){
    return &m_address;
}

//将指针向后偏移，指向未处理的字符
char* http_conn::get_line(){
    return m_read_buf+m_start_line;
}

//初始化数据库连接池
void http_conn::initmysql_result(connection_pool* connPool){
    //从连接池中取一个连接
    MYSQL* mysql = nullptr;
    connectionRAII mysqlcon(&mysql,connPool);      //从数据库连接池中提取一个连接给mysql
    //在usr表中检索usrname，passwd,游览其输入
    if(mysql_query(mysql,"SELECT username,passwd FROM user")){
        LOG_ERROR("SELECT error:%s\n",mysql_error(mysql));    //通过宏调用日志记录
    }
    //从列表检索完整的结果集(结构体)
    MYSQL_RES* result=mysql_store_result(mysql);
    //从结果集中返回列数
    int num_fields=mysql_num_fields(result);
    //返回所以字段结构体数组
    MYSQL_FIELD* fields=mysql_fetch_fields(result);

    //结果集获取下一行，将用户名和密码存入哈希表
    while(MYSQL_ROW row = mysql_fetch_row(result)){
        std::string temp1(row[0]);
        std::string temp2(row[1]);
        users[temp1] = temp2;
    }
}

//对文件描述符设置非阻塞
int setnonblocking(int fd){
    int old_option = fcntl(fd,F_GETFL);
    int new_option = old_option | O_NONBLOCK;   //设置非阻塞
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

//将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd,int fd,bool oneshot,int TRIGMode){
    epoll_event ev;
    ev.data.fd = fd;
    if(TRIGMode==1)
        ev.events=EPOLLIN | EPOLLET | EPOLLRDHUP;
    else ev.events=EPOLLIN | EPOLLRDHUP;

    if(oneshot) ev.events |= EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&ev);
    setnonblocking(fd);
}

//从内核时间删除描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//将事件重置为oneshot
void modfd(int epollfd,int fd,int ev,int TRIGMode){
    epoll_event event;
    event.data.fd=fd;
    if(TRIGMode==1) event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;   //通过EPOLLRDHUP属性，来判断是否对端已经关闭
    else event.events = ev | EPOLLONESHOT | EPOLLRDHUP;  //客户端直接调用close，会触犯EPOLLRDHUP事件

    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//关闭连接 客户总量减一
void http_conn::close_conn(bool real_close){
    if(real_close&&(m_sockfd!=-1)){
        printf("close %d\n",m_sockfd);
        removefd(m_epollfd,m_sockfd);
        m_sockfd=-1;
        m_user_count--;
    }
}

//初始化连接，外部调用初始化套接字地址
void http_conn::init(int sockfd,const sockaddr_in& addr,char* root,int TRIGMode,int close_log, 
                                        std::string user,std::string passwd,std::string sqlname){
    m_sockfd = sockfd;
    m_address = addr;
    addfd(m_epollfd, sockfd, true, m_TRIGMode);  //注册该事件

    //当游览其出现连接重置，可能是网站根目录出错或者http响应格式出错或者访问文件内容为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log;

    strcpy(sql_usr,user.c_str());
    strcpy(sql_passwd,passwd.c_str());
    strcpy(sql_name,sqlname.c_str());
    init();
}

//初始化新接受的连接 check_state默认为分析请求行状态
void http_conn::init(){
    mysql = nullptr;    //单个连接
    bytes_to_send=0;
    bytes_have_send=0;   
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;
    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;  //m_read_buf读取的位置
    m_read_idx = 0;     //缓冲区中m_read_buf中数据的最后一个字节的下一个位置
    m_write_idx=0;     //指示buff中的长度
    cgi = 0;            //是否启用post
    m_state = 0;        //io事件类别：读取0,写入1
    timer_flag = 0;     //是否关闭连接
    improv = 0;         //是否正在处理数据中

    memset(m_read_buf,'\0',READ_BUFFER_SIZE);
    memset(m_write_buf,'\0',WRITE_BUFFER_SIZE);
    memset(m_real_file,'\0',FILENAME_LEN);    //存储读取文件的名称
}

//从状态机 分析出一行内容
//返回值为行的读取状态（定义过的枚举常量LINE_STATUS）LINE_OK,LINE_BAD,LINE_OPEN
http_conn::LINE_STATUS http_conn::parse_line(){
    char temp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx){  //遍历读缓冲区
        temp = m_read_buf[m_checked_idx];
        if(temp=='\r'){                              //'\r' 回车
            if((m_checked_idx+1)==m_read_idx) return LINE_OK;
            else if(m_read_buf[m_checked_idx+1]=='\n'){   //如果回车的下一个是换行，则将换行和下一个替换成两个'\0'
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if(temp=='\n'){
            if(m_checked_idx>1 && m_read_buf[m_checked_idx-1]=='\r'){   //如果换行前一个是回车
                m_read_buf[m_checked_idx-1] = '\0';                     //将回车换成'\0'
                m_read_buf[m_checked_idx++]= '\0';                      //将回车下一个换成'\0'
                return LINE_OK;
            }
            return LINE_BAD;
        }        
    }
    return LINE_OPEN;
}

//循环读取客户数据，直到无数据可读或者对方关闭连接
//非阻塞ET工作模式下，需要一次性将数据读完
bool http_conn::read_once(){
    if(m_read_idx >= READ_BUFFER_SIZE) return false;
    int bytes_read=0;

    //LT读取数据
    if(m_TRIGMode==0){
        //recv 函数： <0 出错； =0 连接关闭； >0 接收到数据大小
        //从缓冲区中m_read_idx位置开始读取，读完
        bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        m_read_idx+=bytes_read;
        if(bytes_read<=0) return false;
        return true;        
    }
    else{  //ET读数据
        //ET 需要循环读取指导读完位置
        while(true){
            bytes_read = recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
            if(bytes_read==-1){
                //非阻塞ET模式下，需要一次性将数据读完，无错
                if(errno == EAGAIN|| errno == EWOULDBLOCK) break;
                return false;
            }
            else if(bytes_read==0) return false;
            m_read_idx+=bytes_read;   //更新开始读取的位置
        }
        return true;
    }
}

//主状态机：解析http请求行，获得 请求方法，目标url，http版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char* text){
    //http报文中请求行用来说明请求的类型
    //要访问的资源以及所使用的HTTP版本，其中各个部分之间通过\t或空格分隔。
    //请求行中最先含有空格和\t任一字符的位置并返回

    //strpbrk在源字符串（s1）中找出 最先 含有搜索字符串（s2)中任一字符的位置并返回，若找不到则返回空指针
    m_url = strpbrk(text," \t");  //注意此处\前面有空格
    if(!m_url) return BAD_REQUEST;

    //读取到url之后将前面的空格置'\0',用于将前面的数据取出
    *m_url++='\0';   //先将url处置'\0'，url再自加

    //取出数据，判断是post或GET
    char* method=text;      //由于已经将url前面空格置'\0'，method只会复制到text的请求方法
    if(strcasecmp(method,"GET")==0){
        m_method = GET;
    }else if(strcasecmp(method,"POST")==0){
        m_method = POST;
        cgi = 1;             //启用POST
    }else return BAD_REQUEST;

    //m_url此时跳过了第一个空格或\t字符
    //strspn：检索字符串 str1 中第一个不在字符串 str2 中出现的字符下标
    m_url+=strspn(m_url," \t");  //跳过空格和tab，找到正确的m_url的下标

    //找到http版本号
    m_version = strpbrk(m_url," \t");  //找到版本号前面的空格
    if(!m_version) return BAD_REQUEST;
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t"); //找到正确的版本号位置

    //仅支持http/1.1
    if(strcasecmp(m_version,"HTTP/1.1")!=0) return BAD_REQUEST;

    //对请求资源前7个字符进行判断
    //报文的请求资源中会带有http://则跳过
    if(strncasecmp(m_url,"http://",7)==0){
        m_url +=7;
        m_url = strchr(m_url,'/');   //查找字符串中的一个字符，并返回该字符在字符串中第一次出现的位置
    }
    //https：//的情况
    if(strncasecmp(m_url,"http://",8)==0){
        m_url +=8;
        m_url = strchr(m_url,'/');  
    }

    if(!m_url||m_url[0]!='/') return BAD_REQUEST;
    //如果url只有/，显示欢迎界面
    if(strlen(m_url)==1) strcat(m_url,"judge.html");

    //解析完毕，将主状态机转移处理请求头（改变主状态机状态）
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

 //主状态机解析报文中的请求头部数据 解析一个
http_conn::HTTP_CODE http_conn::parse_headers(char* text){
    //判断是空行还是请求头
    if(text[0]=='\0'){
        //结合请求GET报文和响应POST报文，判断是GET还是POST请求
        if(m_content_length!=0)  {//GET 没有内容，长度为空，不为空说明是POST报文
            //post报文，主状态机需要跳转到消息体处理状态
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;   //GET报文
    }
    //解析请求头部connection字段
    else if(strncasecmp(text,"Connection:",11)==0){
        text += 11;  //跳过connection
        text +=strspn(text, " \t");   //跳过空格和table
        if(strcasecmp(text,"keep-alive")==0){
            //如果是长连接，则将linger标志设置为true
            m_linger = true;  
        }
        //m_content_length = atol(text);    // long int atol(const char *str) 把参数 str 所指向的字符串转换为一个长整数
    }
    else if(strncasecmp(text,"Content-length:",15)==0){
        text += 15;
        text += strspn(text," \t");  //跳过空格和table
        m_content_length = atol(text);
    }
    else if(strncasecmp(text,"Host:",5)==0){
        text += 5;
        text += strspn(text," \t");  //跳过空格和table
        m_host = text;
    }
    else{
        LOG_INFO("oop!unknow header: %s",text);
    }
    return NO_REQUEST;
}

//判断http请求是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char* text){
    //read_buf最后一位大于 read_buf开始读的位置加上内容长度
    if(m_read_idx>=(m_content_length+m_checked_idx)){  //说明已经读完了
        text[m_content_length] = '\0';
        //POST请求中最后为输入用户名和密码
        m_string = text;            //将请求头文件存储在 m_string
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//从m_read_buf读取数据，处理请求报文
http_conn::HTTP_CODE http_conn::process_read(){
    LINE_STATUS line_status = LINE_OK;       //从状态机状态，文本解析状态
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
                // 主状态机状态是检查报文内容 且 从状态机解析完                  或    从状态机解析完
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)||((line_status = parse_line())==LINE_OK)){
        text = get_line();
        m_start_line = m_checked_idx;  //开始点是当前读的地方
        LOG_INFO("%s",text);           //记录读取内容
        switch(m_check_state){
            case CHECK_STATE_REQUESTLINE:{
                ret = parse_request_line(text);  //主状态机解析http请求行
                if(ret ==BAD_REQUEST) return BAD_REQUEST;
                break;
            }
            case CHECK_STATE_HEADER:{
                ret = parse_headers(text);      //主状态机解析http请求头部
                if(ret == BAD_REQUEST) return BAD_REQUEST;
                else if(ret == GET_REQUEST) return do_request();
                break;                
            }
            case CHECK_STATE_CONTENT:{
                ret = parse_content(text);  
                if(ret==GET_REQUEST) return do_request();   //生成响应报文
                line_status = LINE_OPEN;
                break;
            }
            default: return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

//功能逻辑单元  生成响应报文
http_conn::HTTP_CODE http_conn::do_request(){
    strcpy(m_real_file,doc_root);    // 存储读取文件的名称拷贝
    int len= strlen(doc_root);        //根目录长度
    const char* p=strrchr(m_url,'/'); //搜索最后一次出现字符 c（一个无符号字符）的位置

    //处理cgi 是否启用post
    if(cgi==1&&(*(p+1)=='2'||*(p+1)=='3')){
        //判断登陆检测还是注册检测
        char flag = m_url[1];
        char* m_url_real = (char*)malloc(sizeof(char)*200);   //分配二百个char空间
        strcpy(m_url_real,"/");
        strcat(m_url_real,m_url+2);
        strncpy(m_real_file+len,m_url_real,FILENAME_LEN-len-1);
        free(m_url_real);

        //将用户名和密码提取出来 user=123&passwd=123
        char name[100],passward[100];
        int i;
        for(i=5;m_string[i]!='&';++i) name[i-5] = m_string[i]; //提取名字
        name[i-5] = '\0';
        int j=0;
        for(i=i+10;m_string[i]!='\0';++i,++j)   passward[j] = m_string[i];//提取密码            
        passward[j] ='\0';     //passward密码后面置'\0'
        
        if(*(p+1)=='3'){
            //如果是注册，先检测数据中是否有重名，没有重名增加数据
            char *sql_insert = (char*)malloc(sizeof(char)*200);
            strcpy(sql_insert,"INSERT INTO user(username,passwd) VALUES(");
            strcat(sql_insert,"'");
            strcat(sql_insert,name);
            strcat(sql_insert,"', '");
            strcat(sql_insert,passward);
            strcat(sql_insert,"')");

            if(users.find(name)==users.end()){  //在哈希表中没有发现用户名
                m_lock.lock();
                int res = mysql_query(mysql,sql_insert);      //如果查询成功，返回0。如果出现错误，返回非0值
                //users.insert(std::make_pair(name,passward));
                users.insert(std::pair<std::string ,std::string>(name,passward));
                m_lock.unlock();

                if(!res) strcpy(m_url,"/log.html");   //如果查到了
                else strcpy(m_url,"/registerError.html");
            }else{
                strcpy(m_url,"/registerError.html");
            }
        }
        //如果是登陆
        else if(*(p+1)=='2'){
            if(users.find(name) != users.end() && users[name]  == passward)  //如果查到了且密码正确
                strcpy(m_url,"/welcome.html");
            else 
                strcpy(m_url,"/logError.html");
        }
    } 

    //如果请求资源是/0,跳转到注册界面
      if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    } 
    // //如果请求资源是/1,跳转到登陆界面
    else if(*(p+1)=='1'){
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/log.html");
        strncpy(m_real_file+ len, m_url_real,strlen(m_url_real));
        free (m_url_real);
    }

    //如果请求资源是/5,跳转picture
    else if(*(p+1)=='5'){
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/picture.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    //如果请求资源是/6,跳转video
    else if(*(p+1)=='6'){
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/video.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
        free(m_url_real);
    }
    //如果请求资源是/7,跳转weixin
    else if(*(p+1)=='7'){
        char *m_url_real = (char*)malloc(sizeof(char)*200);
        strcpy(m_url_real,"/fans.html");
        strncpy(m_real_file+len,m_url_real,strlen(m_url_real));
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
    //避免文件描述符的浪费和占用
    close(fd);

    //表示请求文件存在，且可以访问
    return FILE_REQUEST;
}

//取消内存映射
void http_conn::unmap(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = 0;        
    }
}

//往响应报文写入数据
bool http_conn::write(){
    int temp = 0;
    if(bytes_to_send==0){   //
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        init();
        return true;
    }
    while(true){
        //将响应报文状态行，消息头，空行和响应正文发送给浏览器端
        temp = writev(m_sockfd,m_iv,m_iv_count);  //把count个数据buffer(使用iovec描述)写入到文件描述符fd所对应的的文件中
        if(temp<0){
            //判断缓冲区是否满了
            if(errno == EAGAIN){
                modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }
        //更新已发送字节
        bytes_have_send += temp;
        //更新未发送字节
        bytes_to_send -= temp;
        //第一个iovec头部信息已经发送完毕，发送第二个
        if(bytes_have_send>=m_iv[0].iov_len){
            //不再发送头部信息
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        //继续发送第一个iovec头部信息数据
        else{
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }
        //判断条件 数据全部发送完
        if(bytes_to_send<=0){
            //如果发送失败，但不是缓冲区问题，取消映射
            unmap();
            //重新注册写事件
            modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
            //浏览器的请求为长链接
            if(m_linger){
                //重新初始化http对象
                init();
                return true;
            }else return false;
        }
    }
}

//添加响应报文的公共函数
bool http_conn::add_response(const char* format, ...){
    //如果写入内容超出写缓冲区大小则报错
    if(m_write_idx>=WRITE_BUFFER_SIZE) return false;
    //定义可变参数列表
    va_list arg_list;
    //将遍历那个arg_list 初始化传入参数
    va_start(arg_list,format);
    //将数据format从可变参数列表写入缓冲区，返回写的数据长度
    int len = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);
    //如果写入的数据长度超过缓冲区剩余空间，则报错
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx)){
        va_end(arg_list);
        return false;
    }
    //更新m_write_idx 位置
    m_write_idx += len;
    //清空可变参列表
    va_end(arg_list);
    
    LOG_INFO("requset:%s",m_write_buf);
    return true;
}


//添加状态行
bool http_conn::add_status_line(int status,const char* title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

//添加消息报头,具体的添加文本长度、连接状态和空行
bool http_conn::add_headers(int content_len){
    return add_content_length(content_len) && add_linger() && add_blank_line();
}

//添加cotent_length表示响应报文长度
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);
}

//添加文本类型，这里是html
bool http_conn::add_content_type(){
    return add_response("Content-Type:%s\r\n","text/html");
}

//添加连接状态，通知浏览器端是保持连接还是关闭
bool http_conn::add_linger(){    
    return add_response("Connection:%s\r\n",(m_linger==true) ? "keep-alive":"close");
}

//添加空行
bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}

//添加文本
bool http_conn::add_content(const char* content){
    return add_response("%s",content);
}

//生成响应报文 HTTP响应也由四个部分组成，分别是：状态行、消息报头、空行和响应正文
bool http_conn::process_write(HTTP_CODE ret){
    switch(ret){
        case INTERNAL_ERROR:{
            add_status_line(500,error_500_title);
            add_headers(strlen(error_500_form));
            if(!add_content(error_500_form)) return false;
            break;
        }
        case BAD_REQUEST:{
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form)) return false;
            break;
        }
        case FORBIDDEN_REQUEST:{
            add_status_line(403,error_403_title);
            add_headers(strlen(error_403_form));
            if(!add_content(error_403_form)) return false;
            break;
        }
        case FILE_REQUEST:{
            add_status_line(200,ok_200_title);
            if(m_file_stat.st_size != 0){
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buf;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;  //待发送字节数等于两个缓冲区长度中带发送字节数相加
                return true;
            }else{
                const char* ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if(!add_content(ok_string)) return false;
            }
        }
        default: return false;
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_read_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

//成立http报文请求与报文响应
//根据read/write的buffer进行报文解析和响应
void http_conn::process(){
    HTTP_CODE read_ret = process_read();
    if(read_ret==NO_REQUEST){  //请求不完整，继续等待请求
        //注册并监听读事件
        modfd(m_epollfd,m_sockfd,EPOLLIN,m_TRIGMode);
        return;
    }
    //调用process_write 完成响应报文
    bool write_ret = process_write(read_ret);
    if(!write_ret) close_conn();
    //注册监听写事件
    modfd(m_epollfd,m_sockfd,EPOLLOUT,m_TRIGMode);
}