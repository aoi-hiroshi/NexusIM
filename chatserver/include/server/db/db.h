#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
using namespace std;

struct MySQLParameter
{
    enum class Kind { Integer, Text };

    static MySQLParameter integer(std::int64_t value);
    static MySQLParameter text(std::string value);

    Kind kind{Kind::Integer};
    std::int64_t integerValue{0};
    std::string textValue;
};

struct MySQLExecuteResult
{
    bool success{false};
    std::uint64_t affectedRows{0};
    std::uint64_t insertId{0};
};

struct MySQLQueryResult
{
    using Cell = std::optional<std::string>;
    using Row = std::vector<Cell>;
    std::vector<Row> rows;
};

// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    MySQL(const MySQL &) = delete;
    MySQL &operator=(const MySQL &) = delete;
    // 连接数据库
    bool connect();
    // 更新操作
    bool update(string sql);
    // 查询操作
    MYSQL_RES *query(string sql);
    // Parameterized operations used by all externally reachable business models.
    MySQLExecuteResult executePrepared(
        const std::string &sql, const std::vector<MySQLParameter> &parameters = {});
    std::unique_ptr<MySQLQueryResult> queryPrepared(
        const std::string &sql, const std::vector<MySQLParameter> &parameters = {});
    bool beginTransaction();
    bool commit();
    void rollback() noexcept;
    // Escape untrusted text using this connection's character set.
    string escape(const string &value);
    // 获取连接
    MYSQL* getConnection();
private:
    MYSQL *_conn;
};

#endif
