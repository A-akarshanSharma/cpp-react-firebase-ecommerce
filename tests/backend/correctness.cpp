#include "controllers/ControllerSupport.h"
#include "models/Order.h"
#include "models/Product.h"
#include "services/Uploads.h"
#include <atomic>
#include <condition_variable>
#include <drogon/utils/Utilities.h>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
using json = nlohmann::json;
using Response = FirestoreClient::Response;
void check(bool value, const std::string &message)
{
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void apiError(F fn, int status)
{
    try
    {
        fn();
    }
    catch (const ApiError &e)
    {
        check(e.status == status, "Wrong API status");
        return;
    }
    throw std::runtime_error("Expected API failure");
}
template <class F> void dbError(F fn)
{
    try
    {
        fn();
    }
    catch (const DatabaseError &)
    {
        return;
    }
    throw std::runtime_error("Expected database failure");
}
json deliveryAddress();
json checkoutDelivery(FirestoreClient &db, const std::string &uid, const std::string &key, json body)
{
    if (!body.contains("shippingAddress"))
        body["shippingAddress"] = deliveryAddress();
    if (!body.contains("shippingMethodId"))
        body["shippingMethodId"] = "fixture-delivery";
    if (!body.contains("shippingFeeMinor"))
        body["shippingFeeMinor"] = 0;
    return commerce::checkout(db, uid, key, body);
}
// A transport-level MVCC simulator: production serialization, transaction retries,
// masks, preconditions and business code run unchanged. No production credentials.
class MemoryDatabase
{
    struct Doc
    {
        json fields;
        int version;
    };
    struct Transaction
    {
        std::map<std::string, Doc> snapshot;
        std::map<std::string, int> reads;
    };
    std::mutex mutex;
    std::map<std::string, std::string> reviewedQuotes;
    std::condition_variable barrier;
    std::map<std::string, Doc> docs;
    std::map<std::string, Transaction> transactions;
    int sequence = 0, txId = 0, waiting = 0;

  public:
    MemoryDatabase()
    {
        seed("storeSettings/shipping", {{"version", "initial"},
                                        {"methods", json::array({{{"id", "fixture-delivery"},
                                                                  {"name", "Fixture delivery"},
                                                                  {"kind", "delivery"},
                                                                  {"fee", 0},
                                                                  {"feeMinor", 0},
                                                                  {"active", true},
                                                                  {"countries", json::array()},
                                                                  {"estimatedDays", ""}}})}});
    }
    std::string failRead;
    std::string synchronizedPrefix = "products/";
    bool synchronizeReads = false, failAfterCommit = false, alwaysAbort = false;
    int failWriteIndex = -1;
    int committed = 0;
    // Existing fault/concurrency scenarios prepare customer intent from fixture
    // data without consuming transport failures or synchronization barriers.
    // Cache per request so retries preserve the original reviewed prices.
    json reviewedCheckout(FirestoreClient &db, const std::string &uid, const std::string &key, json body)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto &quote = reviewedQuotes[uid + ":" + key];
            if (quote.empty())
            {
                auto cart = docs.count("carts/" + uid) ? docs.at("carts/" + uid).fields : json(nullptr);
                json items = cart.is_null() ? json::array() : cart.value("items", json::array());
                for (auto &item : items)
                {
                    const auto path = "products/" + item.at("productId").get<std::string>();
                    item["priceMinor"] = docs.count(path)
                                             ? validation::productView(docs.at(path).fields,
                                                                       item.at("productId"))["priceMinor"]
                                             : json(0);
                }
                quote = commerce::quoteVersion(uid, commerce::cartVersion(cart), items);
            }
            body["quoteVersion"] = quote;
        }
        return checkoutDelivery(db, uid, key, body);
    }
    void seed(const std::string &key, json value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        docs[key] = {std::move(value), ++sequence};
    }
    json read(const std::string &key)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return docs.count(key) ? docs.at(key).fields : json(nullptr);
    }
    int count(const std::string &prefix)
    {
        std::lock_guard<std::mutex> lock(mutex);
        int n = 0;
        for (const auto &[key, value] : docs)
            if (key.rfind(prefix, 0) == 0)
                ++n;
        return n;
    }
    FirestoreClient client()
    {
        return FirestoreClient(
            "test-project", [this](const auto &m, const auto &p, const auto &b) { return request(m, p, b); });
    }
    Response request(const std::string &method, const std::string &path, const json &body)
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto pos = path.find("/documents");
        check(pos != std::string::npos, "Missing database resource path");
        auto tail = path.substr(pos + 10);
        const auto error = [](int status, const std::string &code) {
            return Response{status, {{"error", {{"status", code}}}}};
        };
        if (tail == ":beginTransaction")
        {
            check(body.contains("options"), "Missing transaction options");
            const auto tx = "tx-" + std::to_string(++txId) + "+/=";
            transactions[tx] = {docs, {}};
            return {200, {{"transaction", tx}}};
        }
        if (tail == ":rollback")
        {
            transactions.erase(body.at("transaction"));
            return {200, json::object()};
        }
        if (tail == ":commit")
        {
            Transaction *tx = nullptr;
            if (body.contains("transaction"))
            {
                const auto token = body.at("transaction").get<std::string>();
                check(transactions.count(token), "Unknown transaction token");
                tx = &transactions.at(token);
                for (const auto &[key, version] : tx->reads)
                    if ((docs.count(key) ? docs.at(key).version : 0) != version)
                        return error(409, "ABORTED");
                if (alwaysAbort)
                    return error(409, "ABORTED");
            }
            auto staged = docs;
            int index = 0;
            for (const auto &write : body.at("writes"))
            {
                if (index++ == failWriteIndex)
                    return error(503, "UNAVAILABLE");
                const auto name = write.at("update").at("name").get<std::string>();
                const auto key = name.substr(name.find("/documents/") + 11);
                if (write.contains("currentDocument"))
                {
                    const bool exists = write.at("currentDocument").at("exists");
                    if (bool(docs.count(key)) != exists)
                        return error(400, "FAILED_PRECONDITION");
                }
                auto fields = FirestoreClient::fromFields(write.at("update"));
                if (write.contains("updateMask"))
                {
                    json merged = staged.count(key) ? staged[key].fields : json::object();
                    for (const auto &field : write.at("updateMask").at("fieldPaths"))
                    {
                        auto f = field.get<std::string>();
                        if (fields.contains(f))
                            merged[f] = fields[f];
                        else
                            merged.erase(f);
                    }
                    fields = merged;
                }
                staged[key] = {fields, ++sequence};
            }
            docs = std::move(staged);
            ++committed;
            if (body.contains("transaction"))
                transactions.erase(body.at("transaction"));
            if (failAfterCommit)
            {
                failAfterCommit = false;
                throw DatabaseError(503, "UNAVAILABLE");
            }
            return {200,
                    {{"writeResults", std::vector<json>(body.at("writes").size(),
                                                        {{"updateTime", "2026-09-12T00:00:00Z"}})}}};
        }
        if (tail == ":runQuery")
        {
            auto &tx = transactions.at(body.at("transaction").get<std::string>());
            const auto &q = body.at("structuredQuery");
            const auto prefix = q.at("from").at(0).at("collectionId").get<std::string>() + "/";
            const auto &filter = q.at("where").at("fieldFilter");
            check(filter.at("op") == "EQUAL", "Unexpected query operator");
            const auto field = filter.at("field").at("fieldPath").get<std::string>();
            const auto value = FirestoreClient::fromFields({{"fields", {{"v", filter.at("value")}}}}).at("v");
            if (failRead == prefix)
                return error(503, "UNAVAILABLE");
            json rows = json::array();
            for (const auto &[key, doc] : tx.snapshot)
                if (key.rfind(prefix, 0) == 0 && doc.fields.contains(field) && doc.fields.at(field) == value)
                {
                    tx.reads[key] = doc.version;
                    rows.push_back({{"document",
                                     {{"name", "projects/test-project/databases/(default)/documents/" + key},
                                      {"fields", FirestoreClient::toFields(doc.fields)}}}});
                }
            if (rows.empty())
                rows.push_back({{"readTime", "2026-09-15T00:00:00Z"}});
            return {200, rows};
        }
        if (method == "GET" || tail == ":batchGet")
        {
            auto query = tail.find('?');
            const auto name = tail == ":batchGet" ? body.at("documents").at(0).get<std::string>() : "";
            const auto key = tail == ":batchGet" ? name.substr(name.find("/documents/") + 11)
                                                 : drogon::utils::urlDecode(tail.substr(
                                                       1, query == std::string::npos ? query : query - 1));
            if (key == failRead)
                return error(503, "UNAVAILABLE");
            std::map<std::string, Doc> *view = &docs;
            if (tail == ":batchGet")
            {
                auto token = body.at("transaction").get<std::string>();
                check(transactions.count(token), "Transaction token is incorrect");
                auto &tx = transactions.at(token);
                view = &tx.snapshot;
                tx.reads[key] = view->count(key) ? view->at(key).version : 0;
                if (synchronizeReads && key.rfind(synchronizedPrefix, 0) == 0 && waiting < 2)
                {
                    ++waiting;
                    if (waiting == 2)
                        barrier.notify_all();
                    else
                        check(barrier.wait_for(lock, std::chrono::seconds(3), [&] { return waiting >= 2; }),
                              "Concurrent test barrier timed out");
                }
            }
            if (tail == ":batchGet")
                return {200, json::array(
                                 {view->count(key)
                                      ? json{{"found",
                                              {{"name", name},
                                               {"fields", FirestoreClient::toFields(view->at(key).fields)}}}}
                                      : json{{"missing", name}}})};
            if (!view->count(key))
                return error(404, "NOT_FOUND");
            return {200,
                    {{"name", "projects/test-project/databases/(default)/documents/" + key},
                     {"fields", FirestoreClient::toFields(view->at(key).fields)}}};
        }
        throw std::runtime_error("Unexpected REST operation: " + method + " " + tail);
    }
};
json product(int stock = 10, double price = 0.10)
{
    return {{"name", "Cup"},  {"description", ""}, {"category", "Home"},
            {"imageUrl", ""}, {"stock", stock},    {"price", price}};
}
json cart(int quantity = 1, std::string id = "cup")
{
    return {{"items", json::array({{{"productId", id}, {"quantity", quantity}}})}, {"version", "cart-v1"}};
}
json productEdit(json p)
{
    p["version"] = validation::productVersion(p);
    p.erase("stock");
    return p;
}
json deliveryAddress()
{
    return {{"name", "Buyer"},         {"phone", "1234567890"},  {"line1", "1 Test Street"}, {"city", "Pune"},
            {"region", "Maharashtra"}, {"postalCode", "411001"}, {"country", "IN"}};
}
json shippingInput()
{
    return {{"version", "initial"},
            {"methods", json::array({{{"id", "standard"},
                                      {"name", "Standard"},
                                      {"fee", 12.50},
                                      {"active", true},
                                      {"countries", json::array({"IN"})},
                                      {"estimatedDays", "3-5 days"}}})}};
}
const std::string imageFixture = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAGHRFWHRDb21tZW50AHByaXZhdGU"
                                 "tbWV0YWRhdGFvg3uxAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==";
