#pragma once

#include "Document.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ir {

struct ReadOptions {
    std::string format = "auto";
    std::string textMode = "paragraphs";

    std::vector<std::string> textFields;

    std::string idField;
    std::string recordsPath;
    std::string sheet;

    bool hasHeader = true;

    std::filesystem::path scratch;
    std::string sourceName;

    std::size_t maxRecordBytes = 8 * 1024 * 1024;
};

class CorpusReader {
public:
    using Handler = std::function<void(const Document&)>;

    void readFile(
        const std::filesystem::path& path,
        const ReadOptions& options,
        const Handler& handler
    ) const;
};

}