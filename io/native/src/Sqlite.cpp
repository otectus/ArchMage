#include "archmage/native/Sqlite.h"

#include "archmage/native/FaultPoints.h"

#include <sqlite3.h>

#include <cerrno>
#include <cstring>
#include <utility>

namespace archmage::native {
namespace {

using base::ErrorCategory;

// `systemErrno` (sqlite3_system_errno) refines I/O failures: a full disk, a quota or a file-size limit is
// resource exhaustion whatever SQLite's own code says.
base::Error mapError(int code, std::string_view what, std::string detail, int systemErrno = 0) {
    const int primary = code & 0xff;
    ErrorCategory category = ErrorCategory::Internal;
    std::string message = "The document file could not be accessed";
    switch (primary) {
    case SQLITE_FULL:
        category = ErrorCategory::ResourceExhausted;
        message = "There is not enough disk space";
        break;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
    case SQLITE_FORMAT:
        category = ErrorCategory::DamagedInput;
        message = "The document file is damaged or is not an ArchMage document";
        break;
    case SQLITE_CANTOPEN:
    case SQLITE_PERM:
    case SQLITE_READONLY:
    case SQLITE_AUTH:
        category = ErrorCategory::PermissionDenied;
        message = "The document file could not be opened or written";
        break;
    case SQLITE_IOERR:
        if (systemErrno == ENOSPC || systemErrno == EDQUOT || systemErrno == EFBIG) {
            category = ErrorCategory::ResourceExhausted;
            message = "There is not enough disk space";
        } else {
            category = (code == SQLITE_IOERR_WRITE || code == SQLITE_IOERR_FSYNC) ? ErrorCategory::ResourceExhausted
                                                                                   : ErrorCategory::Internal;
            message = "Reading or writing the document file failed";
        }
        break;
    case SQLITE_NOMEM:
        category = ErrorCategory::ResourceExhausted;
        message = "There is not enough memory";
        break;
    case SQLITE_INTERRUPT:
        category = ErrorCategory::Cancelled;
        message = "The operation was cancelled";
        break;
    default:
        break;
    }
    std::string text = std::string(what) + ": " + sqlite3_errstr(code) + (detail.empty() ? "" : " (" + detail + ")");
    if (systemErrno != 0) {
        text += "; " + std::string(std::strerror(systemErrno));
    }
    return {category, "sqlite." + std::to_string(primary), message, std::move(text)};
}

// Recommended settings for databases from untrusted sources (sqlite.org/security.html).
void harden(sqlite3 *db) {
    sqlite3_extended_result_codes(db, 1);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DDL, 0, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DQS_DML, 0, nullptr);
    sqlite3_limit(db, SQLITE_LIMIT_LENGTH, 300 << 20);
}

} // namespace

Statement::Statement(sqlite3 *db, sqlite3_stmt *statement, std::string sql)
    : db_(db), statement_(statement), sql_(std::move(sql)) {}

Statement::Statement(Statement &&other) noexcept
    : db_(other.db_), statement_(std::exchange(other.statement_, nullptr)), sql_(std::move(other.sql_)),
      status_(std::move(other.status_)) {}

Statement &Statement::operator=(Statement &&other) noexcept {
    if (this != &other) {
        sqlite3_finalize(statement_);
        db_ = other.db_;
        statement_ = std::exchange(other.statement_, nullptr);
        sql_ = std::move(other.sql_);
        status_ = std::move(other.status_);
    }
    return *this;
}

Statement::~Statement() { sqlite3_finalize(statement_); }

Statement &Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(statement_, index, value), "bind");
    return *this;
}

Statement &Statement::bind(int index, std::string_view text) {
    check(sqlite3_bind_text64(statement_, index, text.data(), text.size(), SQLITE_TRANSIENT, SQLITE_UTF8), "bind");
    return *this;
}

Statement &Statement::bind(int index, std::span<const std::byte> blob) {
    check(sqlite3_bind_blob64(statement_, index, blob.empty() ? "" : static_cast<const void *>(blob.data()),
                              blob.size(), SQLITE_TRANSIENT),
          "bind");
    return *this;
}

Statement &Statement::bindNull(int index) {
    check(sqlite3_bind_null(statement_, index), "bind");
    return *this;
}

void Statement::check(int code, std::string_view what) {
    if (code != SQLITE_OK && status_) {
        status_ = base::unexpected(
            mapError(code, what, sql_.substr(0, 80) + ": " + sqlite3_errmsg(db_), sqlite3_system_errno(db_)));
    }
}

bool Statement::step() {
    const int code = sqlite3_step(statement_);
    if (code == SQLITE_ROW) {
        return true;
    }
    if (code != SQLITE_DONE) {
        check(code, "statement");
    }
    return false;
}

