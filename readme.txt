Project 2: IR Engine Indexer
============================

Build requirements
------------------
- A 64-bit C++17 compiler, CMake 3.20 or newer, and an internet connection
  for CMake's first dependency download.
- Run commands from the project root, the folder containing CMakeLists.txt.
  Input and output paths must be relative to this folder.

Build
-----
  cmake -S . -B build
  cmake --build build --target IndexEngine ParserTests ApplicationTests IREngine InputParser

On Windows, use build\Debug\IndexEngine.exe if using a multi-configuration
generator. On macOS or Linux, use ./build/IndexEngine. The examples below use
the macOS/Linux form.

Build the sample index and look up a term
-----------------------------------------
  ./build/IndexEngine --input resources/corpus/sample.trec --term cats

The program indexes the input, applies the supplied stopword list and Porter
stemmer, and prints every document ID containing the stem of "cats" with its
frequency. Without --term, it prompts for one term at a time. Press Ctrl-D
(or Ctrl-Z then Enter on Windows) to finish.

Index the supplied collection
-----------------------------
Keep the full collection in its own folder so the bundled sample is not
indexed twice. Pass its relative file or folder path with --input. For a
folder containing nested files, add --recursive:

  ./build/IndexEngine --input resources/full_corpus --recursive --output output

For this workspace, the full FT911 collection is in Downloads/proj1/ft911.
From the project root, the equivalent command is:

  ./build/IndexEngine --input ../../Downloads/proj1/ft911 --output output

The default input is resources/corpus. The default output folder is output.
The default stopword list is resources/stoplist.txt. To use a different file:

  ./build/IndexEngine --input resources/corpus/data.trec --stopwords resources/stoplist.txt --output output

To look up more terms later without rebuilding the index:

  ./build/IndexEngine --lookup-only --output output --term running
  ./build/IndexEngine --lookup-only --output output

Options: --input FILE_OR_DIRECTORY, --output DIRECTORY, --stopwords FILE,
--recursive, --term WORD, --lookup-only, and --help. Query text is normalized
with the same parser used for documents. A removed stopword or unknown term
has no postings. Enter one term per query. If a different --stopwords file was
used while indexing, pass the same file for --lookup-only.

Output files
------------
- output/forward_index.txt: one line per document, ordered by document ID.
  Format: docID: termID: frequency; termID: frequency;
- output/inverted_index.txt: one line per indexed term, ordered by term ID.
  Format: termID: docID: frequency; docID: frequency;
- output/inverted_offsets.txt: term ID and byte offset into the inverted file.
- output/text_dictionary.txt: stem and term ID, separated by a tab.
- output/doc_dictionary.txt: document name and document ID, separated by a tab.
- output/parser_output.txt: the existing dictionaries and token streams.

Only terms occurring in a document appear in its forward line. A document
whose terms were all removed still has an empty forward line. Inverted lines
contain only documents where the term occurs. TREC input uses <TEXT> content
and <DOCNO> as the document name. Plain TXT uses paragraphs as documents.

IndexEngine prints elapsed indexing time, the size of the forward, inverted,
and offset files together, and the size including both dictionaries. These
numbers are for the current run; record the full-corpus values for the report.

Test
----
  ctest --test-dir build --output-on-failure

The parser and application test executables still support --self-test:

  ./build/ParserTests --self-test
  ./build/ApplicationTests --self-test

For a manual check, run the sample command above. It should report the stem
"cat" with document 3546 and frequency 2. The original IREngine, InputParser,
ParserTests, and ApplicationTests entry points remain available as before.
