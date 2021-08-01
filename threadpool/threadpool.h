#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<pthread.h>
#include<exception>
#include"locker.h"

template<class T>//T为任务类
class threadpool
{
public:
    threadpool(int thread_numbre=8,int max_requests=10000);
    ~threadpool();
    bool append(T* request);//向工作线程中添加任务

private:
    static void *worker(void* arg);//工作线程运行的函数，即线程历程
    void run();//在各自线程中调用，共同监听请求队列上的内容

    int m_thread_number;//线程池中的线程总数
    int m_max_requests;//请求队列中允许的最大请求数
    pthread_t* m_threads;//线程池数组，大小为m_thread_number
    std::list<T*> m_workqueue;//请求队列
    locker m_queuelocker;//保护请求队列的互斥锁
    sem m_queuestat;//是否有任务需要处理，实际为当前任务量
    bool m_stop;//是否结束线程,所有子线程共用一个threadloop

};

template<class T>
threadpool<T>::threadpool(int thread_number,int max_requests):m_thread_number(thread_number),m_max_requests(max_requests)
,m_stop(false),m_threads(NULL)
{
    if((thread_number<=0)||(max_requests<=0))
    {
        throw std::exception();
    }
    m_threads=new pthread_t[m_thread_number];
    if(!m_threads)
    {
        throw std::exception();
    }
    for(int i=0;i<m_thread_number;++i)//创建m_thread_number个线程
    {
        printf("creat the %dth thread\n",i);
        if(pthread_create(m_threads+i,NULL,worker,this)!=0)//this is a arg for worker，开始工作线程
        {
            delete[] m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]))
        {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template<class T>
threadpool<T>:: ~threadpool()
{
    delete[] m_threads;
    m_stop=true;
}

template<class T>
bool threadpool<T>::append(T* request)//主线程负责添加任务，线程池中的线程自动取任务
{
    m_queuelocker.lock();
    if(m_workqueue.size()>=m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();//将信号量加一，代表有任务来
    return true;
}

template<class T>
void* threadpool<T>::worker(void* arg)//arg is pointer of threadpool
{
    threadpool* pool=(threadpool*)arg;
    pool->run();
    return pool;
}

template<class T>
void threadpool<T>::run()//所有线程都在m_queuestat上等待，若当前有任务，则在加锁的同时取请求并执行；
{
    while(!m_stop)
    {
        m_queuestat.wait();//等待直到工作队列不为空
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request)//如果请求为空
        {
            continue;
        }
        request->process();
    }
}

#endif