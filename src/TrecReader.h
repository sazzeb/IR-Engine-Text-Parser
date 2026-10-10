#pragma once

#include "Document.h"

#include <filesystem>
#include <functional>
#include <istream>

namespace ir {

    // Read separate TREC documents without loading the entire collection into memory.
    class TrecReader {
    public:
        using DocumentHandler = std::function<void(const Document&)>;

        // Read bounded document records and report each completed document separately.
        void read(
            std::istream& input,
            const DocumentHandler& handler,
            std::size_t recordLimit = 8 * 1024 * 1024) const;

        // Open a relative corpus path and report useful source errors.
        void readFile(
            const std::filesystem::path& path,
            const DocumentHandler& handler) const;
    };

}