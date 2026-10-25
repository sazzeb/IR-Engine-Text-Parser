#include "IndexLookup.h"
#include "ParserApplication.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

std::string argumentValue(int& index, int argc, char* argv[]) {
    if (++index >= argc || std::string(argv[index]).empty()) {
        throw std::invalid_argument("An option is missing its value.");
    }

    return argv[index];
}

}

int main(int argc, char* argv[]) {
    try {
        ir::RunOptions options;
        options.traceAll = false;
        std::optional<std::string> query;
        bool lookupOnly = false;

        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];

            if (option == "--help") {
                std::cout
                    << "IndexEngine [--input FILE_OR_DIRECTORY] [--output DIRECTORY] "
                       "[--stopwords FILE] [--recursive] [--term WORD] "
                       "[--lookup-only]\n"
                    << "Builds forward and inverted indexes, then looks up terms.\n"
                    << "Paths are relative to the working directory.\n"
                    << "--lookup-only searches existing output without indexing again.\n"
                    << "Without --term, enter one term per line; Ctrl-D ends lookup.\n";
                return 0;
            }

            if (option == "--input") {
                options.input = argumentValue(index, argc, argv);
            } else if (option == "--output") {
                options.output = std::filesystem::path(
                    argumentValue(index, argc, argv)) / "parser_output.txt";
            } else if (option == "--stopwords") {
                options.stopwords = argumentValue(index, argc, argv);
            } else if (option == "--term") {
                query = argumentValue(index, argc, argv);
            } else if (option == "--recursive") {
                options.recursive = true;
            } else if (option == "--lookup-only") {
                lookupOnly = true;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        const auto paths = ir::OutputWriter::paths(options.output);

        if (!lookupOnly) {
            const auto started = std::chrono::steady_clock::now();
            const auto summary = ir::runParser(options, std::cout);
            const auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();

            const auto indexBytes =
                std::filesystem::file_size(paths.forward) +
                std::filesystem::file_size(paths.inverted) +
                std::filesystem::file_size(paths.offsets);
            const auto totalBytes = indexBytes +
                std::filesystem::file_size(paths.terms) +
                std::filesystem::file_size(paths.documents);

            std::cout << std::fixed << std::setprecision(3)
                      << "[INDEX] " << summary.documents << " documents; "
                      << summary.terms << " terms; " << summary.tokens << " tokens; "
                      << elapsed << " seconds\n"
                      << "[SIZE] " << indexBytes << " index bytes; "
                      << totalBytes << " bytes including dictionaries\n";
        }

        ir::IndexLookup lookup(paths, options.stopwords);

        if (query) {
            lookup.printTerm(*query, std::cout);
            return 0;
        }

        std::string term;

        while (std::cout << "Enter a term (Ctrl-D to finish): " &&
               std::getline(std::cin, term)) {
            if (term.empty()) {
                continue;
            }

            try {
                lookup.printTerm(term, std::cout);
            } catch (const std::invalid_argument& error) {
                std::cout << error.what() << '\n';
            }
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
