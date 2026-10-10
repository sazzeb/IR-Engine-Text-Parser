#include "StreamingWorkbook.h"
#include "Sqlite.h"
#include "OutputWriter.h"

#include <miniz.h>
#include <expat.h>

#include <cstdio>
#include <exception>
#include <map>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace ir {
namespace {

// Check relationship suffixes without reading beyond either supplied string value.
bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(
            value.size() - suffix.size(),
            suffix.size(),
            suffix) == 0;
}

// Read the local XML name while ignoring its namespace prefix.
std::string local(const char* name) {
    const std::string text(name);
    const auto colon = text.find_last_of(':');

    return colon == std::string::npos
        ? text
        : text.substr(colon + 1);
}

// Find a named XML attribute without assuming a fixed order.
std::string attribute(
    const char** attributes,
    const std::string& name) {
    for (int i = 0; attributes[i]; i += 2) {
        if (local(attributes[i]) == name) {
            return attributes[i + 1];
        }
    }

    return {};
}

// Append worksheet text while keeping each record within its limit.
void add(
    std::string& value,
    const std::string& text,
    std::size_t limit) {
    if (text.size() > limit - value.size()) {
        throw std::runtime_error("Excel cell exceeds record limit");
    }

    value += text;
}

// Resolve workbook member paths without allowing access outside the archive.
std::string resolve(
    const std::string& base,
    const std::string& target) {
    auto path = target.empty() || target.front() != '/'
        ? std::filesystem::path(base).parent_path() / target
        : std::filesystem::path(target.substr(1));

    path = path.lexically_normal();
    const auto result = path.generic_string();

    if (result.empty() || result == ".." ||
        result.rfind("../", 0) == 0) {
        throw std::runtime_error(
            "Invalid workbook relationship path");
    }

    return result;
}

// Read compressed workbook members and stream their XML events safely.
class ZipXml {
    mz_zip_archive archive_{};
    std::FILE* file_ = nullptr;

public:
    // Open workbook bytes using the correct filename encoding for this system.
    explicit ZipXml(const std::filesystem::path& path) {
#ifdef _WIN32
        file_ = _wfopen(path.c_str(), L"rb");
#else
        file_ = std::fopen(path.c_str(), "rb");
#endif

        if (!file_) {
            throw std::runtime_error("Cannot open workbook");
        }

        if (!mz_zip_reader_init_cfile(&archive_, file_, 0, 0)) {
            std::fclose(file_);
            throw std::runtime_error("Invalid XLSX ZIP archive");
        }
    }

    // Close the archive and its file handle after workbook processing.
    ~ZipXml() {
        mz_zip_reader_end(&archive_);
        std::fclose(file_);
    }

    // Hold XML handlers and preserve failures across the C callback boundary.
    struct Events {
        std::function<void(const std::string&, const char**)> start;
        std::function<void(const std::string&)> end;
        std::function<void(const std::string&)> text;

        XML_Parser parser = nullptr;
        std::exception_ptr error;
        int depth = 0;

        // Capture callback failures before they can cross the XML library boundary.
        template<class Function>
        void safely(Function action) {
            if (error) {
                return;
            }

            try {
                action();
            } catch (...) {
                error = std::current_exception();
                XML_StopParser(parser, XML_FALSE);
            }
        }
    };

