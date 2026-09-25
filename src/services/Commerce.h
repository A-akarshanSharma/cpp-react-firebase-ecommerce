#pragma once
#include "../FirestoreClient.h"
#include "../Validation.h"
namespace commerce
{
using json = nlohmann::json;
std::string fingerprint(const std::string &value);
json cartItems(const json &doc, bool checkout = false);
std::string cartVersion(const json &doc);
std::string quoteVersion(const std::string &uid, const std::string &version, const json &items);
json getCart(FirestoreClient &db, const std::string &uid);
json mutateCart(FirestoreClient &db, const std::string &uid, const std::string &action, const json &body);
json checkout(FirestoreClient &db, const std::string &uid, const std::string &key, const json &body);
json saveProduct(FirestoreClient &db, const std::string &id, const json &body, bool create,
                 const std::string &actor = "");
json ensureProfile(FirestoreClient &db, const std::string &uid, const std::string &email);
json adjustStock(FirestoreClient &db, const std::string &id, const json &body, const std::string &actor);
} // namespace commerce
