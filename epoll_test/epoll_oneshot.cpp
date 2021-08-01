#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<pthread.h>


#define MAX_EVENT_NUMBER 1024
#define BUFFER_SIZE 1024

typedef sockaddr SA;

struct fds
{
    int epollfd;
    int sockfd;
};

int setnoblock(int fd)//set fd to noblock;
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

void addfd(int epollfd,int fd,bool oneshot)//add fd into epllo_check(read) with et or ont;
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET;
    if(oneshot)
    {
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnoblock(fd);
}

void reset_oneshot(int epollfd,int fd)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLONESHOT;
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

void* worker(void* arg)//work thread;
{
    pthread_detach(pthread_self());
    int sockfd=((fds*)arg)->sockfd;
    int epollfd=((fds*)arg)->epollfd;
    printf("start new thread %d to recive data on fd: %d\n",(int)pthread_self(),sockfd);
    char buff[BUFFER_SIZE];
    memset(buff,'\0',BUFFER_SIZE);
    while(1)
    {
        int ret=read(sockfd,buff,BUFFER_SIZE);
        if(ret<0)
        {
            if(errno==EAGAIN||errno==EWOULDBLOCK)
            {
                printf("read later\n");
                reset_oneshot(epollfd,sockfd);
                break;
            }
            else{
                printf("read error\n");
                close(sockfd);
                break;
            }
        }
        else if(ret==0)
        {
            printf("connetion close\n");
            close(sockfd);
            break;
        }
        else
        {
            printf("get content:%s",buff);
            sleep(5);
        }
    }
    printf("handled once read event on sockfd:%d\n",sockfd);
    return(NULL);
}

int main(int argc,char** argv)
{
    if(argc<=2)
    {
        printf("usage: %s <IP> <portnum> \n",basename(argv[0]));
        return 1;
    }
    int port=atoi(argv[2]);
    char *ip=argv[1];
    int ret=0;
    sockaddr_in servaddr;
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family=AF_INET;
    servaddr.sin_port=htons(port);
    inet_pton(AF_INET,ip,&servaddr.sin_addr);
    //servaddr.sin_addr.s_addr=htonl(INADDR_ANY);
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    assert(listenfd>=0);
    ret=bind(listenfd,(SA*)&servaddr,sizeof(servaddr));
    assert(ret!=-1);
    ret=listen(listenfd,5);
    assert(ret!=-1);
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);
    assert(epollfd!=-1);
    addfd(epollfd,listenfd,false);
    while(1)
    {
        int ret=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(ret<0)
        {
            printf("epoll failure");
            break;
        }
        for(int i=0;i<ret;++i)
        {
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd)
            {
                sockaddr_in cliaddr;
                socklen_t clilen=sizeof(cliaddr);
                int connfd=accept(sockfd,(SA*)&cliaddr,&clilen);
                addfd(epollfd,connfd,true);
                int port=ntohs(cliaddr.sin_port);
                char str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET,&cliaddr.sin_addr,str,INET_ADDRSTRLEN);
                printf("new connection form: %s-%d\n",str,port);
            }
            else if(events[i].events&EPOLLIN)
            {
                fds arg;
                arg.epollfd=epollfd;
                arg.sockfd=sockfd;
                pthread_t tid;
                pthread_create(&tid,NULL,worker,(void*)&arg);
            }
            else
            {
                printf("something else happened\n");
            }
        }
    }
    close(listenfd);
    close(epollfd);
    return 0;
}