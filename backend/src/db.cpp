#include "db.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstdlib>
#include <memory>
#include <type_traits>
std::string env(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name); return value ? value : fallback;
}
Db::Db() : db_(mysql_init(nullptr)) {
    if (!db_) throw std::runtime_error("mysql_init failed");
    unsigned int timeout = 5;
    mysql_options(db_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(db_, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(db_, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    const char* charset = "utf8mb4";
    mysql_options(db_, MYSQL_SET_CHARSET_NAME, charset);
    if (!mysql_real_connect(db_, env("DB_HOST", "127.0.0.1").c_str(),
        env("DB_USER", "registration").c_str(), env("DB_PASSWORD").c_str(),
        env("DB_NAME", "registration").c_str(), std::stoul(env("DB_PORT", "3306")), nullptr, 0)) {
        std::string error = mysql_error(db_); mysql_close(db_); db_ = nullptr;
        throw std::runtime_error("database connection: " + error);
    }
}
Db::~Db() { if (db_) mysql_close(db_); }
void Db::raw(const char* sql) {
    if (mysql_query(db_, sql)) throw std::runtime_error(mysql_error(db_));
}
unsigned long long Db::insert_id() const { return mysql_insert_id(db_); }
json Db::query(const std::string& sql, const std::vector<std::string>& args) {
    using Stmt = std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;
    Stmt stmt(mysql_stmt_init(db_), mysql_stmt_close);
    if (!stmt || mysql_stmt_prepare(stmt.get(), sql.c_str(), sql.size()))
        throw std::runtime_error(stmt ? "prepare " + sql + ": " + mysql_stmt_error(stmt.get()) : "statement init failed");
    if (mysql_stmt_param_count(stmt.get()) != args.size()) throw std::runtime_error("parameter count mismatch");
    std::vector<MYSQL_BIND> binds(args.size());
    std::vector<unsigned long> lengths(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
        lengths[i] = args[i].size(); binds[i] = {};
        binds[i].buffer_type = MYSQL_TYPE_STRING;
        binds[i].buffer = const_cast<char*>(args[i].data());
        binds[i].buffer_length = lengths[i]; binds[i].length = &lengths[i];
    }
    if ((!args.empty() && mysql_stmt_bind_param(stmt.get(), binds.data())) || mysql_stmt_execute(stmt.get()))
        throw std::runtime_error(mysql_stmt_error(stmt.get()));
    using Result = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
    Result meta(mysql_stmt_result_metadata(stmt.get()), mysql_free_result);
    json rows = json::array();
    if (!meta) return rows;
    if (mysql_stmt_store_result(stmt.get())) throw std::runtime_error(mysql_stmt_error(stmt.get()));
    size_t n = mysql_num_fields(meta.get());
    MYSQL_FIELD* fields = mysql_fetch_fields(meta.get());
    using NullFlag = std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;
    auto nulls = std::make_unique<NullFlag[]>(n);
    std::vector<MYSQL_BIND> outputs(n);
    std::vector<unsigned long> sizes(n);
    std::vector<std::vector<char>> buffers(n, std::vector<char>(65536));
    for (size_t i = 0; i < n; ++i) {
        outputs[i] = {}; outputs[i].buffer_type = MYSQL_TYPE_STRING;
        outputs[i].buffer = buffers[i].data(); outputs[i].buffer_length = buffers[i].size();
        outputs[i].length = &sizes[i]; outputs[i].is_null = &nulls[i];
    }
    if (mysql_stmt_bind_result(stmt.get(), outputs.data())) throw std::runtime_error(mysql_stmt_error(stmt.get()));
    int code;
    while ((code = mysql_stmt_fetch(stmt.get())) != MYSQL_NO_DATA) {
        if (code != 0) throw std::runtime_error("result fetch failed or field exceeds 64 KB");
        json row = json::object();
        for (size_t i = 0; i < n; ++i) {
            if (nulls[i]) { row[fields[i].name] = nullptr; continue; }
            std::string value(buffers[i].data(), sizes[i]);
            switch (fields[i].type) {
                case MYSQL_TYPE_TINY: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_LONGLONG: case MYSQL_TYPE_INT24:
                    row[fields[i].name] = std::stoll(value); break;
                default: row[fields[i].name] = value;
            }
        }
        rows.push_back(row);
    }
    return rows;
}
Transaction::Transaction(Db& db, int wait_seconds) : db_(db) {
    auto r = db_.query("SELECT GET_LOCK('course-registration-write', ?) AS acquired", {std::to_string(wait_seconds)});
    if (r[0]["acquired"] != 1) throw ApiError(409, "有选课操作正在处理，请稍后重试");
    try { db_.raw("START TRANSACTION"); }
    catch (...) { db_.query("SELECT RELEASE_LOCK('course-registration-write')"); throw; }
}
void Transaction::commit() { db_.raw("COMMIT"); committed_ = true; }
Transaction::~Transaction() {
    try { if (!committed_) db_.raw("ROLLBACK"); db_.query("SELECT RELEASE_LOCK('course-registration-write')"); }
    catch (...) {}
}
static std::string hex(const unsigned char* bytes, size_t size) {
    const char* digits = "0123456789abcdef"; std::string result; result.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
std::string random_hex(size_t bytes) {
    std::vector<unsigned char> data(bytes);
    if (RAND_bytes(data.data(), static_cast<int>(bytes)) != 1) throw std::runtime_error("random generator failed");
    return hex(data.data(), data.size());
}
std::string sha256(const std::string& value) {
    unsigned char hash[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), hash);
    return hex(hash, sizeof(hash));
}
std::string password_hash(const std::string& password, const std::string& salt) {
    unsigned char key[32];
    if (PKCS5_PBKDF2_HMAC(password.data(), password.size(), reinterpret_cast<const unsigned char*>(salt.data()),
        salt.size(), 210000, EVP_sha256(), sizeof(key), key) != 1) throw std::runtime_error("password hash failed");
    return hex(key, sizeof(key));
}
bool constant_equal(const std::string& a, const std::string& b) {
    return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}
