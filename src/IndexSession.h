#pragma once

#include "OutputWriter.h"
#include "ParserApplication.h"

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

namespace ir {

void printIndexSummary(
    const RunSummary& summary,
    const OutputPaths& paths,
    double elapsedSeconds,
    std::ostream& output);

void runTermLookup(
    const OutputPaths& paths,
    const std::filesystem::path& stopwords,
    const std::optional<std::string>& query,
    std::istream& input,
    std::ostream& output);

}
