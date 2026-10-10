#include "ParserApplication.h"

// Open the graphical chooser and process the selected local corpus file.
int main(int argc, char* argv[]) {
    return ir::runInteractiveParser(argc, argv);
}