#include"chatserver.hpp"
#include"json.hpp"
#include"chatservice.hpp"
#include <functional>
#include<string>
using namespace std;
using namespace placeholders;
using json =nlohmann::json;

ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr, // ip+port
                       const string &nameArg)         // 服务器的名字
    : _server(loop, listenAddr, nameArg),
    _loop(loop)
{
    // 给服务器注册用户连接的创建和断开回调
    _server.setConnectionCallback(bind(&ChatServer::onConnection, this, _1));
    // 给服务器注册用户读写事件回调
    _server.setMessageCallback(bind(&ChatServer::onMessage, this, _1, _2, _3));
    // 设置服务器的线程数量 1个I/O线程 3个work线程
    _server.setThreadNum(4);
}
//启动服务
void ChatServer:: start()
{
    _server.start(); 
}
// 专门处理用户的连接创建和断开   epoll listenfd accept
void ChatServer:: onConnection(const TcpConnectionPtr&conn)
{
    //客户端断开连接
    if(!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}
// 专门处理用户的读写事件
void ChatServer:: onMessage(const TcpConnectionPtr&conn, // 连接
                          Buffer* buffer,               // 缓冲区
                          Timestamp time)               // 接收到数据的时间信息
{
    string buf = buffer->retrieveAllAsString();
    //数据的反序列化
    json js = json::parse(buf);
    //通过js["msgid"] 获取->业务handler->conn js time
   auto msgHandler= ChatService::instance()->getHandler(js["msgid"].get<int>());
  //回调消息绑定好的事件处理器,来指向相应的业务处理
   msgHandler(conn,js,time);
}