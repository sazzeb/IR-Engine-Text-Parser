#include "CorpusReader.h"
#include "OutputWriter.h"
#include "JsonRecords.h"
#include "StreamingWorkbook.h"
#include "TrecReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace ir {
namespace {

using Json = nlohmann::json;

// Recognize empty text without treating ordinary words as missing values.
bool blank(const std::string& text) {
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

// Append input safely while enforcing the configured record size limit.
void append(std::string& text, char ch, std::size_t limit) {
    if (text.size() >= limit) {
        throw std::runtime_error("Record exceeds size limit");
    }

    text.push_back(ch);
}

// Read bounded lines so oversized records cannot grow without limit.
bool readLine(
    std::istream& input,
    std::string& line,
    std::size_t limit) {
    line.clear();
    char ch;

    while (input.get(ch)) {
        if (ch == '\n') {
            return true;
        }

        append(line, ch, limit);
    }

    if (input.bad()) {
        throw std::runtime_error("Input read failed");
    }

    return !line.empty();
}

// Keep document names readable while escaping spaces and control bytes.
std::string escapedName(const std::string& value) {
    const char* hex = "0123456789ABCDEF";
    std::string result;

    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') ||
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.') {
            result.push_back(static_cast<char>(ch));
        } else {
            result += '%';
            result += hex[ch >> 4];
            result += hex[ch & 15];
        }
    }

    return result;
}

// Parse JSON records while rejecting duplicate keys and excessive nesting.
Json parseJson(const std::string& text) {
    std::vector<std::set<std::string>> keys;

    return Json::parse(
        text,
        // Check each parsed object before accepting its keys and values.
        [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 64) {
                throw std::runtime_error(
                    "JSON nesting exceeds 64 levels");
            }

            if (event == Json::parse_event_t::object_start) {
                keys.emplace_back();
            }

            if (event == Json::parse_event_t::key &&
                !keys.back().insert(value.get<std::string>()).second) {
                throw std::runtime_error("Duplicate JSON key");
            }

            if (event == Json::parse_event_t::object_end) {
                keys.pop_back();
            }

            return true;
        });
}

// Find selected fields by name or a standard JSON pointer.
const Json& field(const Json& record, const std::string& name) {
    if (!name.empty() && name.front() == '/') {
        return record.at(Json::json_pointer(name));
    }

    return record.at(name);
}

// Choose document text and names without assigning any numerical identifiers.
Document makeDocument(
    const Json& record,
    const ReadOptions& options,
    const std::string& generatedName) {
    Document document{generatedName, {}};

    if (record.is_string()) {
        document.text = record.get<std::string>();
        return document;
    }

    if (!record.is_object()) {
        throw std::runtime_error(
            "Expected a document object or string");
    }

    auto fields = options.textFields;

    if (fields.empty()) {
        for (const auto* name : {
            "text", "contents", "passage", "passage_text",
            "content", "description", "abstract"
        }) {
            if (record.contains(name)) {
                fields.push_back(name);
                break;
            }
        }

        if (fields.empty()) {
            throw std::runtime_error(
                "No text field found; specify text fields");
        }

        if (record.contains("title")) {
            fields.insert(fields.begin(), "title");
        }
    }

    for (const auto& name : fields) {
        const auto& value = field(record, name);

        if (!value.is_string()) {
            throw std::runtime_error(
                "Text field must be a string: " + name);
        }

        if (!document.text.empty()) {
            document.text += '\n';
        }

        document.text += value.get<std::string>();

        if (document.text.size() > options.maxRecordBytes) {
            throw std::runtime_error("Document exceeds size limit");
        }
    }

    std::string idField = options.idField;

    if (idField.empty()) {
        for (const auto* name : {
            "_id", "doc_id", "id", "docno", "pid", "passage_id"
        }) {
            if (record.contains(name)) {
                idField = name;
                break;
            }
        }
    }

    if (!idField.empty()) {
        const auto& id = field(record, idField);

        if (id.is_string()) {
            document.name = id.get<std::string>();
        } else if (id.is_number_integer()) {
            document.name = id.dump();
        } else {
            throw std::runtime_error(
                "Document ID must be a string or integer");
        }

        if (blank(document.name)) {
            throw std::runtime_error("Empty document ID");
        }

        document.name = escapedName(document.name);
    }

    return document;
}

