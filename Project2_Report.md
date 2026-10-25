# Project 2: IR Engine Indexer

## Design

The existing C++17 text parser reads each document, selects TREC `<TEXT>` content, removes stopwords and words containing digits, and applies Porter stemming. It writes document token streams while collecting unique terms and document names. After stable numeric IDs are assigned, the indexer reads those streams once and counts each term within its document.

Postings are kept in a temporary SQLite table keyed by `(document ID, term ID)`, so the entire collection of postings need not fit in memory. One ordered scan writes the forward index, and another writes the inverted index. Both files contain numeric IDs and term frequencies. A small offset file records each inverted line's byte position. The term lookup interface normalizes the user's term with the same parser, finds its ID in the term dictionary, seeks to that inverted line, and displays all document IDs and frequencies. The four existing executable entry points keep their behavior; `IndexEngine` adds direct input and term arguments.

## Measurement

`IndexEngine` measures elapsed wall-clock time from the start of corpus parsing until all index files are published. It reports two sizes: the forward, inverted, and offset files together; and those three files plus both dictionaries. The existing parser token stream is excluded from index size. Measurements below are from one run of the debug build on this macOS machine.

| Corpus | Documents | Terms | Tokens | Index time | Index files | With dictionaries |
|---|---:|---:|---:|---:|---:|---:|
| Full FT911 collection (15 files) | 5,368 | 32,645 | 1,036,714 | 6.688 s | 13,096,578 bytes | 13,611,725 bytes |
| Bundled `sample.trec` | 2 | 7 | 8 | 0.010 s | 166 bytes | 244 bytes |

The full FT911 run used `../../Downloads/proj1/ft911` from the project root. It produced 5,368 forward lines and 32,645 inverted lines. Summing the frequencies in either index gives 1,036,714, matching the parser's accepted-token count. The automated tests cover exact postings, empty documents, term lookup, the FT911 ID range, and 100,000 synthetic records. See `readme.txt` for build and test commands.