    // Stream one archive member through the configured XML event handlers.
    void read(const std::string& member, Events& events) {
        events.parser = XML_ParserCreate(nullptr);

        if (!events.parser) {
            throw std::runtime_error("Cannot create XML parser");
        }

        const auto cleanup = std::unique_ptr<
            std::remove_pointer_t<XML_Parser>,
            decltype(&XML_ParserFree)
        >(events.parser, XML_ParserFree);

        XML_SetUserData(events.parser, &events);

        XML_SetElementHandler(
            events.parser,
            // Check nesting before forwarding each opening tag to workbook handlers.
            [](void* data, const char* name, const char** attrs) {
                auto& event = *static_cast<Events*>(data);

                // Run the handler while keeping failures inside the XML callback.
                event.safely([&] {
                    if (++event.depth > 64) {
                        throw std::runtime_error(
                            "XML nesting exceeds limit");
                    }

                    if (event.start) {
                        event.start(local(name), attrs);
                    }
                });
            },
            // Finish each element while preserving any failure from its handler.
            [](void* data, const char* name) {
                auto& event = *static_cast<Events*>(data);

                // Run the handler while keeping failures inside the XML callback.
                event.safely([&] {
                    if (event.end) {
                        event.end(local(name));
                    }

                    --event.depth;
                });
            });

        XML_SetCharacterDataHandler(
            events.parser,
            // Forward decoded text while keeping exceptions inside the callback boundary.
            [](void* data, const char* text, int size) {
                auto& event = *static_cast<Events*>(data);

                // Run the handler while keeping failures inside the XML callback.
                event.safely([&] {
                    if (event.text) {
                        event.text(std::string(
                            text, static_cast<std::size_t>(size)));
                    }
                });
            });

        XML_SetStartDoctypeDeclHandler(
            events.parser,
            // Reject document type declarations before they can expand external content.
            [](void* data, const char*, const char*, const char*, int) {
                auto& event = *static_cast<Events*>(data);

                // Stop parsing safely when unsupported document type declarations are found.
                event.safely([] {
                    throw std::runtime_error(
                        "XML document types are not supported");
                });
            });

        // Feed bounded byte chunks into Expat without loading complete worksheet files.
        const auto callback = [](
            void* data,
            mz_uint64,
            const void* bytes,
            size_t count) -> size_t {
            auto& event = *static_cast<Events*>(data);
            const char* cursor = static_cast<const char*>(bytes);
            size_t remaining = count;

            while (remaining) {
                const int size = static_cast<int>(
                    remaining > 65536 ? 65536 : remaining);

                if (XML_Parse(
                    event.parser, cursor, size, XML_FALSE) ==
                    XML_STATUS_ERROR) {
                    return 0;
                }

                cursor += size;
                remaining -= static_cast<size_t>(size);
            }

            return count;
        };

        const bool extracted = mz_zip_reader_extract_file_to_callback(
            &archive_, member.c_str(), callback, &events, 0);

        if (events.error) {
            std::rethrow_exception(events.error);
        }

        if (!extracted ||
            XML_Parse(events.parser, nullptr, 0, XML_TRUE) ==
                XML_STATUS_ERROR) {
            throw std::runtime_error(
                "Cannot read workbook XML: " + member);
        }
    }
};

}