// Read quoted table fields while checking delimiters and record limits.
bool readRow(
    std::istream& input,
    char separator,
    std::vector<std::string>& row,
    std::size_t limit) {
    row.clear();

    std::string value;
    bool quoted = false;
    bool closed = false;
    bool consumed = false;

    std::size_t bytes = 0;
    char ch;

    while (input.get(ch)) {
        consumed = true;

        if (++bytes > limit) {
            throw std::runtime_error("Table row exceeds size limit");
        }

        if (quoted) {
            if (ch == '"') {
                if (input.peek() == '"') {
                    input.get(ch);

                    if (++bytes > limit) {
                        throw std::runtime_error(
                            "Table row exceeds size limit");
                    }

                    value += '"';
                } else {
                    quoted = false;
                    closed = true;
                }
            } else {
                value += ch;
            }
        } else if (
            ch == separator || ch == '\n' || ch == '\r') {
            row.push_back(value);

            if (row.size() > 4096) {
                throw std::runtime_error("Too many table columns");
            }

            value.clear();
            closed = false;

            if (ch != separator) {
                if (ch == '\r' && input.peek() == '\n') {
                    input.get(ch);
                }

                return true;
            }
        } else if (closed) {
            throw std::runtime_error(
                "Unexpected character after closing quote");
        } else if (ch == '"') {
            if (!value.empty()) {
                throw std::runtime_error(
                    "Quote inside unquoted field");
            }

            quoted = true;
        } else {
            value += ch;
        }
    }

    if (input.bad()) {
        throw std::runtime_error("Input read failed");
    }

    if (quoted) {
        throw std::runtime_error("Unclosed quoted field");
    }

    if (consumed) {
        row.push_back(value);

        if (row.size() > 4096) {
            throw std::runtime_error("Too many table columns.");
        }
    }

    return consumed;
}

}

