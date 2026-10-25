#include "ParserApplication.h"
#include "OutputWriter.h"
#include "IndexLookup.h"

#include <miniz.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

// Report failed expectations with enough detail to locate the broken requirement.
void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Confirm bad inputs raise errors without being treated as successful runs.
template<class Action>
void requireFailure(Action action, const std::string& message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }

    throw std::runtime_error(message);
}

// Keep generated fixtures in a relative folder separate from real outputs.
class TestDirectory {
public:
    std::filesystem::path path;

    // Create one unique test folder without replacing any existing user files.
    TestDirectory() {
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();

        for (int attempt = 1; attempt <= 100; ++attempt) {
            path = ".ir-tests-" + std::to_string(stamp) +
                "-" + std::to_string(attempt);

            if (std::filesystem::create_directory(path)) {
                return;
            }
        }

        throw std::runtime_error(
            "Cannot create a relative test folder.");
    }

    // Remove only this test run's temporary folder after all checks finish.
    ~TestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

// Write exact fixture bytes and check for incomplete filesystem writes.
void writeFile(
    const std::filesystem::path& path,
    const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    output.close();

    require(
        static_cast<bool>(output),
        "Cannot write test fixture.");
}

// Read complete result files so exact dictionary contents can be compared.
std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);

    require(
        static_cast<bool>(input),
        "Cannot read a test result.");

    return {
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    };
}

// Create a small workbook that checks format offsets and assigned identifiers.
void writeWorkbook(const std::filesystem::path& path) {
    const std::vector<std::pair<std::string, std::string>> members{
        {
            "_rels/.rels",
            R"(<Relationships><Relationship Type="urn/officeDocument" Target="xl/workbook.xml"/></Relationships>)"
        },
        {
            "xl/workbook.xml",
            R"(<workbook xmlns:r="urn:test"><sheets><sheet name="Corpus" r:id="sheet1"/></sheets></workbook>)"
        },
        {
            "xl/_rels/workbook.xml.rels",
            R"(<Relationships><Relationship Id="sheet1" Type="urn/worksheet" Target="worksheets/sheet1.xml"/><Relationship Id="strings" Type="urn/sharedStrings" Target="sharedStrings.xml"/></Relationships>)"
        },
        {
            "xl/sharedStrings.xml",
            R"(<sst><si><t>id</t></si><si><t>text</t></si><si><t>FT911-3546</t></si><si><t>The cats</t></si></sst>)"
        },
        {
            "xl/worksheets/sheet1.xml",
            R"(<worksheet><sheetData><row><c r="A1" t="s"><v>0</v></c><c r="B1" t="s"><v>1</v></c></row><row><c r="A2" t="s"><v>2</v></c><c r="B2" t="s"><v>3</v></c></row></sheetData></worksheet>)"
        }
    };

    mz_zip_archive archive{};

    require(
        mz_zip_writer_init_file(
            &archive, path.string().c_str(), 0) != 0,
        "Cannot create test workbook.");

    try {
        for (const auto& [name, text] : members) {
            require(
                mz_zip_writer_add_mem(
                    &archive,
                    name.c_str(),
                    text.data(),
                    text.size(),
                    MZ_BEST_SPEED) != 0,
                "Cannot write workbook member.");
        }

        require(
            mz_zip_writer_finalize_archive(&archive) != 0,
            "Cannot finish workbook fixture.");
    } catch (...) {
        mz_zip_writer_end(&archive);
        throw;
    }

    mz_zip_writer_end(&archive);
}

