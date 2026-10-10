#pragma once

#include "Document.h"

#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

namespace ir {

// Collect names first so final identifiers never depend on provisional values.
class Dictionary {
public:
    // Choose whether collected values represent documents or cleaned word stems.
    explicit Dictionary(bool documentNames = false)
        : documentNames_(documentNames) {}

    // Validate document names and preserve the required Financial Times number.
    static std::optional<Identifier> documentNumber(const std::string& name) {
        if (name.empty() || name.size() > 4096) {
            throw std::invalid_argument("Document name is empty or too long.");
        }

        for (unsigned char character : name) {
            if (character <= 32 || character == 127 ||
                character == '<' || character == '>') {
                throw std::invalid_argument(
                    "Document names cannot contain spaces or control characters.");
            }
        }

        if (name.rfind("FT911", 0) != 0) {
            return std::nullopt;
        }

        if (name.rfind("FT911-", 0) != 0 ||
            name.size() < 7 || name[6] == '0') {
            throw std::invalid_argument(
                "Use document names FT911-1 through FT911-5368.");
        }

        Identifier number = 0;

        for (std::size_t index = 6; index < name.size(); ++index) {
            const char digit = name[index];

            if (digit < '0' || digit > '9') {
                throw std::invalid_argument(
                    "An FT911 document must end with a number.");
            }

            number = number * 10 + static_cast<Identifier>(digit - '0');

            if (number > 5368) {
                throw std::invalid_argument(
                    "FT911 document numbers must be between 1 and 5368.");
            }
        }

        return number;
    }

    // Store unique stems and reject repeated document names before assigning identifiers.
    void collect(const std::string& value) {
        if (finalized_ || value.empty()) {
            throw std::logic_error(
                "Collect nonempty values before finalizing the dictionary.");
        }

        if (documentNames_) {
            documentNumber(value);
        } else if (
            value.find_first_not_of("abcdefghijklmnopqrstuvwxyz") !=
            std::string::npos) {
            throw std::invalid_argument(
                "Terms must contain lowercase English letters only.");
        }

        if (!pending_.insert(value).second && documentNames_) {
            throw std::invalid_argument("Duplicate document name: " + value);
        }
    }

    // Sort all names before assigning positive identifiers without reserved number clashes.
    void finalize() {
        if (finalized_) {
            return;
        }

        if (documentNames_) {
            for (const auto& name : pending_) {
                if (const auto number = documentNumber(name)) {
                    identifiers_.emplace(name, *number);
                    values_.emplace(*number, name);
                }
            }
        }

        Identifier next = 1;

        for (const auto& name : pending_) {
            if (identifiers_.count(name)) {
                continue;
            }

            while (values_.count(next)) {
                ++next;
            }

            identifiers_.emplace(name, next);
            values_.emplace(next, name);
            ++next;
        }

        finalized_ = true;
    }

    // Look up final identifiers only after every dictionary value is collected.
    Identifier id(const std::string& name) const {
        if (!finalized_) {
            throw std::logic_error(
                "Finalize the dictionary before reading identifiers.");
        }

        return identifiers_.at(name);
    }

    // Read names by their assigned identifiers while rejecting missing numbers.
    const std::string& value(Identifier identifier) const {
        if (!finalized_ || identifier == 0) {
            throw std::out_of_range("Invalid dictionary identifier.");
        }

        return values_.at(identifier);
    }

    // Return final dictionary rows in increasing order of their identifiers.
    const std::map<Identifier, std::string>& entries() const {
        if (!finalized_) {
            throw std::logic_error(
                "Finalize the dictionary before reading entries.");
        }

        return values_;
    }

    // Report the number of distinct names collected for this dictionary.
    std::size_t size() const noexcept {
        return pending_.size();
    }

private:
    bool documentNames_;
    bool finalized_ = false;

    std::set<std::string> pending_;
    std::map<std::string, Identifier> identifiers_;
    std::map<Identifier, std::string> values_;
};

}