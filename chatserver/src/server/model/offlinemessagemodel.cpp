#include "offlinemessagemodel.hpp"
#include "db.h"

#include <utility>

// 存储用户的离线消息
bool OfflineMsgModel::insert(int userid, string msg)
{
    MySQL mysql;
    if (!mysql.connect()) return false;
    return mysql.executePrepared(
        "insert into `OfflineMessage`(`user_id`,`message`) values(?,?)",
        {MySQLParameter::integer(userid), MySQLParameter::text(std::move(msg))}).success;
}

bool OfflineMsgModel::insertBatch(const vector<pair<int, string>> &messages)
{
    if (messages.empty()) return true;
    MySQL mysql;
    if (!mysql.connect() || !mysql.beginTransaction()) return false;
    for (const auto &message : messages)
    {
        const auto inserted = mysql.executePrepared(
            "insert into `OfflineMessage`(`user_id`,`message`) values(?,?)",
            {MySQLParameter::integer(message.first),
             MySQLParameter::text(message.second)});
        if (!inserted.success || inserted.affectedRows != 1)
        {
            mysql.rollback();
            return false;
        }
    }
    if (!mysql.commit())
    {
        mysql.rollback();
        return false;
    }
    return true;
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    // 1.组装sql语句
    MySQL mysql;
    if (mysql.connect())
    {
        mysql.executePrepared(
            "delete from `OfflineMessage` where `user_id`=?",
            {MySQLParameter::integer(userid)});
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    vector<string> vec;
    MySQL mysql;
    if (mysql.connect())
    {
        auto res = mysql.queryPrepared(
            "select `message` from `OfflineMessage` where `user_id`=? order by `id`",
            {MySQLParameter::integer(userid)});
        if (res != nullptr)
        {
            // 把userid用户的所有离线消息放入vec中返回
            for (const auto &row : res->rows)
            {
                vec.push_back(row[0].value_or(""));
            }
            return vec;
        }
    }
    return vec;
}
