#include "groupmodel.hpp"
#include "db.h"

#include <cstdlib>
#include <utility>

namespace
{
bool exists(MySQL &mysql, const char *sql, int id, bool &querySucceeded)
{
    querySucceeded = false;
    MYSQL_RES *result = mysql.queryPrepared(sql, {MySQLParameter::integer(id)});
    if (result == nullptr) return false;
    const bool found = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    querySucceeded = true;
    return found;
}

bool membership(MySQL &mysql, int userId, int groupId, bool &querySucceeded,
                std::string *role = nullptr)
{
    querySucceeded = false;
    MYSQL_RES *result = mysql.queryPrepared(
        "select `grouprole` from `GroupUser` where `user_id`=? and `group_id`=? limit 1",
        {MySQLParameter::integer(userId), MySQLParameter::integer(groupId)});
    if (result == nullptr) return false;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row != nullptr && role != nullptr) *role = row[0] == nullptr ? "" : row[0];
    const bool found = row != nullptr;
    mysql_free_result(result);
    querySucceeded = true;
    return found;
}
} // namespace

CreateGroupStatus GroupModel::createGroup(Group &group, int ownerId)
{
    if (ownerId <= 0 || group.getName().empty()) return CreateGroupStatus::InvalidUser;
    MySQL mysql;
    if (!mysql.connect() || !mysql.beginTransaction()) return CreateGroupStatus::Error;
    bool lookupSucceeded = false;
    const bool ownerExists = exists(
        mysql, "select `id` from `User` where `id`=? limit 1", ownerId,
        lookupSucceeded);
    if (!lookupSucceeded)
    {
        mysql.rollback();
        return CreateGroupStatus::Error;
    }
    if (!ownerExists)
    {
        mysql.rollback();
        return CreateGroupStatus::InvalidUser;
    }
    const auto created = mysql.executePrepared(
        "insert into `AllGroup`(`groupname`,`groupdesc`) values(?,?)",
        {MySQLParameter::text(group.getName()), MySQLParameter::text(group.getDesc())});
    if (!created.success || created.insertId == 0)
    {
        mysql.rollback();
        return CreateGroupStatus::Error;
    }
    group.setId(static_cast<int>(created.insertId));
    const auto member = mysql.executePrepared(
        "insert into `GroupUser`(`group_id`,`user_id`,`grouprole`) values(?,?,?)",
        {MySQLParameter::integer(group.getId()), MySQLParameter::integer(ownerId),
         MySQLParameter::text("creator")});
    if (!member.success || member.affectedRows != 1 || !mysql.commit())
    {
        mysql.rollback();
        group.setId(-1);
        return CreateGroupStatus::Error;
    }
    group.setRole("creator");
    return CreateGroupStatus::Created;
}

JoinGroupStatus GroupModel::joinGroup(int userid, int groupid)
{
    if (userid <= 0 || groupid <= 0) return JoinGroupStatus::GroupNotFound;
    MySQL mysql;
    if (!mysql.connect()) return JoinGroupStatus::Error;
    bool lookupSucceeded = false;
    const bool groupExists = exists(
        mysql, "select `id` from `AllGroup` where `id`=? limit 1", groupid,
        lookupSucceeded);
    if (!lookupSucceeded) return JoinGroupStatus::Error;
    if (!groupExists)
        return JoinGroupStatus::GroupNotFound;
    bool membershipQuerySucceeded = false;
    const bool alreadyMember = membership(
        mysql, userid, groupid, membershipQuerySucceeded);
    if (!membershipQuerySucceeded) return JoinGroupStatus::Error;
    if (alreadyMember) return JoinGroupStatus::AlreadyMember;
    const auto result = mysql.executePrepared(
        "insert ignore into `GroupUser`(`group_id`,`user_id`,`grouprole`) values(?,?,?)",
        {MySQLParameter::integer(groupid), MySQLParameter::integer(userid),
         MySQLParameter::text("normal")});
    if (!result.success) return JoinGroupStatus::Error;
    return result.affectedRows == 0 ? JoinGroupStatus::AlreadyMember
                                    : JoinGroupStatus::Joined;
}

LeaveGroupStatus GroupModel::leaveGroup(int userid, int groupid)
{
    if (userid <= 0 || groupid <= 0) return LeaveGroupStatus::GroupNotFound;
    MySQL mysql;
    if (!mysql.connect()) return LeaveGroupStatus::Error;
    bool lookupSucceeded = false;
    const bool groupExists = exists(
        mysql, "select `id` from `AllGroup` where `id`=? limit 1", groupid,
        lookupSucceeded);
    if (!lookupSucceeded) return LeaveGroupStatus::Error;
    if (!groupExists)
        return LeaveGroupStatus::GroupNotFound;
    std::string role;
    bool membershipQuerySucceeded = false;
    const bool member = membership(
        mysql, userid, groupid, membershipQuerySucceeded, &role);
    if (!membershipQuerySucceeded) return LeaveGroupStatus::Error;
    if (!member) return LeaveGroupStatus::NotMember;
    if (role == "creator") return LeaveGroupStatus::OwnerCannotLeave;
    const auto result = mysql.executePrepared(
        "delete from `GroupUser` where `user_id`=? and `group_id`=?",
        {MySQLParameter::integer(userid), MySQLParameter::integer(groupid)});
    return result.success && result.affectedRows == 1 ? LeaveGroupStatus::Left
                                                      : LeaveGroupStatus::Error;
}

