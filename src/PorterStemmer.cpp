#include "PorterStemmer.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "../vendor/stemmer.h"

namespace ir {

    // Apply the supplied stemmer with guarded storage and shared state protection.
    std::string PorterStemmer::stemWord(std::string word) const {
        if (word.empty()) {
            return word;
        }

        if (word.size() >
            static_cast<std::size_t>(std::numeric_limits<int>::max() - 2)) {
            throw std::length_error(
                "Word exceeds the supplied stemmer's size limit.");
            }

        for (unsigned char letter : word) {
            if (letter < 'a' || letter > 'z') {
                throw std::invalid_argument(
                    "The stemmer requires lowercase English letters.");
            }
        }

        std::vector<char> buffer(word.size() + 3, '\0');
        std::copy(word.begin(), word.end(), buffer.begin() + 1);

        static std::mutex stemmerMutex;
        std::lock_guard<std::mutex> guard(stemmerMutex);

        const int last = ::stem(
            buffer.data(), 1, static_cast<int>(word.size()));

        if (last < 1 || last > static_cast<int>(word.size())) {
            throw std::runtime_error(
                "The supplied stemmer returned an invalid word length.");
        }

        return std::string(
            buffer.data() + 1,
            static_cast<std::size_t>(last));
    }

}