// Read worksheet rows while keeping assigned shared string identifiers one based.
void readWorkbook(
    const std::filesystem::path& path,
    const std::string& requestedSheet,
    const std::filesystem::path& scratch,
    std::size_t limit,
    const std::function<void(const std::vector<std::string>&)>& rowHandler) {
    requireRelativePath(path);
    requireRelativePath(scratch);

    ZipXml zip(path);
    std::string workbook;
    ZipXml::Events root;

    // Find the workbook part while rejecting external root document relationships.
    root.start = [&](const std::string& tag, const char** attrs) {
        if (tag == "Relationship" &&
            endsWith(attribute(attrs, "Type"), "/officeDocument")) {
            if (attribute(attrs, "TargetMode") == "External") {
                throw std::runtime_error(
                    "External workbook relationship");
            }

            workbook = resolve("root", attribute(attrs, "Target"));
        }
    };

    zip.read("_rels/.rels", root);

    if (workbook.empty()) {
        throw std::runtime_error("Workbook relationship missing");
    }

    std::map<std::string, std::string> targets;
    std::string strings;

    const auto workbookPath = std::filesystem::path(workbook);
    const auto relationships = (
        workbookPath.parent_path() /
        "_rels" /
        (workbookPath.filename().string() + ".rels")
    ).generic_string();

    ZipXml::Events relations;

    // Collect supported workbook relationships and locate the shared string table.
    relations.start = [&](const std::string& tag, const char** attrs) {
        if (tag != "Relationship" ||
            attribute(attrs, "TargetMode") == "External") {
            return;
        }

        if (targets.size() >= 10000) {
            throw std::runtime_error(
                "Too many workbook relationships");
        }

        const auto target = resolve(
            workbook, attribute(attrs, "Target"));

        targets[attribute(attrs, "Id")] = target;

        if (endsWith(attribute(attrs, "Type"), "/sharedStrings")) {
            strings = target;
        }
    };

    zip.read(relationships, relations);

    std::string sheetId;
    ZipXml::Events sheets;

    // Select the requested worksheet or use the first available worksheet.
    sheets.start = [&](const std::string& tag, const char** attrs) {
        if (tag == "sheet" && sheetId.empty() &&
            (requestedSheet.empty() ||
             attribute(attrs, "name") == requestedSheet)) {
            sheetId = attribute(attrs, "id");
        }
    };

    zip.read(workbook, sheets);

    if (!targets.count(sheetId)) {
        throw std::runtime_error("Selected worksheet not found");
    }

    Database database(scratch / "excel-strings.sqlite");

    database.execute(
        "CREATE TABLE IF NOT EXISTS strings("
        "id INTEGER PRIMARY KEY,value TEXT NOT NULL);"
        "DELETE FROM strings;"
        "BEGIN;");

    Statement insert(
        database,
        "INSERT INTO strings VALUES(?1,?2)");

    Statement lookup(
        database,
        "SELECT value FROM strings WHERE id=?1");

    std::int64_t stringId = 1;

    if (!strings.empty()) {
        std::string value;
        bool capture = false;
        int phonetic = 0;

        ZipXml::Events shared;

        // Start collecting displayed shared string text while skipping phonetic guides.
        shared.start = [&](const std::string& tag, const char**) {
            if (tag == "si") {
                value.clear();
            }

            if (tag == "rPh") {
                ++phonetic;
            }

            if (tag == "t" && !phonetic) {
                capture = true;
            }
        };

        // Append shared string text within the configured document size limit.
        shared.text = [&](const std::string& text) {
            if (capture) {
                add(value, text, limit);
            }
        };

        // Store each shared string with a positive internal database identifier.
        shared.end = [&](const std::string& tag) {
            if (tag == "t") {
                capture = false;
            }

            if (tag == "rPh") {
                --phonetic;
            }

            if (tag == "si") {
                insert.reset();
                insert.bind(1, stringId++);
                insert.bind(2, value);
                insert.step();

                if (stringId % 10000 == 0) {
                    database.execute("COMMIT; BEGIN;");
                }
            }
        };

        zip.read(strings, shared);
    }

    database.execute("COMMIT;");

    std::vector<std::string> row;
    std::string value;
    std::string type;

    std::size_t column = 0;
    std::size_t rowBytes = 0;
    std::size_t width = 0;

    bool capture = false;
    int phonetic = 0;

    ZipXml::Events cells;

    // Validate cell positions and reject formulas before reading worksheet values.
    cells.start = [&](const std::string& tag, const char** attrs) {
        if (tag == "row") {
            row.clear();
            rowBytes = 0;
        }

        if (tag == "c") {
            value.clear();
            type = attribute(attrs, "t");

            const auto reference = attribute(attrs, "r");
            column = 0;

            for (char ch : reference) {
                if (ch < 'A' || ch > 'Z') {
                    break;
                }

                column = column * 26 +
                    static_cast<std::size_t>(ch - 'A' + 1);

                if (column > 4096) {
                    throw std::runtime_error(
                        "Too many worksheet columns");
                }
            }

            if (!column || column <= row.size()) {
                throw std::runtime_error(
                    "Invalid cell order/reference");
            }

            row.resize(column);
        }

        if (tag == "f") {
            throw std::runtime_error(
                "Formula found; export values only");
        }

        if (tag == "rPh") {
            ++phonetic;
        }

        if (tag == "v" || (tag == "t" && !phonetic)) {
            capture = true;
        }
    };

    // Collect visible cell text without exceeding the allowed record size.
    cells.text = [&](const std::string& text) {
        if (capture) {
            add(value, text, limit);
        }
    };

    // Resolve shared strings and send each complete worksheet row onward.
    cells.end = [&](const std::string& tag) {
        if (tag == "v" || tag == "t") {
            capture = false;
        }

        if (tag == "rPh") {
            --phonetic;
        }

        if (tag == "c") {
            if (type == "e") {
                throw std::runtime_error("Excel error cell");
            }

            if (type == "s") {
                if (value.empty() ||
                    value.find_first_not_of("0123456789") !=
                        std::string::npos) {
                    throw std::runtime_error(
                        "Invalid shared string index");
                }

                lookup.reset();
                lookup.bind(
                    1,
                    // Convert the workbook format offset into our positive database identifier.
                    ([&] {
                        const auto sourceIndex = std::stoll(value);

                        if (sourceIndex ==
                            std::numeric_limits<std::int64_t>::max()) {
                            throw std::runtime_error(
                                "Shared string index exceeds the supported range.");
                        }

                        return static_cast<std::int64_t>(
                            sourceIndex + 1);
                    })());

                if (!lookup.step()) {
                    throw std::runtime_error(
                        "Shared string index missing");
                }

                value = lookup.text(0);
            }

            if (value.size() > limit - rowBytes) {
                throw std::runtime_error(
                    "Worksheet row exceeds limit");
            }

            rowBytes += value.size();
            row.at(column - 1) = value;
        }

        if (tag == "row") {
            if (!width && !row.empty()) {
                width = row.size();
            }

            if (row.size() < width) {
                row.resize(width);
            }

            rowHandler(row);
        }
    };

    zip.read(targets.at(sheetId), cells);
}

}