int main()
{
    int passed = 0;
    const auto test = [&](const std::string &name, const auto &work) {
        work();
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    try
    {
        test("validate quantity types, boundaries, paths and product money", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            for (const auto &quantity :
                 json::array({-1, 0, 1.5, "2", nullptr, true, 1000001, 18446744073709551615ULL}))
                apiError(
                    [&] {
                        commerce::mutateCart(db, "u", "add", {{"productId", "cup"}, {"quantity", quantity}});
                    },
                    400);
            apiError(
                [&] { commerce::mutateCart(db, "u", "update", {{"productId", "cup"}, {"quantity", -1}}); },
                400);
            apiError([&] { commerce::mutateCart(db, "u", "add", {{"productId", "../users/admin"}}); }, 400);
            for (const auto &bad : json::array({nullptr, 1, "bad", json::array()}))
                apiError([&] { validation::productInput(bad); }, 400);
            for (const auto &price : json::array({-1, 1.234, "12", true, 1e40}))
            {
                auto p = product();
                p["price"] = price;
                apiError([&] { validation::productInput(p); }, 400);
            }
            auto p = product();
            p["stock"] = 2147483648LL;
            apiError([&] { validation::productInput(p); }, 400);
            check(store.committed == 0, "Invalid input performed a write");
        });
        test("exact paise, legacy reads and malformed stored products", [] {
            auto p = validation::productInput(product(2, 19.99));
            check(p["priceMinor"] == 1999, "Incorrect paise conversion");
            check(validation::lineTotal(1999, 3) == 5997, "Incorrect integer multiplication");
            p["price"] = 999;
            check(validation::productView(p, "p")["price"] == 19.99, "Minor units are not authoritative");
            apiError([] { validation::lineTotal(validation::MaxPrice, validation::MaxQuantity); }, 400);
            p = product();
            p["stock"] = -1;
            apiError([&] { validation::productView(p, "p"); }, 500);
        });
        test("cart add/update/remove preserve contract and metadata", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            commerce::mutateCart(db, "u", "add", {{"productId", "cup"}});
            commerce::mutateCart(db, "u", "add", {{"productId", "cup"}, {"quantity", 2}});
            auto items = commerce::getCart(db, "u");
            check(items[0]["quantity"] == 3 && items[0]["priceMinor"] == 10 && items[0]["available"] == true,
                  "Cart enrichment failed");
            commerce::mutateCart(db, "u", "update", {{"productId", "cup"}, {"quantity", 2}});
            commerce::mutateCart(db, "u", "update", {{"productId", "cup"}, {"quantity", 0}});
            check(commerce::getCart(db, "u").empty(), "Zero did not remove item");
        });
        test("checkout rejects changed prices without writes and accepts explicit fresh review", [] {
            for (double changed : {900.0, 500.0})
            {
                MemoryDatabase store;
                auto db = store.client();
                store.seed("products/cup", product(10, 650));
                store.seed("carts/u", cart());
                const auto reviewed = commerce::getCart(db, "u")[0];
                json intent = {{"cartVersion", reviewed["cartVersion"]},
                               {"quoteVersion", reviewed["quoteVersion"]}};
                store.seed("products/cup", product(10, changed));
                const auto commits = store.committed;
                try
                {
                    checkoutDelivery(db, "u", "price-change-key", intent);
                    throw std::runtime_error("Changed price accepted");
                }
                catch (const ApiError &e)
                {
                    check(e.code == "PRICE_CHANGED", "Wrong price error");
                }
                check(store.committed == commits && store.count("orders/") == 0 &&
                          store.count("checkoutRequests/") == 0 && store.count("inventoryMovements/") == 0 &&
                          store.read("products/cup")["stock"] == 10 &&
                          store.read("carts/u")["items"].size() == 1,
                      "Price rejection changed store data");
                intent["quoteVersion"] = commerce::getCart(db, "u")[0]["quoteVersion"];
                const auto result = checkoutDelivery(db, "u", "fresh-review-key", intent);
                check(result["total"] == changed, "Fresh review did not use confirmed price");
                store.seed("products/cup", product(9, 1200));
                check(checkoutDelivery(db, "u", "fresh-review-key", intent) == result,
                      "Committed order retry revalidated current prices");
            }
        });
        test("price change during commit is rejected on transaction retry", [] {
            MemoryDatabase store;
            store.seed("products/cup", product(10, 650));
            store.seed("carts/u", cart());
            bool edited = false;
            FirestoreClient db(
                "test-project", [&](const auto &method, const std::string &path, const json &body) {
                    if (!edited && path.find(":commit") != std::string::npos && !body.at("writes").empty())
                    {
                        edited = true;
                        store.seed("products/cup", product(10, 900));
                    }
                    return store.request(method, path, body);
                });
            const auto quote = commerce::getCart(db, "u")[0]["quoteVersion"];
            try
            {
                checkoutDelivery(db, "u", "commit-race-key", {{"quoteVersion", quote}});
                throw std::runtime_error("Price race accepted");
            }
            catch (const ApiError &e)
            {
                check(e.code == "PRICE_CHANGED", "Retry did not recheck prices");
            }
            check(edited && store.count("orders/") == 0 && store.count("checkoutRequests/") == 0 &&
                      store.read("products/cup")["stock"] == 10 && store.read("carts/u")["items"].size() == 1,
                  "Price race changed checkout data");
        });
        test("new checkout requires a quote and stock-only changes preserve reviewed prices", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(10, 650));
            store.seed("carts/u", cart());
            const auto reviewed = commerce::getCart(db, "u")[0];
            try
            {
                checkoutDelivery(db, "u", "no-quote-key", json::object());
                throw std::runtime_error("Missing quote accepted");
            }
            catch (const ApiError &e)
            {
                check(e.code == "QUOTE_REQUIRED", "Wrong missing quote error");
            }
            check(store.count("orders/") == 0 && store.read("products/cup")["stock"] == 10,
                  "Missing quote changed store data");
            store.seed("products/cup", product(5, 650));
            auto result =
                checkoutDelivery(db, "u", "stock-only-key", {{"quoteVersion", reviewed["quoteVersion"]}});
            check(result["total"] == 650 && store.read("products/cup")["stock"] == 4,
                  "Stock-only edit invalidated price review");
        });
        test("new orders reserve for four hours and expiry releases stock exactly once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart(2));
            auto order = store.reviewedCheckout(db, "u", "expiry-once-key", json::object());
            check(order["expiresAt"].get<int64_t>() - order["createdAt"].get<int64_t>() == 14400,
                  "Wrong reservation duration");
            const std::string id = order["id"];
            check(commerce::expireOrder(db, id)["status"] == "pending", "Reservation expired early");
            order["expiresAt"] = commerce::nowSeconds() - 1;
            order["pendingExpiresAt"] = order["expiresAt"];
            store.seed("orders/" + id, order);
            auto expired = commerce::expireOrder(db, id);
            check(expired["status"] == "expired" && expired["pendingExpiresAt"].is_null(),
                  "Expiry not recorded");
            check(commerce::expireOrder(db, id) == expired && store.read("products/cup")["stock"] == 10,
                  "Repeated expiry changed stock");
            check(store.count("inventoryMovements/") == 2 && store.count("notifications/") == 2,
                  "Expiry history duplicated");
            check(store.reviewedCheckout(db, "u", "expiry-once-key", json::object())["id"] == id &&
                      store.count("orders/") == 1,
                  "Expired checkout retry recreated order");
            apiError([&] { commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}}); }, 409);
        });
        test("legacy reservations and paid orders are not automatically expired", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order = store.reviewedCheckout(db, "u", "legacy-expiry-key", json::object());
            const std::string id = order["id"];
            order.erase("expiresAt");
            store.seed("orders/" + id, order);
            check(commerce::expireOrder(db, id)["status"] == "pending", "Legacy order expired silently");
            commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}});
            order = store.read("orders/" + id);
            order["expiresAt"] = 1;
            store.seed("orders/" + id, order);
            check(commerce::expireOrder(db, id)["status"] == "paid" &&
                      store.read("products/cup")["stock"] == 9,
                  "Paid order expired");
        });
        test("overdue payment is rejected even before the expiry sweep", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order = store.reviewedCheckout(db, "u", "late-payment-key", json::object());
            const std::string id = order["id"];
            order["expiresAt"] = 1;
            store.seed("orders/" + id, order);
            try
            {
                commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}});
                throw std::runtime_error("Late payment accepted");
            }
            catch (const ApiError &e)
            {
                check(e.code == "ORDER_EXPIRED", "Wrong late-payment error");
            }
            commerce::expireOrder(db, id);
            check(store.read("products/cup")["stock"] == 10, "Overdue order failed to release stock");
        });
        test("expiry and cancellation races release inventory only once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order = store.reviewedCheckout(db, "u", "expiry-race-key", json::object());
            const std::string id = order["id"];
            order["expiresAt"] = 1;
            store.seed("orders/" + id, order);
            store.synchronizeReads = true;
            store.synchronizedPrefix = "orders/";
            auto a = std::async(std::launch::async, [&] { commerce::expireOrder(db, id); });
            auto b = std::async(std::launch::async, [&] {
                try
                {
                    commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}});
                }
                catch (const ApiError &e)
                {
                    check(e.status == 409, "Unexpected cancellation race error");
                }
            });
            a.get();
            b.get();
            check(store.read("products/cup")["stock"] == 10 && store.count("inventoryMovements/") == 2,
                  "Expiry race double-restocked");
        });
        test("expiry write failures roll back all stock history and notifications", [] {
            for (int i = 0; i < 5; ++i)
            {
                MemoryDatabase store;
                auto db = store.client();
                store.seed("products/cup", product());
                store.seed("carts/u", cart());
                auto order = store.reviewedCheckout(db, "u", "expiry-failure-key", json::object());
                const std::string id = order["id"];
                order["expiresAt"] = 1;
                store.seed("orders/" + id, order);
                store.failWriteIndex = i;
                dbError([&] { commerce::expireOrder(db, id); });
                check(store.read("orders/" + id)["status"] == "pending" &&
                          store.read("products/cup")["stock"] == 9 && store.count("notifications/") == 1,
                      "Expiry partially committed");
                store.failWriteIndex = -1;
                commerce::expireOrder(db, id);
                check(store.read("products/cup")["stock"] == 10, "Expiry could not recover");
            }
        });
        test("disabled shipping and missing delivery review cannot create orders", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            json body = {{"quoteVersion", commerce::getCart(db, "u")[0]["quoteVersion"]},
                         {"shippingMethodId", "fixture-delivery"},
                         {"shippingFeeMinor", 0}};
            apiError([&] { commerce::checkout(db, "u", "missing-address-key", body); }, 400);
            body["shippingAddress"] = deliveryAddress();
            body.erase("shippingFeeMinor");
            apiError([&] { commerce::checkout(db, "u", "missing-fee-key", body); }, 400);
            store.seed("storeSettings/shipping", {{"methods", json::array()}, {"version", "initial"}});
            body.erase("shippingMethodId");
            try
            {
                commerce::checkout(db, "u", "closed-store-key", body);
                throw std::runtime_error("Disabled checkout accepted");
            }
            catch (const ApiError &e)
            {
                check(e.code == "CHECKOUT_DISABLED", "Wrong closed-store error");
            }
            check(store.count("orders/") == 0 && store.read("products/cup")["stock"] == 10,
                  "Shipping rejection consumed stock");
        });
        test("pickup requires contact and supports fulfillment without a delivery address", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto settings = shippingInput();
            settings["methods"][0]["kind"] = "pickup";
            settings["methods"][0]["pickupInstructions"] = "Collect at 1 Store Street";
            commerce::saveShipping(db, "admin", settings);
            json body = {{"quoteVersion", commerce::getCart(db, "u")[0]["quoteVersion"]},
                         {"shippingMethodId", "standard"},
                         {"shippingFeeMinor", 1250}};
            apiError([&] { commerce::checkout(db, "u", "pickup-key", body); }, 400);
            body["pickupContact"] = {{"name", "Buyer"}, {"phone", "1234567890"}};
            auto order = commerce::checkout(db, "u", "pickup-key", body);
            const std::string id = order["id"];
            check(order["shippingAddress"].is_null() && order["pickupContact"]["name"] == "Buyer",
                  "Pickup snapshot incorrect");
            for (const auto *status : {"paid", "shipped", "delivered"})
                commerce::transitionOrder(db, id, "admin", true, {{"status", status}});
            check(store.read("orders/" + id)["status"] == "delivered", "Pickup fulfillment failed");
        });
        test("checkout creates exact snapshot, decrements stock and clears cart atomically", [] {
            MemoryDatabase store;
            auto db = store.client();
            auto p = product();
            p["custom"] = 42;
            store.seed("products/cup", p);
            store.seed("carts/u", cart(3));
            auto result = store.reviewedCheckout(db, "u", "key-success", {{"cartVersion", "cart-v1"}});
            check(result["totalMinor"] == 30 && result["total"] == 0.30, "Checkout total wrong");
            check(store.read("products/cup")["stock"] == 7 && store.read("products/cup")["custom"] == 42,
                  "Stock mask lost fields");
            check(store.read("carts/u")["items"].empty(), "Cart was not cleared");
            check(store.count("orders/") == 1 && store.count("checkoutRequests/") == 1,
                  "Missing atomic records");
        });
        test("repeated checkout returns original order and rejects changed intent", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto first = store.reviewedCheckout(db, "u", "key-repeat", {{"cartVersion", "cart-v1"}});
            for (int i = 0; i < 5; ++i)
                check(store.reviewedCheckout(db, "u", "key-repeat", {{"cartVersion", "cart-v1"}}) == first,
                      "Replay changed response");
            apiError([&] { store.reviewedCheckout(db, "u", "key-repeat", {{"cartVersion", "different"}}); },
                     409);
            check(store.count("orders/") == 1 && store.read("products/cup")["stock"] == 9,
                  "Duplicate order or stock decrement");
        });
        test("database read failures are not missing records or empty lists", [] {
            FirestoreClient db("p", [](auto, auto, auto) {
                return Response{503, {{"error", {{"status", "UNAVAILABLE"}}}}};
            });
            dbError([&] { db.getDocument("products", "p"); });
            dbError([&] { db.listDocuments("products"); });
            MemoryDatabase store;
            auto real = store.client();
            store.seed("users/u", {{"role", "admin"}, {"email", "admin@test"}});
            store.failRead = "users/u";
            dbError([&] { commerce::ensureProfile(real, "u", "other@test"); });
            check(store.read("users/u")["role"] == "admin" && store.committed == 0,
                  "Failed read overwrote profile");
        });
        test("profile creation is conditional and preserves existing roles", [] {
            MemoryDatabase store;
            auto db = store.client();
            auto profile = commerce::ensureProfile(db, "u", "u@test");
            check(profile["role"] == "customer", "Default role wrong");
            store.seed("users/u", {{"role", "admin"}, {"email", "u@test"}});
            check(commerce::ensureProfile(db, "u", "u@test")["role"] == "admin", "Existing role overwritten");
            dbError([&] { db.createDocument("users", "u", {{"role", "customer"}}); });
        });
        test("malformed cart, stock conflict and stale review cause no writes", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(1));
            for (int quantity : {-1, 0, 2, 1000001})
            {
                store.seed("carts/u", cart(quantity));
                apiError([&] { store.reviewedCheckout(db, "u", "key-invalid", json::object()); }, 409);
            }
            store.seed("carts/u", cart());
            apiError([&] { store.reviewedCheckout(db, "u", "key-stale", {{"cartVersion", "stale"}}); }, 409);
            store.seed("carts/u", cart(1, "missing"));
            apiError([&] { store.reviewedCheckout(db, "u", "key-missing", json::object()); }, 409);
            check(store.read("products/cup")["stock"] == 1 && store.count("orders/") == 0,
                  "Rejected checkout changed inventory");
        });
        test("failure at every commit write leaves all documents unchanged", [] {
            for (int index = 0; index < 6; ++index)
            {
                MemoryDatabase store;
                auto db = store.client();
                store.seed("products/cup", product());
                store.seed("carts/u", cart());
                store.failWriteIndex = index;
                dbError([&] { store.reviewedCheckout(db, "u", "key-failure", json::object()); });
                check(store.read("products/cup")["stock"] == 10 &&
                          store.read("carts/u")["items"].size() == 1 && store.count("orders/") == 0 &&
                          store.count("checkoutRequests/") == 0,
                      "Partial commit escaped");
            }
        });
        test("lost commit response is recoverable with the same idempotency key", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            store.failAfterCommit = true;
            dbError([&] { store.reviewedCheckout(db, "u", "key-lost-response", json::object()); });
            auto order = store.reviewedCheckout(db, "u", "key-lost-response", json::object());
            check(!order["id"].get<std::string>().empty() && store.count("orders/") == 1 &&
                      store.read("products/cup")["stock"] == 9,
                  "Lost response duplicated order");
        });
        test("two simultaneous customers cannot purchase the last unit", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(1));
            store.seed("carts/a", cart());
            store.seed("carts/b", cart());
            store.synchronizeReads = true;
            std::atomic<int> success{0}, conflict{0};
            auto buy = [&](std::string uid) {
                try
                {
                    store.reviewedCheckout(db, uid, "key-concurrent", json::object());
                    ++success;
                }
                catch (const ApiError &e)
                {
                    if (e.status == 409)
                        ++conflict;
                    else
                        throw;
                }
            };
            auto a = std::async(std::launch::async, buy, "a"), b = std::async(std::launch::async, buy, "b");
            a.get();
            b.get();
            check(success == 1 && conflict == 1 && store.read("products/cup")["stock"] == 0 &&
                      store.count("orders/") == 1,
                  "Last unit oversold");
        });
        test("concurrent same-key checkout returns one result", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            store.synchronizeReads = true;
            auto buy = [&] { return store.reviewedCheckout(db, "u", "key-same-concurrent", json::object()); };
            auto a = std::async(std::launch::async, buy), b = std::async(std::launch::async, buy);
            check(a.get() == b.get() && store.count("orders/") == 1,
                  "Concurrent duplicate request was not replayed");
        });
        test("different checkout keys cannot consume the same reviewed cart twice", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            store.synchronizeReads = true;
            auto buy = [&](std::string key) {
                try
                {
                    store.reviewedCheckout(db, "u", key, {{"cartVersion", "cart-v1"}});
                    return 1;
                }
                catch (const ApiError &e)
                {
                    check(e.code == "CART_CHANGED", "Unexpected concurrent checkout error");
                    return 0;
                }
            };
            auto a = std::async(std::launch::async, buy, "tab-one-key");
            auto b = std::async(std::launch::async, buy, "tab-two-key");
            check(a.get() + b.get() == 1 && store.count("orders/") == 1 &&
                      store.read("products/cup")["stock"] == 9,
                  "Two tabs consumed the same cart");
        });
        test("multi-product checkout validates every line before committing", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(10, 19.99));
            store.seed("products/bowl", product(1, 0.10));
            auto bag = cart(3);
            bag["items"].push_back({{"productId", "bowl"}, {"quantity", 2}});
            store.seed("carts/u", bag);
            apiError([&] { store.reviewedCheckout(db, "u", "multi-product-key", json::object()); }, 409);
            check(store.committed == 0 && store.read("products/cup")["stock"] == 10 &&
                      store.read("carts/u") == bag,
                  "A later invalid line caused partial writes");
            store.seed("products/bowl", product(2, 0.10));
            auto order = store.reviewedCheckout(db, "u", "multi-product-key", json::object());
            check(order["totalMinor"] == 6017 && order["items"].size() == 2 &&
                      store.read("products/cup")["stock"] == 7 && store.read("products/bowl")["stock"] == 0,
                  "Multi-product totals or stock incorrect");
        });
        test("concurrent cart additions do not lose quantities", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.synchronizeReads = true;
            auto add = [&] { commerce::mutateCart(db, "u", "add", {{"productId", "cup"}}); };
            auto a = std::async(std::launch::async, add), b = std::async(std::launch::async, add);
            a.get();
            b.get();
            check(store.read("carts/u")["items"][0]["quantity"] == 2, "Concurrent addition lost");
        });
        test("transaction retries are bounded and never retry arbitrary failures", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            store.alwaysAbort = true;
            apiError([&] { store.reviewedCheckout(db, "u", "key-abort", json::object()); }, 409);
            check(store.committed == 0, "Aborted transaction wrote data");
        });
        test("listing follows tokens and rejects malformed upstream data", [] {
            int calls = 0;
            FirestoreClient db("p", [&](const auto &, const std::string &path, const auto &) {
                ++calls;
                json p = {
                    {"name", "projects/p/databases/(default)/documents/products/" + std::to_string(calls)},
                    {"fields", FirestoreClient::toFields(product())}};
                if (calls == 1)
                    return Response{200, {{"documents", json::array({p})}, {"nextPageToken", "next+/="}}};
                check(path.find("pageToken=next%2B%2F%3D") != std::string::npos,
                      "Page token was not encoded");
                return Response{200, {{"documents", json::array({p})}}};
            });
            check(db.listDocuments("products").size() == 2 && calls == 2, "Pagination truncated data");
            FirestoreClient invalid("p", [](auto, auto, auto) { return Response{200, {{"documents", 1}}}; });
            dbError([&] { invalid.listDocuments("products"); });
        });
        test("legacy product and order reads keep public response fields", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            auto p = validation::productView(db.getDocument("products", "cup"), "cup");
            check(p["price"] == 0.1 && p["priceMinor"] == 10, "Legacy product failed");
            auto o = validation::orderView(
                {{"userId", "u"},
                 {"items",
                  json::array({{{"productId", "cup"}, {"name", "Cup"}, {"price", 0.1}, {"quantity", 3}}})},
                 {"status", "pending"},
                 {"total", 0.1 + 0.1 + 0.1},
                 {"createdAt", 1}},
                "o");
            check(o["totalMinor"] == 30, "Legacy floating-point noise rejected");
            check(Product::fromJson(p).toFields()["priceMinor"] == 10, "Product model conversion failed");
            check(Order::fromJson(o).toFields()["totalMinor"] == 30, "Order model conversion failed");
            apiError([&] { commerce::saveProduct(db, "missing", productEdit(product()), false); }, 404);
        });
        test("malformed Firestore field values fail instead of changing stored data", [] {
            for (const auto &wrapped :
                 json::array({json{{"integerValue", "12oops"}}, json{{"integerValue", " 12"}},
                              json{{"arrayValue", {{"values", json::object()}}}},
                              json{{"mapValue", {{"fields", json::array()}}}},
                              json{{"stringValue", "text"}, {"integerValue", "12"}}}))
            {
                FirestoreClient db("p", [wrapped](auto, auto, auto) {
                    return Response{200,
                                    {{"name", "projects/p/databases/(default)/documents/products/p"},
                                     {"fields", {{"test", wrapped}}}}};
                });
                dbError([&] { db.getDocument("products", "p"); });
            }
        });
        test("order details enforce ownership and allow administrators", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order = store.reviewedCheckout(db, "u", "details-key", json::object());
            const std::string id = order["id"];
            check(commerce::orderDetail(db, id, "u", false)["id"] == id, "Owner cannot read order");
            check(commerce::orderDetail(db, id, "admin", true)["id"] == id, "Admin cannot read order");
            apiError([&] { commerce::orderDetail(db, id, "other", false); }, 404);
            apiError([&] { commerce::transitionOrder(db, id, "other", false, {{"status", "cancelled"}}); },
                     404);
            check(store.read("products/cup")["stock"] == 9, "Unauthorized cancellation changed stock");
        });
        test("address snapshot is validated, immutable and included in checkout intent", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            json address = {{"name", "Buyer"}, {"phone", "1234567890"},   {"line1", "1 Test Street"},
                            {"city", "Pune"},  {"region", "Maharashtra"}, {"postalCode", "411001"},
                            {"country", "IN"}};
            auto bad = address;
            bad["postalCode"] = 123;
            apiError([&] { store.reviewedCheckout(db, "u", "address-key", {{"shippingAddress", bad}}); },
                     400);
            check(store.committed == 0, "Invalid address wrote data");
            auto order = store.reviewedCheckout(db, "u", "address-key", {{"shippingAddress", address}});
            const std::string id = order["id"];
            check(order["shippingAddress"]["line1"] == "1 Test Street", "Address missing from snapshot");
            check(Order::fromJson(order).toFields()["shippingAddress"] == order["shippingAddress"],
                  "Order model lost fulfillment metadata");
            address["line1"] = "Changed address";
            apiError([&] { store.reviewedCheckout(db, "u", "address-key", {{"shippingAddress", address}}); },
                     409);
            apiError([&] { commerce::setOrderAddress(db, id, "admin", address); }, 409);
            check(commerce::orderDetail(db, id, "u", false)["shippingAddress"]["line1"] == "1 Test Street",
                  "Snapshot changed");
        });
        test("lifecycle is forward-only and legacy orders need an address before shipment", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            const std::string id = store.reviewedCheckout(db, "u", "lifecycle-key", json::object())["id"];
            auto legacy = store.read("orders/" + id);
            legacy["shippingAddress"] = nullptr;
            store.seed("orders/" + id, legacy);
            apiError([&] { commerce::transitionOrder(db, id, "u", false, {{"status", "paid"}}); }, 403);
            apiError([&] { commerce::transitionOrder(db, id, "admin", true, {{"status", "shipped"}}); }, 409);
            commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}});
            apiError([&] { commerce::transitionOrder(db, id, "admin", true, {{"status", "shipped"}}); }, 409);
            json address = {{"name", "Buyer"}, {"phone", "1234567890"},   {"line1", "1 Test Street"},
                            {"city", "Pune"},  {"region", "Maharashtra"}, {"postalCode", "411001"},
                            {"country", "IN"}};
            commerce::setOrderAddress(db, id, "admin", address);
            commerce::setOrderAddress(db, id, "admin", address);
            commerce::transitionOrder(db, id, "admin", true, {{"status", "shipped"}});
            apiError([&] { commerce::transitionOrder(db, id, "admin", true, {{"status", "cancelled"}}); },
                     409);
            auto done = commerce::transitionOrder(db, id, "admin", true, {{"status", "delivered"}});
            check(done["statusHistory"].size() == 4 && done.contains("deliveredAt"),
                  "Lifecycle history incomplete");
            commerce::transitionOrder(db, id, "admin", true, {{"status", "delivered"}});
            check(store.read("orders/" + id)["statusHistory"].size() == 4,
                  "Repeated delivery duplicated history");
            apiError([&] { commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}}); }, 409);
            check(store.read("products/cup")["stock"] == 9, "Fulfillment altered stock again");
        });
        test("customer cancellation restocks archived products exactly once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart(2));
            const std::string id = store.reviewedCheckout(db, "u", "cancel-key", json::object())["id"];
            commerce::archiveProduct(db, "cup");
            apiError([&] { commerce::mutateCart(db, "u", "add", {{"productId", "cup"}}); }, 404);
            auto cancelled = commerce::transitionOrder(
                db, id, "u", false, {{"status", "cancelled"}, {"reason", "Changed plans"}});
            commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}});
            check(store.read("products/cup")["stock"] == 10 && store.read("products/cup")["archived"] == true,
                  "Cancellation lost archived stock or double-restocked");
            check(store.count("inventoryMovements/") == 2 && cancelled["refundStatus"] == "not_required" &&
                      cancelled["statusHistory"].size() == 2,
                  "Cancellation audit incorrect");
        });
        test("paid cancellation requires an administrator and flags manual refund", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            const std::string id = store.reviewedCheckout(db, "u", "paid-cancel-key", json::object())["id"];
            commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}});
            apiError([&] { commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}}); }, 409);
            apiError(
                [&] {
                    commerce::transitionOrder(db, id, "admin", true,
                                              {{"status", "cancelled"}, {"expectedStatus", "pending"}});
                },
                409);
            auto order = commerce::transitionOrder(db, id, "admin", true,
                                                   {{"status", "cancelled"}, {"expectedStatus", "paid"}});
            check(order["refundStatus"] == "manual_review_required" &&
                      store.read("products/cup")["stock"] == 10,
                  "Paid cancellation silently omitted refund review");
        });
        test("cancellation failures leave inventory, audit and status unchanged", [] {
            for (int index = 0; index < 4; ++index)
            {
                MemoryDatabase store;
                auto db = store.client();
                store.seed("products/cup", product());
                store.seed("carts/u", cart());
                const std::string id =
                    store.reviewedCheckout(db, "u", "cancel-failure-key", json::object())["id"];
                store.failWriteIndex = index;
                dbError([&] { commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}}); });
                check(store.read("products/cup")["stock"] == 9 &&
                          store.read("orders/" + id)["status"] == "pending" &&
                          store.count("inventoryMovements/") == 1,
                      "Partial cancellation escaped");
            }
        });
        test("lost cancellation response is safely replayed", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            const std::string id = store.reviewedCheckout(db, "u", "cancel-lost-key", json::object())["id"];
            store.failAfterCommit = true;
            dbError([&] { commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}}); });
            commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}});
            check(store.read("products/cup")["stock"] == 10 && store.count("inventoryMovements/") == 2,
                  "Uncertain cancellation caused duplicate restock");
        });
        test("concurrent cancellation requests restore inventory once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            const std::string id = store.reviewedCheckout(db, "u", "cancel-race-key", json::object())["id"];
            store.synchronizeReads = true;
            auto cancel = [&] {
                return commerce::transitionOrder(db, id, "u", false, {{"status", "cancelled"}});
            };
            auto a = std::async(std::launch::async, cancel), b = std::async(std::launch::async, cancel);
            check(a.get() == b.get() && store.read("products/cup")["stock"] == 10 &&
                      store.count("inventoryMovements/") == 2,
                  "Concurrent cancellation double-restocked");
        });
        test("product stock adjustments and their inventory entries commit together", [] {
            MemoryDatabase store;
            auto db = store.client();
            const std::string id = commerce::saveProduct(db, "", product(5), true, "admin")["id"];
            check(store.count("inventoryMovements/") == 1, "Initial inventory record missing");
            commerce::adjustStock(
                db, id,
                {{"delta", 2}, {"reason", "Delivery"}, {"version", store.read("products/" + id)["version"]}},
                "admin");
            check(store.count("inventoryMovements/") == 2 && store.read("products/" + id)["stock"] == 7,
                  "Stock adjustment not audited");
            store.failWriteIndex = 1;
            dbError([&] {
                commerce::adjustStock(db, id,
                                      {{"delta", 2},
                                       {"reason", "Delivery"},
                                       {"version", store.read("products/" + id)["version"]}},
                                      "admin");
            });
            check(store.read("products/" + id)["stock"] == 7 && store.count("inventoryMovements/") == 2,
                  "Inventory failure still updated product");
        });
        test("shipping and cancellation cannot both commit for one order", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            json address = {{"name", "Buyer"}, {"phone", "1234567890"},   {"line1", "1 Test Street"},
                            {"city", "Pune"},  {"region", "Maharashtra"}, {"postalCode", "411001"},
                            {"country", "IN"}};
            const std::string id =
                store.reviewedCheckout(db, "u", "ship-cancel-race", {{"shippingAddress", address}})["id"];
            commerce::transitionOrder(db, id, "admin", true, {{"status", "paid"}});
            store.synchronizeReads = true;
            store.synchronizedPrefix = "orders/";
            auto change = [&](std::string status) {
                try
                {
                    commerce::transitionOrder(db, id, "admin", true,
                                              {{"status", status}, {"expectedStatus", "paid"}});
                    return 1;
                }
                catch (const ApiError &e)
                {
                    check(e.status == 409, "Unexpected race error");
                    return 0;
                }
            };
            auto a = std::async(std::launch::async, change, "shipped");
            auto b = std::async(std::launch::async, change, "cancelled");
            check(a.get() + b.get() == 1, "Shipping and cancellation both committed");
            const auto final = store.read("orders/" + id)["status"];
            check(store.read("products/cup")["stock"] == (final == "cancelled" ? 10 : 9),
                  "Race left inconsistent inventory");
        });
        test("missing legacy stock records block cancellation without partial changes", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed(
                "orders/legacy",
                {{"userId", "u"},
                 {"status", "pending"},
                 {"total", 1},
                 {"createdAt", 1},
                 {"items",
                  json::array(
                      {{{"productId", "missing"}, {"name", "Missing"}, {"price", 1}, {"quantity", 1}}})}});
            apiError([&] { commerce::transitionOrder(db, "legacy", "u", false, {{"status", "cancelled"}}); },
                     409);
            check(store.committed == 0 && store.read("orders/legacy")["status"] == "pending",
                  "Missing inventory silently skipped");
        });
        test("shipping fees are authoritative and snapshotted with the order", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(5, 19.99));
            store.seed("carts/u", cart(2));
            auto settings = commerce::saveShipping(db, "admin", shippingInput());
            const auto body = json{{"shippingAddress", deliveryAddress()},
                                   {"shippingMethodId", "standard"},
                                   {"shippingFeeMinor", 1250}};
            auto order = store.reviewedCheckout(db, "u", "shipping-order", body);
            check(order["totalMinor"] == 5248 && order["subtotalMinor"] == 3998 &&
                      order["shippingFeeMinor"] == 1250,
                  "Shipping fee total incorrect");
            auto changed = shippingInput();
            changed["version"] = settings["version"];
            changed["methods"][0]["fee"] = 20;
            commerce::saveShipping(db, "admin", changed);
            check(store.reviewedCheckout(db, "u", "shipping-order", body) == order,
                  "New shipping rate changed old checkout replay");
        });
        test("unavailable destinations and stale shipping fees cause no checkout writes", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            commerce::saveShipping(db, "admin", shippingInput());
            auto address = deliveryAddress();
            address["country"] = "US";
            apiError(
                [&] {
                    store.reviewedCheckout(db, "u", "shipping-failed",
                                           {{"shippingAddress", address}, {"shippingMethodId", "standard"}});
                },
                409);
            apiError(
                [&] {
                    store.reviewedCheckout(db, "u", "shipping-failed",
                                           {{"shippingAddress", deliveryAddress()},
                                            {"shippingMethodId", "standard"},
                                            {"shippingFeeMinor", 1}});
                },
                409);
            apiError([&] { store.reviewedCheckout(db, "u", "shipping-failed", json::object()); }, 409);
            check(store.read("products/cup")["stock"] == 10 && store.count("orders/") == 0,
                  "Invalid shipping consumed stock");
        });
        test("shipping settings use version checks and atomic audit", [] {
            MemoryDatabase store;
            auto db = store.client();
            const auto first = commerce::saveShipping(db, "admin", shippingInput());
            auto stale = shippingInput();
            stale["methods"][0]["fee"] = 99;
            apiError([&] { commerce::saveShipping(db, "admin", stale); }, 409);
            stale["version"] = first["version"];
            store.failWriteIndex = 1;
            dbError([&] { commerce::saveShipping(db, "admin", stale); });
            check(store.read("storeSettings/shipping") == first && store.count("auditLogs/") == 1,
                  "Audit failure still changed settings");
        });
        test("variants have unique SKUs and independent checkout/restock quantities", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/root", product(8));
            auto input = product(3, 22);
            input["sku"] = "blue-m";
            input["variantLabel"] = "Blue / Medium";
            const std::string variant = commerce::createVariant(db, "root", "admin", input)["id"];
            input["sku"] = "BLUE-M";
            apiError([&] { commerce::createVariant(db, "root", "admin", input); }, 409);
            commerce::mutateCart(db, "u", "add", {{"productId", variant}, {"quantity", 2}});
            auto order = store.reviewedCheckout(db, "u", "variant-order", json::object());
            check(order["items"][0]["sku"] == "BLUE-M" && order["totalMinor"] == 4400 &&
                      store.read("products/root")["stock"] == 8,
                  "Variant snapshot or parent stock incorrect");
            check(Order::fromJson(order).toFields()["items"][0]["sku"] == "BLUE-M",
                  "Model lost variant snapshot");
            commerce::transitionOrder(db, order["id"], "u", false, {{"status", "cancelled"}});
            check(store.read("products/" + variant)["stock"] == 3 &&
                      store.read("products/root")["stock"] == 8,
                  "Variant cancellation changed parent stock");
        });
        test("archiving a parent prevents its variants being purchased", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/root", product());
            auto input = product();
            input["sku"] = "SKU-A";
            input["variantLabel"] = "Option A";
            const std::string child = commerce::createVariant(db, "root", "admin", input)["id"];
            commerce::mutateCart(db, "u", "add", {{"productId", child}});
            commerce::archiveProduct(db, "root", "admin");
            check(!commerce::getCart(db, "u")[0]["available"].get<bool>(),
                  "Archived variant stayed available");
            apiError([&] { store.reviewedCheckout(db, "u", "archived-variant", json::object()); }, 409);
            apiError([&] { commerce::mutateCart(db, "v", "add", {{"productId", child}}); }, 404);
        });
        test("concurrent variant checkouts cannot oversell one SKU", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/root", product());
            auto input = product(1);
            input["sku"] = "LAST-SKU";
            input["variantLabel"] = "Last option";
            const std::string child = commerce::createVariant(db, "root", "admin", input)["id"];
            store.seed("carts/a", cart(1, child));
            store.seed("carts/b", cart(1, child));
            store.synchronizeReads = true;
            auto buy = [&](std::string uid) {
                try
                {
                    store.reviewedCheckout(db, uid, "last-variant-key", json::object());
                    return 1;
                }
                catch (const ApiError &e)
                {
                    check(e.status == 409, "Unexpected variant race error");
                    return 0;
                }
            };
            auto a = std::async(std::launch::async, buy, "a"), b = std::async(std::launch::async, buy, "b");
            check(a.get() + b.get() == 1 && store.read("products/" + child)["stock"] == 0,
                  "Variant oversold");
        });
        test("tracking updates are versioned, audited and notify once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order =
                store.reviewedCheckout(db, "u", "tracking-order", {{"shippingAddress", deliveryAddress()}});
            json tracking = {{"carrier", "Test Carrier"},
                             {"trackingNumber", "TRACK-123"},
                             {"trackingUrl", "https://example.test/track/123"},
                             {"version", "initial"}};
            apiError([&] { commerce::saveShipment(db, order["id"], "admin", tracking); }, 409);
            commerce::transitionOrder(db, order["id"], "admin", true, {{"status", "paid"}});
            const auto saved = commerce::saveShipment(db, order["id"], "admin", tracking);
            commerce::saveShipment(db, order["id"], "admin", tracking);
            check(store.count("notifications/") == 3 && store.count("auditLogs/") == 2,
                  "Tracking retry duplicated notifications/audit");
            tracking["trackingNumber"] = "OTHER";
            apiError([&] { commerce::saveShipment(db, order["id"], "admin", tracking); }, 409);
            tracking["trackingUrl"] = "javascript:alert(1)";
            apiError([&] { commerce::saveShipment(db, order["id"], "admin", tracking); }, 400);
            check(saved["shipment"]["trackingNumber"] == "TRACK-123", "Wrong tracking snapshot");
        });
        test("notification read flags enforce ownership", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("notifications/n", {{"userId", "u"}, {"read", false}});
            apiError([&] { commerce::markNotificationRead(db, "n", "other"); }, 404);
            check(store.read("notifications/n")["read"] == false, "Another user read notification");
            commerce::markNotificationRead(db, "n", "u");
            commerce::markNotificationRead(db, "n", "u");
            check(store.committed == 1 && store.read("notifications/n")["read"] == true,
                  "Read acknowledgement not idempotent");
        });
        test("checkout and cancellation notifications share the transaction", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart());
            auto order = store.reviewedCheckout(db, "u", "notifications-order", json::object());
            store.reviewedCheckout(db, "u", "notifications-order", json::object());
            check(store.count("notifications/") == 1, "Checkout replay duplicated notification");
            store.failWriteIndex = 3;
            dbError(
                [&] { commerce::transitionOrder(db, order["id"], "u", false, {{"status", "cancelled"}}); });
            check(store.read("products/cup")["stock"] == 9 && store.count("notifications/") == 1,
                  "Notification failure partially cancelled order");
        });
        test("product detail edits require fresh versions and cannot write stock", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            auto edit = productEdit(product());
            edit["name"] = "New cup";
            auto missing = edit;
            missing.erase("version");
            apiError([&] { commerce::saveProduct(db, "cup", missing, false); }, 400);
            auto absolute = edit;
            absolute["stock"] = 20;
            apiError([&] { commerce::saveProduct(db, "cup", absolute, false); }, 400);
            commerce::saveProduct(db, "cup", edit, false, "admin");
            check(store.read("products/cup")["stock"] == 10 && store.count("inventoryMovements/") == 0,
                  "Detail edit changed inventory");
            apiError([&] { commerce::saveProduct(db, "cup", edit, false); }, 409);
            check(store.count("auditLogs/") == 1, "Stale edit created audit");
        });
        test("checkout and cancellation invalidate stale product forms", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.seed("carts/u", cart(2));
            auto edit = productEdit(product());
            const auto order = store.reviewedCheckout(db, "u", "stale-stock-checkout", json::object());
            apiError([&] { commerce::saveProduct(db, "cup", edit, false); }, 409);
            check(store.read("products/cup")["stock"] == 8, "Stale edit restored sold inventory");
            edit = productEdit(store.read("products/cup"));
            commerce::transitionOrder(db, order["id"], "u", false, {{"status", "cancelled"}});
            apiError([&] { commerce::saveProduct(db, "cup", edit, false); }, 409);
            check(store.read("products/cup")["stock"] == 10, "Cancellation lost stock");
        });
        test("stock adjustments validate bounds and reject replay after uncertain success", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product(2));
            for (auto delta : json::array({0, -3, 2147483647, 1.5, "2"}))
                apiError(
                    [&] {
                        commerce::adjustStock(db, "cup",
                                              {{"delta", delta}, {"reason", "Count"}, {"version", "initial"}},
                                              "admin");
                    },
                    400);
            apiError(
                [&] {
                    commerce::adjustStock(db, "cup", {{"delta", 1}, {"reason", ""}, {"version", "initial"}},
                                          "admin");
                },
                400);
            const json adjustment = {{"delta", -1}, {"reason", "Damaged unit"}, {"version", "initial"}};
            store.failWriteIndex = 2;
            dbError([&] { commerce::adjustStock(db, "cup", adjustment, "admin"); });
            check(store.read("products/cup")["stock"] == 2 && store.count("inventoryMovements/") == 0,
                  "Audit failure partially adjusted stock");
            store.failWriteIndex = -1;
            store.failAfterCommit = true;
            dbError([&] { commerce::adjustStock(db, "cup", adjustment, "admin"); });
            apiError([&] { commerce::adjustStock(db, "cup", adjustment, "admin"); }, 409);
            check(store.read("products/cup")["stock"] == 1 && store.count("inventoryMovements/") == 1 &&
                      store.count("auditLogs/") == 1,
                  "Stock replay duplicated adjustment");
        });
        test("concurrent stock adjustments using one version commit only once", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("products/cup", product());
            store.synchronizeReads = true;
            auto adjust = [&] {
                try
                {
                    commerce::adjustStock(
                        db, "cup", {{"delta", 2}, {"reason", "Delivery"}, {"version", "initial"}}, "admin");
                    return 1;
                }
                catch (const ApiError &e)
                {
                    if (e.status == 409)
                        return 0;
                    throw;
                }
            };
            auto a = std::async(std::launch::async, adjust), b = std::async(std::launch::async, adjust);
            check(a.get() + b.get() == 1 && store.read("products/cup")["stock"] == 12,
                  "Concurrent stock changes both committed");
        });
        test("last admin protection and transactional acting-admin authorization", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("users/a", {{"role", "admin"}});
            store.seed("users/b", {{"role", "customer"}});
            apiError([&] { commerce::changeRole(db, "a", "a", {{"role", "customer"}}); }, 409);
            apiError([&] { commerce::changeRole(db, "b", "b", {{"role", "admin"}}); }, 403);
            check(store.count("auditLogs/") == 0, "Rejected role changes wrote audit");
            commerce::changeRole(db, "b", "a", {{"role", "admin"}});
            commerce::changeRole(db, "a", "a", {{"role", "customer"}});
            apiError([&] { commerce::changeRole(db, "b", "a", {{"role", "customer"}}); }, 403);
            apiError([&] { commerce::changeRole(db, "b", "b", {{"role", "customer"}}); }, 409);
            commerce::changeRole(db, "b", "b", {{"role", "admin"}});
            check(store.count("auditLogs/") == 2, "No-op role request duplicated audit");
        });
        test("simultaneous self and cross demotions retain one admin", [] {
            for (bool cross : {false, true})
            {
                MemoryDatabase store;
                auto db = store.client();
                store.seed("users/a", {{"role", "admin"}});
                store.seed("users/b", {{"role", "admin"}});
                store.synchronizeReads = true;
                store.synchronizedPrefix = "storeSettings/adminRoles";
                auto demote = [&](std::string actor, std::string target) {
                    try
                    {
                        commerce::changeRole(db, target, actor, {{"role", "customer"}});
                        return 1;
                    }
                    catch (const ApiError &e)
                    {
                        if (e.status == 409 || e.status == 403)
                            return 0;
                        throw;
                    }
                };
                auto a = std::async(std::launch::async, demote, "a", cross ? "b" : "a");
                auto b = std::async(std::launch::async, demote, "b", cross ? "a" : "b");
                check(a.get() + b.get() == 1, "Both admin demotions committed");
                check((store.read("users/a")["role"] == "admin") !=
                          (store.read("users/b")["role"] == "admin"),
                      "No admin survived");
                check(store.count("auditLogs/") == 1, "Rejected demotion created audit");
            }
        });
        test("admin query and role guard failures do not change roles", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("users/a", {{"role", "admin"}});
            store.seed("users/b", {{"role", "admin"}});
            store.failRead = "users/";
            dbError([&] { commerce::changeRole(db, "b", "a", {{"role", "customer"}}); });
            store.failRead = "";
            store.failWriteIndex = 2;
            dbError([&] { commerce::changeRole(db, "b", "a", {{"role", "customer"}}); });
            check(store.read("users/b")["role"] == "admin" && store.count("auditLogs/") == 0,
                  "Role guard failure partially committed");
        });
        test("role changes cannot commit without their admin audit entry", [] {
            MemoryDatabase store;
            auto db = store.client();
            store.seed("users/admin", {{"role", "admin"}});
            store.seed("users/u", {{"role", "customer"}, {"email", "keep@example.test"}});
            store.failWriteIndex = 1;
            dbError([&] { commerce::changeRole(db, "u", "admin", {{"role", "admin"}}); });
            check(store.read("users/u")["role"] == "customer", "Role changed without audit");
            store.failWriteIndex = -1;
            commerce::changeRole(db, "u", "admin", {{"role", "admin"}});
            check(store.count("auditLogs/") == 1 && store.read("users/u")["email"] == "keep@example.test",
                  "Role audit or field preservation failed");
        });
        test("PNG uploads are decoded, sanitized and restricted to generated file paths", [] {
            MemoryDatabase store;
            auto db = store.client();
            const auto bytes = drogon::utils::base64Decode(imageFixture);
            const auto folder =
                (std::filesystem::temp_directory_path() / ("studio-upload-test-" + drogon::utils::getUuid()))
                    .string();
            auto image = commerce::uploadImage(db, "admin", bytes, folder);
            auto again = commerce::uploadImage(db, "admin", bytes, folder);
            check(image == again && store.count("uploads/") == 1 && store.count("auditLogs/") == 1,
                  "Upload retry duplicated records");
            auto p = product();
            p["imageUrl"] = image["url"];
            validation::productInput(p);
            check(commerce::sanitizedPng(bytes).find("private-metadata") == std::string::npos,
                  "Image metadata was preserved");
            apiError([&] { commerce::mediaFile(folder, "../.env"); }, 404);
            apiError([] { commerce::sanitizedPng("<svg onload='bad'>"); }, 400);
            apiError([] { commerce::sanitizedPng(std::string(commerce::MaxUploadBytes + 1, 'x')); }, 413);
            apiError([&] { commerce::sanitizedPng(bytes.substr(0, 40)); }, 400);
            std::filesystem::remove_all(folder);
        });
        test("catalog availability includes active variants without changing default stock", [] {
            auto parent = product(0);
            parent["id"] = "root";
            parent["hasVariants"] = true;
            auto child = product(2);
            child["id"] = "child";
            child["parentProductId"] = "root";
            auto rows = commerce::catalogView(json::array({parent, child}));
            check(rows.size() == 1 && rows[0]["stock"] == 0 && rows[0]["hasAvailableVariants"] == true,
                  "Variant availability was hidden");
            child["archived"] = true;
            check(commerce::catalogView(json::array({parent, child}))[0]["hasAvailableVariants"] == false,
                  "Archived variant counted as available");
        });
        test("HTTP boundary returns JSON for malformed bodies and database failures", [] {
            for (const auto &raw : {"{", "[]", "null", "1"})
            {
                auto req = drogon::HttpRequest::newHttpRequest();
                req->setBody(raw);
                apiError([&] { controller::body(req); }, 400);
            }
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setBody(std::string(32769, 'x'));
            apiError([&] { controller::body(req); }, 413);
            drogon::HttpResponsePtr response;
            controller::respond([&](const auto &r) { response = r; },
                                []() -> json { throw DatabaseError(503, "UNAVAILABLE"); });
            check(response->getStatusCode() == 503 &&
                      json::parse(response->getBody())["code"] == "DATABASE_UNAVAILABLE",
                  "Uncontrolled database error");
        });
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    std::cout << passed << " backend correctness tests passed\n";
}
