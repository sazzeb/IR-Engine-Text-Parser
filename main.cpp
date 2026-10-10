#include "ParserApplication.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

// Process bundled corpus files with the default stoplist and no file dialog.
int main(int argc, char* argv[]) {
    try {
        ir::RunOptions options;

        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];

            if (option == "--help") {
                std::cout
                    << "IREngine [--progress] [--recursive]\n"
                    << "Reads ./resources/corpus using ./resources/stoplist.txt.\n"
                    << "Run InputParser to choose a different corpus file.\n";

                return 0;
            }

            if (option == "--progress") {
                options.traceAll = false;
            } else if (option == "--recursive") {
                options.recursive = true;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        ir::runParser(options, std::cout);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}