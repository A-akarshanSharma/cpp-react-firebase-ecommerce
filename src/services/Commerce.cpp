#include "Commerce.h"
#include "Operations.h"
#include <chrono>
#include <drogon/utils/Utilities.h>
#include <iomanip>
#include <openssl/sha.h>
#include <set>
#include <sstream>
using namespace validation;
namespace commerce
{
std::string fingerprint(const std::string &value)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char *>(value.data()), value.size(), hash);
    std::ostringstream result;
    for (auto byte : hash)
        result << std::hex << std::setw(2) << std::setfill('0') << int(byte);
    return result.str();
}
json cartItems(const json &doc, bool checkout)
{
    if (doc.is_null())
        return json::array();
    try
    {
        object(doc);
        require(doc.contains("items") && doc["items"].is_array(), "Invalid cart items");
        // Allow removal from legacy oversized carts, but never check them out.
        if (checkout)
            require(doc["items"].size() <= MaxCartLines, "Too many cart items");
        std::set<std::string> ids;
        for (const auto &item : doc["items"])
        {
            object(item);
            auto productId = id(text(item, "productId", 128));
            integer(item, "quantity", checkout ? 1 : -2147483647LL, 2147483647);
            if (checkout)
            {
                integer(item, "quantity", 1, MaxQuantity);
                require(ids.insert(productId).second, "Duplicate cart item");
            }
        }
        return doc["items"];
    }
    catch (const ApiError &)
    {
        throw ApiError(409, "INVALID_CART",
                       "Your cart contains invalid items. Remove or correct them before checkout.");
    }
}
std::string cartVersion(const json &doc)
{
    if (!doc.is_null() && doc.contains("version") && doc["version"].is_string())
        return doc["version"].get<std::string>();
    return "legacy-" + fingerprint(doc.is_null() ? "[]" : doc.dump());
}
std::string quoteVersion(const std::string &uid, const std::string &version, const json &items)
{
    json prices = json::array();
    for (const auto &item : items)
        prices.push_back({item.at("productId"), item.at("quantity"), item.at("priceMinor")});
    return fingerprint(json::array({uid, version, prices}).dump());
}
json getCart(FirestoreClient &db, const std::string &uid)
{
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &) {
        const auto doc = db.getDocument("carts", uid, tx);
        auto items = cartItems(doc);
        for (auto &item : items)
        {
            const auto product = db.getDocument("products", item["productId"], tx);
            item["cartVersion"] = cartVersion(doc);
            if (!sellable(db, product, tx))
            {
                item["name"] = "No longer available";
                item["price"] = 0;
                item["priceMinor"] = 0;
                item["stock"] = 0;
                item["available"] = false;
            }
            else
            {
                const auto p = productView(product, item["productId"]);
                for (const auto *field : {"name", "price", "priceMinor", "imageUrl", "stock", "currency"})
                    item[field] = p[field];
                const auto qty = item["quantity"].get<int64_t>();
                item["available"] = qty > 0 && qty <= MaxQuantity && qty <= p["stock"].get<int64_t>();
            }
        }
        const auto quote = quoteVersion(uid, cartVersion(doc), items);
        for (auto &item : items)
            item["quoteVersion"] = quote;
        return items;
    });
}
json mutateCart(FirestoreClient &db, const std::string &uid, const std::string &action, const json &body)
{
    object(body);
    const auto productId = id(text(body, "productId", 128));
    require(action == "add" || action == "update" || action == "remove", "Invalid cart operation");
    int64_t requested = 0;
    if (action != "remove")
        requested = action == "add" && !body.contains("quantity")
                        ? 1
                        : integer(body, "quantity", action == "add" ? 1 : 0, MaxQuantity);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto doc = db.getDocument("carts", uid, tx);
        auto items = cartItems(doc);
        auto found = std::find_if(items.begin(), items.end(),
                                  [&](const json &i) { return i["productId"] == productId; });
        int64_t quantity = requested;
        if (action == "add" && found != items.end())
        {
            const auto old = (*found)["quantity"].get<int64_t>();
            require(old > 0 && old <= MaxQuantity, "Correct this cart quantity before adding more");
            quantity += old;
            require(quantity <= MaxQuantity, "Cart quantity is too large");
        }
        if (action != "remove" && quantity > 0)
        {
            auto docProduct = db.getDocument("products", productId, tx);
            if (!sellable(db, docProduct, tx))
                throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
            const auto p = productView(docProduct, productId);
            if (quantity > p["stock"].get<int64_t>())
                throw ApiError(409, "STOCK_UNAVAILABLE", "Not enough stock available",
                               {{"available", p["stock"]}, {"requested", quantity}});
        }
        if (action == "remove" || quantity == 0)
        {
            items.erase(std::remove_if(items.begin(), items.end(),
                                       [&](const json &i) { return i["productId"] == productId; }),
                        items.end());
        }
        else if (found != items.end())
            (*found)["quantity"] = quantity;
        else if (action == "add")
        {
            require(items.size() < MaxCartLines, "Your cart has reached its item limit");
            items.push_back({{"productId", productId}, {"quantity", quantity}});
        }
        else
            throw ApiError(404, "CART_ITEM_NOT_FOUND", "This item is not in your cart");
        writes.push_back(
            {"carts", uid, {{"items", items}, {"version", drogon::utils::getUuid()}}, {"items", "version"}});
        return json{{"success", true}, {"cart", items}};
    });
}
json checkout(FirestoreClient &db, const std::string &uid, const std::string &key, const json &body)
{
    object(body);
    for (const auto &entry : body.items())
        require(entry.key() == "cartVersion" || entry.key() == "shippingAddress" ||
                    entry.key() == "shippingMethodId" || entry.key() == "shippingFeeMinor" ||
                    entry.key() == "quoteVersion" || entry.key() == "pickupContact",
                "Unsupported checkout field");
    const auto address =
        body.contains("shippingAddress") ? addressInput(body["shippingAddress"]) : json(nullptr);
    const auto expectedVersion = text(body, "cartVersion", 128, false);
    const auto expectedQuote = text(body, "quoteVersion", 64, false);
    if (!key.empty())
    {
        require(key.size() >= 8 && key.size() <= 128, "Idempotency-Key must contain 8 to 128 characters");
        require(std::all_of(key.begin(), key.end(), [](unsigned char c) { return c >= 33 && c <= 126; }),
                "Invalid Idempotency-Key");
    }
    const auto operationId = fingerprint(json::array({uid, "POST /orders", key}).dump());
    const auto requestHash = fingerprint(body.dump());
    const auto orderId = drogon::utils::getUuid();
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        if (!key.empty())
        {
            const auto existing = db.getDocument("checkoutRequests", operationId, tx);
            if (!existing.is_null())
            {
                if (existing.value("requestHash", "") != requestHash)
                    throw ApiError(409, "IDEMPOTENCY_CONFLICT",
                                   "This checkout key was already used for a different request");
                if (!existing.contains("response") || !existing["response"].is_object())
                    throw ApiError(500, "INVALID_CHECKOUT_RECORD",
                                   "The saved checkout result needs administrator attention");
                return existing["response"];
            }
        }
        auto cart = db.getDocument("carts", uid, tx);
        if (!expectedVersion.empty() && expectedVersion != cartVersion(cart))
            throw ApiError(409, "CART_CHANGED", "Your cart changed. Review it before placing your order.");
        const auto items = cartItems(cart, true);
        if (items.empty())
            throw ApiError(400, "EMPTY_CART", "Cart is empty");
        const auto shipping = shippingQuote(db, tx, body, address);
        json contact = nullptr;
        if (shipping.value("kind", "delivery") == "pickup")
        {
            require(body.contains("pickupContact"), "Supply pickup contact details");
            contact = {{"name", text(body["pickupContact"], "name", 160)},
                       {"phone", text(body["pickupContact"], "phone", 40)}};
        }
        json snapshots = json::array();
        int64_t total = 0;
        for (const auto &item : items)
        {
            const auto productId = item["productId"].get<std::string>();
            auto doc = db.getDocument("products", productId, tx);
            if (!sellable(db, doc, tx))
                throw ApiError(409, "STOCK_UNAVAILABLE", "Product no longer available");
            const auto product = productView(doc, productId);
            const auto quantity = item["quantity"].get<int>();
            const auto stock = product["stock"].get<int>();
            if (quantity > stock)
                throw ApiError(409, "STOCK_UNAVAILABLE",
                               "Not enough stock for " + product["name"].get<std::string>(),
                               {{"available", stock}, {"requested", quantity}});
            const auto price = product["priceMinor"].get<int64_t>();
            const auto line = lineTotal(price, quantity);
            require(total <= MaxMoney - line, "Order amount is too large");
            total += line;
            snapshots.push_back({{"productId", productId},
                                 {"name", product["name"]},
                                 {"quantity", quantity},
                                 {"priceMinor", price},
                                 {"price", price / 100.0}});
            for (const auto *field : {"parentProductId", "variantLabel", "sku"})
                if (doc.contains(field))
                    snapshots.back()[field] = doc[field];
            // Write masks preserve product metadata, including future fields.
            writes.push_back({"products",
                              productId,
                              {{"stock", stock - quantity}, {"version", drogon::utils::getUuid()}},
                              {"stock", "version"},
                              false,
                              true});
            inventoryMovement(writes, productId, stock, stock - quantity, "order_placed", uid, orderId);
        }
        // Compare inside the stock/order transaction; a concurrent price edit causes
        // a transaction retry and is checked again before any writes can commit.
        if (expectedQuote.empty())
            throw ApiError(409, "QUOTE_REQUIRED", "Review checkout before placing your order.");
        if (expectedQuote != quoteVersion(uid, cartVersion(cart), snapshots))
            throw ApiError(409, "PRICE_CHANGED",
                           "Prices changed. Review the updated items and total, then confirm again.");
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        const auto subtotal = total;
        const auto shippingFee = shipping["feeMinor"].get<int64_t>();
        require(total <= MaxMoney - shippingFee, "Order amount is too large");
        total += shippingFee;
        json order = {{"userId", uid},          {"items", snapshots}, {"totalMinor", total},
                      {"total", total / 100.0}, {"currency", "INR"},  {"status", "pending"},
                      {"createdAt", now}};
        order["expiresAt"] = now + 4 * 60 * 60;
        order["pendingExpiresAt"] = order["expiresAt"];
        order["pickupContact"] = contact;
        order["shippingAddress"] = shipping.value("kind", "delivery") == "pickup" ? json(nullptr) : address;
        order["shippingMethod"] = shipping;
        order["shippingFeeMinor"] = shippingFee;
        order["subtotalMinor"] = subtotal;
        order["statusHistory"] = json::array(
            {{{"from", ""}, {"to", "pending"}, {"actorId", uid}, {"createdAt", now}, {"reason", ""}}});
        order["updatedAt"] = now;
        writes.push_back({"orders", orderId, order, {}, true});
        writes.push_back({"carts",
                          uid,
                          {{"items", json::array()}, {"version", drogon::utils::getUuid()}},
                          {"items", "version"}});
        order["id"] = orderId;
        notification(writes, order, "order_placed", "Your order has been placed.");
        if (!key.empty())
            writes.push_back(
                {"checkoutRequests",
                 operationId,
                 {{"userId", uid}, {"requestHash", requestHash}, {"response", order}, {"createdAt", now}},
                 {},
                 true});
        return order;
    });
}
json saveProduct(FirestoreClient &db, const std::string &productId, const json &body, bool create,
                 const std::string &actor)
{
    auto fields = productInput(body, create);
    fields["version"] = drogon::utils::getUuid();
    const auto target = create ? drogon::utils::getUuid() : id(productId);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto existing = db.getDocument("products", target, tx);
        if (!create && (existing.is_null() || existing.value("archived", false)))
            throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
        if (!create && text(body, "version", 128) != productVersion(existing))
            throw ApiError(409, "PRODUCT_CHANGED",
                           "This product changed. Reload the latest product before saving.");
        const auto before = existing.is_null() ? 0 : integer(existing, "stock", 0, 2147483647);
        const auto after = create ? fields["stock"].get<int64_t>() : before;
        writes.push_back({"products", target, fields,
                          create ? std::vector<std::string>{}
                                 : std::vector<std::string>{"name", "description", "category", "imageUrl",
                                                            "price", "priceMinor", "currency", "version"},
                          create, !create});
        if (create || before != after)
            inventoryMovement(writes, target, before, after, create ? "product_created" : "admin_adjustment",
                              actor);
        audit(writes, actor, create ? "product_created" : "product_updated", target,
              {{"before", existing.is_null() ? json(nullptr)
                                             : json{{"name", existing.value("name", "")},
                                                    {"stock", before},
                                                    {"price", existing.value("price", 0.0)}}},
               {"after", fields}});
        return json{{"id", target}, {"success", true}, {"version", fields["version"]}};
    });
}
json adjustStock(FirestoreClient &db, const std::string &productId, const json &body,
                 const std::string &actor)
{
    id(productId);
    const auto expected = text(body, "version", 128);
    const auto delta = integer(body, "delta", -2147483647LL, 2147483647);
    require(delta != 0, "Stock adjustment must not be zero");
    const auto reason = text(body, "reason", 500);
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto p = db.getDocument("products", productId, tx);
        if (!sellable(db, p, tx))
            throw ApiError(404, "PRODUCT_NOT_FOUND", "Product not found");
        if (expected != productVersion(p))
            throw ApiError(409, "PRODUCT_CHANGED",
                           "Stock or product details changed. Reload before adjusting stock.");
        const auto before = integer(p, "stock", 0, 2147483647);
        const auto after = before + delta;
        require(after >= 0 && after <= 2147483647, "Resulting stock must be between 0 and 2,147,483,647");
        const auto version = drogon::utils::getUuid();
        writes.push_back({"products",
                          productId,
                          {{"stock", after}, {"version", version}},
                          {"stock", "version"},
                          false,
                          true});
        inventoryMovement(writes, productId, before, after, "admin_adjustment", actor);
        writes.back().fields["note"] = reason;
        audit(writes, actor, "stock_adjusted", productId,
              {{"before", before}, {"after", after}, {"delta", delta}, {"reason", reason}});
        p["stock"] = after;
        p["version"] = version;
        return json{{"success", true}, {"product", productView(p, productId)}};
    });
}
json ensureProfile(FirestoreClient &db, const std::string &uid, const std::string &email)
{
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        auto doc = db.getDocument("users", uid, tx);
        if (doc.is_null())
        {
            doc = {{"email", email}, {"role", "customer"}};
            writes.push_back({"users", uid, doc, {}, true});
        }
        if (!doc.contains("role") || !doc["role"].is_string())
            throw ApiError(500, "INVALID_PROFILE", "Your account record needs administrator attention");
        return doc;
    });
}
} // namespace commerce
