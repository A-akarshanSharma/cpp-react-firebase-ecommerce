#pragma once
#include <string>
#include "FirebaseAuth.h"
#include <nlohmann/json.hpp>

// Minimal Firestore REST wrapper. Just enough for phase 1: get + set a document.
// Note: Firestore REST uses a typed document format (not plain JSON) -
// this wrapper hides that conversion behind plain nlohmann::json in/out.
class FirestoreClient
{
public:
    FirestoreClient(const std::string &projectId, FirebaseAuth &auth);

    // Writes/overwrites a document at collection/docId with the given plain JSON fields.
    // Returns true on success.
    bool setDocument(const std::string &collection,
                      const std::string &docId,
                      const nlohmann::json &fields);

    // Reads a document, returns plain JSON fields (empty json if not found / error).
    nlohmann::json getDocument(const std::string &collection,
                                const std::string &docId);

    // Lists all documents in a collection. Returns a JSON array, each item
    // being the document's fields plus an "id" key. Empty array on error.
    // NOTE: fetches everything in one call - fine at your traffic/data size,
    // revisit with pagination only if a collection grows into the thousands.
    nlohmann::json listDocuments(const std::string &collection);

    // Creates a document with an auto-generated ID (Firestore picks it).
    // Returns the new document's ID, or empty string on failure.
    std::string addDocument(const std::string &collection,
                             const nlohmann::json &fields);

    // Deletes a document. Returns true on success (Firestore returns success
    // even if the doc didn't exist, so this mainly signals a real request failure).
    bool deleteDocument(const std::string &collection, const std::string &docId);

private:
    std::string projectId_;
    FirebaseAuth &auth_;
    std::string baseUrl();

    // Converts plain JSON -> Firestore's typed document format for writes.
    nlohmann::json toFirestoreFields(const nlohmann::json &plain);
    // Converts Firestore's typed document format -> plain JSON for reads.
    nlohmann::json fromFirestoreFields(const nlohmann::json &firestoreDoc);

    // Recursive single-value converters - handle nesting (arrays of objects, etc),
    // used internally by toFirestoreFields/fromFirestoreFields above.
    nlohmann::json toFirestoreValue(const nlohmann::json &val);
    nlohmann::json fromFirestoreValue(const nlohmann::json &wrapped);
};
