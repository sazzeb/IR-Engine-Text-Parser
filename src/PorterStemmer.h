#pragma once

#include "../vendor/stemmer.h"

#include <string>

namespace ir {

class PorterStemmer {
public:
    std::string stemWord(std::string word) const;
};

}
