#include"config.h"

config::config(){
        //端口号
    PORT=9006;    //默认9006

    //日志写入方式
    LOGWrite=0;   //日志写入方式，默认同步

    //触发组合模式  
    TRIGMode=0;     //默认listenfd LT + connfd LT

    //listenfd触发方式
    LISTENTrigmode=0;  //默认LT

    //connfd触发方式
    CONNTrigmode=0;   //默认LT

    //优雅关闭连接
    OPT_LINGER=0;     //默认不使用

    //数据库连接池连接数量
    sql_num=8;          //默认8

    //线程池内线程数量
    thread_num=8;      //默认8

    //是否关闭日志
    close_log=0;       //默认不关闭
    
    //并法模型选择
    actor_model=0;     //默认proactor
}

void config::prase_arg(int argc,char* argv[]){
    int opt;
    const char* str = "p:l:m:o:s:t:c:a:";

    //getopt() 方法是用来分析命令行参数的，该方法由 Unix 标准库提供，包含在 <unistd.h> 头文件中
    while((opt = getopt(argc,argv,str))!= -1){
        switch(opt){
            case 'p':{
                PORT=atoi(optarg);  //atoi: 指向的字符串转换为一个整数（类型为 int 型）
                break;
            }
            case 'l':{
                LOGWrite = atoi(optarg);
                break;
            }
            case 'm':{
                TRIGMode = atoi(optarg);
                break;
            }
            case 'o':{
                OPT_LINGER = atoi(optarg);
                break;
            }
            case 's':{
                sql_num = atoi(optarg);
                break;
            }
            case 't':{
                thread_num = atoi(optarg);
                break;
            }
            case 'c':{
                close_log = atoi(optarg);
                break;
            }
            case 'a':{
                actor_model = atoi(optarg);
                break;
            }
            default: break;
        }
    }
}
