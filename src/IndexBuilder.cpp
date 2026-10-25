#include "IndexBuilder.h"

#include <fstream>
#include <stdexcept>

namespace ir {
namespace {

void finishFile(std::ofstream& output) {
    output.close();

    if (!output) {
        throw std::runtime_error("Cannot finish an index file.");
    }
}

void writeForwardIndex(Database& database, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);

    if (!output) {
        throw std::runtime_error("Cannot create the forward index.");
    }

    Statement rows(
        database,
        "SELECT d.id,COALESCE(p.term_id,0),COALESCE(p.frequency,0) "
        "FROM document_ids d LEFT JOIN postings p ON p.doc_id=d.id "
        "ORDER BY d.id,p.term_id");

    std::int64_t previousDocument = 0;

    while (rows.step()) {
        const auto documentId = rows.integer(0);

        if (documentId != previousDocument) {
            if (previousDocument != 0) {
                output << '\n';
            }

            output << documentId << ':';
            previousDocument = documentId;
        }

        if (const auto termId = rows.integer(1); termId != 0) {
            output << ' ' << termId << ": " << rows.integer(2) << ';';
        }
    }

    if (previousDocument != 0) {
        output << '\n';
    }

    finishFile(output);
}

void writeInvertedIndex(
    Database& database,
    const std::filesystem::path& indexPath,
    const std::filesystem::path& offsetPath) {
    std::ofstream output(indexPath, std::ios::binary | std::ios::trunc);
    std::ofstream offsets(offsetPath, std::ios::binary | std::ios::trunc);

    if (!output || !offsets) {
        throw std::runtime_error("Cannot create the inverted index.");
    }

    Statement rows(
        database,
        "SELECT term_id,doc_id,frequency FROM postings "
        "ORDER BY term_id,doc_id");

    std::int64_t previousTerm = 0;

    while (rows.step()) {
        const auto termId = rows.integer(0);

        if (termId != previousTerm) {
            if (previousTerm != 0) {
                output << '\n';
            }

            const auto offset = static_cast<std::streamoff>(output.tellp());

            if (offset < 0) {
                throw std::runtime_error("Cannot record an inverted index offset.");
            }

            offsets << termId << '\t' << offset << '\n';
            output << termId << ':';
            previousTerm = termId;
        }

        output << ' ' << rows.integer(1) << ": " << rows.integer(2) << ';';
    }

    if (previousTerm != 0) {
        output << '\n';
    }

    finishFile(output);
    finishFile(offsets);
}

}

IndexBuilder::IndexBuilder(Database& database)
    : database_(database) {
    database_.execute(
        "CREATE TABLE postings("
        "doc_id INTEGER NOT NULL CHECK(doc_id>0),"
        "term_id INTEGER NOT NULL CHECK(term_id>0),"
        "frequency INTEGER NOT NULL CHECK(frequency>0),"
        "PRIMARY KEY(doc_id,term_id)) WITHOUT ROWID;"
        "BEGIN;");

    insertPosting_ = std::make_unique<Statement>(
        database_,
        "INSERT INTO postings(doc_id,term_id,frequency) VALUES(?1,?2,?3)");
}

void IndexBuilder::addDocument(
    std::int64_t documentId,
    const std::unordered_map<std::int64_t, std::int64_t>& frequencies) {
    for (const auto& [termId, frequency] : frequencies) {
        insertPosting_->reset();
        insertPosting_->bind(1, documentId);
        insertPosting_->bind(2, termId);
        insertPosting_->bind(3, frequency);
        insertPosting_->step();
    }

    if (++documentsSinceCommit_ == 1000) {
        database_.execute("COMMIT;BEGIN;");
        documentsSinceCommit_ = 0;
    }
}

void IndexBuilder::write(const OutputPaths& paths) {
    database_.execute(
        "COMMIT;"
        "CREATE INDEX postings_by_term ON postings(term_id,doc_id);");

    writeForwardIndex(database_, paths.forward);
    writeInvertedIndex(database_, paths.inverted, paths.offsets);
}

}
