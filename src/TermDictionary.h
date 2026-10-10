#pragma once

#include "Dictionary.h"

namespace ir {

    // Assign word identifiers only after collecting and alphabetically sorting all stems.
    class TermDictionary : public Dictionary {};

}