#include"config.h"

//服务器主程序，调用webserver类实现web服务器
int main(int argc, char* argv[]){
    //需要修改的数据库信息，登陆名，密码，库名
    std::string user = "root";
    std::string passwd = "5300784wYw$";
    std::string databasename = "webserver01";

    //命令行解析
    config config;
    config.prase_arg(argc,argv);

    WebServer server;
    //初始化
    server.init(config.PORT,user,passwd,databasename,config.LOGWrite,config.OPT_LINGER,
                config.TRIGMode,config.sql_num,config.thread_num,config.close_log,config.actor_model);
    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}