// Read supported formats and pass each complete document to the caller.
void CorpusReader::readFile(
    const std::filesystem::path& path,
    const ReadOptions& options,
    const Handler& handler) const {
    requireRelativePath(path);

    if (!options.scratch.empty()) {
        requireRelativePath(options.scratch);
    }

    if (!handler || options.maxRecordBytes == 0) {
        throw std::invalid_argument(
            "A handler and positive record limit are required.");
    }

    std::size_t count = 0;

    // Apply record limits before passing each document to the caller.
    const auto send = [&](Document document) {
        if (document.text.size() > options.maxRecordBytes) {
            throw std::runtime_error("Document exceeds size limit");
        }

        handler(document);
        ++count;
    };

    // Give unnamed documents a source label and positive record number.
    const auto name = [&] {
        const auto source = options.sourceName.empty()
            ? path.filename().u8string()
            : options.sourceName;

        return escapedName(source) + "#" + std::to_string(count + 1);
    };

    try {
        std::ifstream input(path, std::ios::binary);

        if (!input) {
            throw std::runtime_error("Cannot open input");
        }

        std::string format = options.format;

        if (format == "auto") {
            format = path.extension().string();

            for (char& ch : format) {
                if (ch >= 'A' && ch <= 'Z') {
                    ch += 'a' - 'A';
                }
            }

            if (!format.empty()) {
                format.erase(0, 1);
            }

            if (format.empty() || format == "txt") {
                std::string window;
                char character;
                format = "txt";

                while (input.get(character)) {
                    if (character >= 'a' && character <= 'z') {
                        character -= 'a' - 'A';
                    }

                    window += character;

                    if (window.size() > 5) {
                        window.erase(0, 1);
                    }

                    if (window.size() == 5 &&
                        window.compare(0, 4, "<DOC") == 0 &&
                        std::string("> \t\r\n").find(window[4]) !=
                            std::string::npos) {
                        format = "trec";
                        break;
                    }
                }

                if (input.bad()) {
                    throw std::runtime_error(
                        "Cannot detect the input format.");
                }

                input.clear();
                input.seekg(0);

                if (!input) {
                    throw std::runtime_error(
                        "Cannot rewind the input file.");
                }
            }
        }

        if (format == "sgm" || format == "sgml") {
            format = "trec";
        }

        if (format == "ndjson") {
            format = "jsonl";
        }

        if (format != "xlsx") {
            char bom[3]{};
            input.read(bom, 3);

            const bool utf8Bom =
                input.gcount() == 3 &&
                std::string(bom, 3) == "\xEF\xBB\xBF";

            input.clear();
            input.seekg(utf8Bom ? 3 : 0);
        }

        std::vector<std::string> headers;
        bool firstRow = true;

        // Map table columns to fields using one based column labels.
        const auto tableRow = [&](const std::vector<std::string>& row) {
            if (std::all_of(row.begin(), row.end(), blank)) {
                return;
            }

            if (firstRow) {
                firstRow = false;

                if (options.hasHeader) {
                    headers = row;
                    std::set<std::string> unique;

                    for (const auto& header : headers) {
                        if (blank(header) ||
                            !unique.insert(header).second) {
                            throw std::runtime_error(
                                "Empty or duplicate column header");
                        }
                    }

                    return;
                }

                for (std::size_t index = 0; index < row.size(); ++index) {
                    headers.push_back(std::to_string(index + 1));
                }

                if (options.textFields.empty()) {
                    throw std::runtime_error(
                        "Headerless tables require text column numbers");
                }
            }

            if (row.size() != headers.size()) {
                throw std::runtime_error(
                    "Inconsistent table column count");
            }

            Json record = Json::object();

            for (std::size_t index = 0; index < row.size(); ++index) {
                record[headers[index]] = row[index];
            }

            send(makeDocument(record, options, name()));
        };

        if (format == "xlsx") {
            if (options.scratch.empty()) {
                throw std::runtime_error(
                    "XLSX requires a scratch directory");
            }

            readWorkbook(
                path,
                options.sheet,
                options.scratch,
                options.maxRecordBytes,
                tableRow);
        } else if (format == "csv" || format == "tsv") {
            std::vector<std::string> row;

            while (readRow(
                input,
                format == "csv" ? ',' : '\t',
                row,
                options.maxRecordBytes)) {
                tableRow(row);
            }
        } else if (format == "jsonl") {
            std::string line;
            std::size_t lineNumber = 0;

            while (readLine(input, line, options.maxRecordBytes)) {
                ++lineNumber;

                if (blank(line)) {
                    continue;
                }

                try {
                    send(makeDocument(
                        parseJson(line), options, name()));
                } catch (const std::exception& error) {
                    throw std::runtime_error(
                        "Line " + std::to_string(lineNumber) +
                        ": " + error.what());
                }
            }
        } else if (format == "json") {
            readJsonRecords(
                input,
                options.recordsPath,
                options.maxRecordBytes,
                // Convert each streamed JSON value into a single document record.
                [&](const Json& record) {
                    send(makeDocument(record, options, name()));
                });
        } else if (format == "trec") {
            TrecReader().read(input, send, options.maxRecordBytes);
        } else if (format == "txt") {
            if (options.textMode != "paragraphs" &&
                options.textMode != "lines" &&
                options.textMode != "document") {
                throw std::runtime_error("Unknown text mode");
            }

            std::string line;
            std::string text;

            // Send the completed text block and prepare for another document.
            const auto finish = [&] {
                if (!text.empty()) {
                    send({name(), text});
                    text.clear();
                }
            };

            while (readLine(input, line, options.maxRecordBytes)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                if (options.textMode == "lines") {
                    if (!blank(line)) {
                        send({name(), line});
                    }
                } else if (
                    options.textMode == "paragraphs" && blank(line)) {
                    finish();
                } else {
                    for (char ch : line) {
                        append(text, ch, options.maxRecordBytes);
                    }

                    append(text, '\n', options.maxRecordBytes);
                }
            }

            finish();
        } else {
            throw std::runtime_error(
                "Unsupported format: " + format +
                "; Excel input must be .xlsx");
        }

        if (count == 0) {
            throw std::runtime_error("No documents found");
        }
    } catch (const std::exception& error) {
        throw std::runtime_error(
            path.u8string() + ": " + error.what());
    }
}

}