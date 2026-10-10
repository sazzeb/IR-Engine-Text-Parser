#pragma once

#include "Dictionary.h"

namespace ir {

    // Preserve Financial Times numbers while assigning unused positive identifiers elsewhere.
    class DocumentDictionary : public Dictionary {
    public:
        // Enable document name checks and preserve required Financial Times identifiers.
        DocumentDictionary() : Dictionary(true) {}
    };

}