#pragma once
#include "Errors.h"
#include "FirebaseAuth.h"
#include "cache/CatalogCache.h"
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// HTTP transport is injectable for isolated fault/concurrency tests. Production always
// uses the authenticated Google REST transport constructed in main.cpp.
class FirestoreClient
{
  public:
    using Json = nlohmann::json;
    struct Response
    {
        int status;
        Json body;
    };
    using Transport = std::function<Response(const std::string &, const std::string &, const Json &)>;
    struct Write
    {
        std::string collection, id;
        Json fields;
        std::vector<std::string> mask;
        bool createOnly = false;
        bool existsOnly = false;
    };
    FirestoreClient(const std::string &projectId, FirebaseAuth &auth);
    FirestoreClient(const std::string &projectId, Transport transport);
    static Transport httpTransport(std::string baseUrl, std::function<std::string()> tokenProvider);
    // Only a genuine 404 returns null. Every other failure throws DatabaseError.
    Json getDocument(const std::string &collection, const std::string &id,
                     const std::string &transaction = "");
    Json dueOrders(int64_t now, const Json &after = nullptr);
    Json listDocuments(const std::string &collection);
    Json catalogProducts();
    // Bounded history query, newest first; cursor is scoped to collection/filter.
    Json historyPage(const std::string &collection, int limit = 25, const std::string &cursor = "",
                     const std::string &field = "", const std::string &value = "");
    Json queryEqual(const std::string &collection, const std::string &field, const Json &value,
                    const std::string &transaction);
    bool setDocument(const std::string &collection, const std::string &id, const Json &fields);
    bool createDocument(const std::string &collection, const std::string &id, const Json &fields);
    std::string addDocument(const std::string &collection, const Json &fields);
    bool deleteDocument(const std::string &collection, const std::string &id);
    Json transact(const std::function<Json(const std::string &, std::vector<Write> &)> &operation);
    static Json toFields(const Json &plain);
    static Json fromFields(const Json &doc);

  private:
    CatalogCache catalogCache_;
    std::string root_;
    Transport transport_;
    Json call(const std::string &method, const std::string &path, const Json &body = nullptr,
              bool allowMissing = false);
    Json writeJson(const Write &write) const;
    static std::string segment(const std::string &value);
};
