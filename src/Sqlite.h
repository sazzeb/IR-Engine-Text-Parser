#pragma once

#include "OutputWriter.h"

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace ir {

// Keep temporary dictionary storage on disk with checked database operations.
class Database {
    sqlite3* handle_ = nullptr;

public:
    // Open temporary storage using relative paths and bounded database cache settings.
    explicit Database(const std::filesystem::path& path) {
        const int result = sqlite3_open_v2(
            requireRelativePath(path).u8string().c_str(),
            &handle_,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
            nullptr);

        if (result != SQLITE_OK) {
            const std::string message = handle_
                ? sqlite3_errmsg(handle_)
                : "Cannot allocate database";

            sqlite3_close(handle_);
            throw std::runtime_error(message);
        }

        try {
            execute(
                "PRAGMA cache_size=-8192;"
                "PRAGMA temp_store=FILE;"
                "PRAGMA mmap_size=0;"
                "PRAGMA journal_mode=DELETE;"
                "PRAGMA synchronous=FULL;");
        } catch (...) {
            sqlite3_close(handle_);
            throw;
        }
    }

    // Release the database connection after its prepared statements have been destroyed.
    ~Database() {
        sqlite3_close(handle_);
    }

    // Prevent shared ownership because this object alone closes its database handle.
    Database(const Database&) = delete;

    // Prevent assignment from replacing a database handle that still needs cleanup.
    Database& operator=(const Database&) = delete;

    // Provide the database handle required by the prepared statement wrapper.
    sqlite3* handle() const {
        return handle_;
    }

    // Run a complete SQL command and report any database failure.
    void execute(const char* sql) {
        if (sqlite3_exec(handle_, sql, nullptr, nullptr, nullptr) != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(handle_));
        }
    }
};

// Manage prepared SQL statements without leaking handles after parsing failures.
class Statement {
    sqlite3_stmt* statement_ = nullptr;
    sqlite3* database_;

    // Turn SQLite failures into readable exceptions for the calling code.
    void check(int result) {
        if (result != SQLITE_OK) {
            throw std::runtime_error(sqlite3_errmsg(database_));
        }
    }

public:
    // Prepare one SQL statement against the supplied open database connection.
    Statement(Database& database, const char* sql)
        : database_(database.handle()) {
        check(sqlite3_prepare_v2(
            database_, sql, -1, &statement_, nullptr));
    }

    // Release the prepared statement before its database connection is closed.
    ~Statement() {
        sqlite3_finalize(statement_);
    }

    // Prevent shared ownership because each prepared statement needs exactly one owner.
    Statement(const Statement&) = delete;

    // Prevent assignment from replacing a statement that still needs cleanup.
    Statement& operator=(const Statement&) = delete;

    // Clear previous results and parameters before reusing this prepared statement.
    void reset() {
        check(sqlite3_reset(statement_));
        check(sqlite3_clear_bindings(statement_));
    }

    // Bind text values safely without building SQL from document contents.
    void bind(int column, const std::string& value) {
        check(sqlite3_bind_text64(
            statement_,
            column,
            value.data(),
            value.size(),
            SQLITE_TRANSIENT,
            SQLITE_UTF8));
    }

    // Bind an integer using SQLite parameters that are numbered from one.
    void bind(int column, std::int64_t value) {
        check(sqlite3_bind_int64(statement_, column, value));
    }

    // Read the next result row or confirm the command has finished.
    bool step() {
        const int result = sqlite3_step(statement_);

        if (result == SQLITE_ROW) {
            return true;
        }

        if (result == SQLITE_DONE) {
            return false;
        }

        throw std::runtime_error(sqlite3_errmsg(database_));
    }

    // Read a result column using the offset required by SQLite.
    std::int64_t integer(int column) const {
        return sqlite3_column_int64(statement_, column);
    }

    // Copy a result column while preserving its full stored byte length.
    std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);

        return value
            ? std::string(
                reinterpret_cast<const char*>(value),
                sqlite3_column_bytes(statement_, column))
            : std::string{};
    }
};

}