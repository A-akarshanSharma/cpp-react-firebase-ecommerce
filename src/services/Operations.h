#pragma once
#include "Ordering.h"
namespace commerce
{
void audit(std::vector<FirestoreClient::Write> &writes, const std::string &actor, const std::string &action,
           const std::string &target, const json &changes = json::object());
void notification(std::vector<FirestoreClient::Write> &writes, const json &order, const std::string &event,
                  const std::string &message);
bool sellable(FirestoreClient &db, const json &product, const std::string &tx = "");
json shippingSettings(FirestoreClient &db, const std::string &tx = "");
json saveShipping(FirestoreClient &db, const std::string &actor, const json &input);
json shippingQuote(FirestoreClient &db, const std::string &tx, const json &body, const json &address);
json saveShipment(FirestoreClient &db, const std::string &orderId, const std::string &actor,
                  const json &input);
json markNotificationRead(FirestoreClient &db, const std::string &id, const std::string &uid);
json createVariant(FirestoreClient &db, const std::string &parentId, const std::string &actor,
                   const json &input);
json catalogView(const json &documents);
json variants(FirestoreClient &db, const std::string &parentId);
json changeRole(FirestoreClient &db, const std::string &target, const std::string &actor, const json &input);
} // namespace commerce
