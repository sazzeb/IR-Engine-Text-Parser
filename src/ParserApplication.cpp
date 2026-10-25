#include "ParserApplication.h"
#include "IndexBuilder.h"
#include "IndexSession.h"
#include "Dictionary.h"
#include "OutputWriter.h"
#include "Sqlite.h"
#include "TextParser.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#include <portable-file-dialogs.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ir {
namespace {

#ifndef _WIN32
// Check helper support using the interface supplied by the pinned dialog version.
class DialogSupport : private pfd::internal::dialog {
public:
    // Report whether this system can open a native graphical file chooser.
    bool available() const {
#if defined(__APPLE__)
        return true;
#else
        return is_zenity() || is_kdialog();
#endif
    }
};
#endif

// Detect file aliases before any parser output is cleared or replaced.
bool sameFile(
    const std::filesystem::path& first,
    const std::filesystem::path& second) {
    return first.lexically_normal() == second.lexically_normal() ||
        (std::filesystem::exists(first) &&
         std::filesystem::exists(second) &&
         std::filesystem::equivalent(first, second));
}

// Collect supported input files while protecting resources and all result files.
std::vector<std::filesystem::path> inputFiles(
    const RunOptions& options,
    const OutputPaths& outputs) {
    namespace fs = std::filesystem;

    const auto source = requireRelativePath(options.input);
    const auto stopwords = requireRelativePath(options.stopwords);

    const std::vector<fs::path> results{
        outputs.parser, outputs.terms, outputs.documents,
        outputs.forward, outputs.inverted, outputs.offsets
    };

    for (const auto& path : results) {
        if (sameFile(path, stopwords)) {
            throw std::runtime_error(
                "Output would overwrite the stoplist.");
        }
    }

    std::vector<fs::path> files;

    const std::set<std::string> extensions{
        "", ".txt", ".trec", ".sgm", ".sgml", ".csv", ".tsv",
        ".json", ".jsonl", ".ndjson", ".xlsx"
    };

    // Add usable files while excluding output files from folder based input.
    const auto add = [&](const fs::path& path, bool direct) {
        if (!fs::is_regular_file(path)) {
            return;
        }

        for (const auto& result : results) {
            if (sameFile(path, result)) {
                if (direct) {
                    throw std::runtime_error(
                        "A result file cannot be selected as input.");
                }

                return;
            }
        }

        if (!direct && sameFile(path, stopwords)) {
            return;
        }

        auto extension = path.extension().string();

        for (char& character : extension) {
            if (character >= 'A' && character <= 'Z') {
                character += 'a' - 'A';
            }
        }

        if (direct || options.reader.format != "auto" ||
            extensions.count(extension)) {
            std::ifstream check(path, std::ios::binary);

            if (!check) {
                throw std::runtime_error(
                    "Cannot read input: " + path.u8string());
            }

            files.push_back(requireRelativePath(path));
        }
    };

    const auto collect = [&](const fs::path& input) {
        if (fs::is_regular_file(input)) {
            add(input, true);
        } else if (fs::is_directory(input) && options.recursive) {
            for (fs::recursive_directory_iterator entry(input), end;
                 entry != end; ++entry) {
                const auto name = entry->path().filename().string();

                if (!name.empty() && name.front() == '.') {
                    entry.disable_recursion_pending();
                    continue;
                }

                add(entry->path(), false);
            }
        } else if (fs::is_directory(input)) {
            for (const auto& entry : fs::directory_iterator(input)) {
                const auto name = entry.path().filename().string();

                if (!name.empty() && name.front() != '.') {
                    add(entry.path(), false);
                }
            }
        } else {
            throw std::runtime_error("Input is missing: " + input.u8string());
        }
    };

    collect(source);
    for (const auto& input : options.additionalInputs) {
        collect(requireRelativePath(input));
    }

    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    if (files.empty()) {
        throw std::runtime_error("No supported input files were found.");
    }

    return files;
}

// Finish each stream before its file becomes a published result.
void closeOutput(std::ofstream& output) {
    output.close();

    if (!output) {
        throw std::runtime_error(
            "Writing failed; check folder access and free disk space.");
    }
}

// Assign final document numbers after all reserved Financial Times numbers are known.
void assignDocuments(Database& database, bool useInputOrder) {
    std::set<std::int64_t> reserved;

    Statement fixed(
        database,
        "SELECT fixed_id FROM documents WHERE fixed_id>0");

    while (fixed.step()) {
        reserved.insert(fixed.integer(0));
    }

    Statement names(
        database,
        useInputOrder
            ? "SELECT name,fixed_id FROM documents ORDER BY input_order"
            : "SELECT name,fixed_id FROM documents ORDER BY name COLLATE BINARY");

    Statement insert(
        database,
        "INSERT INTO document_ids(id,name) VALUES(?1,?2)");

    std::int64_t next = 1;

    while (names.step()) {
        auto identifier = names.integer(1);

        if (identifier == 0) {
            while (reserved.count(next)) {
                ++next;
            }

            identifier = next++;
        }

        insert.reset();
        insert.bind(1, identifier);
        insert.bind(2, names.text(0));
        insert.step();
    }
}

// Export identical dictionary rows to their own files and combined parser output.
void writeDictionary(
    Database& database,
    const char* query,
    const std::filesystem::path& path,
    std::ostream& combined) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output) {
        throw std::runtime_error("Cannot create dictionary output.");
    }

    Statement rows(database, query);

    while (rows.step()) {
        const auto line =
            rows.text(0) + "\t" +
            std::to_string(rows.integer(1)) + "\n";

        output << line;
        combined << line;
    }

    closeOutput(output);

    if (!combined) {
        throw std::runtime_error(
            "Cannot write combined dictionary output.");
    }
}

