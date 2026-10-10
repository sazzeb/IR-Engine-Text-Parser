#include "InputFilePicker.h"

#include <portable-file-dialogs.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

namespace ir {
namespace {

// Read environment values without assuming every desktop defines each variable.
std::string environmentValue(const char* name) {
    const char* value = std::getenv(name);

    return value != nullptr
        ? std::string(value)
        : std::string{};
}

// Check starting folders without throwing for missing or inaccessible locations.
bool usableDirectory(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;

    return std::filesystem::is_directory(path, error) &&
           !error;
}

// Choose a reasonable starting folder without requiring typed user input.
std::filesystem::path initialDirectory(
    const std::filesystem::path& requested
) {
    if (usableDirectory(requested)) {
        return requested;
    }

#ifdef _WIN32
    const auto homeValue = environmentValue("USERPROFILE");
#else
    const auto homeValue = environmentValue("HOME");
#endif

    if (!homeValue.empty()) {
        const auto homeDirectory =
            std::filesystem::u8path(homeValue);

        const auto downloadsDirectory =
            homeDirectory / "Downloads";

        if (usableDirectory(downloadsDirectory)) {
            return downloadsDirectory;
        }

        if (usableDirectory(homeDirectory)) {
            return homeDirectory;
        }
    }

    std::error_code error;

    const auto currentDirectory =
        std::filesystem::current_path(error);

    return error
        ? std::filesystem::path{}
        : currentDirectory;
}

#ifdef __linux__

// Locate executable desktop helpers using the current process search path.
bool hasProgram(const std::string& name) {
    const auto searchPath = environmentValue("PATH");
    std::size_t start = 0;

    while (start <= searchPath.size()) {
        const auto end = searchPath.find(':', start);

        const auto folder = searchPath.substr(
            start,
            end - start
        );

        const auto candidate =
            (
                folder.empty()
                    ? std::filesystem::path(".")
                    : std::filesystem::path(folder)
            ) / name;

        std::error_code error;

        if (std::filesystem::is_regular_file(candidate, error) &&
            !error &&
            ::access(candidate.c_str(), X_OK) == 0) {
            return true;
        }

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    return false;
}

#endif

// Check desktop prerequisites without calling APIs missing from the pinned library.
void requireGraphicalDialog() {
#ifdef __linux__
    if (environmentValue("DISPLAY").empty() &&
        environmentValue("WAYLAND_DISPLAY").empty()) {
        throw std::runtime_error(
            "A graphical Linux desktop is required for file selection."
        );
    }

    if (!hasProgram("which") ||
        (
            !hasProgram("zenity") &&
            !hasProgram("kdialog") &&
            !hasProgram("matedialog") &&
            !hasProgram("qarma")
        )) {
        throw std::runtime_error(
            "Install Zenity or KDialog and the which utility, "
            "then restart the application inside your desktop session."
        );
    }

#elif defined(__APPLE__)
    std::error_code error;

    if (!std::filesystem::is_regular_file(
            "/usr/bin/osascript",
            error
        ) || error) {
        throw std::runtime_error(
            "The macOS dialog service is unavailable."
        );
    }
#endif
}

}

// Return cancellation safely and validate selected files before corpus processing.
std::optional<std::filesystem::path> chooseInputPath(
    const std::filesystem::path& startingDirectory
) {
    requireGraphicalDialog();

    const auto directory =
        initialDirectory(startingDirectory);

    const std::vector<std::string> filters{
        "Supported corpus files",
        "*.txt *.trec *.sgm *.sgml *.csv *.tsv *.json "
        "*.jsonl *.ndjson *.xlsx "
        "*.TXT *.TREC *.SGM *.SGML *.CSV *.TSV *.JSON "
        "*.JSONL *.NDJSON *.XLSX",
        "All files",
        "*"
    };

    pfd::open_file dialog(
        "Select a corpus file",
        directory.u8string(),
        filters
    );

    const auto selectedFiles = dialog.result();

    if (selectedFiles.empty()) {
        return std::nullopt;
    }

    if (selectedFiles.size() != 1) {
        throw std::runtime_error(
            "Please select exactly one corpus file."
        );
    }

    const auto selectedPath =
        std::filesystem::u8path(selectedFiles.front());

    std::error_code error;

    if (!std::filesystem::is_regular_file(
            selectedPath,
            error
        ) || error) {
        throw std::runtime_error(
            "The selected item is not an accessible regular file."
        );
    }

    std::ifstream input(selectedPath, std::ios::binary);

    if (!input) {
        throw std::runtime_error(
            "Cannot open the selected file: " +
            selectedPath.u8string()
        );
    }

    const auto resolvedPath =
        std::filesystem::canonical(selectedPath, error);

    if (error) {
        throw std::runtime_error(
            "Cannot resolve the selected file: " +
            error.message()
        );
    }

    return resolvedPath;
}

}