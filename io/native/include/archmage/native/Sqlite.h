// A small RAII layer over SQLite for the native container (D-13). Errors become structured errors with
// the right category: a full disk is resource_exhausted, a corrupt or non-database file damaged_input,
// a refused open permission_denied (spec §26.2).
//
// Thread safety: a Database and its statements belong to one thread.
#pragma once

#include "archmage/base/Error.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace archmage::native {

class Statement {
public:
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    ~Statement();

    Statement &bind(int index, std::int64_t value);
    Statement &bind(int index, std::string_view text);
    Statement &bind(int index, std::span<const std::byte> blob);
    Statement &bindNull(int index);

    /// True while a row is available; false when done. The first failure of a bind or step is kept in
    /// status() until the statement is destroyed: callers check it once after a sequence of operations.
    bool step();
    [[nodiscard]] base::Status status() const { return status_; }
    void reset();

    [[nodiscard]] std::int64_t integer(int column) const;
    [[nodiscard]] std::string text(int column) const;
    [[nodiscard]] std::vector<std::byte> blob(int column) const;
    [[nodiscard]] bool isNull(int column) const;

private:
    friend class Database;
    Statement(sqlite3 *db, sqlite3_stmt *statement, std::string sql);
    void check(int code, std::string_view what); // records the first failure in status_

    sqlite3 *db_;
    sqlite3_stmt *statement_;
    std::string sql_;
    base::Status status_;
};

class Database {
public:
    enum class Mode { ReadOnly, ReadWriteCreate };

    /// Every connection gets the defenses SQLite recommends for untrusted files: defensive mode, no trusted
    /// schema, and a length limit just above the largest blob a reader accepts.
    static base::Result<Database> open(const std::filesystem::path &path, Mode mode);
    /// An empty in-memory database: older files are migrated in a copy, never in place.
    static base::Result<Database> openMemory();
    Database(Database &&other) noexcept;
    Database &operator=(Database &&other) noexcept;
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    ~Database();

    base::Status exec(std::string_view sql);
    base::Result<Statement> prepare(std::string_view sql);
    /// Copies this database into `destination` with SQLite's online backup API.
    base::Status copyTo(Database &destination);
    /// Closes now (also done by the destructor); reports a failure to close.
    base::Status close();

    /// The structured error for SQLite result `code` while doing `what`.
    [[nodiscard]] base::Error error(int code, std::string_view what) const;
    [[nodiscard]] sqlite3 *handle() const { return db_; }

private:
    explicit Database(sqlite3 *db, std::filesystem::path path);

    sqlite3 *db_;
    std::filesystem::path path_;
};

} // namespace archmage::native