// Choose a local corpus file or folder and return a relative path.
std::optional<std::vector<std::filesystem::path>> chooseRelativeInput(bool indexer) {
#ifdef __linux__
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");

    if ((!display || !*display) && (!wayland || !*wayland)) {
        throw std::runtime_error(
            "Run inside a graphical Linux desktop to select input.");
    }
#endif

#ifndef _WIN32
    if (!DialogSupport().available()) {
        throw std::runtime_error(
            "No graphical file chooser is available. On Linux, install Zenity or KDialog.");
    }
#endif

    bool selectFolder = false;
    std::vector<std::filesystem::path> selectedPaths;

#ifdef _WIN32
        std::vector<wchar_t> filename(65536, L'\0');

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = GetActiveWindow();
        dialog.lpstrTitle = indexer
            ? L"Select one or more corpus files"
            : L"Select one corpus file";
        dialog.lpstrFilter =
            L"Corpus files\0"
            L"*.txt;*.trec;*.sgm;*.sgml;*.csv;*.tsv;*.json;*.jsonl;*.ndjson;*.xlsx\0"
            L"All files\0*.*\0";
        dialog.nFilterIndex = 1;
        dialog.lpstrFile = filename.data();
        dialog.nMaxFile = static_cast<DWORD>(filename.size());
        dialog.Flags =
            OFN_EXPLORER |
            OFN_FILEMUSTEXIST |
            OFN_PATHMUSTEXIST |
            OFN_NOCHANGEDIR |
            (indexer ? OFN_ALLOWMULTISELECT : 0);

        if (!GetOpenFileNameW(&dialog)) {
            const DWORD error = CommDlgExtendedError();

            if (error != 0) {
                throw std::runtime_error(
                    "Windows file chooser failed: " +
                    std::to_string(error));
            }

        } else {
            const std::filesystem::path first(filename.data());
            const wchar_t* next = filename.data() + first.native().size() + 1;
            if (indexer && *next != L'\0') {
                while (*next != L'\0') {
                    selectedPaths.push_back(first / next);
                    next += std::wcslen(next) + 1;
                }
            } else {
                selectedPaths.push_back(first);
            }
        }
#else
        pfd::open_file chooser(
            indexer ? "Select one or more corpus files" : "Select one corpus file",
            "",
            {
                "Corpus files",
                "*.txt *.trec *.sgm *.sgml *.csv *.tsv *.json *.jsonl *.ndjson *.xlsx",
                "All files",
                "*"
            }, indexer ? pfd::opt::multiselect : pfd::opt::none);

        const auto selected = chooser.result();

        if (!indexer && selected.size() != 1) {
            if (!selected.empty()) {
                throw std::runtime_error("Select exactly one corpus file.");
            }
        }

        for (const auto& path : selected) {
            selectedPaths.push_back(std::filesystem::u8path(path));
        }
#endif

    if (selectedPaths.empty()) {
        if (!indexer || pfd::message(
                "Select a corpus folder?",
                "No files selected. Choose Yes to select a whole folder, or No to cancel.",
                pfd::choice::yes_no,
                pfd::icon::question).result() != pfd::button::yes) {
            return std::nullopt;
        }

        const auto selected = pfd::select_folder(
            "Select a corpus folder").result();
        if (selected.empty()) {
            return std::nullopt;
        }

        selectFolder = true;
        selectedPaths.push_back(std::filesystem::u8path(selected));
    }

    // Desktop APIs return full locations, so convert them before opening files.
    std::vector<std::filesystem::path> relativePaths;
    for (const auto& selectedPath : selectedPaths) {
        std::error_code error;
        const auto relative = std::filesystem::relative(selectedPath, error);

        if (error || relative.empty() || relative.has_root_path()) {
            throw std::runtime_error(
                "Copy the input onto the project's drive so a relative path can be used.");
        }

        const auto path = requireRelativePath(relative);
        if (selectFolder
                ? !std::filesystem::is_directory(path)
                : !std::filesystem::is_regular_file(path)) {
            throw std::runtime_error("Select readable corpus input.");
        }
        relativePaths.push_back(path);
    }

    return relativePaths;
}

