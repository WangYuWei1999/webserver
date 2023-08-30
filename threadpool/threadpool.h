#ifndef _THREADPOOL_H
#define _THREADPOOL_H

#include<list>
#include<cstdio>
#include<pthread.h>
#include"../lock/locker.h"
#include"../CGImysql/sql_connection_pool.h"   //数据库连接池

template<typename T>
class threadpool{
public:
    threadpool(int actor_model,connection_pool* connPool,int thread_number=8,int max_request=10000);
    ~threadpool();
    bool append(T* request,int state);
    bool append_p(T* request);
private:
    static void* worker(void* arg); //线程工作函数
    void run();
private:
    int m_thread_number;   //线程数
    int m_max_requests;    //请求队列中最大请求数
    pthread_t* m_threads;  //线程数组
    std::list<T*> m_workqueue;  //请求队列
    Locker m_queuelocker;       //保护请求队列的互斥锁
    Sem m_queuestat;        //信号量，任务数量
    connection_pool* m_connPool; //数据库连接池
    int m_actor_model;           //reactor/proactor切换
};

template<typename T>
threadpool<T>::threadpool(int actor_model,connection_pool* connPool,int thread_number,int max_request)
:m_actor_model(actor_model),m_thread_number(thread_number),m_max_requests(max_request),m_threads(nullptr),m_connPool(connPool){
    if(thread_number<=0||max_request<=0) throw std::exception();
    m_threads = new pthread_t[m_thread_number];   
    if(!m_threads) throw std::exception();
    for(int i=0;i<thread_number;++i){
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){
            delete[] m_threads;
            throw std::exception();
        }
        //将线程属性改为unjoinable 主线程分离，便于资源释放
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
}

template<typename T>  //reactor模式下的请求入队
bool threadpool<T>::append(T* request,int state){
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    //读写事件
    request->m_state = state;
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();     //信号量加一
    return true;
}

template<typename T>  //proactor模式下的请求入队
bool threadpool<T>::append_p(T* request){
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests){
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

//工作线程
template<typename T> 
void* threadpool<T>:: worker(void* arg){
    threadpool* pool = (threadpool*) arg;
    pool->run(); //每个线程创建时都会调用run(),睡眠在队列中
    return pool;
}

//线程睡眠，等待请求队列中新增任务
template<typename T>
void threadpool<T>::run(){
    while(true){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request) continue;
        //reacotr
        if(m_actor_model==1){
            //io事件：0为读取
            if(request->m_state==0){
                if(request->read_once()){
                request->improv = 1;
                connectionRAII mysqlcon(&request->mysql,m_connPool);
                request->process();
                }
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
            else{
                if(request->write()) request->improv = 1;
                else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }
        //default:Proactor，线程池不需要进行数据读取，而是直接开始业务处理
        //之前的操作已经将数据读取到http的read和write的buffer中了
        else{
            connectionRAII mysqlcon(&request->mysql,m_connPool);
            request->process();
        }
    }
}

#endif