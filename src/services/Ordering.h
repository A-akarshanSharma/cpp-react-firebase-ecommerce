#pragma once
#include "Commerce.h"
namespace commerce
{
int64_t nowSeconds();
json expireOrder(FirestoreClient &db, const std::string &orderId);
json addressInput(const json &input);
void inventoryMovement(std::vector<FirestoreClient::Write> &writes, const std::string &productId,
                       int64_t before, int64_t after, const std::string &reason, const std::string &actor,
                       const std::string &orderId = "");
json orderDetail(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin);
json transitionOrder(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin,
                     const json &input);
json archiveProduct(FirestoreClient &db, const std::string &productId, const std::string &actor = "");
json setOrderAddress(FirestoreClient &db, const std::string &orderId, const std::string &uid,
                     const json &input);
} // namespace commerce
