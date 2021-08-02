#include"http_conn.h"

const char* ok_200_tiltle="OK";
const char* error_400_title="Bad Request";
const char* error_400_form="Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title="Forbidden";
const char* error_403_form="You do not have permission to get file from this server.\n";
const char* error_404_title="Not Found";
const char* error_404_form="The requested file was not found on this server.\n";
const char* error_500_title="Internal Error";
const char* error_500_form="There was an unusual problem serving the requested file.\n";


int setnonblocking(int fd)
{
    int old_option=fcntl(fd,F_GETFL);
    int new_option=old_option|O_NONBLOCK;
    fcntl(fd,F_SETFL,new_option);
    return old_option;
}

void addfd(int epollfd,int fd,bool one_shot)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=EPOLLIN|EPOLLET|EPOLLRDHUP;
    if(one_shot)
    {
        event.events|=EPOLLONESHOT;
    }
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
    setnonblocking(fd);
}

void removefd(int epollfd,int fd)
{
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

void modfd(int epollfd,int fd,int ev)
{
    epoll_event event;
    event.data.fd=fd;
    event.events=ev|EPOLLET|EPOLLONESHOT|EPOLLRDHUP;//这里会重置epollnoneshot
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

int http_conn::m_user_count=0;
int http_conn::m_epollfd=-1;

void http_conn::close_conn(bool real_close)
{
    if(real_close&&(m_sockfd!=-1))//m_socket代表连接套接字，连接套接字为-1代表该http_conn已经被close过了
    {
        removefd(m_epollfd,m_sockfd);//将对应的连接套接字踢出监听队列并关闭连接套接字
        m_sockfd=-1;//对应的http_conn管理的套接字置为-1；
        m_user_count--;//连接总数减少1
    }
}

void http_conn::init(int sockfd,const sockaddr_in& addr)//初始化一个用户连接，内部条用init()
{
    m_sockfd=sockfd;
    m_address=addr;

    int reuse=1;//用于设置地址复用套接字选项
    setsockopt(m_sockfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    addfd(m_epollfd,sockfd,true);
    m_user_count++;
    
    init();
}

void http_conn::init()
{
    m_check_state=CHECK_STATE_REQUESTLINE;
    m_linger=false;
    m_method=GET;
    m_url=0;//filname(string)
    m_version=0;//version(string)
    m_content_length=0;//message body length(int)
    m_host=0;//主机名, 由请求方在请求头部中确定

    is_static=true;

    m_start_line=0;//行起始位置
    m_checked_idx=0;//当前检查的字符位置
    m_read_idx=0;//已读入数据的下一个位置
    m_write_idx=0;
    bzero(m_read_buf,READ_BUFFER_SIZE);
    bzero(m_write_buf,WRITE_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);
}


http_conn::LINE_STATUS http_conn::parse_line()//其中更新m_check_idx，使其要不指向新一行的开始，要不指向m_read_idx：说明还需要继续读数据
{
    char temp;
    for(;m_checked_idx<m_read_idx;++m_checked_idx)
    {
        temp=m_read_buf[m_checked_idx];
        if(temp=='\r')
        {
            if((m_checked_idx+1)==m_read_idx)//同时要保持m_checked_idx指向‘r’字符
            {
                return LINE_OPEN;//说明还需要继续读数据
            }
            else if(m_read_buf[m_checked_idx+1]=='\n')
            {
                m_read_buf[m_checked_idx++]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;//说明读完一行
            }
            return LINE_BAD;
        }
        else if(temp=='\n')
        {
            if(m_checked_idx>1&&m_read_buf[m_checked_idx-1]=='\r')
            {
                m_read_buf[m_checked_idx-1]='\0';
                m_read_buf[m_checked_idx++]='\0';
                return LINE_OK;
            }
            return LINE_BAD;//说明有语法问题
        }
    }
    return LINE_OPEN;
}


bool http_conn::read()//循环读取数据直到无数据可读
{
    if(m_read_idx>=READ_BUFFER_SIZE)//代表该连接的读缓冲区已经写满
    {
        return false;
    }
    int bytes_read=0;
    while(1)
    {
        bytes_read=recv(m_sockfd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(bytes_read==-1)
        {
            if(errno==EAGAIN||errno==EWOULDBLOCK)
            {
                break;//会在之后调用process()中重置epolloneshot
            }
            return false;//代表出错,导致关闭连接
        }
        else if(bytes_read==0)
        {
            return false;//读到EOF,关闭连接
        }
        m_read_idx+=bytes_read;
    }
    return true;//代表该描述符上的数据读取完毕
}

//分析请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char* text)
{
    m_url=strpbrk(text," \t");
    if(!m_url)
    {
        return BAD_REQUEST;//未找到空格分隔符，则返回请求语法有问题
    }
    *m_url++='\0';
    char *method=text;
    if(strcasecmp(method,"GET")==0)
    {
        m_method=GET;
    }
    else
    {
        return BAD_REQUEST;
    }
    m_url+=strspn(m_url," \t");
    m_version=strpbrk(m_url," \t");
    if(!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++='\0';
    m_version+=strspn(m_version," \t");
    if(strcasecmp(m_version,"http/1.1")!=0)
    {
        return BAD_REQUEST;
    }
    if(strncasecmp(m_url,"http://",7)==0)
    {
        m_url+=7;
        m_url=strchr(m_url,'/');//找到第一个目录斜杠
    }
    if(!m_url||m_url[0]!='/')//若没有找到目录斜杠
    {
        return BAD_REQUEST;
    }
    if(strstr(m_url,"cgi-bin"))
    {
        is_static=false;
    }
    m_check_state= CHECK_STATE_HEADER;//开始分析请求头部
    return NO_REQUEST;//请求分析结束，需要继续读取
}

http_conn::HTTP_CODE http_conn::parse_headers(char* text)//解析请求头部的一行
{
    if(text[0]=='\0')//在parse_line阶段将空行转换程空字符，遇到空白行代表头部解析完毕
    {
        if(m_content_length!=0)//http有消息体
        {
            m_check_state=CHECK_STATE_CONTENT;//转换阶段到分析消息体
            return NO_REQUEST;//代表还需要继续读取消息体
        }
        return GET_REQUEST;//没有消息体则代表已经获得一个完整的http请求
    }
    else if(strncasecmp(text,"Connection:",11)==0)
    {
        text+=11;
        text+=strspn(text," \t");
        if(strcasecmp(text,"keep-alive")==0)
        {
            m_linger=true;
        }
    }
    else if(strncasecmp(text,"Content-length:",15)==0)
    {
        text+=15;
        text+=strspn(text," \t");
        m_content_length=atoi(text);
    }
    else if(strncasecmp(text,"Host:",5)==0)
    {
        text+=5;
        text+=strspn(text," \t");
        m_host=text;
    }
    else
    {
        printf("oop! unknow header: %s",text);
    }
    return NO_REQUEST;//代表请求不完整，需要继续读取数据
}

http_conn::HTTP_CODE http_conn::parse_content(char* text)
{
     //存在一点问题：若当前读缓冲区中已经有一部分的消息体被读入，下一次继续分析时，m_checked_idx指向的应为上一次部分消息体的结尾，下面条件就有问题??????
    if(m_read_idx>=(m_content_length+m_checked_idx))//消息体被完全读入到读缓冲区中
    {
        text[m_content_length]='\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;//否则需要继续读,m_check_state保持不变
}

//分析http请求的状态机,在分析过程中依此确定m_method,m_url,m_version,m_host,m_content_length,m_linger
//m_filename,同时m_read_idx,m_check_idx,m_start_line发生变化
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status=LINE_OK;
    HTTP_CODE ret=NO_REQUEST;
    char *text=0;
    //parse_line中保持check_idx指向下一行的开头
    while(((m_check_state==CHECK_STATE_CONTENT)&&(line_status=LINE_OK))||((line_status=parse_line())==LINE_OK))//如果是上一次消息体未分析完，则第一个条件满足??????
    {
        text=get_line();
        m_start_line=m_checked_idx;//将m_start_line指向下一行的开头
        printf("got 1 http line: %s",text);
        switch(m_check_state)
        {
            case CHECK_STATE_REQUESTLINE:
            {
                ret=parse_request_line(text);
                if(ret==BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER:
            {
                ret=parse_headers(text);
                if(ret==BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if(ret==GET_REQUEST)
                {
                    return do_request();//没有消息体的情况
                }
                break;
            }
            case CHECK_STATE_CONTENT:
            {
                ret=parse_content(text);
                if(ret==GET_REQUEST)
                {
                    return do_request();//要么返回出错(指定process write写的内容)，要么返回file—request
                }
                line_status=LINE_OPEN;//代表要继续向下读取content的内容
                break;
            }
            default:
            {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}
//根据请求的文件设置相应的映射mmap
http_conn::HTTP_CODE http_conn::do_request()//seems to only support GET filedata
{
    char* ptr=strchr(m_url,'?');
    if(ptr)
    {
        *ptr++='\0';
        strcpy(args,ptr);
    }
    else
    {
        strcpy(args," ");
    }
    strcpy(m_real_file,".");
    strcat(m_real_file,m_url);
    if(stat(m_real_file,&m_file_stat)<0)
    {
        return NO_RESOURCE;
    }
    if(!(m_file_stat.st_mode&S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if(S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    if(is_static)
    {
        int fd=open(m_real_file,O_RDONLY);
        m_file_address=(char*)mmap(0,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);//最后一个0为offset
        close(fd);
        return FILE_REQUEST;
    }
    else
    {
        strcpy(filename,m_real_file);
        if(!(S_IXOTH&m_file_stat.st_mode))
            return BAD_REQUEST;
        return DYNAMIC_REQUEST;
    }
    
}
void http_conn::unmap()
{
    if(m_file_address)
    {
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address=0;
    }
}

//下面为写http的响应

bool http_conn::write()
{
    int temp=0;
    int bytes_have_send=0;
    int bytes_to_send=m_write_idx;//写缓冲区中待发送的字符数
    if(bytes_to_send==0)
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);//已经没有要发送的数据，则更改监听事件为是否可读
        init();//同时还原所用状态用于对应新的连接
        return true;
    }
    while(1)
    {
        temp=writev(m_sockfd,m_iv,m_iv_count);
        if(temp<=-1)
        {
            if(errno==EAGAIN)
            {
                modfd(m_epollfd,m_sockfd,EPOLLOUT);//如果当前不能写，则继续监听写事件
                return true;
            }
            unmap();//其它错误则取消映射
            return false;
        }
        bytes_to_send-=temp;
        bytes_have_send+=temp;
        if(bytes_to_send<=bytes_have_send)//有问题吧??????
        {
            unmap();
            if(m_linger||!is_static)
            {
                init();
                modfd(m_epollfd,m_sockfd,EPOLLIN);
                return true;//对应不关闭连接
            }
            else//如果不保持长连接
            {
                modfd(m_epollfd,m_sockfd,EPOLLIN);//返回原始的监听状态
                return false;//对应关闭连接
            }
        }
    }
}

bool http_conn::add_response(const char* format,...)//思考c++中如何实现的
{
    if(m_write_idx>=WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;//represent the ...
    va_start(arg_list,format);//相当于初始化
    int len=vsnprintf(m_write_idx+m_write_buf,WRITE_BUFFER_SIZE-1-m_write_idx,format,arg_list);//使用参数列表进行格式化输出
    if(len>=(WRITE_BUFFER_SIZE-1-m_write_idx))
    {
        return false;
    }
    m_write_idx+=len;
    va_end(arg_list);//保证函数可以返回
    return true;
}

bool http_conn::add_status_line(int status,const char* title)
{
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}

bool http_conn::add_header(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content_length: %d\r\n",content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n",(m_linger)?"keep-alive":"close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s","\r\n");
}

bool http_conn::add_content(const char* content)
{
    return add_response("%s",content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    switch(ret)//根据http请求的结果决定返回给客户端的内容
    {
        case INTERNAL_ERROR:
        {
            add_status_line(500,error_500_title);
            add_header(strlen(error_500_form));
            if(!add_content(error_500_form))
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line(400,error_400_title);
            add_header(strlen(error_400_form));
            if(!add_content(error_400_form))
            {
                return false;
            }
            break;
        
        }
        case NO_RESOURCE:
        {
            add_status_line(404,error_404_title);
            add_header(strlen(error_404_form));
            if(!add_content(error_404_form))
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line(403,error_403_title);
            add_header(strlen(error_403_form));
            if(!add_content(error_403_form))
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST:
        {
            add_status_line(200,ok_200_tiltle);
            if(m_file_stat.st_size!=0)//如果不是空文件
            {
                add_header(m_file_stat.st_size);
                m_iv[0].iov_base=m_write_buf;
                m_iv[0].iov_len=m_write_idx;
                m_iv[1].iov_base=m_file_address;
                m_iv[1].iov_len=m_file_stat.st_size;
                m_iv_count=2;
                return true;
            }
            break;
        }
        case DYNAMIC_REQUEST:
        {
            add_status_line(200,ok_200_tiltle);
            add_linger();
            break;
        }
        default:
        {
            return false;
        }
    }
    //如果没有请求文件或者文件长度为零则只发送写缓冲区中的内容
    m_iv[0].iov_base=m_write_buf;
    m_iv[0].iov_len=m_write_idx;
    m_iv_count=1;
    return true;

}
//process()函数根据读缓冲区中的内容，调用process_write()填充写缓冲区的内容，最后添加写事件
void http_conn::process()
{
    HTTP_CODE read_ret=process_read();
    if(read_ret==NO_REQUEST)//未读到完整的http请求
    {
        modfd(m_epollfd,m_sockfd,EPOLLIN);//重置为初始状态，需要继续监听读事件
        return;
    }
    bool write_ret=process_write(read_ret);
    if(!write_ret)//如果存在不合理的请求或者写操作未成功，则直接关闭连接
    {
        close_conn();
    }
    modfd(m_epollfd,m_sockfd,EPOLLOUT);//代表写缓冲区已经准备就绪，监听套接字可写事件
}
