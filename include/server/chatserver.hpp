#ifndef CHATSERVER_H
#define CHATSERVER_H
#include<muduo/net/TcpServer.h>
#include<muduo/net/EventLoop.h>
using namespace muduo;
using namespace muduo::net;
class ChatServer
{
public:
    ChatServer(EventLoop *loop,
               const InetAddress &listenAddr, // ip+port
               const string &nameArg);         // 服务器的名字
       
    void start();
   
private:
    // 专门处理用户的连接创建和断开   epoll listenfd accept
    void onConnection(const TcpConnectionPtr&);
    // 专门处理用户的读写事件
    void onMessage(const TcpConnectionPtr&, // 连接
                  Buffer*,                  // 缓冲区
                  Timestamp);               // 接收到数据的时间信息
    TcpServer _server; // 组合的muduo库 ,实现服务器功能的类对象
    EventLoop *_loop;              // 指向事件循环的指针
};

#endif