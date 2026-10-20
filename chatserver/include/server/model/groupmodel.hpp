#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include <string>
#include <vector>
using namespace std;

enum class CreateGroupStatus { Created, InvalidUser, Error };
enum class JoinGroupStatus { Joined, AlreadyMember, GroupNotFound, Error };
enum class LeaveGroupStatus { Left, NotMember, OwnerCannotLeave, GroupNotFound, Error };

// 维护群组信息的操作接口方法
class GroupModel
{
public:
    // 创建群组
    CreateGroupStatus createGroup(Group &group, int ownerId);
    // 加入群组
    JoinGroupStatus joinGroup(int userid, int groupid);
    LeaveGroupStatus leaveGroup(int userid, int groupid);
    bool isMember(int userid, int groupid, bool &querySucceeded);
    // 查询用户所在群组信息
    vector<Group> queryGroups(int userid, bool *querySucceeded = nullptr);
    vector<GroupUser> queryMembers(int userid, int groupid,
                                   bool &authorized, bool &querySucceeded);
    // 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
    vector<int> queryGroupUsers(int userid, int groupid,
                                bool &senderIsMember, bool &querySucceeded);
};

#endif
