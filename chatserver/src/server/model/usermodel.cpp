#include "usermodel.hpp"
#include "db.h"
#include <iostream>
using namespace std;

// User表的增加方法
bool UserModel::insert(User &user)
{
    MySQL mysql;
    if (mysql.connect())
    {
        const auto inserted = mysql.executePrepared(
            "insert into `User`(`name`,`password`,`state`) values(?,?,'offline')",
            {MySQLParameter::text(user.getName()),
             MySQLParameter::text(user.getPwd())});
        if (inserted.success)
        {
            user.setId(static_cast<int>(inserted.insertId));
            return true;
        }
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    User user;
    queryById(id, user);
    return user;
}

UserQueryStatus UserModel::queryById(int id, User &user)
{
    user = User();
    if (id <= 0) return UserQueryStatus::NotFound;
    MySQL mysql;
    if (!mysql.connect()) return UserQueryStatus::Error;
    MYSQL_RES *res = mysql.queryPrepared(
        "select `id`,`name`,`password`,`state` from `User` where `id`=? limit 1",
        {MySQLParameter::integer(id)});
    if (res == nullptr) return UserQueryStatus::Error;
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == nullptr)
    {
        mysql_free_result(res);
        return UserQueryStatus::NotFound;
    }
    user.setId(atoi(row[0]));
    user.setName(row[1] == nullptr ? "" : row[1]);
    user.setPwd(row[2] == nullptr ? "" : row[2]);
    user.setState(row[3] == nullptr ? "offline" : row[3]);
    mysql_free_result(res);
    return UserQueryStatus::Found;
}

bool UserModel::exists(int id, bool &querySucceeded)
{
    User user;
    const auto status = queryById(id, user);
    querySucceeded = status != UserQueryStatus::Error;
    return status == UserQueryStatus::Found;
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1.组装sql语句
    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.executePrepared(
                "update `User` set `state`=? where `id`=?",
                {MySQLParameter::text(user.getState()),
                 MySQLParameter::integer(user.getId())}).success)
        {
            return true;
        }
    }
    return false;
}

bool UserModel::updatePassword(int id, const std::string &encodedPassword)
{
    MySQL mysql;
    if (!mysql.connect())
    {
        return false;
    }
    return mysql.executePrepared(
        "update `User` set `password`=? where `id`=?",
        {MySQLParameter::text(encodedPassword), MySQLParameter::integer(id)}).success;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1.组装sql语句
    char sql[1024] = "update `User` set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
    }
}
