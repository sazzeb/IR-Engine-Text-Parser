#pragma once

#include "OutputWriter.h"
#include "Sqlite.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace ir {

class IndexBuilder {
public:
    explicit IndexBuilder(Database& database);

    void addDocument(
        std::int64_t documentId,
        const std::unordered_map<std::int64_t, std::int64_t>& frequencies);

    void write(const OutputPaths& paths);

private:
    Database& database_;
    std::unique_ptr<Statement> insertPosting_;
    std::size_t documentsSinceCommit_ = 0;
};

}