void Statement::reset() {
    sqlite3_reset(statement_);
    sqlite3_clear_bindings(statement_);
}

std::int64_t Statement::integer(int column) const { return sqlite3_column_int64(statement_, column); }

std::string Statement::text(int column) const {
    const auto *text = reinterpret_cast<const char *>(sqlite3_column_text(statement_, column));
    return text != nullptr ? std::string(text, static_cast<std::size_t>(sqlite3_column_bytes(statement_, column)))
                           : std::string();
}

std::vector<std::byte> Statement::blob(int column) const {
    const auto *data = static_cast<const std::byte *>(sqlite3_column_blob(statement_, column));
    const auto size = static_cast<std::size_t>(sqlite3_column_bytes(statement_, column));
    return data != nullptr ? std::vector<std::byte>(data, data + size) : std::vector<std::byte>();
}

bool Statement::isNull(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

Database::Database(sqlite3 *db, std::filesystem::path path) : db_(db), path_(std::move(path)) {}

Database::Database(Database &&other) noexcept : db_(std::exchange(other.db_, nullptr)), path_(std::move(other.path_)) {}

Database &Database::operator=(Database &&other) noexcept {
    if (this != &other) {
        sqlite3_close_v2(db_);
        db_ = std::exchange(other.db_, nullptr);
        path_ = std::move(other.path_);
    }
    return *this;
}

Database::~Database() { sqlite3_close_v2(db_); }

base::Result<Database> Database::open(const std::filesystem::path &path, Mode mode) {
    sqlite3 *db = nullptr;
    const int flags = mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    const int code = sqlite3_open_v2(path.c_str(), &db, flags | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_EXRESCODE, nullptr);
    if (code != SQLITE_OK) {
        base::Error error = mapError(code, "open", path.string());
        sqlite3_close_v2(db);
        return base::unexpected(std::move(error));
    }
    harden(db);
    return Database(db, path);
}

base::Result<Database> Database::openMemory() {
    sqlite3 *db = nullptr;
    const int code = sqlite3_open_v2(":memory:", &db,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX |
                                         SQLITE_OPEN_EXRESCODE,
                                     nullptr);
    if (code != SQLITE_OK) {
        base::Error error = mapError(code, "open", ":memory:");
        sqlite3_close_v2(db);
        return base::unexpected(std::move(error));
    }
    harden(db);
    return Database(db, ":memory:");
}

base::Status Database::copyTo(Database &destination) {
    if (auto fault = faultPoint(FaultPoint::Backup); !fault) {
        return fault;
    }
    sqlite3_backup *backup = sqlite3_backup_init(destination.db_, "main", db_, "main");
    if (backup == nullptr) {
        return base::unexpected(destination.error(sqlite3_errcode(destination.db_), "backup"));
    }
    const int stepped = sqlite3_backup_step(backup, -1);
    const int finished = sqlite3_backup_finish(backup);
    if (stepped != SQLITE_DONE) {
        return base::unexpected(mapError(stepped, "backup", path_.string()));
    }
    if (finished != SQLITE_OK) {
        return base::unexpected(mapError(finished, "backup", path_.string()));
    }
    return {};
}

base::Status Database::exec(std::string_view sql) {
    char *message = nullptr;
    const std::string text(sql);
    const int code = sqlite3_exec(db_, text.c_str(), nullptr, nullptr, &message);
    if (code != SQLITE_OK) {
        std::string detail = message != nullptr ? message : "";
        sqlite3_free(message);
        return base::unexpected(mapError(code, "exec", text.substr(0, 80) + ": " + detail, sqlite3_system_errno(db_)));
    }
    return {};
}

base::Result<Statement> Database::prepare(std::string_view sql) {
    sqlite3_stmt *statement = nullptr;
    const int code = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &statement, nullptr);
    if (code != SQLITE_OK) {
        return base::unexpected(mapError(code, "prepare", std::string(sql.substr(0, 80)) + ": " + sqlite3_errmsg(db_),
                                         sqlite3_system_errno(db_)));
    }
    return Statement(db_, statement, std::string(sql));
}

base::Status Database::close() {
    if (db_ == nullptr) {
        return {};
    }
    const int code = sqlite3_close(db_);
    if (code != SQLITE_OK) {
        return base::unexpected(mapError(code, "close", path_.string()));
    }
    db_ = nullptr;
    return {};
}

base::Error Database::error(int code, std::string_view what) const {
    return mapError(code, what, db_ != nullptr ? sqlite3_errmsg(db_) : "", db_ != nullptr ? sqlite3_system_errno(db_) : 0);
}

} // namespace archmage::native
