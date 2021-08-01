#include"processpool.h"
#include"cgiconn.h"
#include <arpa/inet.h>

template<typename T>
processpool<T>* processpool<T>::m_instance=NULL;

int cgi_conn::m_epollfd=-1;

int main(int argc,char** argv)
{
    if(argc<=2)
    {
        printf("usage: %s <IP> <Portnum>",basename(argv[0]));
        return 1;
    }
    const char *ip=argv[1];
    int port=atoi(argv[2]);
    int listenfd=socket(AF_INET,SOCK_STREAM,0);
    assert(listenfd>0);
    int ret=0;
    struct sockaddr_in hostaddr;
    bzero(&hostaddr,sizeof(hostaddr));
    hostaddr.sin_family=AF_INET;
    hostaddr.sin_port=htons(port);
    inet_pton(AF_INET,ip,&hostaddr.sin_addr);
    ret=bind(listenfd,(SA*)&hostaddr,sizeof(hostaddr));
    assert(ret!=-1);
    ret=listen(listenfd,5);
    assert(ret!=-1);
    processpool<cgi_conn>* pool=processpool<cgi_conn>::create(listenfd);
    if(pool)
    {
        pool->run();
        delete pool;
    }
    close(listenfd);
    return 0;
}
