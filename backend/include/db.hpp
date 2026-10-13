#pragma once
#include <mysql.h>
#include <string>
#include <vector>
#include <stdexcept>
#include "json.hpp"
using json = nlohmann::json;
std::string env(const char* name, const std::string& fallback = "");
struct ApiError : std::runtime_error {
    int status;
    ApiError(int status, const std::string& message) : std::runtime_error(message), status(status) {}
};
class Db {
    MYSQL* db_;
public:
    Db();
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    json query(const std::string& sql, const std::vector<std::string>& args = {});
    unsigned long long insert_id() const;
    void raw(const char* sql);
};
// All registration mutations share a database advisory lock, across server processes.
// The close endpoint fails immediately if another mutation is in progress.
class Transaction {
    Db& db_;
    bool committed_ = false;
public:
    explicit Transaction(Db& db, int wait_seconds = 3);
    void commit();
    ~Transaction();
};
std::string random_hex(size_t bytes);
std::string sha256(const std::string& value);
std::string password_hash(const std::string& password, const std::string& salt);
bool constant_equal(const std::string& a, const std::string& b);