// Read a setting value without asking users to type input file paths.
std::string settingValue(int& index, int argc, char* argv[]) {
    if (++index >= argc ||
        std::string(argv[index]).empty() ||
        std::string(argv[index]).rfind("--", 0) == 0) {
        throw std::invalid_argument(
            "A command option is missing its value.");
    }

    return argv[index];
}

// Collect stems first, assign final identifiers, then write consistent output files.
RunSummary processCorpus(
    const RunOptions& options,
    std::ostream& log,
    bool useUploadLayout,
    bool buildIndexes = true) {
    const auto outputs = OutputWriter::paths(options.output);
    const auto files = inputFiles(options, outputs);

    if (useUploadLayout && files.size() != 1) {
        throw std::invalid_argument("Select one corpus file for each upload run.");
    }

    requireRelativePath(options.stopwords);
    const auto parser = TextParser::fromStopwordFile(options.stopwords);

    RunSummary summary;

    // Keep temporary dictionaries on disk while processing one document at a time.
    OutputWriter::publish(
        options.output,
        [&](const OutputPaths& staged,
            const std::filesystem::path& scratch) {
            Database database(scratch / "dictionary.sqlite");

            database.execute(
                "CREATE TABLE terms(value TEXT PRIMARY KEY) WITHOUT ROWID;"
                "CREATE TABLE documents("
                "name TEXT PRIMARY KEY,fixed_id INTEGER UNIQUE,"
                "input_order INTEGER UNIQUE NOT NULL CHECK(input_order>0)) WITHOUT ROWID;"
                "CREATE TABLE term_ids("
                "id INTEGER PRIMARY KEY CHECK(id>0),value TEXT UNIQUE NOT NULL);"
                "CREATE TABLE document_ids("
                "id INTEGER PRIMARY KEY CHECK(id>0),name TEXT UNIQUE NOT NULL);"
                "BEGIN;");

            Statement addTerm(
                database,
                "INSERT OR IGNORE INTO terms(value) VALUES(?1)");

            Statement addDocument(
                database,
                "INSERT INTO documents(name,fixed_id,input_order) "
                "VALUES(?1,NULLIF(?2,0),?3)");

            std::unordered_set<std::string> seenTerms;

            std::ofstream spool(
                scratch / "tokens.txt",
                std::ios::binary | std::ios::trunc);

            if (!spool) {
                throw std::runtime_error(
                    "Cannot create temporary token output.");
            }

            for (const auto& file : files) {
                auto reader = options.reader;
                reader.scratch = scratch;
                reader.sourceName = useUploadLayout
                    ? "DOC"
                    : file.generic_u8string();

                if (options.verbose) {
                    log << "[READ] " << file.generic_u8string() << '\n';
                }

                // Save document names and stems without exposing temporary numeric identifiers.
                CorpusReader().readFile(
                    file,
                    reader,
                    [&](const Document& document) {
                        if (summary.documents >= static_cast<std::uint64_t>(
                                std::numeric_limits<std::int64_t>::max())) {
                            throw std::overflow_error("Too many documents for positive identifiers.");
                        }

                        const auto number =
                            Dictionary::documentNumber(document.name);

                        addDocument.reset();
                        addDocument.bind(1, document.name);
                        addDocument.bind(
                            2,
                            static_cast<std::int64_t>(number.value_or(0)));
                        addDocument.bind(
                            3,
                            static_cast<std::int64_t>(summary.documents + 1));

                        try {
                            addDocument.step();
                        } catch (const std::exception& error) {
                            throw std::runtime_error(
                                "Duplicate or invalid document " +
                                document.name + ": " + error.what());
                        }

                        TokenObserver observer;

                        if (options.verbose && options.traceAll) {
                            // Show token decisions before final sorted identifiers become available.
                            observer = [&](const TokenEvent& event) {
                                log << "[TOKEN] " << event.original
                                    << " -> " << event.normalized;

                                if (event.disposition ==
                                    TokenDisposition::accepted) {
                                    log << " -> " << event.stem;
                                } else if (event.disposition ==
                                           TokenDisposition::stopword) {
                                    log << " [removed: stopword]";
                                } else {
                                    log << " [removed: contains digit]";
                                }

                                log << '\n';
                            };
                        }

                        const auto tokens =
                            parser.parseText(document.text, observer);

                        spool << document.name << '\t';
                        bool first = true;

                        for (const auto& token : tokens) {
                            if (seenTerms.insert(token).second) {
                                addTerm.reset();
                                addTerm.bind(1, token);
                                addTerm.step();

                                if (seenTerms.size() >= 20000) {
                                    seenTerms.clear();
                                }
                            }

                            spool << (first ? "" : " ") << token;
                            first = false;
                        }

                        spool << '\n';

                        if (!spool) {
                            throw std::runtime_error(
                                "Cannot save temporary token streams.");
                        }

                        ++summary.documents;
                        summary.tokens += tokens.size();

                        if (summary.documents % 1000 == 0) {
                            database.execute("COMMIT;BEGIN;");

                            if (options.verbose) {
                                log << "[PROGRESS] "
                                    << summary.documents
                                    << " documents\n";
                            }
                        }
                    });
            }

            closeOutput(spool);

            database.execute(
                "INSERT INTO term_ids(id,value) "
                "SELECT ROW_NUMBER() OVER "
                "(ORDER BY value COLLATE BINARY),value FROM terms;");

            assignDocuments(database, useUploadLayout);
            database.execute("COMMIT;");

            Statement count(database, "SELECT COUNT(*) FROM term_ids");

            if (count.step()) {
                summary.terms =
                    static_cast<std::size_t>(count.integer(0));
            }

            std::ofstream output(
                staged.parser,
                std::ios::binary | std::ios::trunc);

            if (!output) {
                throw std::runtime_error("Cannot create parser output.");
            }

            output << "TERM_DICTIONARY\n";

            writeDictionary(
                database,
                "SELECT value,id FROM term_ids ORDER BY id",
                staged.terms,
                output);

            output << "\nDOCUMENT_DICTIONARY\n";

            writeDictionary(
                database,
                "SELECT name,id FROM document_ids ORDER BY id",
                staged.documents,
                output);

            output << "\nTOKEN_STREAMS\n";

            Statement findDocument(
                database,
                "SELECT id FROM document_ids WHERE name=?1");

            Statement findTerm(
                database,
                "SELECT id FROM term_ids WHERE value=?1");

            std::optional<IndexBuilder> index;
            if (buildIndexes) {
                index.emplace(database);
            }

            std::unordered_map<std::string, std::int64_t> identifiers;
            std::ifstream tokens(scratch / "tokens.txt", std::ios::binary);

            if (!tokens) {
                throw std::runtime_error(
                    "Cannot reopen temporary token streams.");
            }

            std::string line;

            while (std::getline(tokens, line)) {
                const auto separator = line.find('\t');

                if (separator == std::string::npos) {
                    throw std::runtime_error(
                        "Invalid temporary document record.");
                }

                const auto name = line.substr(0, separator);

                findDocument.reset();
                findDocument.bind(1, name);

                if (!findDocument.step()) {
                    throw std::runtime_error(
                        "Final document identifier is missing.");
                }

                const auto id = findDocument.integer(0);
                output << name << '\t' << id << '\t';

                if (options.verbose && options.traceAll) {
                    log << "[DOCUMENT] " << name << " -> " << id
                        << "\n[STREAM] ";
                }

                std::istringstream words(line.substr(separator + 1));
                std::string word;
                bool first = true;
                std::unordered_map<std::int64_t, std::int64_t> frequencies;

                while (words >> word) {
                    auto found = identifiers.find(word);

                    if (found == identifiers.end()) {
                        findTerm.reset();
                        findTerm.bind(1, word);

                        if (!findTerm.step()) {
                            throw std::runtime_error(
                                "Final term identifier is missing.");
                        }

                        if (identifiers.size() >= 20000) {
                            identifiers.clear();
                        }

                        found = identifiers.emplace(
                            word, findTerm.integer(0)).first;
                    }

                    output << (first ? "" : " ")
                           << word << ':' << found->second;

                    auto& frequency = frequencies[found->second];

                    if (frequency == std::numeric_limits<std::int64_t>::max()) {
                        throw std::overflow_error("Document term frequency exceeds its limit.");
                    }

                    ++frequency;

                    if (options.verbose && options.traceAll) {
                        log << (first ? "" : " ")
                            << word << ':' << found->second;
                    }

                    first = false;
                }

                output << '\n';
                if (index) {
                    index->addDocument(id, frequencies);
                }

                if (options.verbose && options.traceAll) {
                    log << '\n';
                }
            }

            if (tokens.bad() || !log) {
                throw std::runtime_error(
                    "Reading tokens or writing the console failed.");
            }

            if (index) {
                index->write(staged);
            }

            closeOutput(output);
        }, buildIndexes);

    log << "[DONE] " << summary.documents << " documents; "
        << summary.terms << " terms; "
        << summary.tokens << " tokens\n"
        << "Saved: " << outputs.parser.generic_u8string() << '\n'
        << outputs.terms.generic_u8string() << '\n'
        << outputs.documents.generic_u8string() << '\n';
    if (buildIndexes) {
        log << outputs.forward.generic_u8string() << '\n'
            << outputs.inverted.generic_u8string() << '\n'
            << outputs.offsets.generic_u8string() << '\n';
    }

    return summary;
}

}

