/*************************************************************
*循环数组实现的阻塞队列 m_back = (m_back + 1) % m_max_size;  
*线程安全，每个操作前都要先加互斥锁，操作完后，再解锁
**************************************************************/

#ifndef _BLOCK_QUEUE_H
#define _BLOCK_QUEUE_H
#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<thread>
#include<sys/time.h>
#include"../lock/locker.h"

template<typename T>
class BlockQueue{
public:
    BlockQueue(int max_size=1000){
        if(max_size<=0) exit(-1);

        m_max_size=max_size;
        m_arry=new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~BlockQueue(){
        m_mutex.lock();
        if(m_arry!=nullptr) delete[] m_arry;
        m_mutex.unlock();
    }

    void clear(){
        m_mutex.lock();
        m_size=0;
        m_front=-1;
        m_back=-1;
        m_mutex.unlock();
    }

    bool full(){    //判断队列是否满了
        m_mutex.lock();
        if(m_size>=m_max_size){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty(){  //判断队列是否为空
        m_mutex.lock();
        if(m_size==0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T& value){   //返回队首元素
        m_mutex.lock();
        if(m_size==0){
            m_mutex.unlock();
            return false;
        }
        value=m_arry[m_front];  //将队首元素传到value中
        m_mutex.unlock();
        return true;
    }

    bool back(T& value){   //返回队尾元素
        m_mutex.lock();
        if(m_size==0){
            m_mutex.unlock();
            return false;
        }
        value=m_arry[m_back];
        m_mutex.unlock();
        return true;
    }

    //返回当前队列大小
    int size(){
        int temp=0;
        m_mutex.lock();
        temp=m_size;
        m_mutex.unlock();
        return temp;
    }

    int max_size(){
        int temp=0;
        m_mutex.lock();
        temp=m_max_size;
        m_mutex.unlock();
        return temp;
    }

    //往当前队列添加元素
    bool push(const T& item){
        m_mutex.lock();
        if(m_size>=m_max_size){
            m_cond.brodcast();
            m_mutex.unlock();
            return false;
        }
        m_back=(m_back+1)%m_max_size;
        m_arry[m_back]=item;
        m_size++;
        m_cond.brodcast();
        m_mutex.unlock();
        return true;
    }

    //pop 
    bool pop(T& item){
        m_mutex.lock();
        while(m_size<=0){
            //如果队列为空，等待条件变量
            if(!m_cond.wait(m_mutex.get())){
                m_mutex.unlock();
                return false;
            }
        }
        m_front=(m_front+1)%m_max_size;
        item=m_arry[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    //超时处理
    bool pop(T& item,int ms_timeout){
        struct timespec t={0,0};
        struct timeval now={0,0};
        gettimeofday(&now,NULL);
        m_mutex.lock();
        if(m_size<=0){
            t.tv_sec=now.tv_sec+ms_timeout/1000;  //设置时间结构体
            t.tv_nsec=(ms_timeout%1000)*1000;
            if(!m_cond.timewait(m_mutex.get(),t)){
                m_mutex.unlock();
                return false;
            }
        }
        if(m_size<=0){
            m_mutex.lock();
            return false;
        }
        m_front=(m_front+1)%m_max_size;
        item=m_arry[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

private:
    Locker m_mutex;
    Cond m_cond;

    T* m_arry;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};


#endif