#include "db.h"
#include <muduo/base/Logging.h>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

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

MySQLParameter MySQLParameter::integer(std::int64_t value)
{
    MySQLParameter parameter;
    parameter.kind = Kind::Integer;
    parameter.integerValue = value;
    return parameter;
}

MySQLParameter MySQLParameter::text(std::string value)
{
    MySQLParameter parameter;
    parameter.kind = Kind::Text;
    parameter.textValue = std::move(value);
    return parameter;
}

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
    const string user = requiredEnvironment("CHAT_DB_USER");
    const string password = requiredEnvironment("CHAT_DB_PASSWORD");
    const string dbname = requiredEnvironment("CHAT_DB_NAME");
    const string portText = optionalEnvironment("CHAT_DB_PORT", "3306");
    if (user.empty() || password.empty() || dbname.empty())
    {
        LOG_ERROR << "CHAT_DB_NAME, CHAT_DB_USER and CHAT_DB_PASSWORD are required";
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
        LOG_ERROR << "connect mysql failed: " << mysql_error(_conn);
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

namespace
{
bool bindParameters(MYSQL_STMT *statement,
                    const std::vector<MySQLParameter> &parameters,
                    std::vector<MYSQL_BIND> &bindings,
                    std::vector<unsigned long> &lengths)
{
    if (mysql_stmt_param_count(statement) != parameters.size()) return false;
    if (parameters.empty()) return true;

    bindings.assign(parameters.size(), MYSQL_BIND{});
    lengths.assign(parameters.size(), 0);
    for (std::size_t index = 0; index < parameters.size(); ++index)
    {
        MYSQL_BIND &binding = bindings[index];
        const MySQLParameter &parameter = parameters[index];
        if (parameter.kind == MySQLParameter::Kind::Integer)
        {
            binding.buffer_type = MYSQL_TYPE_LONGLONG;
            binding.buffer = const_cast<std::int64_t *>(&parameter.integerValue);
            binding.is_unsigned = 0;
        }
        else
        {
            lengths[index] = static_cast<unsigned long>(parameter.textValue.size());
            binding.buffer_type = MYSQL_TYPE_STRING;
            binding.buffer = const_cast<char *>(parameter.textValue.data());
            binding.buffer_length = lengths[index];
            binding.length = &lengths[index];
        }
    }
    return mysql_stmt_bind_param(statement, bindings.data()) == 0;
}
} // namespace

MySQLExecuteResult MySQL::executePrepared(
    const std::string &sql, const std::vector<MySQLParameter> &parameters)
{
    MySQLExecuteResult result;
    MYSQL_STMT *statement = mysql_stmt_init(_conn);
    if (statement == nullptr) return result;
    if (mysql_stmt_prepare(statement, sql.data(),
                           static_cast<unsigned long>(sql.size())) != 0)
    {
        LOG_ERROR << "mysql prepare failed: " << mysql_stmt_error(statement);
        mysql_stmt_close(statement);
        return result;
    }
    std::vector<MYSQL_BIND> bindings;
    std::vector<unsigned long> lengths;
    if (!bindParameters(statement, parameters, bindings, lengths) ||
        mysql_stmt_execute(statement) != 0)
    {
        LOG_ERROR << "mysql prepared update failed: " << mysql_stmt_error(statement);
        mysql_stmt_close(statement);
        return result;
    }
    result.success = true;
    result.affectedRows = static_cast<std::uint64_t>(mysql_stmt_affected_rows(statement));
    result.insertId = static_cast<std::uint64_t>(mysql_stmt_insert_id(statement));
    mysql_stmt_close(statement);
    return result;
}

std::unique_ptr<MySQLQueryResult> MySQL::queryPrepared(
    const std::string &sql, const std::vector<MySQLParameter> &parameters)
{
    struct StatementCloser
    {
        void operator()(MYSQL_STMT *statement) const
        {
            if (statement != nullptr) mysql_stmt_close(statement);
        }
    };
    std::unique_ptr<MYSQL_STMT, StatementCloser> statement(mysql_stmt_init(_conn));
    if (statement == nullptr) return nullptr;
    if (mysql_stmt_prepare(statement.get(), sql.data(),
                           static_cast<unsigned long>(sql.size())) != 0)
    {
        LOG_ERROR << "mysql prepare failed: " << mysql_stmt_error(statement.get());
        return nullptr;
    }
    std::vector<MYSQL_BIND> bindings;
    std::vector<unsigned long> lengths;
    if (!bindParameters(statement.get(), parameters, bindings, lengths) ||
        mysql_stmt_execute(statement.get()) != 0)
    {
        LOG_ERROR << "mysql prepared query failed: " << mysql_stmt_error(statement.get());
        return nullptr;
    }
    std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> metadata(
        mysql_stmt_result_metadata(statement.get()), &mysql_free_result);
    if (metadata == nullptr)
    {
        LOG_ERROR << "mysql prepared query has no result metadata: "
                  << mysql_stmt_error(statement.get());
        return nullptr;
    }
    if (mysql_stmt_store_result(statement.get()) != 0)
    {
        LOG_ERROR << "mysql store prepared result failed: "
                  << mysql_stmt_error(statement.get());
        return nullptr;
    }

    const unsigned int columnCount = mysql_num_fields(metadata.get());
    std::vector<MYSQL_BIND> resultBindings(columnCount);
    std::vector<unsigned long> resultLengths(columnCount, 0);
    std::vector<char> smallBuffers(columnCount, '\0');
    std::unique_ptr<bool[]> nullFlags(new bool[columnCount]{});
    for (unsigned int column = 0; column < columnCount; ++column)
    {
        MYSQL_BIND &binding = resultBindings[column];
        binding.buffer_type = MYSQL_TYPE_STRING;
        binding.buffer = &smallBuffers[column];
        binding.buffer_length = 1;
        binding.length = &resultLengths[column];
        binding.is_null = &nullFlags[column];
    }
    if (mysql_stmt_bind_result(statement.get(), resultBindings.data()) != 0)
    {
        LOG_ERROR << "mysql bind prepared result failed: "
                  << mysql_stmt_error(statement.get());
        return nullptr;
    }

    auto result = std::make_unique<MySQLQueryResult>();
    constexpr unsigned long kMaxColumnBytes = 4UL * 1024UL * 1024UL;
    while (true)
    {
        const int fetchStatus = mysql_stmt_fetch(statement.get());
        if (fetchStatus == MYSQL_NO_DATA) break;
        if (fetchStatus != 0 && fetchStatus != MYSQL_DATA_TRUNCATED)
        {
            LOG_ERROR << "mysql fetch prepared result failed: "
                      << mysql_stmt_error(statement.get());
            return nullptr;
        }
        MySQLQueryResult::Row row;
        row.reserve(columnCount);
        for (unsigned int column = 0; column < columnCount; ++column)
        {
            if (nullFlags[column])
            {
                row.emplace_back(std::nullopt);
                continue;
            }
            if (resultLengths[column] > kMaxColumnBytes)
            {
                LOG_ERROR << "mysql prepared result column exceeds size limit";
                return nullptr;
            }
            std::string value(resultLengths[column] + 1, '\0');
            MYSQL_BIND valueBinding{};
            valueBinding.buffer_type = MYSQL_TYPE_STRING;
            valueBinding.buffer = value.data();
            valueBinding.buffer_length = static_cast<unsigned long>(value.size());
            if (mysql_stmt_fetch_column(statement.get(), &valueBinding,
                                        column, 0) != 0)
            {
                LOG_ERROR << "mysql fetch prepared column failed: "
                          << mysql_stmt_error(statement.get());
                return nullptr;
            }
            value.resize(resultLengths[column]);
            row.emplace_back(std::move(value));
        }
        result->rows.push_back(std::move(row));
    }
    return result;
}

bool MySQL::beginTransaction()
{
    return mysql_autocommit(_conn, 0) == 0;
}

bool MySQL::commit()
{
    const bool committed = mysql_commit(_conn) == 0;
    mysql_autocommit(_conn, 1);
    return committed;
}

void MySQL::rollback() noexcept
{
    mysql_rollback(_conn);
    mysql_autocommit(_conn, 1);
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
