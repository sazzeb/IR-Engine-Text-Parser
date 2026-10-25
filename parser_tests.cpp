#include "ParserApplication.h"
#include "OutputWriter.h"
#include "TextParser.h"
#include "TrecReader.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

// Stop a test with a clear message when its expected result differs.
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Confirm invalid input fails instead of silently producing incorrect results.
template<class Action>
void requireFailure(Action action, const std::string& message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }

    throw std::runtime_error(message);
}

// Check token rules, supplied stemming, dictionary numbering, and TREC document boundaries.
void runSelfTests() {
    std::istringstream stopwords("\xEF\xBB\xBFthe\nand\nof\n");
    ir::TextParser parser(stopwords);

    require(
        parser.parseText(
            "The CATS and cats, running! abc123 123abc 2026.") ==
            std::vector<std::string>{"cat", "cat", "run"},
        "Token rules or stopwords differ.");

    require(
        parser.parseText("red-blue O'Neil") ==
            std::vector<std::string>{"red", "blue", "o", "neil"},
        "Punctuation was not split correctly.");

    require(
        parser.parseText("THE and of 12 x9").empty(),
        "Rejected tokens were retained.");

    require(
        parser.parseText("").empty(),
        "Empty text produced tokens.");

    require(
        parser.parseText(std::string(4096, 'a')).front().size() == 4096,
        "Valid token limit failed.");

    // Reject oversized tokens instead of returning a partially parsed word.
    requireFailure(
        [&] { parser.parseText(std::string(4097, 'a')); },
        "Oversized token accepted.");

    ir::PorterStemmer stemmer;

    for (const auto& [word, expected] :
         std::vector<std::pair<std::string, std::string>>{
             {"caresses", "caress"},
             {"ponies", "poni"},
             {"agreed", "agre"},
             {"relational", "relat"},
             {"conditional", "condit"},
             {"vietnamization", "vietnam"},
             {"triplicate", "triplic"},
             {"revival", "reviv"},
             {"probate", "probat"},
             {"controll", "control"},
             {"ies", "i"},
             {"ied", "i"},
             {"a", "a"},
             {"", ""}
         }) {
        require(
            stemmer.stemWord(word) == expected,
            "Supplied stemmer result differs for " + word);
    }

    // Reject uppercase input because normalization must happen before stemming begins.
    requireFailure(
        [&] { stemmer.stemWord("ABC"); },
        "Unnormalized stemmer input accepted.");

    std::vector<ir::TokenEvent> events;

    // Collect token decisions without changing the parser's returned stem sequence.
    parser.parseText(
        "THE Cats abc123",
        [&](const ir::TokenEvent& event) {
            events.push_back(event);
        });

    require(
        events.size() == 3 &&
        events[0].disposition == ir::TokenDisposition::stopword &&
        events[1].stem == "cat" &&
        events[2].disposition == ir::TokenDisposition::containsDigit,
        "Token observer decisions differ.");

    ir::TermDictionary terms;

    for (const auto& word :
         {"echo", "delta", "lima", "foxtrot", "hotel", "echo"}) {
        terms.collect(word);
    }

    // Prevent callers from using term identifiers before alphabetical assignment finishes.
    requireFailure(
        [&] { terms.id("echo"); },
        "A provisional term identifier was exposed.");

    terms.finalize();

    ir::Identifier expected = 1;

    for (const auto& word :
         {"delta", "echo", "foxtrot", "hotel", "lima"}) {
        require(
            terms.id(word) == expected++,
            "Term identifiers are not alphabetical and one based.");
    }

    // Reject identifier zero because every assigned dictionary identifier starts at one.
    requireFailure(
        [&] { terms.value(0); },
        "Term identifier zero was accepted.");

    ir::DocumentDictionary documents;

    for (const auto& name :
         {"FT911-3546", "FT911-1", "FT911-5368", "other"}) {
        documents.collect(name);
    }

    // Reject repeated document names before final document identifiers are assigned.
    requireFailure(
        [&] { documents.collect("FT911-1"); },
        "Duplicate document name accepted.");

    documents.finalize();

    require(
        documents.id("FT911-3546") == 3546 &&
        documents.id("FT911-5368") == 5368 &&
        documents.id("FT911-1") == 1 &&
        documents.id("other") == 2,
        "Document numbers were changed or collided.");

    for (const auto& name :
         {"FT911-0", "FT911-5369", "FT911-01", "FT911-X", "bad name"}) {
        // Reject malformed Financial Times numbers and names with unsafe separators.
        requireFailure(
            [&] { ir::Dictionary::documentNumber(name); },
            "Invalid document name accepted.");
    }

    std::istringstream trec(
        "<doc><docno>FT911-3546</docno><headline>Cats</headline>"
        "<title>TitleOnly</title><ti>TiOnly</ti><hl>HlOnly</hl>"
        "<leadpara>LeadOnly</leadpara>"
        "<text type=\"body\">Running <b>cats</b> &amp; dogs &#65;.</text></doc>"
        "<DOC><DOCNO>FT911-1</DOCNO><TEXT/></DOC>");

    std::vector<ir::Document> records;

    // Preserve emitted records so document boundaries and content can be checked.
    ir::TrecReader().read(
        trec,
        [&](const ir::Document& document) {
            records.push_back(document);
        });

    require(
        records.size() == 2 &&
        records[0].name == "FT911-3546" &&
        parser.parseText(records[0].text) ==
            std::vector<std::string>{"run", "cat", "dog", "a"} &&
        records[1].text.empty(),
        "TREC records or entity decoding differ.");

    for (const auto& malformed : {
        "",
        "<DOC><DOCNO>FT911-1</DOCNO><TEXT>cats</TEXT>",
        "<DOC><TEXT>cats</TEXT></DOC>",
        "<DOC><DOCNO>X</DOCNO><TEXT>cats</DOC>",
        "<DOC><DOCNO>X</DOCNO><DOCNO>Y</DOCNO><TEXT/></DOC>"
    }) {
        // Reject incomplete records instead of silently accepting damaged document boundaries.
        requireFailure(
            [&] {
                std::istringstream input(malformed);

                // Ignore valid records while checking that malformed markup raises errors.
                ir::TrecReader().read(input, [](const ir::Document&) {});
            },
            "Malformed TREC input accepted.");
    }

    std::ostringstream output;

    ir::OutputWriter().write(
        output, terms, documents, {{3546, {2, 1, 5}}});

    require(
        output.str().find(
            "FT911-3546\t3546\techo:2 delta:1 lima:5") !=
            std::string::npos,
        "Written token identifiers differ from their final dictionaries.");

    std::cout << "[PASS] Parser rules and identifier tests passed.\n";
}

}

// Parse bundled corpus files normally and run isolated tests only when requested.
int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--self-test") {
            runSelfTests();
        } else if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout
                << "ParserTests [--self-test]\n"
                << "Normal runs parse ./resources/corpus using ./resources/stoplist.txt.\n";
        } else if (argc == 1) {
            ir::runDefaultResource(std::cout);
        } else {
            throw std::invalid_argument(
                "Usage: ParserTests [--self-test|--help]");
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
