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
#define BUFFER_SIZE 10
typedef sockaddr SA;

char* sock_ntop(sockaddr*sa,socklen_t salen)
{
    static char str[128];
    char portstr[8];
    sockaddr_in *sin=(sockaddr_in*) sa;
    if(inet_ntop(AF_INET,&sin->sin_addr,str,sizeof(str))==NULL)
    return NULL;
    if(ntohs(sin->sin_port)!=0)
    {
        snprintf(portstr,sizeof(portstr),":%d",ntohs(sin->sin_port));
        strcat(str,portstr);
    }
    return str;
} 

int setnoblock(int fd)//set fd to noblock;
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

void addfd(int epollfd,int fd,bool enable_et)//add fd into epllo_check(read) with et or ont;
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN;
    if(enable_et)
    {
        event.events|=EPOLLET;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnoblock(fd);
}

void lt(int epollfd,epoll_event *events,int num,int listenfd)//recive datas under lt mode;
{
    char buf[BUFFER_SIZE];
    int n;
    for(int i=0;i<num;++i)
    {
        int socket=events[i].data.fd;
        if(socket==listenfd)
        {
            sockaddr_in cliaddr;
            socklen_t clilen=sizeof(cliaddr);
            int connfd=accept(socket,(SA*)&cliaddr,&clilen);
            addfd(epollfd,connfd,false);
            int port=ntohs(cliaddr.sin_port);
            char str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,&cliaddr.sin_addr,str,INET_ADDRSTRLEN);
            printf("new connection form: %s-%d\n",str,port);
        }
        else if(events[i].events&EPOLLIN)
        {
            printf("event trriger once\n");
            memset(buf,'\0',BUFFER_SIZE);
            if((n=read(socket,buf,BUFFER_SIZE-1))<0)
            {
                close(socket);//error happened;
                continue;
            }
            else if(n==0)
            {
                printf("connetion closed\n");
                close(socket);
                continue;
            }
            printf("get %d bytes content: %s\n",n,buf);
        }
        else
        printf("somethings else happend");
    }
}

void et(int epollfd,epoll_event *events,int num,int listenfd)
{
    char buf[BUFFER_SIZE];
    int n;
    for(int i=0;i<num;++i)
    {
        int socket=events[i].data.fd;
        if(socket==listenfd)
        {
            sockaddr_in cliaddr;
            socklen_t clilen=sizeof(cliaddr);
            int connfd=accept(socket,(SA*)&cliaddr,&clilen);
            addfd(epollfd,connfd,true);
            int port=ntohs(cliaddr.sin_port);
            char str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET,&cliaddr.sin_addr,str,INET_ADDRSTRLEN);
            printf("new connection form: %s-%d\n",str,port);
        }
        else if(events[i].events&EPOLLIN)
        {
            char buf[BUFFER_SIZE];
            printf("event trrigle once\n");
            while(1)
            {
                memset(buf,'\0',BUFFER_SIZE);
                n=read(socket,buf,BUFFER_SIZE-1);
                if(n<0)
                {
                    if(errno==EAGAIN||errno==EWOULDBLOCK)
                    {
                        printf("read finished under et\n");
                        break;
                    }
                    close(socket);//error happened;
                    break;
                }
                else if(n==0)
                {
                    close(socket);
                }
                else
                {
                    printf("get %d bytes contents: %s\n",n,buf);
                }
            }
        }
        else 
        {
            printf("somethings else happened\n");
        }
    }
}

int main(int argc,char** argv)
{
    if(argc<=3)
    {
        printf("usage: %s <IP> <portnum> <mode>\n",basename(argv[0]));
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
    addfd(epollfd,listenfd,true);
    while(1)
    {
        int ret=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if(ret<0)
        {
            printf("epoll failure");
            break;
        }
        else{
            int mode=atoi(argv[3]);
            if(mode) 
            et(epollfd,events,ret,listenfd);
            else
            lt(epollfd,events,ret,listenfd);
        }
    }
    close(listenfd);
    close(epollfd);
    return 0;

}

