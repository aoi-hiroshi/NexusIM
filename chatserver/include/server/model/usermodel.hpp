#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

// User表的数据操作类
class UserModel {
public:
    // User表的增加方法
    bool insert(User &user);

    // 根据用户号码查询用户信息
    User query(int id);

    // Distinguishes a missing user from a database/query failure.
    bool exists(int id, bool &querySucceeded);

    // 更新用户的状态信息
    bool updateState(User user);

    // Replaces a legacy/plain value with a versioned password hash.
    bool updatePassword(int id, const std::string &encodedPassword);

    // 重置用户的状态信息
    void resetState();
};

#endif
