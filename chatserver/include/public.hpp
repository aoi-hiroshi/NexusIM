#ifndef PUBLIC_H
#define PUBLIC_H

/*
server和client的公共文件
*/
enum EnMsgType
{
    LOGIN_MSG = 1, // 登录消息
    LOGIN_MSG_ACK, // 登录响应消息
    LOGINOUT_MSG, // 注销消息
    REG_MSG, // 注册消息
    REG_MSG_ACK, // 注册响应消息
    ONE_CHAT_MSG, // 聊天消息
    ADD_FRIEND_MSG, // 添加好友消息

    CREATE_GROUP_MSG, // 创建群组
    ADD_GROUP_MSG, // 加入群组
    GROUP_CHAT_MSG, // 群聊天
    HEARTBEAT_MSG, // 独立心跳/Presence续租
    HEARTBEAT_MSG_ACK,
    ERROR_MSG,
    AI_CHAT_MSG,
    AI_CHAT_MSG_ACK,
    ADD_FRIEND_MSG_ACK,
    QUERY_FRIENDS_MSG,
    QUERY_FRIENDS_MSG_ACK,
    CREATE_GROUP_MSG_ACK,
    ADD_GROUP_MSG_ACK,
    QUERY_GROUPS_MSG,
    QUERY_GROUPS_MSG_ACK,
    QUERY_GROUP_MEMBERS_MSG,
    QUERY_GROUP_MEMBERS_MSG_ACK,
    LEAVE_GROUP_MSG,
    LEAVE_GROUP_MSG_ACK,
    GROUP_CHAT_MSG_ACK,
};

#endif
