#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/epoll.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<assert.h>
#include<sys/stat.h>
#include<string.h>
#include<pthread.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include<sys/uio.h>
#include"locker.h"

extern char args[1024];
extern char filename[1024];
class http_conn
{
private:
    /* data */
public:
    static const int FILENAME_LEN=200;
    static const int READ_BUFFER_SIZE=2048;
    static const int WRITE_BUFFER_SIZE=1024;
    enum METHOD {GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT,PATCH};
    enum CHECK_STATE {CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};//用于状态转移的状态变量
    enum HTTP_CODE {NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,FORBIDDEN_REQUEST,FILE_REQUEST,DYNAMIC_REQUEST,INTERNAL_ERROR,CLOSED_CONNECTION};
    enum LINE_STATUS {LINE_OK=0,LINE_BAD,LINE_OPEN};
    
    http_conn(){}
    ~http_conn(){}

    bool isstatic(){return is_static;}
    bool linger(){return m_linger;}

    void init(int sockfd,const sockaddr_in& addr);//初始化新的连接
    void close_conn(bool real_close=true);//用于关闭连接
    void process();//处理客户请求
    bool read();//非阻塞读操作
    bool write();//非阻塞写操作

private:
    void init();//初始化一些参数
    HTTP_CODE process_read();//解析HTTP请求
    bool process_write(HTTP_CODE ret);//根据HTTP_CODE的类型填充HTTP应答

    //下面一组函数用于背process_read()调用分析HTTP请求
    HTTP_CODE parse_request_line(char* text);//分析请求行
    HTTP_CODE parse_headers(char* text);//分析请求头部
    HTTP_CODE parse_content(char* text);//分析请求内容
    HTTP_CODE do_request();//根据请求内容映射文件
    char* get_line(){return m_read_buf+m_start_line;}
    LINE_STATUS parse_line();

    //下面一组函数用于process_write()调用用于填充HTTP应答
    void unmap();
    bool add_response(const char* format,...);//include<stdarg.h> for ...
    bool add_content(const char* content);
    bool add_status_line(int status,const char* title);
    bool add_header(int content_length);
    bool add_content_length(int content_length);
    bool add_linger();//构造应答头部：是否保持长连接
    bool add_blank_line();

public:
    static int m_epollfd;
    static int m_user_count;//统计所有http_conn所拥有的客户数量，即监听的总套接字数量
    
private:
    int m_sockfd;
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];//读缓冲区
    int m_read_idx;//标识读缓冲区中已经读入的客户数据的最后一个字节的下一个位置
    int m_checked_idx;//当前正在分析的字符在读缓冲区中的位置
    int m_start_line;//当前正在分析的字符在读缓冲区中的位置
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx;//写缓冲区中待发送的字符数

    CHECK_STATE m_check_state;//主状态机所属的状态
    METHOD m_method;//http请求方法

    char m_real_file[FILENAME_LEN];//完整路径名
    char *m_url;
    char *m_version;
    char* m_host;
    int m_content_length;
    bool m_linger;//http是否保持长连接

    bool is_static;

    char* m_file_address;//客户请求的文件被mmap到内存中的起始位置
    struct stat m_file_stat;//文件状态
    struct iovec m_iv[2];//用于writev
    int m_iv_count;
};


#endif