#include "IndexSession.h"
#include "IndexLookup.h"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ir {

void printIndexSummary(
    const RunSummary& summary,
    const OutputPaths& paths,
    double elapsedSeconds,
    std::ostream& output) {
    const auto indexBytes =
        std::filesystem::file_size(paths.forward) +
        std::filesystem::file_size(paths.inverted) +
        std::filesystem::file_size(paths.offsets);
    const auto totalBytes = indexBytes +
        std::filesystem::file_size(paths.terms) +
        std::filesystem::file_size(paths.documents);

    std::ostringstream result;
    result << std::fixed << std::setprecision(3)
           << "[INDEX] " << summary.documents << " documents; "
           << summary.terms << " terms; " << summary.tokens << " tokens; "
           << elapsedSeconds << " seconds\n"
           << "[SIZE] " << indexBytes << " index bytes; "
           << totalBytes << " bytes including dictionaries\n";
    output << result.str();
}

void runTermLookup(
    const OutputPaths& paths,
    const std::filesystem::path& stopwords,
    const std::optional<std::string>& query,
    std::istream& input,
    std::ostream& output) {
    IndexLookup lookup(paths, stopwords);

    if (query) {
        lookup.printTerm(*query, output);
        return;
    }

    std::string term;

    while (output << "Enter a term (Ctrl-D to finish): " &&
           std::getline(input, term)) {
        if (term.empty()) {
            continue;
        }

        try {
            lookup.printTerm(term, output);
        } catch (const std::invalid_argument& error) {
            output << error.what() << '\n';
        }
    }
}

}
