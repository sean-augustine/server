#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include<stdio.h>
#include<unistd.h>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<errno.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netdb.h>
#include<sys/wait.h>
#include<netinet/in.h>//包含sockaddr_in结构
#include<assert.h>
#include<signal.h>
#include<sys/epoll.h>

typedef void sighandler(int);
typedef struct sockaddr SA;

class process
{
    public:
    process():m_pid(-1){}
    public:
    pid_t m_pid;
    int m_pipefd[2];//pipe for comunicating with father process
};

template<class T>
class processpool{
    private:
    processpool(int listenfd,int processnum=8);//singleton, only created by static function
    public:
    static processpool<T>* create(int listenfd, int processnum=8)
    {
        if(!m_instance)
        {
            m_instance = new processpool<T>(listenfd);
        }
        return m_instance;
    }
    ~processpool()
    {
        delete[] m_sub_process;
    }
    void run();//根据不同的进程索引号决定是run_parent还是run_child

    private:
    void setup_sig_pipe();//每个进程调用该函数设置epoll和信号通道
    void run_parent();
    void run_child();
    
    private:
    static const int MAX_PROCESS_NUMBER=16;//进程池最大进程数
    static const int USER_PER_PROCESS=65536;//？每个子进程最多处理的客户数量？
    static const int MAX_EVENT_NUMBER=10000;//epoll最多处理的事件数
    int m_process_number;//进程池的进程总数
    int m_idx;//子进程在进程池中的序号
    int m_epollfd;//每一个进程都有一个内核事件表用于epoll_wait，在fork之前创建，子进程继承父进程的m_epollfd
    int m_listenfd;//监听套接字
    int m_stop;//子进程通过该标志决定是否停止运行,同样继承而来
    process* m_sub_process;//保存所有子进程的描述信息
    static processpool<T>* m_instance;//静态实例
};

//??????

static int sig_pipefd[2];//每个进程都存在该信号管道

static int setnonblocking(int fd)
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

static void addfd(int epollfd,int fd)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

static void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

static void sig_handler(int sig)
{
    int save_errno=errno;
    int msg=sig;
    send(sig_pipefd[1],(char*)&msg,1,0);
    errno=save_errno;
}

static void addsig(int sig,sighandler* handler,bool restart=true)
{
    struct sigaction sa;
    bzero(&sa,sizeof(sa));
    sa.sa_handler=handler;
    if(restart)
    {
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);//NULL represent to ignore the old_sigaction
}

template<class T>
processpool<T>::processpool(int listenfd,int process_number)
:m_listenfd(listenfd),m_process_number(process_number),m_idx(-1),m_stop(false)
{
    assert((process_number>0)&&(process_number<=MAX_PROCESS_NUMBER));
    m_sub_process=new process[process_number];//动态分配进程空间
    assert(m_sub_process);

    for(int i=0;i<process_number;++i)
    {
        int ret=socketpair(AF_UNIX,SOCK_STREAM,0,m_sub_process[i].m_pipefd);//create fd_pipe for each sub_process
        assert(ret==0);
        m_sub_process[i].m_pid=fork();
        assert(m_sub_process[i].m_pid>=0);
        if(m_sub_process[i].m_pid>0)//父进程
        {
            close(m_sub_process[i].m_pipefd[1]);//父进程关闭管道1端
            continue;
        }
        else//每一个子进程中只执行一次循环
        {
            close(m_sub_process[i].m_pipefd[0]);//子进程关闭管道0端
            m_idx=i;//设置子进程子在数组中的下标
            break;
        }
    }
}

//设置统一事件源
template<class T>
void processpool<T>::setup_sig_pipe()
{
    m_epollfd=epoll_create(5);
    assert(m_epollfd!=-1);
    int ret=socketpair(AF_UNIX,SOCK_STREAM,0,sig_pipefd);//创建fd_pipe;
    assert(ret!=-1);
    setnonblocking(sig_pipefd[1]);//没当一个信号产生时，信号处理函数向管道中写一个信号值
    addfd(m_epollfd,sig_pipefd[0]);//监听fd_pipe的另一端，其中含有信号值

    addsig(SIGCHLD,sig_handler);
    addsig(SIGTERM,sig_handler);
    addsig(SIGINT,sig_handler);
    addsig(SIGPIPE,SIG_IGN);//往对端关闭的管道中写数据会产生该信息
}

template<class T>
void processpool<T>::run()
{
    if(m_idx!=-1)
    {
        run_child();
    }
    else
    run_parent();   
}