// Check complete runs, relative paths, data formats, and output replacement behavior.
void runSelfTests() {
    TestDirectory temporary;
    ir::RunOptions options;

    options.input = temporary.path / "sample.trec";
    options.stopwords = temporary.path / "stoplist.txt";
    options.output = temporary.path / "parser_output.txt";
    options.verbose = false;

    writeFile(options.stopwords, "the\nand\nof\n");

    const std::string first =
        "<DOC><DOCNO>FT911-3546</DOCNO>"
        "<TEXT>Echo delta lima foxtrot hotel the cats cat.</TEXT></DOC>";

    const std::string second =
        "<DOC><DOCNO>FT911-1</DOCNO>"
        "<TEXT>Running</TEXT></DOC>";

    writeFile(options.input, first + second);

    std::ostringstream log;
    const auto summary = ir::runParser(options, log);
    const auto outputs = ir::OutputWriter::paths(options.output);

    const std::string terms =
        "cat\t1\n"
        "delta\t2\n"
        "echo\t3\n"
        "foxtrot\t4\n"
        "hotel\t5\n"
        "lima\t6\n"
        "run\t7\n";

    const std::string documents =
        "FT911-1\t1\n"
        "FT911-3546\t3546\n";

    require(
        summary.documents == 2 &&
        summary.terms == 7 &&
        summary.tokens == 8,
        "Summary counts differ.");

    require(
        readFile(outputs.terms) == terms &&
        readFile(outputs.documents) == documents,
        "Standalone dictionary files differ from required identifiers.");

    const std::string forward =
        "1: 7: 1;\n"
        "3546: 1: 2; 2: 1; 3: 1; 4: 1; 5: 1; 6: 1;\n";
    const std::string inverted =
        "1: 3546: 2;\n"
        "2: 3546: 1;\n"
        "3: 3546: 1;\n"
        "4: 3546: 1;\n"
        "5: 3546: 1;\n"
        "6: 3546: 1;\n"
        "7: 1: 1;\n";

    require(
        readFile(outputs.forward) == forward &&
        readFile(outputs.inverted) == inverted,
        "Forward or inverted frequencies differ.");

    ir::IndexLookup lookup(outputs, options.stopwords);
    std::ostringstream postings;

    require(
        lookup.printTerm("CATS", postings) &&
        postings.str() == "Term: cat (ID 1)\nDocument 3546: 2\n",
        "A stemmed query did not return its document frequency.");

    postings.str("");
    postings.clear();

    require(
        lookup.printTerm("running", postings) &&
        postings.str() == "Term: run (ID 7)\nDocument 1: 1\n",
        "A query did not use the same stemming as the corpus.");

    postings.str("");
    postings.clear();

    require(
        !lookup.printTerm("the", postings) &&
        !lookup.printTerm("absent", postings),
        "An absent or removed term was found in the index.");

    requireFailure(
        [&] { lookup.printTerm("cats running", postings); },
        "A multiword query was accepted as one term.");

    const auto expected = readFile(outputs.parser);

    require(
        expected ==
            "TERM_DICTIONARY\n" + terms +
            "\nDOCUMENT_DICTIONARY\n" + documents +
            "\nTOKEN_STREAMS\n"
            "FT911-3546\t3546\techo:3 delta:2 lima:6 foxtrot:4 hotel:5 cat:1 cat:1\n"
            "FT911-1\t1\trun:7\n",
        "Combined output contains incorrect identifiers.");

    for (int repetition = 1; repetition <= 3; ++repetition) {
        for (const auto& path : {
            outputs.parser, outputs.terms, outputs.documents,
            outputs.forward, outputs.inverted, outputs.offsets
        }) {
            writeFile(path, std::string(20000, 'x'));
        }

        ir::runParser(options, log);

        require(
            readFile(outputs.parser) == expected &&
            readFile(outputs.terms) == terms &&
            readFile(outputs.documents) == documents &&
            readFile(outputs.forward) == forward &&
            readFile(outputs.inverted) == inverted,
            "A repeated run appended or retained old data.");
    }

    writeFile(options.input, second + first);
    ir::runParser(options, log);

    require(
        readFile(outputs.terms) == terms &&
        readFile(outputs.documents) == documents,
        "Changing document order changed dictionary identifiers.");

    const auto stopwords = options.stopwords;
    options.stopwords = temporary.path / "missing.txt";

    // Preserve previous results when required input validation fails before processing.
    requireFailure(
        [&] { ir::runParser(options, log); },
        "Missing stoplist accepted.");

    require(
        readFile(outputs.terms) == terms,
        "Validation failure replaced previous dictionaries.");

    options.stopwords = stopwords;

    // Reject an operating system location instead of opening an absolute path.
    requireFailure(
        [&] {
            ir::requireRelativePath(std::filesystem::current_path());
        },
        "Absolute path accepted.");

    for (const auto& invalid : {
        first + first,
        std::string(
            "<DOC><DOCNO>FT911-5369</DOCNO><TEXT>cats</TEXT></DOC>"),
        std::string(
            "<DOC><DOCNO>FT911-0</DOCNO><TEXT>cats</TEXT></DOC>"),
        std::string(
            "<DOC><DOCNO>FT911-1</DOCNO><TEXT>cats")
    }) {
        writeFile(options.input, invalid);

        // Clear previous results when parsing fails after the new run starts.
        requireFailure(
            [&] { ir::runParser(options, log); },
            "Invalid corpus accepted.");

        for (const auto& path : {
            outputs.parser, outputs.terms, outputs.documents,
            outputs.forward, outputs.inverted, outputs.offsets
        }) {
            require(
                readFile(path).empty(),
                "Failed parsing left old or partial results.");
        }
    }

    writeFile(options.input, first + second);
    options.input = outputs.terms;

    // Protect dictionary outputs from being selected as the next input file.
    requireFailure(
        [&] { ir::runParser(options, log); },
        "Dictionary input overwrite accepted.");

    const std::vector<std::pair<std::string, std::string>> formats{
        {
            "sample.txt",
            "The cats.\n\nDogs and running."
        },
        {
            "sample.csv",
            "id,text\nA,The cats\nB,Dogs and running\n"
        },
        {
            "sample.tsv",
            "id\ttext\nA\tThe cats\nB\tDogs and running\n"
        },
        {
            "sample.json",
            R"([{"id":"A","text":"The cats"},{"id":"B","text":"Dogs and running"}])"
        },
        {
            "sample.jsonl",
            "{\"id\":\"A\",\"text\":\"The cats\"}\n"
            "{\"id\":\"B\",\"text\":\"Dogs and running\"}\n"
        },
        {
            "sample.ndjson",
            "{\"id\":\"A\",\"text\":\"The cats\"}\n"
            "{\"id\":\"B\",\"text\":\"Dogs and running\"}\n"
        }
    };

    for (const auto& [name, contents] : formats) {
        options.input = temporary.path / name;
        writeFile(options.input, contents);

        const auto result = ir::runParser(options, log);

        require(
            result.documents == 2 &&
            result.tokens == 3 &&
            readFile(outputs.terms) == "cat\t1\ndog\t2\nrun\t3\n",
            "Format processing failed: " + name);
    }

    options.input = temporary.path / "mixed.json";

    writeFile(
        options.input,
        R"([{"id":"B","text":"cats"},{"id":"FT911-1","text":"dogs"},{"id":"A","text":"running"}])");

    ir::runParser(options, log);

    require(
        readFile(outputs.documents) == "FT911-1\t1\nA\t2\nB\t3\n",
        "Mixed document names caused identifier clashes.");

    options.input = temporary.path / "postings.trec";
    writeFile(
        options.input,
        "<DOC><DOCNO>A</DOCNO><TEXT>cat cat moon</TEXT></DOC>"
        "<DOC><DOCNO>B</DOCNO><TEXT>cat</TEXT></DOC>"
        "<DOC><DOCNO>C</DOCNO><TEXT>the and</TEXT></DOC>");
    ir::runParser(options, log);

    require(
        readFile(outputs.forward) ==
            "1: 1: 2; 2: 1;\n2: 1: 1;\n3:\n" &&
        readFile(outputs.inverted) ==
            "1: 1: 2; 2: 1;\n2: 1: 1;\n",
        "Shared terms or an empty document changed index postings.");

    ir::IndexLookup sharedLookup(outputs, options.stopwords);
    postings.str("");
    postings.clear();

    require(
        sharedLookup.printTerm("cats", postings) &&
        postings.str() ==
            "Term: cat (ID 1)\nDocument 1: 2\nDocument 2: 1\n",
        "Lookup omitted a document from a posting list.");

    // Clear staged failures without publishing any incomplete dictionary or token file.
    requireFailure(
        [&] {
            // Simulate a writer failure after one staged result has been started.
            ir::OutputWriter::publish(
                options.output,
                [&](const ir::OutputPaths& staged,
                    const std::filesystem::path&) {
                    writeFile(staged.parser, "incomplete");
                    throw std::runtime_error("Simulated write failure.");
                });
        },
        "A failed writer was reported as successful.");

    for (const auto& path : {
        outputs.parser, outputs.terms, outputs.documents,
        outputs.forward, outputs.inverted, outputs.offsets
    }) {
        require(
            readFile(path).empty(),
            "An incomplete result escaped the staging folder.");
    }

    options.input = temporary.path / "headerless.csv";
    writeFile(options.input, "FT911-3546,The cats\n");

    options.reader.hasHeader = false;
    options.reader.textFields = {"2"};
    options.reader.idField = "1";

    ir::runParser(options, log);

    require(
        readFile(outputs.documents) == "FT911-3546\t3546\n",
        "Headerless columns were not one based.");

    options.reader = {};
    options.input = temporary.path / "delayed.txt";

    writeFile(
        options.input,
        std::string(12000, ' ') + first + second);

    require(
        ir::runParser(options, log).documents == 2,
        "Delayed TREC markup was treated as plain text.");

    options.input = temporary.path / "nested.json";

    writeFile(
        options.input,
        R"({"data":[{"id":"FT911-3546","text":"The cats"}]})");

    options.reader.recordsPath = "/data";

    require(
        ir::runParser(options, log).documents == 1,
        "Nested JSON record selection failed.");

    options.reader = {};
    options.input = temporary.path / "sample.xlsx";
    writeWorkbook(options.input);

    require(
        ir::runParser(options, log).tokens == 1 &&
        readFile(outputs.documents) == "FT911-3546\t3546\n",
        "Workbook string offsets or final document identifiers differ.");

    options.input = temporary.path / "all-ft911.trec";
    std::ostringstream corpus;

    for (int id = 5368; id >= 1; --id) {
        corpus
            << "<DOC><DOCNO>FT911-" << id
            << "</DOCNO><TEXT>cats running</TEXT></DOC>\n";
    }

    writeFile(options.input, corpus.str());

    require(
        ir::runParser(options, log).documents == 5368,
        "The complete FT911 number range failed.");

    std::istringstream dictionary(readFile(outputs.documents));

    for (int expectedId = 1; expectedId <= 5368; ++expectedId) {
        std::string name;
        int id = 0;
        dictionary >> name >> id;

        require(
            name == "FT911-" + std::to_string(expectedId) &&
            id == expectedId,
            "An FT911 suffix does not match its identifier.");
    }

    options.input = temporary.path / "large.jsonl";

    {
        std::ofstream large(options.input);

        for (int id = 1; id <= 100000; ++id) {
            large
                << "{\"id\":\"record-" << id
                << "\",\"text\":\"The cats running\"}\n";
        }

        large.close();

        require(
            static_cast<bool>(large),
            "Cannot finish the large test fixture.");
    }

    const auto large = ir::runParser(options, log);

    require(
        large.documents == 100000 &&
        large.terms == 2 &&
        large.tokens == 200000,
        "Large corpus counts differ.");

    std::cout
        << "[PASS] Application formats, output files, and full FT911 range passed.\n";
}

}

// Open the normal chooser unless isolated automated tests were explicitly requested.
int main(int argc, char* argv[]) {
    if (argc != 2 || std::string(argv[1]) != "--self-test") {
        return ir::runInteractiveParser(argc, argv);
    }

    try {
        runSelfTests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
