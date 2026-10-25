#include "TrecReader.h"
#include "Dictionary.h"
#include "OutputWriter.h"

#include <fstream>
#include <stdexcept>

namespace ir {
namespace {

// Remove surrounding whitespace while keeping the document identifier itself unchanged.
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    return text.substr(
        first,
        text.find_last_not_of(" \t\r\n") - first + 1);
}

// Read complete markup while allowing quoted attributes and ordinary SGML comments.
std::string readMarkup(std::istream& input) {
    std::string tag;
    char character;
    char quote = 0;

    while (input.get(character)) {
        tag += character;

        if (tag.size() > 4096) {
            throw std::runtime_error("TREC markup exceeds its size limit.");
        }

        if (tag.rfind("!--", 0) == 0) {
            if (tag.size() >= 5 &&
                tag.compare(tag.size() - 3, 3, "-->") == 0) {
                return tag;
            }

            continue;
        }

        if (quote != 0) {
            if (character == quote) {
                quote = 0;
            }
        } else if (character == '\'' || character == '"') {
            quote = character;
        } else if (character == '>') {
            return tag;
        }
    }

    throw std::runtime_error("Unclosed TREC markup tag.");
}

// Decode text entities without adding markup names to the term dictionary.
std::string decodeEntities(const std::string& text) {
    std::string decoded;

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '&') {
            decoded += text[index];
            continue;
        }

        const auto end = text.find(';', index + 1);

        if (end == std::string::npos || end - index > 16) {
            decoded += ' ';
            continue;
        }

        const auto entity = text.substr(index + 1, end - index - 1);

        if (entity == "amp") {
            decoded += '&';
        } else if (entity == "lt") {
            decoded += '<';
        } else if (entity == "gt") {
            decoded += '>';
        } else if (entity == "quot") {
            decoded += '"';
        } else if (entity == "apos") {
            decoded += '\'';
        } else if (!entity.empty() && entity.front() == '#') {
            unsigned long value = 0;

            try {
                const bool hexadecimal =
                    entity.size() > 2 &&
                    (entity[1] == 'x' || entity[1] == 'X');

                const auto digits = entity.substr(hexadecimal ? 2 : 1);
                std::size_t used = 0;

                value = std::stoul(
                    digits, &used, hexadecimal ? 16 : 10);

                if (used != digits.size()) {
                    value = 0;
                }
            } catch (const std::exception&) {
                value = 0;
            }

            decoded += value > 0 && value < 128
                ? static_cast<char>(value)
                : ' ';
        } else {
            decoded += ' ';
        }

        index = end;
    }

    return decoded;
}

}

// Preserve document boundaries and select content fields from the TREC stream.
void TrecReader::read(
    std::istream& input,
    const DocumentHandler& handler,
    std::size_t recordLimit) const {
    if (!input || !handler || recordLimit == 0) {
        throw std::invalid_argument("Invalid TREC reader settings.");
    }

    Document document;
    bool insideDocument = false;
    bool insideName = false;
    bool nameSeen = false;
    bool contentSeen = false;

    std::string contentTag;
    std::size_t recordBytes = 0;
    std::size_t documents = 0;
    char character;

    while (input.get(character)) {
        if (insideDocument && ++recordBytes > recordLimit) {
            throw std::runtime_error(
                "TREC document exceeds the size limit.");
        }

        if (character != '<') {
            if (insideName) {
                document.name += character;
            } else if (!contentTag.empty()) {
                document.text += character;
            } else if (
                !insideDocument &&
                std::string(" \t\r\n").find(character) ==
                    std::string::npos) {
                throw std::runtime_error(
                    "Unexpected text outside a TREC document.");
            }

            continue;
        }

        const auto markup = readMarkup(input);

        if (insideDocument) {
            recordBytes += markup.size();

            if (recordBytes > recordLimit) {
                throw std::runtime_error(
                    "TREC document exceeds the size limit.");
            }
        }

        if (markup.rfind("!--", 0) == 0 ||
            markup.front() == '?' || markup.front() == '!') {
            continue;
        }

        auto tag = trim(markup.substr(0, markup.size() - 1));

        if (tag.empty()) {
            throw std::runtime_error("Empty TREC markup tag.");
        }

        const bool closing = tag.front() == '/';
        const bool selfClosing = tag.back() == '/';

        if (closing) {
            tag.erase(0, 1);
        }

        if (selfClosing && !tag.empty()) {
            tag.pop_back();
        }

        tag = trim(tag);
        tag = tag.substr(0, tag.find_first_of(" \t\r\n"));

        for (char& letter : tag) {
            if (letter >= 'a' && letter <= 'z') {
                letter -= 'a' - 'A';
            }
        }

        if (tag == "DOC") {
            if (selfClosing) {
                throw std::runtime_error(
                    "A DOC record cannot be empty markup.");
            }

            if (!closing) {
                if (insideDocument) {
                    throw std::runtime_error(
                        "Nested DOC records are invalid.");
                }

                insideDocument = true;
                document = {};
                nameSeen = contentSeen = false;
                recordBytes = markup.size() + 1;
            } else {
                if (!insideDocument || insideName ||
                    !contentTag.empty() || !nameSeen || !contentSeen) {
                    throw std::runtime_error(
                        "Incomplete TREC document or content field.");
                }

                document.name = trim(decodeEntities(document.name));
                Dictionary::documentNumber(document.name);
                document.text = decodeEntities(document.text);

                handler(document);
                ++documents;
                insideDocument = false;
            }
        } else if (tag == "DOCNO") {
            if (!insideDocument || selfClosing || !contentTag.empty()) {
                throw std::runtime_error("Invalid DOCNO field.");
            }

            if (!closing) {
                if (nameSeen || insideName) {
                    throw std::runtime_error("Repeated DOCNO field.");
                }

                insideName = true;
                nameSeen = true;
            } else {
                if (!insideName) {
                    throw std::runtime_error(
                        "Unexpected closing DOCNO tag.");
                }

                insideName = false;
            }
        } else if (tag == "TEXT") {
            if (!insideDocument || insideName) {
                throw std::runtime_error(
                    "Content field outside its document.");
            }

            if (closing) {
                if (contentTag != tag) {
                    throw std::runtime_error(
                        "Mismatched TREC content tags.");
                }

                contentTag.clear();
                document.text += '\n';
            } else {
                if (!contentTag.empty()) {
                    throw std::runtime_error(
                        "Nested TREC content fields.");
                }

                contentSeen = true;

                if (!selfClosing) {
                    contentTag = tag;
                }
            }
        } else {
            if (insideName) {
                throw std::runtime_error(
                    "Markup is not allowed inside DOCNO.");
            }

            if (!contentTag.empty()) {
                document.text += ' ';
            }
        }
    }

    if (input.bad()) {
        throw std::runtime_error("Cannot read the TREC stream.");
    }

    if (insideDocument) {
        throw std::runtime_error(
            "The last TREC document is incomplete.");
    }

    if (documents == 0) {
        throw std::runtime_error("No TREC documents were found.");
    }
}

// Read one corpus file through a checked relative filesystem path.
void TrecReader::readFile(
    const std::filesystem::path& path,
    const DocumentHandler& handler) const {
    const auto relative = requireRelativePath(path);
    std::ifstream input(relative, std::ios::binary);

    if (!input) {
        throw std::runtime_error(
            "Cannot open corpus: " + relative.u8string());
    }

    read(input, handler);
}

}
