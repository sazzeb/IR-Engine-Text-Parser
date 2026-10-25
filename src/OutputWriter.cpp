#include "OutputWriter.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace ir {

// Require portable relative paths before opening application data or result files.
std::filesystem::path requireRelativePath(
    const std::filesystem::path& path) {
    const auto text = path.generic_u8string();

    if (text.empty() || path.has_root_path() ||
        text.front() == '\\' ||
        (text.size() > 1 && text[1] == ':')) {
        throw std::invalid_argument(
            "Use a relative path, such as ./resources/corpus/sample.trec.");
    }

    return path.lexically_normal();
}

namespace {

// Hold an output lock and remove temporary files after processing ends.
class OutputWorkspace {
public:
    std::filesystem::path path;

    // Reserve one output folder so concurrent runs cannot overwrite each other.
    explicit OutputWorkspace(const std::filesystem::path& parent)
        : path(parent / ".parser-output.lock") {
        if (!std::filesystem::create_directory(path)) {
            throw std::runtime_error(
                "Output is locked; close other runs before removing " +
                path.u8string());
        }
    }

    // Remove this run's temporary files without throwing during error cleanup.
    ~OutputWorkspace() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    // Keep one owner responsible for this run's lock and temporary files.
    OutputWorkspace(const OutputWorkspace&) = delete;

    // Prevent assignment from losing track of a lock that needs cleanup.
    OutputWorkspace& operator=(const OutputWorkspace&) = delete;
};

// Empty a result file and check that its contents were cleared.
void clearFile(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.close();

    if (!output) {
        throw std::runtime_error("Cannot clear output: " + path.u8string());
    }
}

}

// Place dictionaries and indexes beside the required parser output.
OutputPaths OutputWriter::paths(
    const std::filesystem::path& parserOutput) {
    const auto parser = requireRelativePath(parserOutput);

    if (parser.filename() != "parser_output.txt") {
        throw std::invalid_argument(
            "The parser output filename must be parser_output.txt.");
    }

    return {
        parser,
        parser.parent_path() / "text_dictionary.txt",
        parser.parent_path() / "doc_dictionary.txt",
        parser.parent_path() / "forward_index.txt",
        parser.parent_path() / "inverted_index.txt",
        parser.parent_path() / "inverted_offsets.txt"
    };
}

// Replace all result files together and clear partial results after failures.
void OutputWriter::publish(
    const std::filesystem::path& parserOutput,
    const Producer& producer) {
    namespace fs = std::filesystem;

    const auto output = paths(parserOutput);
    const auto parent = output.parser.has_parent_path()
        ? output.parser.parent_path()
        : fs::path(".");

    fs::create_directories(parent);

    const std::array<fs::path, 6> destinations{
        output.parser, output.terms, output.documents,
        output.forward, output.inverted, output.offsets
    };

    for (std::size_t index = 0; index < destinations.size(); ++index) {
        const auto& path = destinations[index];

        if (fs::is_symlink(fs::symlink_status(path)) ||
            (fs::exists(path) && !fs::is_regular_file(path))) {
            throw std::runtime_error(
                "Output must be a regular file, not a link: " +
                path.u8string());
        }

        for (std::size_t previous = 0; previous < index; ++previous) {
            if (fs::exists(path) && fs::exists(destinations[previous]) &&
                fs::equivalent(path, destinations[previous])) {
                throw std::runtime_error(
                    "Result files must not be hard links to each other.");
            }
        }
    }

    OutputWorkspace workspace(parent);
    const auto staged = paths(workspace.path / "parser_output.txt");

    const std::array<fs::path, 6> pending{
        staged.parser, staged.terms, staged.documents,
        staged.forward, staged.inverted, staged.offsets
    };

    try {
        for (const auto& path : destinations) {
            clearFile(path);
        }

        producer(staged, workspace.path);

        for (const auto& path : pending) {
            if (!fs::is_regular_file(path)) {
                throw std::runtime_error(
                    "A required result file was not produced.");
            }
        }

        for (std::size_t index = 0; index < destinations.size(); ++index) {
            fs::remove(destinations[index]);
            fs::rename(pending[index], destinations[index]);
        }
    } catch (...) {
        for (const auto& path : destinations) {
            std::ofstream empty(path, std::ios::binary | std::ios::trunc);
        }

        throw;
    }
}

// Print dictionary entries and streams without assuming document numbers are consecutive.
void OutputWriter::write(
    std::ostream& output,
    const TermDictionary& terms,
    const DocumentDictionary& documents,
    const std::vector<ParsedDocument>& streams) const {
    output << "TERM_DICTIONARY\n";

    for (const auto& [id, word] : terms.entries()) {
        output << word << '\t' << id << '\n';
    }

    output << "\nDOCUMENT_DICTIONARY\n";

    for (const auto& [id, name] : documents.entries()) {
        output << name << '\t' << id << '\n';
    }

    output << "\nTOKEN_STREAMS\n";

    for (const auto& document : streams) {
        output << documents.value(document.id)
               << '\t' << document.id << '\t';

        bool first = true;

        for (const auto term : document.terms) {
            output << (first ? "" : " ")
                   << terms.value(term) << ':' << term;
            first = false;
        }

        output << '\n';
    }

    if (!output) {
        throw std::runtime_error("Cannot write parser output.");
    }
}

}
