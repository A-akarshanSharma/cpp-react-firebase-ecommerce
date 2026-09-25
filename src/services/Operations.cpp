#include "Operations.h"
#include <drogon/utils/Utilities.h>
#include <set>
using namespace validation;
namespace commerce
{
void audit(std::vector<FirestoreClient::Write> &writes, const std::string &actor, const std::string &action,
           const std::string &target, const json &changes)
{
    writes.push_back({"auditLogs",
                      drogon::utils::getUuid(),
                      {{"actorId", actor},
                       {"action", action},
                       {"targetId", target},
                       {"changes", changes},
                       {"createdAt", nowSeconds()}},
                      {},
                      true});
}
void notification(std::vector<FirestoreClient::Write> &writes, const json &order, const std::string &event,
                  const std::string &message)
{
    writes.push_back({"notifications",
                      drogon::utils::getUuid(),
                      {{"userId", order.at("userId")},
                       {"orderId", order.at("id")},
                       {"event", event},
                       {"message", message},
                       {"read", false},
                       {"createdAt", nowSeconds()}},
                      {},
                      true});
}
bool sellable(FirestoreClient &db, const json &product, const std::string &tx)
{
    if (product.is_null() || product.value("archived", false))
        return false;
    const auto parent = product.value("parentProductId", "");
    if (parent.empty())
        return true;
    const auto root = db.getDocument("products", parent, tx);
    return !root.is_null() && !root.value("archived", false);
}
json shippingSettings(FirestoreClient &db, const std::string &tx)
{
    auto doc = db.getDocument("storeSettings", "shipping", tx);
    return doc.is_null() ? json{{"methods", json::array()}, {"version", "initial"}} : doc;
}
json saveShipping(FirestoreClient &db, const std::string &actor, const json &input)
{
    object(input);
    require(input.contains("methods") && input["methods"].is_array() && input["methods"].size() <= 20,
            "Supply up to 20 shipping methods");
    const auto expected = text(input, "version", 128);
    json methods = json::array();
    std::set<std::string> ids;
    for (const auto &method : input["methods"])
    {
        object(method);
        const auto methodId = id(text(method, "id", 128));
        require(ids.insert(methodId).second, "Shipping method IDs must be unique");
        require(method.contains("active") && method["active"].is_boolean(), "active must be true or false");
        require(method.contains("countries") && method["countries"].is_array() &&
                    method["countries"].size() <= 100,
                "Supply country codes");
        for (const auto &country : method["countries"])
            require(country.is_string() && country.get<std::string>().size() == 2 &&
                        std::all_of(country.get_ref<const std::string &>().begin(),
                                    country.get_ref<const std::string &>().end(),
                                    [](char c) { return c >= 'A' && c <= 'Z'; }),
                    "Country codes must have two uppercase letters");
        const auto kind = method.contains("kind") ? text(method, "kind", 20) : "delivery";
        require(kind == "delivery" || kind == "pickup", "Choose delivery or pickup");
        const auto instructions = text(method, "pickupInstructions", 500, kind == "pickup");
        const auto fee = minorUnits(method, "fee", MaxPrice);
        methods.push_back({{"id", methodId},
                           {"name", text(method, "name", 100)},
                           {"feeMinor", fee},
                           {"fee", fee / 100.0},
                           {"active", method["active"]},
                           {"kind", kind},
                           {"pickupInstructions", instructions},
                           {"countries", method["countries"]},
                           {"estimatedDays", text(method, "estimatedDays", 80, false)}});
    }
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        const auto before = shippingSettings(db, tx);
        if (before["methods"] == methods)
            return before;
        if (before.value("version", "initial") != expected)
            throw ApiError(409, "SETTINGS_CHANGED", "Shipping settings changed. Reload before saving.");
        json after = {{"methods", methods}, {"version", drogon::utils::getUuid()}};
        writes.push_back({"storeSettings", "shipping", after, {"methods", "version"}});
        audit(writes, actor, "shipping_settings_updated", "shipping",
              {{"before", before["methods"]}, {"after", methods}});
        return after;
    });
}
json shippingQuote(FirestoreClient &db, const std::string &tx, const json &body, const json &address)
{
    const auto settings = shippingSettings(db, tx);
    const auto selected = text(body, "shippingMethodId", 128, false);
    bool active = false;
    for (const auto &m : settings.at("methods"))
    {
        if (!m.at("active").get<bool>())
            continue;
        active = true;
        if (m["id"] != selected)
            continue;
        const auto pickup = m.value("kind", "delivery") == "pickup";
        if (!pickup)
            require(!address.is_null(), "A shipping address is required");
        const auto countries = m.at("countries");
        if (!pickup && !countries.empty() &&
            std::find(countries.begin(), countries.end(), address["country"]) == countries.end())
            throw ApiError(409, "SHIPPING_UNAVAILABLE", "This shipping method does not serve your country");
        const auto fee = integer(m, "feeMinor", 0, MaxPrice);
        if (integer(body, "shippingFeeMinor", 0, MaxPrice) != fee)
            throw ApiError(409, "SHIPPING_CHANGED", "Shipping price changed. Review your total again.");
        return m;
    }
    if (!selected.empty() || active)
        throw ApiError(409, "SHIPPING_UNAVAILABLE", "Select an available shipping method");
    throw ApiError(409, "CHECKOUT_DISABLED",
                   "Checkout is temporarily unavailable. Please contact the store.");
}
json saveShipment(FirestoreClient &db, const std::string &orderId, const std::string &actor,
                  const json &input)
{
    id(orderId);
    object(input);
    const auto expected = text(input, "version", 128);
    const auto url = text(input, "trackingUrl", 2048, false);
    require(url.empty() || url.rfind("https://", 0) == 0, "Tracking link must use HTTPS");
    json shipment = {{"carrier", text(input, "carrier", 100)},
                     {"trackingNumber", text(input, "trackingNumber", 160)},
                     {"trackingUrl", url}};
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto order = db.getDocument("orders", orderId, tx);
        if (order.is_null())
            throw ApiError(404, "ORDER_NOT_FOUND", "Order not found");
        const auto old = order.value("shipment", json::object());
        auto comparable = old;
        comparable.erase("version");
        comparable.erase("updatedAt");
        if (comparable == shipment)
        {
            order["id"] = orderId;
            return order;
        }
        if (order["status"] != "paid" && order["status"] != "shipped")
            throw ApiError(409, "INVALID_ORDER_TRANSITION",
                           "Tracking can be recorded for paid or shipped orders");
        if (!order.contains("shippingAddress") || order["shippingAddress"].is_null())
            throw ApiError(409, "ADDRESS_REQUIRED", "Record the delivery address first");
        if (old.value("version", "initial") != expected)
            throw ApiError(409, "SHIPMENT_CHANGED", "Tracking changed. Refresh before editing.");
        auto saved = shipment;
        saved["version"] = drogon::utils::getUuid();
        saved["updatedAt"] = nowSeconds();
        writes.push_back({"orders",
                          orderId,
                          {{"shipment", saved}, {"updatedAt", saved["updatedAt"]}},
                          {"shipment", "updatedAt"},
                          false,
                          true});
        order["id"] = orderId;
        order["shipment"] = saved;
        order["updatedAt"] = saved["updatedAt"];
        audit(writes, actor, "shipment_updated", orderId, {{"before", old}, {"after", saved}});
        notification(writes, order, "tracking_updated",
                     "Tracking information for your order has been updated.");
        return order;
    });
}
json markNotificationRead(FirestoreClient &db, const std::string &notificationId, const std::string &uid)
{
    id(notificationId);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto doc = db.getDocument("notifications", notificationId, tx);
        if (doc.is_null() || doc.value("userId", "") != uid)
            throw ApiError(404, "NOTIFICATION_NOT_FOUND", "Notification not found");
        if (!doc.value("read", false))
            writes.push_back({"notifications", notificationId, {{"read", true}}, {"read"}, false, true});
        return json{{"success", true}};
    });
}
json createVariant(FirestoreClient &db, const std::string &parentId, const std::string &actor,
                   const json &input)
{
    id(parentId);
    object(input);
    const auto label = text(input, "variantLabel", 80);
    auto sku = id(text(input, "sku", 80));
    std::transform(sku.begin(), sku.end(), sku.begin(),
                   [](unsigned char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; });
    const auto variantId = drogon::utils::getUuid();
    const auto fields = productInput(input);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        const auto parent = db.getDocument("products", parentId, tx);
        if (parent.is_null() || parent.value("archived", false))
            throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
        require(parent.value("parentProductId", "").empty(), "Variants cannot have child variants");
        auto skuDoc = db.getDocument("productSkus", sku, tx);
        if (!skuDoc.is_null())
            throw ApiError(409, "SKU_EXISTS", "This SKU is already used");
        auto product = fields;
        product["version"] = drogon::utils::getUuid();
        product["parentProductId"] = parentId;
        product["variantLabel"] = label;
        product["sku"] = sku;
        writes.push_back({"productSkus", sku, {{"productId", variantId}}, {}, true});
        writes.push_back({"products", variantId, product, {}, true});
        writes.push_back({"products", parentId, {{"hasVariants", true}}, {"hasVariants"}, false, true});
        inventoryMovement(writes, variantId, 0, fields["stock"], "product_created", actor);
        audit(writes, actor, "variant_created", variantId,
              {{"parentProductId", parentId}, {"sku", sku}, {"label", label}});
        return json{{"success", true}, {"id", variantId}};
    });
}
json catalogView(const json &documents)
{
    std::set<std::string> available;
    for (const auto &p : documents)
    {
        const auto parent = p.value("parentProductId", "");
        if (!parent.empty() && !p.value("archived", false) && integer(p, "stock", 0, 2147483647) > 0)
            available.insert(parent);
    }
    json rows = json::array();
    for (const auto &p : documents)
        if (!p.value("archived", false) && p.value("parentProductId", "").empty())
        {
            auto row = productView(p, p.at("id"));
            row["hasAvailableVariants"] = available.count(p.at("id").get<std::string>()) > 0;
            rows.push_back(row);
        }
    return rows;
}
json variants(FirestoreClient &db, const std::string &parentId)
{
    id(parentId);
    auto parent = db.getDocument("products", parentId);
    if (!sellable(db, parent))
        throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
    json rows = json::array();
    for (const auto &p : db.listDocuments("products"))
        if (p.value("parentProductId", "") == parentId && !p.value("archived", false))
            rows.push_back(productView(p, p.at("id")));
    return rows;
}
json changeRole(FirestoreClient &db, const std::string &target, const std::string &actor, const json &input)
{
    id(target);
    id(actor);
    const auto role = text(input, "role", 20);
    require(role == "admin" || role == "customer", "role must be admin or customer");
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        // All role changes share this document, including the first change on legacy stores.
        // This serializes concurrent changes without maintaining a potentially stale admin count.
        db.getDocument("storeSettings", "adminRoles", tx);
        const auto acting = db.getDocument("users", actor, tx);
        if (acting.is_null() || acting.value("role", "customer") != "admin")
            throw ApiError(403, "FORBIDDEN", "Your account no longer has administrator access");
        auto doc = db.getDocument("users", target, tx);
        if (doc.is_null())
            throw ApiError(404, "USER_NOT_FOUND", "User not found");
        if (doc.value("role", "customer") != role)
        {
            if (doc.value("role", "customer") == "admin" && role == "customer")
            {
                const auto admins = db.queryEqual("users", "role", "admin", tx);
                if (admins.size() <= 1)
                    throw ApiError(409, "LAST_ADMIN",
                                   "Keep at least one administrator. Promote another account before removing "
                                   "this access.");
            }
            writes.push_back({"users", target, {{"role", role}}, {"role"}, false, true});
            audit(writes, actor, "role_changed", target,
                  {{"before", doc.value("role", "customer")}, {"after", role}});
            writes.push_back(
                {"storeSettings", "adminRoles", {{"version", drogon::utils::getUuid()}}, {"version"}});
        }
        return json{{"success", true}};
    });
}
} // namespace commerce
