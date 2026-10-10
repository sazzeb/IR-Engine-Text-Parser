#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ir {

using Identifier = std::size_t;

struct Document {
    std::string name;
    std::string text;
};

struct ParsedDocument {
    Identifier id;
    std::vector<Identifier> terms;
};

}