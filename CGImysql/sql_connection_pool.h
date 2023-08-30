//校验 & 数据库连接池
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include"../lock/locker.h"
#include"../log/log.h"

class connection_pool{
public:
    MYSQL* GetConnection();   //获取数据库连接
    bool ReleaseConnection(MYSQL* conn);  //释放连接
    int GetFreeConn();          //获取连接
    void DestroyPool();             //销毁所有连接

    //c++11之后，局部静态变量懒汉模式不用加锁
    static connection_pool* GetInstance(); //单例模式

    void init(std::string url,std::string usr,std::string password,std::string database, int port,int maxconn,int close_log);

private:
    connection_pool();  //私有化构造函数，单例模式
    ~connection_pool(); 
    int m_MaxConn; //最大连接数量
    int m_CurConn; //当前连接数
    int m_FreeConn; //当前空闲连接数
    Locker lock;
    std::list<MYSQL*> connlist; //连接池
    Sem reserve;           //信号量

public:
    std::string m_url;    //主机地址
    std::string m_port;   //数据库端口
    std::string m_usr;    //登陆数据库用户名
    std::string m_password; //登陆数据库密码
    std::string m_databasename; //数据库名字
    int m_close_log;      //日志开关
};

/*****************************************************************************/
//新建一个类来管理数据库连接池的单个连接的生命周期
//rescource acquisition is initlization
class connectionRAII{
public:
    connectionRAII(MYSQL** con, connection_pool* connPool);   //需要一个连接和连接池 构造
    ~connectionRAII();
private:
    MYSQL* conRAII;
    connection_pool* poolRAII;
};

#endif