#include "IndexLookup.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace ir {

IndexLookup::IndexLookup(
    const OutputPaths& paths,
    const std::filesystem::path& stopwords)
    : paths_(paths), parser_(TextParser::fromStopwordFile(stopwords)) {
    std::ifstream terms(paths_.terms, std::ios::binary);
    std::ifstream offsets(paths_.offsets, std::ios::binary);

    if (!terms || !offsets) {
        throw std::runtime_error("Cannot open index lookup files.");
    }

    std::string line;

    while (std::getline(terms, line)) {
        const auto separator = line.find('\t');

        if (separator == std::string::npos) {
            throw std::runtime_error("Invalid term dictionary entry.");
        }

        std::istringstream identifier(line.substr(separator + 1));
        std::int64_t termId = 0;

        if (!(identifier >> termId) || termId <= 0 ||
            !(identifier >> std::ws).eof()) {
            throw std::runtime_error("Invalid term dictionary identifier.");
        }

        termIds_.emplace(line.substr(0, separator), termId);
    }

    if (terms.bad()) {
        throw std::runtime_error("Cannot read the term dictionary.");
    }

    while (std::getline(offsets, line)) {
        std::istringstream entry(line);
        std::int64_t termId = 0;
        std::streamoff offset = -1;

        if (!(entry >> termId >> offset) || termId <= 0 || offset < 0 ||
            !(entry >> std::ws).eof()) {
            throw std::runtime_error("Invalid inverted index offset.");
        }

        offsets_.emplace(termId, offset);
    }

    if (offsets.bad()) {
        throw std::runtime_error("Cannot read inverted index offsets.");
    }
}

bool IndexLookup::printTerm(
    const std::string& input,
    std::ostream& output) const {
    const auto tokens = parser_.parseText(input);

    if (tokens.empty()) {
        output << "No indexed term for: " << input << '\n';
        return false;
    }

    if (tokens.size() != 1) {
        throw std::invalid_argument("Enter one term at a time.");
    }

    const auto term = termIds_.find(tokens.front());

    if (term == termIds_.end()) {
        output << "Term not found: " << tokens.front() << '\n';
        return false;
    }

    const auto offset = offsets_.find(term->second);

    if (offset == offsets_.end()) {
        throw std::runtime_error("The term has no inverted index entry.");
    }

    std::ifstream inverted(paths_.inverted, std::ios::binary);

    if (!inverted || !inverted.seekg(offset->second)) {
        throw std::runtime_error("Cannot seek to the term's index entry.");
    }

    std::string line;

    if (!std::getline(inverted, line)) {
        throw std::runtime_error("Cannot read the term's index entry.");
    }

    std::istringstream postings(line);
    std::int64_t termId = 0;
    char separator = 0;

    if (!(postings >> termId >> separator) ||
        termId != term->second || separator != ':') {
        throw std::runtime_error("Invalid inverted index term entry.");
    }

    output << "Term: " << tokens.front() << " (ID " << termId << ")\n";

    std::int64_t documentId = 0;
    bool foundPosting = false;

    while (postings >> documentId) {
        std::int64_t frequency = 0;
        char colon = 0;
        char semicolon = 0;

        if (!(postings >> colon >> frequency >> semicolon) ||
            documentId <= 0 || colon != ':' ||
            frequency <= 0 || semicolon != ';') {
            throw std::runtime_error("Invalid inverted index posting.");
        }

        output << "Document " << documentId << ": " << frequency << '\n';
        foundPosting = true;
    }

    if (!foundPosting || !postings.eof()) {
        throw std::runtime_error("Incomplete inverted index entry.");
    }

    return true;
}

}