template<class T>
void processpool<T>::run_child()
{
    setup_sig_pipe();//每个子进程都设置信号管道，其中还设置了epollfd；
    int pipefd=m_sub_process[m_idx].m_pipefd[1];//得到与父进程通信的管道，在构造函数中创建
    addfd(m_epollfd,pipefd);
    
    epoll_event events[MAX_EVENT_NUMBER];
    T* users=new T[USER_PER_PROCESS];//每个子进程管理多个用户连接
    assert(users);
    int number=0;
    int ret=-1;
    
    while(!m_stop)
    {
        number=epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll error\n");
            break;
        }
        for(int i=0;i<number;++i)
        {
            int sockfd=events[i].data.fd;
            //处理父进程发送的消息，创建一个新连接
            if((sockfd==pipefd)&&(events[i].events&EPOLLIN))//还可能是出错
            {
                int client=0;
                ret=recv(sockfd,(char*)&client,sizeof(client),0);
                if((ret<0)&&(errno!=EAGAIN)||ret==0)
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlen=sizeof(client_address);
                    int connfd=accept(m_listenfd,(SA*)&client_address,&client_addrlen);
                    if(connfd<0)
                    {
                        printf("accept error:%s\n",strerror(errno));
                        continue;
                    }
                    addfd(m_epollfd,connfd);
                    users[connfd].init(m_epollfd,connfd,client_address);//逻辑处理对象必须实现init方法，初始化一个客户连接，并声明其所属的epoll_fd，连接套接字，以及客户地址
                }
            }
            //处理子进程收到的信号
            else if((sockfd==sig_pipefd[0])&&(events[i].events&EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret=recv(sockfd,signals,sizeof(signals),0);
                if(ret<=0)
                    continue;
                else
                {
                    for(int i=0;i<ret;++i)
                    {
                        switch(signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while((pid=waitpid(-1,&stat,WNOHANG))>0)
                                {
                                    continue;
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                m_stop=true;
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else if(events[i].events&EPOLLIN)
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }
    //释放子进程拥有的内存资源
    delete[] users;
    users=NULL;
    close(pipefd);//关闭子进程与父进程的管道，在初始化的过程中已经关闭的另一端
    close(m_epollfd);//setup_sig_pipe()时创建
}

template<class T>
void processpool<T>::run_parent()
{
    setup_sig_pipe();
    addfd(m_epollfd,m_listenfd);
    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter=0;//用于寻找可用子进程
    int new_conn=1;//用于通知子进程新连接，写入管道
    int number=0;//used for epoll_wait
    int ret=-1;//used for recv
    while(!m_stop)
    {
        number=epoll_wait(m_epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==m_listenfd)
            {
                int j=sub_process_counter;//每次从sub_process_counter开始出发寻找可用子进程
                do
                {
                    if(m_sub_process[j].m_pid!=-1)
                    {
                        break;
                    }
                    j=(j+1)%m_process_number;
                } while (j!=sub_process_counter);
                if(m_sub_process[j].m_pid==-1)//所有子进程停止运行
                {
                    m_stop=true;//父进程也停止运行
                    break;
                }
                sub_process_counter=(j+1)%m_process_number;//更新counter值指向下一个子进程
                send(m_sub_process[j].m_pipefd[0],(char*)&new_conn,sizeof(new_conn),0);//notify the sub_process to handle this connection
                printf("send request to child process: %d\n",j);
            }
            //处理信号
            else if((sockfd==sig_pipefd[0])&&events[i].events&EPOLLIN)
            {
                int sig;
                char signals[1024];
                ret=recv(sockfd,signals,sizeof(signals),0);
                if(ret<=0)
                    continue;
                else
                {
                    for(int i=0;i<ret;++i)
                    {
                        switch(signals[i])
                        {
                            case SIGCHLD:
                            {
                                pid_t pid;
                                int stat;
                                while((pid=waitpid(-1,&stat,WNOHANG))>0)
                                {
                                    for(int j=0;j<m_process_number;++j)
                                    {
                                        if(pid==m_sub_process[j].m_pid)
                                        {
                                            close(m_sub_process[j].m_pipefd[0]);
                                            m_sub_process[j].m_pid=-1;
                                            printf("child process %d join\n",j);
                                        }
                                    }
                                }
                                m_stop=true;
                                for(int j=0;j<m_process_number;++j)
                                {
                                    if(m_sub_process[j].m_pid!=-1)
                                    {
                                        m_stop=false;
                                        break;
                                    }
                                }
                                break;
                            }
                            case SIGTERM:
                            case SIGINT:
                            {
                                printf("kill all the child now\n");
                                for(int j=0;j<m_process_number;++j)
                                {
                                    int pid=m_sub_process[j].m_pid;
                                    if(pid!=-1)
                                    {
                                        kill(pid,SIGTERM);
                                    }
                                }
                                break;
                            }
                            default:
                            {
                                break;
                            }
                        }
                    }
                }
            }
            else//父进程只关注listenfd上是否有新连接和信号
                continue;
        }
    }
    close(m_epollfd);
}
#endif