// Preserve the existing document names and numbering for automatic parser runs.
RunSummary runParser(const RunOptions& options, std::ostream& log) {
    return processCorpus(options, log, false);
}

RunSummary runTextParser(const RunOptions& options, std::ostream& log) {
    if (!options.additionalInputs.empty() ||
        !std::filesystem::is_regular_file(options.input)) {
        throw std::invalid_argument("InputParser requires one regular file.");
    }
    return processCorpus(options, log, true, false);
}

// Apply the full preprocessing rules to bundled corpus files without file dialogs.
RunSummary runDefaultResource(std::ostream& log) {
    RunOptions options;
    return runParser(options, log);
}

// Select a corpus graphically, build its indexes, and look up terms.
int runInteractiveApplication(int argc, char* argv[], bool indexer) {
    try {
        RunOptions options;
        options.traceAll = false;
        std::optional<std::string> query;

        for (int index = 1; index < argc; ++index) {
            const std::string option = argv[index];

            if (option == "--help") {
                std::cout
                    << (indexer
                        ? "Select one or more files; cancel the file chooser to select a folder instead.\n"
                        : "Select one file for text parsing only.\n")
                    << "--trace | --progress\n"
                    << (indexer ? "--recursive\n--term WORD\n" : "")
                    << "--output DIRECTORY\n"
                    << "--stopwords FILE\n"
                    << "--format auto|trec|txt|csv|tsv|json|jsonl|xlsx\n"
                    << "--text-mode paragraphs|lines|document\n"
                    << "--text-field NAME (repeatable)\n"
                    << "--id-field NAME\n"
                    << "--records-path JSON_POINTER\n"
                    << "--sheet NAME\n"
                    << "--no-header\n"
                    << "Headerless column numbers start at 1. Cancel keeps existing results.\n"
                    << "Unnamed records use DOC#1, DOC#2, and so on in input order.\n"
                    << "Plain text uses one document per paragraph unless --text-mode changes it.\n"
                    << (indexer ? "After indexing, enter terms to inspect their inverted postings.\n" : "No indexes or lookup are produced.\n")
                    << "Existing document names stay unchanged; FT911-X keeps document ID X.\n";

                return 0;
            }

            if (option == "--trace") {
                options.traceAll = true;
            } else if (option == "--progress") {
                options.traceAll = false;
            } else if (option == "--recursive") {
                if (!indexer) {
                    throw std::invalid_argument("--recursive is only for the Phase 2 indexer.");
                }
                options.recursive = true;
            } else if (option == "--term") {
                if (!indexer) {
                    throw std::invalid_argument("--term is only for the Phase 2 indexer.");
                }
                query = settingValue(index, argc, argv);
            } else if (option == "--output") {
                options.output = std::filesystem::path(
                    settingValue(index, argc, argv)) / "parser_output.txt";
            } else if (option == "--stopwords") {
                options.stopwords = settingValue(index, argc, argv);
            } else if (option == "--format") {
                options.reader.format = settingValue(index, argc, argv);
            } else if (option == "--text-mode") {
                options.reader.textMode = settingValue(index, argc, argv);
            } else if (option == "--text-field") {
                options.reader.textFields.push_back(
                    settingValue(index, argc, argv));
            } else if (option == "--id-field") {
                options.reader.idField = settingValue(index, argc, argv);
            } else if (option == "--records-path") {
                options.reader.recordsPath =
                    settingValue(index, argc, argv);
            } else if (option == "--sheet") {
                options.reader.sheet = settingValue(index, argc, argv);
            } else if (option == "--no-header") {
                options.reader.hasHeader = false;
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        const auto selected = chooseRelativeInput(indexer);

        if (!selected) {
            std::cout
                << "Selection cancelled. Existing results were not changed.\n";
            return 0;
        }

        options.input = selected->front();
        options.additionalInputs.assign(selected->begin() + 1, selected->end());
        const bool selectedFolder = std::filesystem::is_directory(options.input);
        options.recursive = options.recursive || selectedFolder;

        const auto started = std::chrono::steady_clock::now();
        const auto summary = indexer
            ? runParser(options, std::cout)
            : runTextParser(options, std::cout);
        const auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started).count();
        const auto paths = OutputWriter::paths(options.output);

        if (indexer) {
            printIndexSummary(summary, paths, elapsed, std::cout);
            runTermLookup(paths, options.stopwords, query, std::cin, std::cout);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}

int runInteractiveParser(int argc, char* argv[]) {
    return runInteractiveApplication(argc, argv, false);
}

int runInteractiveIndexer(int argc, char* argv[]) {
    return runInteractiveApplication(argc, argv, true);
}

}
