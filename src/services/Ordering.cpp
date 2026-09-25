#include "Operations.h"
#include <chrono>
#include <drogon/utils/Utilities.h>
#include <set>
using namespace validation;
namespace commerce
{
int64_t nowSeconds()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
json addressInput(const json &input)
{
    object(input);
    const auto country = text(input, "country", 2);
    require(country.size() == 2 &&
                std::all_of(country.begin(), country.end(), [](char c) { return c >= 'A' && c <= 'Z'; }),
            "country must be a two-letter uppercase code");
    return {{"name", text(input, "name", 160)},
            {"phone", text(input, "phone", 40)},
            {"line1", text(input, "line1", 200)},
            {"line2", text(input, "line2", 200, false)},
            {"city", text(input, "city", 100)},
            {"region", text(input, "region", 100)},
            {"postalCode", text(input, "postalCode", 24)},
            {"country", country}};
}
void inventoryMovement(std::vector<FirestoreClient::Write> &writes, const std::string &productId,
                       int64_t before, int64_t after, const std::string &reason, const std::string &actor,
                       const std::string &orderId)
{
    writes.push_back({"inventoryMovements",
                      drogon::utils::getUuid(),
                      {{"productId", productId},
                       {"stockBefore", before},
                       {"stockAfter", after},
                       {"delta", after - before},
                       {"reason", reason},
                       {"actorId", actor},
                       {"orderId", orderId},
                       {"createdAt", nowSeconds()}},
                      {},
                      true});
}
static json ownedOrder(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin,
                       const std::string &tx = "")
{
    id(orderId);
    auto doc = db.getDocument("orders", orderId, tx);
    // Hide the existence of another customer's order.
    if (doc.is_null() || (!admin && doc.value("userId", "") != uid))
        throw ApiError(404, "ORDER_NOT_FOUND", "Order not found");
    return orderView(doc, orderId);
}
json orderDetail(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin)
{
    return ownedOrder(db, orderId, uid, admin);
}
static json changeOrder(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin,
                        const json &input, bool expiring = false)
{
    object(input);
    const auto next = expiring ? "expired" : text(input, "status", 20);
    const auto reason = text(input, "reason", 500, false);
    const auto expected = text(input, "expectedStatus", 20, false);
    require(expiring || next == "paid" || next == "shipped" || next == "delivered" || next == "cancelled",
            "Unsupported order status");
    if (!admin && next != "cancelled")
        throw ApiError(403, "FORBIDDEN", "Customers can only cancel pending orders");
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto order = ownedOrder(db, orderId, uid, admin, tx);
        const auto previous = order["status"].get<std::string>();
        const auto now = nowSeconds();
        const bool overdue = order.contains("expiresAt") && order["expiresAt"].is_number_integer() &&
                             order["expiresAt"].get<int64_t>() <= now;
        if (expiring && (previous != "pending" || !overdue))
            return order;
        if (!expiring && previous == "pending" && overdue && next != "cancelled")
            throw ApiError(
                409, "ORDER_EXPIRED",
                "The reservation expired. Refresh the order; stock will be released automatically.");
        // Repeat requests must never duplicate inventory adjustments or history.
        if (previous == next)
            return order;
        if (!expected.empty() && expected != previous)
            throw ApiError(409, "ORDER_CHANGED", "The order changed. Refresh its details.");
        const bool allowed =
            expiring || (admin ? ((previous == "pending" && (next == "paid" || next == "cancelled")) ||
                                  (previous == "paid" && (next == "shipped" || next == "cancelled")) ||
                                  (previous == "shipped" && next == "delivered"))
                               : (previous == "pending" && next == "cancelled"));
        if (!allowed)
            throw ApiError(409, "INVALID_ORDER_TRANSITION", "This order cannot move to that status");
        const bool pickup =
            order.contains("shippingMethod") && order["shippingMethod"].value("kind", "delivery") == "pickup";
        if (!pickup && next == "shipped" &&
            (!order.contains("shippingAddress") || order["shippingAddress"].is_null()))
            throw ApiError(409, "ADDRESS_REQUIRED", "Add the delivery address before shipping this order");
        if (!pickup && next == "shipped")
            addressInput(order["shippingAddress"]);
        json changes = {{"status", next}, {"updatedAt", now}, {"pendingExpiresAt", nullptr}};
        auto history = order.value("statusHistory", json::array());
        if (!history.is_array())
            throw ApiError(500, "INVALID_STORED_ORDER", "Order history needs administrator attention");
        history.push_back(
            {{"from", previous}, {"to", next}, {"actorId", uid}, {"createdAt", now}, {"reason", reason}});
        changes["statusHistory"] = history;
        if (next == "cancelled" || expiring)
        {
            require(!order["items"].empty() && order["items"].size() <= MaxCartLines, "Invalid order items");
            std::set<std::string> seen;
            for (const auto &item : order["items"])
            {
                const auto productId = id(item["productId"]);
                require(seen.insert(productId).second, "Duplicate order items need administrator repair");
                auto product = db.getDocument("products", productId, tx);
                if (product.is_null())
                    throw ApiError(409, "STOCK_RECORD_MISSING",
                                   "An inventory record is missing. Restore it before cancellation.");
                const auto before = integer(product, "stock", 0, 2147483647);
                const auto quantity = integer(item, "quantity", 1, MaxQuantity);
                require(before <= 2147483647 - quantity, "Restocked quantity exceeds the stock limit");
                writes.push_back({"products",
                                  productId,
                                  {{"stock", before + quantity}, {"version", drogon::utils::getUuid()}},
                                  {"stock", "version"},
                                  false,
                                  true});
                inventoryMovement(writes, productId, before, before + quantity,
                                  expiring ? "order_expired" : "order_cancelled", uid, orderId);
            }
            changes[expiring ? "expiredAt" : "cancelledAt"] = now;
            changes["cancellationReason"] = reason;
            changes["refundStatus"] = previous == "paid" ? "manual_review_required" : "not_required";
        }
        if (next == "paid")
            changes["paidAt"] = now;
        if (next == "shipped")
            changes["shippedAt"] = now;
        if (next == "delivered")
            changes["deliveredAt"] = now;
        std::vector<std::string> mask;
        for (const auto &field : changes.items())
        {
            mask.push_back(field.key());
            order[field.key()] = field.value();
        }
        writes.push_back({"orders", orderId, changes, mask, false, true});
        if (admin)
            audit(writes, uid, "order_status_changed", orderId, {{"from", previous}, {"to", next}});
        notification(writes, order, "order_" + next,
                     "Your order is now " +
                         (pickup && next == "shipped"     ? "ready for pickup"
                          : pickup && next == "delivered" ? "collected"
                                                          : next) +
                         ".");
        return order;
    });
}
json transitionOrder(FirestoreClient &db, const std::string &orderId, const std::string &uid, bool admin,
                     const json &input)
{
    return changeOrder(db, orderId, uid, admin, input);
}
json expireOrder(FirestoreClient &db, const std::string &orderId)
{
    return changeOrder(db, orderId, "system:expiry", true,
                       {{"reason", "Pending order exceeded its 4-hour confirmation window"}}, true);
}
json setOrderAddress(FirestoreClient &db, const std::string &orderId, const std::string &uid,
                     const json &input)
{
    const auto address = addressInput(input);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto order = ownedOrder(db, orderId, uid, true, tx);
        if (order.contains("shippingAddress") && !order["shippingAddress"].is_null())
        {
            if (order["shippingAddress"] == address)
                return order;
            throw ApiError(409, "ADDRESS_ALREADY_SNAPSHOTTED", "The order address is already recorded");
        }
        if (order["status"] != "pending" && order["status"] != "paid")
            throw ApiError(409, "INVALID_ORDER_TRANSITION", "The address cannot be added at this stage");
        const json changes = {
            {"shippingAddress", address}, {"addressRecordedBy", uid}, {"updatedAt", nowSeconds()}};
        writes.push_back(
            {"orders", orderId, changes, {"shippingAddress", "addressRecordedBy", "updatedAt"}, false, true});
        audit(writes, uid, "order_address_recorded", orderId);
        order.update(changes);
        return order;
    });
}
json archiveProduct(FirestoreClient &db, const std::string &productId, const std::string &actor)
{
    id(productId);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto product = db.getDocument("products", productId, tx);
        if (!product.is_null() && !product.value("archived", false))
        {
            audit(writes, actor, "product_archived", productId);
            writes.push_back({"products",
                              productId,
                              {{"archived", true}, {"archivedAt", nowSeconds()}},
                              {"archived", "archivedAt"},
                              false,
                              true});
        }
        return json{{"success", true}};
    });
}
} // namespace commerce
