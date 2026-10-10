#pragma once

#include "PorterStemmer.h"

#include <filesystem>
#include <functional>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace ir {

enum class TokenDisposition {
    accepted,
    stopword,
    containsDigit
};

struct TokenEvent {
    std::string original;
    std::string normalized;
    std::string stem;
    TokenDisposition disposition;
};

using TokenObserver = std::function<void(const TokenEvent&)>;

class TextParser {
public:
    explicit TextParser(std::istream& stopwordInput);

    static TextParser fromStopwordFile(
        const std::filesystem::path& path
    );

    std::size_t stopwordCount() const noexcept {
        return stopwords_.size();
    }

    // Observer calls finish before parseText returns.
    std::vector<std::string> parseText(
        std::string_view text,
        const TokenObserver& observer = {}
    ) const;

private:
    std::unordered_set<std::string> stopwords_;
    PorterStemmer stemmer_;
};

}
