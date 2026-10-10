#pragma once

#include "CorpusReader.h"

#include <cstddef>
#include <filesystem>
#include <ostream>

namespace ir {

    // Keep runtime paths relative to the folder used to launch the program.
    struct RunOptions {
        std::filesystem::path input = "./resources/corpus";
        std::filesystem::path stopwords = "./resources/stoplist.txt";
        std::filesystem::path output = "./output/parser_output.txt";

        ReadOptions reader;

        bool verbose = true;
        bool traceAll = true;
        bool recursive = false;
    };

    // Report completed document counts without confusing counts with the largest identifier.
    struct RunSummary {
        std::size_t documents = 0;
        std::size_t terms = 0;
        std::size_t tokens = 0;
    };

    // Process corpus files using sorted term identifiers and validated document numbers.
    RunSummary runParser(const RunOptions& options, std::ostream& log);

    // Read the bundled corpus and stoplist automatically without opening file dialogs.
    RunSummary runDefaultResource(std::ostream& log);

    // Select one file graphically and convert its path before opening data.
    int runInteractiveParser(int argc, char* argv[]);

}