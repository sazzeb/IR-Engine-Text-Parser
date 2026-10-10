#include "InteractiveParser.h"
#include "InputFilePicker.h"
#include "ParserApplication.h"

#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace ir {
namespace {

// Explain available settings while keeping file selection entirely graphical.
void printUsage(const std::string& programName) {
    std::cout
        << "Usage: " << programName << " [settings]\n\n"
        << "A graphical chooser always selects the corpus file.\n"
        << "Canceling the chooser leaves existing output unchanged.\n\n"
        << "Settings:\n"
        << "  --trace                 Print every token decision and stream\n"
        << "  --progress              Print processing progress only\n"
        << "  --format FORMAT         auto, trec, txt, csv, tsv, json, jsonl, xlsx\n"
        << "  --text-mode MODE        paragraphs, lines, or document\n"
        << "  --text-field NAME       Select a text field; repeat for more fields\n"
        << "  --id-field NAME         Select the document identifier field\n"
        << "  --records-path POINTER  Select a JSON array using a JSON pointer\n"
        << "  --sheet NAME            Select an Excel worksheet\n"
        << "  --no-header             Read a table without a header row\n"
        << "  --help                  Show these instructions\n";
}

// Read a supplied setting value without requesting anything through standard input.
std::string argumentValue(
    int& index,
    int argc,
    char* argv[],
    const std::string& option
) {
    if (index + 1 >= argc) {
        throw std::runtime_error(
            option + " requires a value."
        );
    }

    const std::string value = argv[++index];

    if (value.empty() || value.rfind("--", 0) == 0) {
        throw std::runtime_error(
            option + " requires a valid value."
        );
    }

    return value;
}

}

// Select one corpus graphically and process it using shared application settings.
int runInteractiveParser(int argc, char* argv[]) {
    try {
        const auto programName =
            std::filesystem::path(argv[0]).filename().string();

        auto options = projectRunOptions();

        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];

            if (argument == "--help") {
                printUsage(programName);
                return 0;
            }

            if (argument == "--trace") {
                options.traceAll = true;
            } else if (argument == "--progress") {
                options.traceAll = false;
            } else if (argument == "--format") {
                options.reader.format = argumentValue(
                    index, argc, argv, argument
                );
            } else if (argument == "--text-mode") {
                options.reader.textMode = argumentValue(
                    index, argc, argv, argument
                );
            } else if (argument == "--text-field") {
                options.reader.textFields.push_back(
                    argumentValue(index, argc, argv, argument)
                );
            } else if (argument == "--id-field") {
                options.reader.idField = argumentValue(
                    index, argc, argv, argument
                );
            } else if (argument == "--records-path") {
                options.reader.recordsPath = argumentValue(
                    index, argc, argv, argument
                );
            } else if (argument == "--sheet") {
                options.reader.sheet = argumentValue(
                    index, argc, argv, argument
                );
            } else if (argument == "--no-header") {
                options.reader.hasHeader = false;
            } else {
                throw std::runtime_error(
                    "Unknown setting: " + argument +
                    ". Use --help to view supported settings."
                );
            }
        }

        std::cout
            << "Select your corpus file in the graphical chooser.\n"
            << std::flush;

        const auto selectedFile = chooseInputPath();

        if (!selectedFile) {
            std::cout
                << "Selection cancelled. Existing output was not changed.\n";
            return 0;
        }

        options.input = *selectedFile;
        options.recursive = false;

        std::cout << "[SELECTED] " << options.input << '\n';

        runParser(options, std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

}