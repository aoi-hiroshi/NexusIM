#include "db.h"
#include <muduo/base/Logging.h>
#include <cstdlib>

namespace
{
string requiredEnvironment(const char *name)
{
    const char *value = std::getenv(name);
    return value == nullptr ? string() : string(value);
}

string optionalEnvironment(const char *name, const char *fallback)
{
    const string value = requiredEnvironment(name);
    return value.empty() ? string(fallback) : value;
}
} // namespace

// 初始化数据库连接
MySQL::MySQL()
{
    _conn = mysql_init(nullptr);
}

// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
        mysql_close(_conn);
}

// 连接数据库
bool MySQL::connect()
{
    const string server = optionalEnvironment("CHAT_DB_HOST", "127.0.0.1");
    const string user = optionalEnvironment("CHAT_DB_USER", "chatapp");
    const string password = requiredEnvironment("CHAT_DB_PASSWORD");
    const string dbname = optionalEnvironment("CHAT_DB_NAME", "chat");
    const string portText = optionalEnvironment("CHAT_DB_PORT", "3306");
    if (password.empty())
    {
        LOG_ERROR << "CHAT_DB_PASSWORD is required; refusing an insecure default";
        return false;
    }

    unsigned int timeoutSeconds = 3;
    mysql_options(_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeoutSeconds);
    mysql_options(_conn, MYSQL_OPT_READ_TIMEOUT, &timeoutSeconds);
    mysql_options(_conn, MYSQL_OPT_WRITE_TIMEOUT, &timeoutSeconds);
    unsigned int port = 3306;
    try
    {
        port = static_cast<unsigned int>(std::stoul(portText));
    }
    catch (...)
    {
        LOG_ERROR << "invalid CHAT_DB_PORT";
        return false;
    }
    MYSQL *p = mysql_real_connect(_conn, server.c_str(), user.c_str(),
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)
    {
        mysql_set_character_set(_conn, "utf8mb4");
        LOG_INFO << "connect mysql success!";
    }
    else
    {
        LOG_INFO << "connect mysql fail!";
    }

    return p;
}

// 更新操作
bool MySQL::update(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << "mysql update failed: " << mysql_error(_conn);
        return false;
    }

    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_ERROR << "mysql query failed: " << mysql_error(_conn);
        return nullptr;
    }
    
    return mysql_use_result(_conn);
}

string MySQL::escape(const string &value)
{
    string escaped(value.size() * 2 + 1, '\0');
    const unsigned long length = mysql_real_escape_string(
        _conn, &escaped[0], value.data(), static_cast<unsigned long>(value.size()));
    escaped.resize(length);
    return escaped;
}

// 获取连接
MYSQL* MySQL::getConnection()
{
    return _conn;
}
