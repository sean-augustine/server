#include"threadpool.h"
#include"http_conn.h"
#include<cassert>
#include<sys/wait.h>

#define MAX_FD 65536//主线程管理的最大连接数
#define MAX_EVENT_NUMBER 10000//epoll监听的最大事件数
#define MAXLINE 1024

typedef void sighandler(int);
typedef struct sockaddr SA;

extern int addfd(int epollfd,int fd,bool one_shot);
extern int removefd(int epollfd,int fd);

char args[MAXLINE];
char filename[MAXLINE];

void server_dynamic(int fd)
{
    int cpid;
    char* emptylist[]={NULL};
    if((cpid=fork())==0)
    {
        setenv("QUERY_STRING",args,1);
        close(STDOUT_FILENO);
        dup(fd);
        execve(filename,emptylist,environ);
        exit(0);
    }
    waitpid(cpid,NULL,0);
}

void addsig(int sig,sighandler* handler,bool restart=true)
{
    struct sigaction sa;
    bzero(&sa,sizeof(sa));
    sa.sa_handler=handler;
    if(restart)
    {
        sa.sa_flags|=SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig,&sa,NULL)!=-1);
}

void show_error(int connfd,const char* info)
{
    printf("%s",info);
    send(connfd,info,strlen(info),0);
    close(connfd);
}

int main(int argc,char** argv)
{
    if(argc<=2)
    {
        printf("usage: %s <IP> <PORT>\n",basename(argv[0]));
        return 1;
    }
    const char*ip=argv[1];
    int port=atoi(argv[2]);
    addsig(SIGPIPE,SIG_IGN);
    threadpool<http_conn>* pool=NULL;
    try{
        pool=new threadpool<http_conn>;//初始化线程池，同时默认开启8个工作线程
    }
    catch(...)
    {
        return 1;
    }

    http_conn* users=new http_conn[MAX_FD];
    assert(users);
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);
    struct linger tmp={1,0};//linger{int l_onoff开关,int l_linger拖延时间}
    setsockopt(listenfd,SOL_SOCKET,SO_LINGER,&tmp,sizeof(tmp));
    int ret=0;
    struct sockaddr_in address;
    bzero(&address,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(port);
    inet_pton(AF_INET,ip,&address.sin_addr);
    ret=bind(listenfd,(SA*)&address,sizeof(address));
    assert(ret>=0);

    ret=listen(listenfd,5);
    assert(ret>=0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);
    assert(epollfd!=0);
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;//直接赋值，因为m_epollfd为public对象
     
    while(1)//主线程负责accept和分发connfd
    {
        int number=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((number<0)&&(errno!=EINTR))
        {
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<number;++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                struct sockaddr_in peeraddr;
                socklen_t addrlen=sizeof(peeraddr);
                int connfd=accept(listenfd,(SA*)&peeraddr,&addrlen);
                if(connfd<0)
                {
                    printf("accept error: %s",strerror(errno));
                    close(connfd);
                    continue;
                }
                if(http_conn::m_user_count>=MAX_FD)
                {
                    show_error(connfd,"Internal server busy");//close(connfd) internal
                    continue;
                }
                users[connfd].init(connfd,peeraddr);// 每次新建一个连接都要对http_conn进行初始化
            }
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR))//fd上出错
            {
                users[sockfd].close_conn();
            }
            else if(events[i].events&EPOLLIN)
            {
                if(users[sockfd].read())//根据读的结果，决定将任务添加到线程池中还是关闭连接
                {
                    pool->append(users+sockfd);//最终调用对应http_conn的process()函数
                }
                else
                {
                    users[sockfd].close_conn();//读出错或者读到eof或者应用层的读缓冲区已经写满
                }
            }
            else if(events[i].events&EPOLLOUT)
            {
                if(users[sockfd].isstatic())
                {
                    if(!users[sockfd].write())//根据写的结果决定是否关闭连接
                    {
                        users[sockfd].close_conn();
                    }
                }
                else
                {
                    users[sockfd].write();
                    server_dynamic(sockfd);
                    if(users[sockfd].linger())
                    {
                        users[sockfd].close_conn();
                    }
                }
            }
            else
            {}

        }

    }
    close(listenfd);
    close(epollfd);
    delete[] users;
    delete pool;
    return 0;
}