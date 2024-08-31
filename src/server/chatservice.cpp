#include "chatservice.hpp"
#include "public.hpp"
#include <muduo/base/Logging.h> //muduo自带的日志
using namespace muduo;
#include <string>
using namespace std;
#include <vector>
#include <map>
// 获取单例对象的接口函数

ChatService *ChatService::instance()
{
    static ChatService service;
    return &service;
}
// 注册消息以及对应的handle(回调)操作
ChatService::ChatService()
{
    _msgHandlerMap.insert({LOGIN_MSG, std::bind(&ChatService::login, this, _1, _2, _3)});
    _msgHandlerMap.insert({REG_MSG, bind(&ChatService::reg, this, _1, _2, _3)});
    _msgHandlerMap.insert({ONE_CHAT_MSG, bind(&ChatService::oneChat, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_FRIEND_MSG, bind(&ChatService::addFriend, this, _1, _2, _3)});
    _msgHandlerMap.insert({CREATE_GROUP_MSG, bind(&ChatService::createGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({ADD_GROUP_MSG, bind(&ChatService::addGroup, this, _1, _2, _3)});
    _msgHandlerMap.insert({GROUP_CHAT_MSG, bind(&ChatService::groupChat, this, _1, _2, _3)});
_msgHandlerMap.insert({ LOGINOUT_MSG, bind(&ChatService::loginout, this, _1, _2, _3)});
 // 连接redis服务器
    if (_redis.connect())
    {
        // 设置上报消息的回调
        _redis.init_notify_handler(std::bind(&ChatService::handleRedisSubscribeMessage, this, _1, _2));
    }
}
// 服务器异常,业务重置方法
void ChatService::reset()
{
    // 把online状态的用户,设置成offline
    _userModel.resetState();
}


// 获取消息对应的处理器
MsgHandler ChatService::getHandler(int msgid)
{
    // 记录错误日志,msgid没有对应的事件处理回调
    auto it = _msgHandlerMap.find(msgid);
    if (it == _msgHandlerMap.end())
    {

        // 返回一个默认的处理器(空操作)
        return [=](const TcpConnectionPtr &conn, json &js, Timestamp)
        {
            // 用muduo提供的日志记录了一下错误
            LOG_ERROR << "msgid :" << msgid << "can not find handler!";
        };
    }
    else
    {
        return _msgHandlerMap[msgid];
    }
}

// 处理登录业务
void ChatService::login(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int id = js["id"].get<int>();
    string pwd = js["password"];

    User user = _userModel.query(id);
    if (user.getId() == id && user.getPwd() == pwd)
    {
        if (user.getState() == "online")
        {
            // 该用户已经登录,不允许重复登录
            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 2;
            response["errmsg"] = "该用户已经登录,不允许重复登录";
            conn->send(response.dump());
        }
        else
        {

            { // 定义一个lock_guard 对象,这个对象在构造函数的时候传进去
                // 一个互斥锁,并上锁,析构的时候自动解锁
                // 主要是为了保证记录用户连接的时候导致的线程不安全
                // 所有加一个{ }保证 _userConnMap.insert({id,conn});
                // 线程安全就行了
                lock_guard<mutex> lock(_connMutex);
                // 登录成功,记录用户连接信息
                _userConnMap.insert({id, conn});
            }
// id用户登录成功后，向redis订阅channel(id)
            _redis.subscribe(id); 

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");
            // 登录成功  更新用户状态信息 state offli=>online
            user.setState("online");
            _userModel.updateState(user);
           

            // 登录成功，更新用户状态信息 state offline=>online
            user.setState("online");

            json response;
            response["msgid"] = LOGIN_MSG_ACK;
            response["errno"] = 0;
            response["id"] = user.getId();
            response["name"] = user.getName();
            // 查询该用户是否有离线消息
            vector<string> vec = _offlineMsgModel.query(id);
            if (!vec.empty())
            {
                response["offlinemsg"] = vec;
                // 读取该用户的离线消息后,把该用户的所有离线消息删除掉
                _offlineMsgModel.remove(id);
            }
            // 查询该用户的好友信息并返回
            vector<User> userVec = _friendModel.query(id);
            if (!userVec.empty())
            {
                vector<string> vec2;
                for (User &user : userVec)
                {
                    json js;
                    js["id"] = user.getId();
                    js["name"] = user.getName();
                    js["state"] = user.getState();
                    vec2.push_back(js.dump());
                }

                response["friend"] = vec2;
            }
            conn->send(response.dump());
        }
    }
    else
    {
        // 该用户不存在,登录失败
        json response;
        response["msgid"] = LOGIN_MSG_ACK;
        response["errno"] = 1;
        response["errmsg"] = "用户名或密码错误";
        conn->send(response.dump());
    }
}

// 处理注册业务   name password
void ChatService::reg(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    string name = js["name"];
    string pwd = js["password"];

    User user;
    user.setName(name);
    user.setPwd(pwd);
    bool state = _userModel.insert(user);
    if (state)
    {
        // 注册成功
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 0;
        response["id"] = user.getId();
        conn->send(response.dump());
    }
    else
    {
        // 注册失败
        json response;
        response["msgid"] = REG_MSG_ACK;
        response["errno"] = 1;
        conn->send(response.dump());
    }
}
// 处理客户端异常退出
void ChatService::clientCloseException(const TcpConnectionPtr &conn)
{
    User user;
    {
        // 定义一个lock_guard 对象,这个对象在构造函数的时候传进去
        // 一个互斥锁,并上锁,析构的时候自动解锁
        // 这里也要注意线程安全
        lock_guard<mutex> lock(_connMutex);

        for (auto it : _userConnMap)
        {

            if (it.second == conn)
            {
                // 从map表删除用户的连接信息
                user.setId(it.first);
                _userConnMap.erase(it.first);
                break;
            }
        }
    }
    // 确保是一个有效用户
    if (user.getId() != -1)
    {
        // 更新用户的状态信息
        user.setState("offline");

        _userModel.updateState(user);
    }
}
// 一对一聊天业务
void ChatService::oneChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int toid = js["to"].get<int>();

    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toid);
        if (it != _userConnMap.end())
        {
            // toid在线,转发消息
            // 服务器主动推送消息给toid用户
            it->second->send(js.dump());
            return;
        }
    }
    // 查询toid是否在线 
    User user = _userModel.query(toid);
    if (user.getState() == "online")
    {
        _redis.publish(toid, js.dump());
        return;
    }

    // toid不在线，存储离线消息
    _offlineMsgModel.insert(toid, js.dump());
}
// 添加好友业务  msgid id friendid
void ChatService::addFriend(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    int friendid = js["friendid"].get<int>();

    // 存储好友信息
    _friendModel.insert(userid, friendid);
}

// 创建群组
void ChatService:: createGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();
    string name = js["groupname"];
    string desc = js["groupdesc"];

    //存储新创建的群组消息
    Group group (-1,name,desc);
    if(_groupModel.createGroup(group))
    {
        //存储群组创建人信息
        _groupModel.addGroup(userid,group.getId(),"creator");
    }
}
// 加入群组业务
void  ChatService::addGroup(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid =js["id"].get<int>();
    int groupid = js["groupid"].get<int>();
    _groupModel.addGroup(userid,groupid,"normal"); 
}
// 群组聊天业务
void  ChatService::groupChat(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid =js["id"].get<int>();
    int groupid=js["groupid"].get<int>();
    vector<int>useridVec = _groupModel.queryGroupUsers(userid,groupid);
    lock_guard<mutex> lock(_connMutex);
    for(int id :useridVec)
    {
       
        auto it = _userConnMap.find(id);
        if(it !=  _userConnMap.end())
        {
            //转发消息
             it->second->send(js.dump());
        }else
        {
            //存储离线消息
            _offlineMsgModel.insert(id,js.dump());
        }


    }
}

// 处理注销业务
void ChatService::loginout(const TcpConnectionPtr &conn, json &js, Timestamp time)
{
    int userid = js["id"].get<int>();

    {  
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(userid);
        if (it != _userConnMap.end())
        {
            _userConnMap.erase(it);
            LOG_INFO <<"USER:    "<<userid<<"  offline!";
        }
    }
 // 用户注销，相当于就是下线，在redis中取消订阅通道
    _redis.unsubscribe(userid); 

    // 更新用户的状态信息
    User user(userid, "", "", "offline");
    _userModel.updateState(user);
    
}
// 从redis消息队列中获取订阅的消息
void ChatService::handleRedisSubscribeMessage(int userid, string msg)
{
    lock_guard<mutex> lock(_connMutex);
    auto it = _userConnMap.find(userid);
    if (it != _userConnMap.end())
    {
        it->second->send(msg);
        return;
    }

    // 存储该用户的离线消息
    _offlineMsgModel.insert(userid, msg);
}