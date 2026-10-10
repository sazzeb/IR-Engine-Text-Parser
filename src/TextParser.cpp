#include "TextParser.h"
#include "OutputWriter.h"

#include <fstream>
#include <stdexcept>

namespace ir {
namespace {

// Check English letters without relying on machine specific locale settings.
bool isLetter(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z');
}

// Recognize decimal digits before deciding whether to reject a token.
bool isDigit(unsigned char ch) {
    return ch >= '0' && ch <= '9';
}

// Convert English uppercase letters while leaving other bytes unchanged safely.
char lowercase(unsigned char ch) {
    return static_cast<char>(
        ch >= 'A' && ch <= 'Z'
            ? ch + ('a' - 'A')
            : ch);
}

}

// Load lowercase stopwords and report read failures before processing any corpus.
TextParser::TextParser(std::istream& input) {
    std::string word;

    while (input >> word) {
        if (word.rfind("\xEF\xBB\xBF", 0) == 0) {
            word.erase(0, 3);
        }

        if (word.empty()) {
            continue;
        }

        for (char& ch : word) {
            ch = lowercase(static_cast<unsigned char>(ch));
        }

        stopwords_.insert(word);
    }

    if (input.bad() || (input.fail() && !input.eof())) {
        throw std::runtime_error("Cannot read stopwords");
    }
}

// Open the stoplist through a checked relative path before parsing.
TextParser TextParser::fromStopwordFile(
    const std::filesystem::path& path) {
    std::ifstream input(requireRelativePath(path));

    if (!input) {
        throw std::runtime_error(
            "Cannot open stopwords: " + path.string());
    }

    return TextParser(input);
}

// Apply token rules before passing every accepted word to the supplied stemmer.
std::vector<std::string> TextParser::parseText(
    std::string_view text,
    const TokenObserver& observer) const {
    std::vector<std::string> result;
    std::string token;
    std::string original;
    bool hasDigit = false;

    // Finish each candidate once and record why it was kept or removed.
    const auto finishToken = [&] {
        if (token.empty()) {
            return;
        }

        auto disposition = TokenDisposition::accepted;
        std::string stem;

        if (hasDigit) {
            disposition = TokenDisposition::containsDigit;
        } else if (stopwords_.find(token) != stopwords_.end()) {
            disposition = TokenDisposition::stopword;
        } else {
            stem = stemmer_.stemWord(token);
            result.push_back(stem);
        }

        if (observer) {
            observer({
                original,
                token,
                stem,
                disposition
            });
        }

        token.clear();
        original.clear();
        hasDigit = false;
    };

    for (unsigned char ch : text) {
        if (isLetter(ch) || isDigit(ch)) {
            if (token.size() >= 4096) {
                throw std::runtime_error("Token exceeds 4096 bytes");
            }

            if (observer) {
                original.push_back(static_cast<char>(ch));
            }

            token.push_back(lowercase(ch));
            hasDigit = hasDigit || isDigit(ch);
        } else {
            finishToken();
        }
    }

    finishToken();
    return result;
}

}