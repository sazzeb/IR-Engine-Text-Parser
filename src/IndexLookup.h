#pragma once

#include "OutputWriter.h"
#include "TextParser.h"

#include <cstdint>
#include <filesystem>
#include <ios>
#include <iosfwd>
#include <string>
#include <unordered_map>

namespace ir {

class IndexLookup {
public:
    IndexLookup(
        const OutputPaths& paths,
        const std::filesystem::path& stopwords);

    bool printTerm(const std::string& input, std::ostream& output) const;

private:
    OutputPaths paths_;
    TextParser parser_;
    std::unordered_map<std::string, std::int64_t> termIds_;
    std::unordered_map<std::int64_t, std::streamoff> offsets_;
};

}
