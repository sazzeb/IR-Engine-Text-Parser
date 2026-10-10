#pragma once

#include <filesystem>
#include <optional>

namespace ir {

    // Open a graphical chooser and return one selected local file.
    std::optional<std::filesystem::path> chooseInputPath(
        const std::filesystem::path& startingDirectory = {}
    );

}