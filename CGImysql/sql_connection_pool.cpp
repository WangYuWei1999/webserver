#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include"sql_connection_pool.h"

//构造函数 将当前连接和空余连接设置为0
//connection_pool::connection_pool():m_CurConn(0),m_FreeConn(0){}
connection_pool::connection_pool(){
    m_CurConn=0;
    m_FreeConn=0;
}

//单例模式，静态局部变量懒汉
connection_pool* connection_pool::GetInstance(){
    static connection_pool connPool;      //创建唯一对象 静态局部变量
    return &connPool;                     //返回指针
}

//初始化
void connection_pool::init(std::string url,std::string usr,std::string password,std::string database, int port,int maxconn,int close_log){
    m_url=url;
    m_port=port;
    m_usr=usr;
    m_password=password;
    m_databasename=database;
    m_close_log=close_log;

    for(int i=0;i<maxconn;i++){
        MYSQL* con=NULL;
        con=mysql_init(con);

        if(con==NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }                          //参数const char* 使用.c_str()
        con=mysql_real_connect(con,url.c_str(),usr.c_str(),password.c_str(),database.c_str(),port,NULL,0);
        if(con==NULL){
            LOG_ERROR("MYSQL Error");
            exit(1);
        }
        connlist.emplace_back(con);
        ++m_FreeConn;
    }
    reserve=Sem(m_FreeConn);
    m_MaxConn=m_FreeConn;
}

//有请求时，从数据库连接池中返回一个可用连接MYSQL*，同时更新使用和空闲连接数
MYSQL* connection_pool::GetConnection(){
    MYSQL* con=NULL;
    if(connlist.size()==0){          //池（双向链表）没有任何连接
        return NULL;
    }
    reserve.wait();  //信号量等待
    lock.lock();

    con=connlist.front();  //取出链表头节点
    connlist.pop_front();

    m_FreeConn--;   //空闲数量-1
    m_CurConn++;    //连接数量+1
    lock.unlock();
    return con;
}

//释放当前连接，即将其重新放入连接池中
bool connection_pool::ReleaseConnection(MYSQL* con){
    if(con==NULL) return false;
    lock.lock();
    //connlist.emplace_back(con);   //将MYSQL* con重新放入连接池中
    connlist.push_back(con);

    m_FreeConn++;  //空闲数量+1
    m_CurConn--;   //连接数量-1
    lock.unlock();
    reserve.post(); //信号量通知连接池容量加一
    return true;
}

//销毁数据库连接池 遍历连接池 提取连接MYSQL* con 调用mysql_close(con)销毁连接
void connection_pool::DestroyPool(){
    lock.lock();
    if(connlist.size()>0){
        //std::list<MYSQL*>::iterator it;
         //矛盾点：非常量引用不能绑定右值，若将其改为常量引用，则不能自加；
         //解决方法：使用右值引用 或者不使用引用
        for(auto&& ait=connlist.begin();ait!=connlist.end();ait++){   
            MYSQL* con=*ait;
            mysql_close(con);   //调用mysql_close(con)销毁连接
        }
        m_CurConn = 0;
        m_FreeConn = 0;
    }
    lock.unlock();
}

//当前空闲连接数
int connection_pool::GetFreeConn(){
    return this->m_FreeConn;
}

connection_pool::~connection_pool(){
    DestroyPool();
}


/*****************************************************************************/
//利用单个连接构造对象管理该连接
connectionRAII::connectionRAII(MYSQL** SQL,connection_pool* connPool){
    *SQL=connPool->GetConnection();   //从数据库连接池中提取一个连接
    conRAII = *SQL;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII(){
    poolRAII->ReleaseConnection(conRAII);
}