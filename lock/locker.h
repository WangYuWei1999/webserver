#ifndef _LOCKER_H
#define _LOCKER_H
#include<exception>
#include<pthread.h>
#include<semaphore.h>  //信号量 信号量本质上是一个具有原子性的计数器
//#include<mutex>   //todo

//信号量本质上是一个计数器
//（不设置全局变量是因为进程间是相互独立的，而这不一定能看到，看到也不能保证++引用计数为原子操作）,用于多进程对共享数据对象的读取，它和管道有所不同，它不以传送数据为主要目的，它主要是用来保护共享资源（信号量也属于临界资源），使得资源在一个时刻只有一个进程独享。
class Sem{
public:
    Sem(){
        //初始化信号量
        //第二个参数 pshared不为０时此信号量在进程间共享，否则只能为当前进程的所有线程共享；
        if((sem_init(&m_sem,0,0))!=0) throw std::exception();
    }
    Sem(int num){     //信号量赋初值
        if(sem_init(&m_sem,0,num)!=0) throw std::exception();
    }
    ~Sem(){
        sem_destroy(&m_sem);
    }

    //sem_wait 原子操作
    //如果信号量的值value大于零，就给它减1；如果它的值为零，就挂起该进程的执行 表明公共资源经使用后减少
    bool wait() {
        return sem_wait(&m_sem)==0;
    }

    //sem_post 原子操作
    //如果有其他进程因等待value而被挂起，就让它恢复运行，如果没有进程因等待sv而挂起，就给它加1.
    bool post(){
        return sem_post(&m_sem)==0;
    }

private: 
    sem_t m_sem; //创建一个信号量
};

//互斥锁
class Locker{
public:
    Locker(){
        if(pthread_mutex_init(&m_mutex,NULL)!=0) throw std::exception();
    }
    ~Locker(){
        pthread_mutex_destroy(&m_mutex);
    }
    bool lock(){
        return pthread_mutex_lock(&m_mutex)==0;
    }
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex)==0;
    }
    pthread_mutex_t* get(){
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
//std::mutex m_mutex;   
};

//条件变量
//条件变量要与互斥量一起使用，条件本身是由互斥量保护的。线程在改变条件状态之前必须首先锁住互斥量
class Cond{
public:
    Cond(){
        if(pthread_cond_init(&m_cond,NULL)!=0) throw std::exception();
    }
    ~Cond(){
        pthread_cond_destroy(&m_cond);
    }

    bool wait(pthread_mutex_t* mtx){
        int ret=0;
        //pthread_mutex_lock(&m_mutex);  //前提：互斥量提前锁定
        //阻塞等待条件变量变为真，阻塞状态的时候，mutex互斥量被解锁，当pthread_cond_wait函数返回时，互斥量再次被锁住
        ret=pthread_cond_wait(&m_cond,mtx);
        //pthread_mutex_unlock(&m_mutex);
        return ret==0;
    }

    bool timewait(pthread_mutex_t* mtx,struct timespec t){
        int ret=0;
        ret=pthread_cond_timedwait(&m_cond,mtx,&t);
        return ret==0;
    }

    //这两个函数用于通知线程条件变量已经满足条件（变为真）。在调用这两个函数时，是在给线程或者条件发信号
    bool signal(){
        return pthread_cond_signal(&m_cond)==0;  //至少能唤醒一个等待该条件的线程
    }

    bool brodcast(){
        return pthread_cond_broadcast(&m_cond)==0;  //唤醒等待该条件的所有线程
    }
private:
    pthread_cond_t m_cond;
};

#endif