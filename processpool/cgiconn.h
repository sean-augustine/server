#ifndef CGICONN_H
#define CGICONN_H
#include"processpool.h"

#define MAXLINE 8192

extern char **environ;

class cgi_conn
{
private:
    static const int BUFFER_SIZE=1024;
    static int m_epollfd;
    int m_sockfd;
    struct sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];
    int m_read_idx;//记录待读数据在m_buf中的起始位置
public:
    cgi_conn(){}
    ~cgi_conn(){}
    void init(int epollfd,int sockfd,const sockaddr_in& clientaddr)
    {
        m_sockfd=sockfd;
        m_epollfd=epollfd;
        m_address=clientaddr;
        memset(m_buf,'\0',BUFFER_SIZE);
        m_read_idx=0;
    }
    void process();
};

#endif