bool GroupModel::isMember(int userid, int groupid, bool &querySucceeded)
{
    querySucceeded = false;
    if (userid <= 0 || groupid <= 0) return false;
    MySQL mysql;
    if (!mysql.connect()) return false;
    MYSQL_RES *result = mysql.queryPrepared(
        "select 1 from `GroupUser` where `user_id`=? and `group_id`=? limit 1",
        {MySQLParameter::integer(userid), MySQLParameter::integer(groupid)});
    if (result == nullptr) return false;
    const bool found = mysql_fetch_row(result) != nullptr;
    mysql_free_result(result);
    querySucceeded = true;
    return found;
}

vector<Group> GroupModel::queryGroups(int userid, bool *querySucceeded)
{
    vector<Group> groups;
    if (querySucceeded) *querySucceeded = false;
    MySQL mysql;
    if (!mysql.connect()) return groups;
    MYSQL_RES *result = mysql.queryPrepared(
        "select a.`id`,a.`groupname`,a.`groupdesc`,b.`grouprole` "
        "from `AllGroup` a inner join `GroupUser` b on a.`id`=b.`group_id` "
        "where b.`user_id`=? order by a.`id`",
        {MySQLParameter::integer(userid)});
    if (result == nullptr) return groups;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        Group group;
        group.setId(std::atoi(row[0]));
        group.setName(row[1] == nullptr ? "" : row[1]);
        group.setDesc(row[2] == nullptr ? "" : row[2]);
        group.setRole(row[3] == nullptr ? "" : row[3]);
        groups.push_back(std::move(group));
    }
    mysql_free_result(result);
    if (querySucceeded) *querySucceeded = true;
    return groups;
}

vector<GroupUser> GroupModel::queryMembers(int userid, int groupid,
                                            bool &authorized, bool &querySucceeded)
{
    vector<GroupUser> users;
    authorized = false;
    querySucceeded = false;
    MySQL mysql;
    if (!mysql.connect()) return users;
    MYSQL_RES *membershipResult = mysql.queryPrepared(
        "select 1 from `GroupUser` where `user_id`=? and `group_id`=? limit 1",
        {MySQLParameter::integer(userid), MySQLParameter::integer(groupid)});
    if (membershipResult == nullptr) return users;
    authorized = mysql_fetch_row(membershipResult) != nullptr;
    mysql_free_result(membershipResult);
    querySucceeded = true;
    if (!authorized) return users;

    MYSQL_RES *result = mysql.queryPrepared(
        "select u.`id`,u.`name`,u.`state`,gu.`grouprole` from `User` u "
        "inner join `GroupUser` gu on gu.`user_id`=u.`id` "
        "where gu.`group_id`=? order by u.`id`",
        {MySQLParameter::integer(groupid)});
    if (result == nullptr)
    {
        querySucceeded = false;
        return users;
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
    {
        GroupUser user;
        user.setId(std::atoi(row[0]));
        user.setName(row[1] == nullptr ? "" : row[1]);
        user.setState(row[2] == nullptr ? "offline" : row[2]);
        user.setRole(row[3] == nullptr ? "" : row[3]);
        users.push_back(std::move(user));
    }
    mysql_free_result(result);
    return users;
}

vector<int> GroupModel::queryGroupUsers(int userid, int groupid,
                                        bool &senderIsMember, bool &querySucceeded)
{
    vector<int> ids;
    senderIsMember = false;
    querySucceeded = false;
    MySQL mysql;
    if (!mysql.connect()) return ids;
    MYSQL_RES *membershipResult = mysql.queryPrepared(
        "select 1 from `GroupUser` where `user_id`=? and `group_id`=? limit 1",
        {MySQLParameter::integer(userid), MySQLParameter::integer(groupid)});
    if (membershipResult == nullptr) return ids;
    senderIsMember = mysql_fetch_row(membershipResult) != nullptr;
    mysql_free_result(membershipResult);
    querySucceeded = true;
    if (!senderIsMember) return ids;

    MYSQL_RES *result = mysql.queryPrepared(
        "select distinct `user_id` from `GroupUser` where `group_id`=? and `user_id`<>? "
        "order by `user_id`",
        {MySQLParameter::integer(groupid), MySQLParameter::integer(userid)});
    if (result == nullptr)
    {
        querySucceeded = false;
        return ids;
    }
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result)) != nullptr)
        ids.push_back(std::atoi(row[0]));
    mysql_free_result(result);
    return ids;
}
