#include "friendmodel.hpp"
#include "db.h"

namespace
{
enum class Lookup { Found, Missing, Error };

Lookup userLookup(MySQL &mysql, int userId)
{
    MYSQL_RES *result = mysql.queryPrepared(
        "select `id` from `User` where `id`=? limit 1",
        {MySQLParameter::integer(userId)});
    if (result == nullptr) return Lookup::Error;
    const bool exists = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    return exists ? Lookup::Found : Lookup::Missing;
}

Lookup relationshipLookup(MySQL &mysql, int userId, int friendId)
{
    MYSQL_RES *result = mysql.queryPrepared(
        "select 1 from `Friend` where `user_id`=? and `friend_id`=? limit 1",
        {MySQLParameter::integer(userId), MySQLParameter::integer(friendId)});
    if (result == nullptr) return Lookup::Error;
    const bool exists = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    return exists ? Lookup::Found : Lookup::Missing;
}
} // namespace

AddFriendStatus FriendModel::addBidirectional(int userid, int friendid)
{
    if (userid <= 0 || friendid <= 0 || userid == friendid)
        return AddFriendStatus::UserNotFound;
    MySQL mysql;
    if (!mysql.connect() || !mysql.beginTransaction())
        return AddFriendStatus::Error;

    const Lookup sourceUser = userLookup(mysql, userid);
    const Lookup targetUser = userLookup(mysql, friendid);
    if (sourceUser == Lookup::Error || targetUser == Lookup::Error)
    {
        mysql.rollback();
        return AddFriendStatus::Error;
    }
    if (sourceUser == Lookup::Missing || targetUser == Lookup::Missing)
    {
        mysql.rollback();
        return AddFriendStatus::UserNotFound;
    }
    const Lookup forwardLookup = relationshipLookup(mysql, userid, friendid);
    const Lookup reverseLookup = relationshipLookup(mysql, friendid, userid);
    if (forwardLookup == Lookup::Error || reverseLookup == Lookup::Error)
    {
        mysql.rollback();
        return AddFriendStatus::Error;
    }
    const bool forward = forwardLookup == Lookup::Found;
    const bool reverse = reverseLookup == Lookup::Found;
    if (!forward)
    {
        const auto inserted = mysql.executePrepared(
            "insert ignore into `Friend`(`user_id`,`friend_id`) values(?,?)",
            {MySQLParameter::integer(userid), MySQLParameter::integer(friendid)});
        if (!inserted.success)
        {
            mysql.rollback();
            return AddFriendStatus::Error;
        }
    }
    if (!reverse)
    {
        const auto inserted = mysql.executePrepared(
            "insert ignore into `Friend`(`user_id`,`friend_id`) values(?,?)",
            {MySQLParameter::integer(friendid), MySQLParameter::integer(userid)});
        if (!inserted.success)
        {
            mysql.rollback();
            return AddFriendStatus::Error;
        }
    }
    if (!mysql.commit())
    {
        mysql.rollback();
        return AddFriendStatus::Error;
    }
    return forward && reverse ? AddFriendStatus::AlreadyFriends
                              : AddFriendStatus::Added;
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid, bool *querySucceeded)
{
    vector<User> vec;
    if (querySucceeded) *querySucceeded = false;
    if (userid <= 0) return vec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.queryPrepared(
            "select distinct a.`id`,a.`name`,a.`state` from `User` a "
            "inner join `Friend` b on b.`friend_id`=a.`id` "
            "where b.`user_id`=? order by a.`id`",
            {MySQLParameter::integer(userid)});
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while((row = mysql_fetch_row(res)) != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
            if (querySucceeded) *querySucceeded = true;
            return vec;
        }
    }
    return vec;
}
