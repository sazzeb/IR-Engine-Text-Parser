#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ir {

void readWorkbook(
    const std::filesystem::path& path,
    const std::string& sheet,
    const std::filesystem::path& scratch,
    std::size_t limit,
    const std::function<void(const std::vector<std::string>&)>& rowHandler
);

}