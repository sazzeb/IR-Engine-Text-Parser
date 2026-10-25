#pragma once

#include "DocumentDictionary.h"
#include "TermDictionary.h"

#include <filesystem>
#include <functional>
#include <ostream>

namespace ir {

    // Keep all required result files together in one relative output folder.
    struct OutputPaths {
        std::filesystem::path parser;
        std::filesystem::path terms;
        std::filesystem::path documents;
        std::filesystem::path forward;
        std::filesystem::path inverted;
        std::filesystem::path offsets;
    };

    // Validate application paths without converting them into machine specific locations.
    std::filesystem::path requireRelativePath(const std::filesystem::path& path);

    // Write complete result files after clearing output from the previous run.
    class OutputWriter {
    public:
        using Producer = std::function<
            void(const OutputPaths&, const std::filesystem::path&)>;

        // Build the result filenames beside the chosen parser output.
        static OutputPaths paths(const std::filesystem::path& parserOutput);

        // Clear previous results and publish new files only after production succeeds.
        static void publish(
            const std::filesystem::path& parserOutput,
            const Producer& producer);

        // Write finalized dictionaries and token streams using their final identifiers.
        void write(
            std::ostream& output,
            const TermDictionary& terms,
            const DocumentDictionary& documents,
            const std::vector<ParsedDocument>& streams) const;
    };

}
