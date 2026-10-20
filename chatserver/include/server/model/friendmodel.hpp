#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;

enum class AddFriendStatus
{
    Added,
    AlreadyFriends,
    UserNotFound,
    Error
};

// 维护好友信息的操作接口方法
class FriendModel
{
public:
    // V1 friendships are symmetric. Both directed rows are committed together.
    AddFriendStatus addBidirectional(int userid, int friendid);

    // 返回用户好友列表
    vector<User> query(int userid, bool *querySucceeded = nullptr);
};

#endif
