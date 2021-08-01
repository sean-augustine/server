#include"cgiconn.h"

void cgi_conn::process()//处理sockfd上的可读事件
{
    char* emptylist[]={NULL};
    int idx=0;
    int ret=-1;//用于recv
    while(1)//因为是ET模式，所以必须在循环中读取数据，直到遇到EAGAIN为止
    {
        idx=m_read_idx;
        ret=recv(m_sockfd,m_buf+idx,sizeof(m_buf)-1-idx,0);
        if(ret<0)
        {
            if(errno!=EAGAIN)//代表出错
            {
                removefd(m_epollfd,m_sockfd);
            }
            break;//不管出错还是读完都退出循环
        }
        else if(ret==0)//recive EOF
        {
            removefd(m_epollfd,m_sockfd);
            break;//退出循环并取消监听m_sockfd
            //??为什么不用关闭m_sockfd(connfd)??
        }
        else
        {
            m_read_idx+=ret;
            printf("user content is: %s\n",m_buf);
            //如果遇到/r/n则开始处理用户请求
            for(;idx<m_read_idx;++idx)
            {
                if((idx>=1)&&(m_buf[idx-1]=='\r')&&(m_buf[idx]=='\n'))
                {
                    break;
                }
            }
            if(idx==m_read_idx)//未读到完整的一行
            {
                continue;
            }
            m_buf[idx-1]='\0';
            char pathname[MAXLINE],args[MAXLINE];
            strcpy(pathname,".");
            char *p=strchr(m_buf,'?');
            *p++='\0';
            strcat(pathname,m_buf);
            p+=strspn(p,"?");
            strcpy(args,p);
            if(access(pathname,F_OK)==-1)//文件不存在
            {
                printf("file doesn't exit\n");
                removefd(m_epollfd,m_sockfd);
                break;
            }
            ret=fork();
            if(ret==-1)
            {
                removefd(m_epollfd,m_sockfd);
                break;
            }
            else if(ret>0)
            {
                removefd(m_epollfd,m_sockfd);
                break;
            }
            else//子进程执行cgi程序
            {
                setenv("QUERY_STRING",args,1);
                close(STDOUT_FILENO);
                dup(m_sockfd);
                execve(pathname,emptylist,environ);
                exit(0);
            }
        }
    }
}
