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
        const string sql = "insert into `User`(name, password, state) values('" +
                           mysql.escape(user.getName()) + "', '" +
                           mysql.escape(user.getPwd()) + "', 'offline')";
        if (mysql.update(sql))
        {
            // 获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(mysql.getConnection()));
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
    const string sql = "select id, name, password, state from `User` where id = " +
                       std::to_string(id) + " limit 1";

    MySQL mysql;
    if (!mysql.connect()) return UserQueryStatus::Error;
    MYSQL_RES *res = mysql.query(sql);
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
    char sql[1024] = {0};
    sprintf(sql, "update `User` set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
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
    const string sql = "update `User` set password = '" +
                       mysql.escape(encodedPassword) + "' where id = " +
                       std::to_string(id);
    return mysql.update(sql);
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
