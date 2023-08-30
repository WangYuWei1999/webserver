#ifndef _CONFIG_H
#define _CONFIG_H

#include"webserver.h"

class config
{
public:
    config();
    //config(){}
    ~config(){}

    void prase_arg(int arg,char*argv[]);

    //端口号
    int PORT;    //默认9006

    //日志写入方式
    int LOGWrite;   //日志写入方式，默认同步

    //触发组合模式  
    int TRIGMode;     //默认listenfd LT + connfd LT

    //listenfd触发方式
    int LISTENTrigmode;  //默认LT

    //connfd触发方式
    int CONNTrigmode;   //默认LT

    //优雅关闭连接
    int OPT_LINGER;     //默认不使用

    //数据库连接池连接数量
    int sql_num;          //默认8

    //线程池内线程数量
    int thread_num;      //默认8

    //是否关闭日志
    int close_log;       //默认不关闭
    
    //并法模型选择
    int actor_model;     //默认proactor



        //端口号
    // int PORT=9006;    //默认9006

    // //日志写入方式
    // int LOGWrite=0;   //日志写入方式，默认同步

    // //触发组合模式  
    // int TRIGMode=0;     //默认listenfd LT + connfd LT

    // //listenfd触发方式
    // int LISTENTrigmode=0;  //默认LT

    // //connfd触发方式
    // int CONNTrigmode=0;   //默认LT

    // //优雅关闭连接
    // int OPT_LINGER=0;     //默认不使用

    // //数据库连接池连接数量
    // int sql_num=8;          //默认8

    // //线程池内线程数量
    // int thread_num=8;      //默认8

    // //是否关闭日志
    // int close_log=0;       //默认不关闭
    
    // //并法模型选择
    // int actor_model=0;     //默认proactor
};

#endif