#include "services/Uploads.h"
#include <drogon/utils/Utilities.h>
#include <filesystem>
#include <future>
#include <iostream>
using json = nlohmann::json;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
json deliveryIntent(json body)
{
    if (!body.contains("shippingAddress"))
        body["shippingAddress"] = {{"name", "Buyer"}, {"phone", "1234567890"},   {"line1", "1 Test Street"},
                                   {"city", "Pune"},  {"region", "Maharashtra"}, {"postalCode", "411001"},
                                   {"country", "IN"}};
    if (!body.contains("shippingMethodId"))
        body["shippingMethodId"] = "fixture-delivery";
    if (!body.contains("shippingFeeMinor"))
        body["shippingFeeMinor"] = 0;
    return body;
}
json checkoutDelivery(FirestoreClient &db, const std::string &uid, const std::string &key, json body)
{
    return commerce::checkout(db, uid, key, deliveryIntent(body));
}
int main()
{
    try
    {
        // Fixed loopback host and demo project: no production credentials or config.
        FirestoreClient db("demo-studio-correctness",
                           FirestoreClient::httpTransport("http://127.0.0.1:8189", [] { return "owner"; }));
        db.setDocument("storeSettings", "shipping",
                       {{"methods", json::array({{{"id", "fixture-delivery"},
                                                  {"name", "Fixture delivery"},
                                                  {"kind", "delivery"},
                                                  {"active", true},
                                                  {"fee", 0},
                                                  {"feeMinor", 0},
                                                  {"countries", json::array()}}})},
                        {"version", "initial"}});
        const auto suffix = drogon::utils::getUuid(), productId = "cup-" + suffix, uid = "user-" + suffix;
        json product = {{"name", "Emulator cup"}, {"category", "Home"}, {"description", "Test"},
                        {"imageUrl", ""},         {"price", 19.99},     {"stock", 5}};
        db.createDocument("products", productId, product);
        auto profile = commerce::ensureProfile(db, uid, "test@example.test");
        check(profile["role"] == "customer", "Profile provisioning failed");
        commerce::mutateCart(db, uid, "add", {{"productId", productId}, {"quantity", 3}});
        auto cart = commerce::getCart(db, uid);
        check(cart.size() == 1 && cart[0]["priceMinor"] == 1999, "Cart REST read failed");
        const json intent = {{"cartVersion", cart[0]["cartVersion"]},
                             {"quoteVersion", cart[0]["quoteVersion"]}};
        auto order = checkoutDelivery(db, uid, "checkout-key-" + suffix, intent);
        check(order["totalMinor"] == 5997 && order["total"] == 59.97, "Money calculation failed");
        check(commerce::getCart(db, uid).empty(), "Cart not cleared");
        check(db.getDocument("products", productId)["stock"] == 2, "Stock not decremented");
        check(checkoutDelivery(db, uid, "checkout-key-" + suffix, intent) == order,
              "Idempotency replay failed");
        std::cout << "PASS emulator: legacy records, profile provisioning, cart, checkout and idempotency\n";
        bool failed = false;
        try
        {
            checkoutDelivery(db, uid, "checkout-key-" + suffix, {{"cartVersion", "different"}});
        }
        catch (const ApiError &e)
        {
            failed = e.status == 409;
        }
        check(failed, "Changed intent not rejected");
        std::cout << "PASS emulator: changed idempotency payload rejected\n";
        const auto last = "last-" + suffix, a = "a-" + suffix, b = "b-" + suffix;
        product["stock"] = 1;
        db.createDocument("products", last, product);
        commerce::mutateCart(db, a, "add", {{"productId", last}});
        commerce::mutateCart(db, b, "add", {{"productId", last}});
        auto buy = [&](std::string user) {
            try
            {
                checkoutDelivery(db, user, "last-key-" + user,
                                 {{"quoteVersion", commerce::getCart(db, user)[0]["quoteVersion"]}});
                return 1;
            }
            catch (const ApiError &e)
            {
                if (e.status == 409)
                    return 0;
                throw;
            }
        };
        auto one = std::async(std::launch::async, buy, a), two = std::async(std::launch::async, buy, b);
        check(one.get() + two.get() == 1, "Concurrent last unit oversold");
        check(db.getDocument("products", last)["stock"] == 0, "Last unit stock invalid");
        std::cout << "PASS emulator: two customers racing for the last unit\n";
        auto add = [&] { commerce::mutateCart(db, uid, "add", {{"productId", productId}}); };
        auto x = std::async(std::launch::async, add), y = std::async(std::launch::async, add);
        x.get();
        y.get();
        check(commerce::getCart(db, uid)[0]["quantity"] == 2, "Concurrent cart mutation lost");
        std::cout << "PASS emulator: concurrent cart updates\n";
        const auto sameQuote = commerce::getCart(db, uid)[0]["quoteVersion"];
        auto same = [&] {
            return checkoutDelivery(db, uid, "concurrent-same-" + suffix, {{"quoteVersion", sameQuote}});
        };
        auto o1 = std::async(std::launch::async, same), o2 = std::async(std::launch::async, same);
        check(o1.get() == o2.get(), "Concurrent duplicate checkout failed");
        std::cout << "PASS emulator: concurrent duplicate checkout\n";
        check(!db.listDocuments("orders").empty(), "Order history read failed");
        check(db.getDocument("products", "missing-" + suffix).is_null(), "404 was not distinguished");
        std::cout << "PASS emulator: history and not-found response\n";
        const std::string orderId = order["id"];
        auto cancel = [&] {
            return commerce::transitionOrder(db, orderId, uid, false, {{"status", "cancelled"}});
        };
        auto c1 = std::async(std::launch::async, cancel), c2 = std::async(std::launch::async, cancel);
        check(c1.get() == c2.get(), "Concurrent cancellation results differ");
        check(db.getDocument("products", productId)["stock"] == 3,
              "Concurrent cancellation double-restocked");
        check(commerce::orderDetail(db, orderId, uid, false)["status"] == "cancelled",
              "Order detail was stale");
        std::cout << "PASS emulator: concurrent cancellation and order detail\n";
        int cancellations = 0;
        for (const auto &movement : db.listDocuments("inventoryMovements"))
            if (movement["orderId"] == orderId && movement["reason"] == "order_cancelled")
                ++cancellations;
        check(cancellations == 1, "Cancellation inventory audit duplicated");
        std::cout << "PASS emulator: inventory history audit\n";
        const json address = {{"name", "Buyer"}, {"phone", "1234567890"},   {"line1", "1 Test Street"},
                              {"city", "Pune"},  {"region", "Maharashtra"}, {"postalCode", "411001"},
                              {"country", "IN"}};
        commerce::mutateCart(db, uid, "add", {{"productId", productId}});
        const auto fulfillment = checkoutDelivery(
            db, uid, "fulfill-" + suffix,
            {{"shippingAddress", address}, {"quoteVersion", commerce::getCart(db, uid)[0]["quoteVersion"]}});
        const std::string fulfillId = fulfillment["id"];
        commerce::transitionOrder(db, fulfillId, "admin", true, {{"status", "paid"}});
        auto race = [&](std::string next) {
            try
            {
                commerce::transitionOrder(db, fulfillId, "admin", true,
                                          {{"status", next}, {"expectedStatus", "paid"}});
                return 1;
            }
            catch (const ApiError &e)
            {
                if (e.status == 409)
                    return 0;
                throw;
            }
        };
        auto ship = std::async(std::launch::async, race, "shipped"),
             cancelPaid = std::async(std::launch::async, race, "cancelled");
        check(ship.get() + cancelPaid.get() == 1, "Shipping/cancellation both committed");
        auto finalOrder = commerce::orderDetail(db, fulfillId, uid, false);
        check(db.getDocument("products", productId)["stock"] == (finalOrder["status"] == "cancelled" ? 3 : 2),
              "Race stock inconsistent");
        std::cout << "PASS emulator: shipment versus cancellation race\n";
        commerce::mutateCart(db, uid, "add", {{"productId", productId}});
        const std::string deliveredId =
            checkoutDelivery(db, uid, "deliver-" + suffix,
                             {{"shippingAddress", address},
                              {"quoteVersion", commerce::getCart(db, uid)[0]["quoteVersion"]}})["id"];
        commerce::transitionOrder(db, deliveredId, "admin", true, {{"status", "paid"}});
        commerce::transitionOrder(db, deliveredId, "admin", true, {{"status", "shipped"}});
        auto delivered = commerce::transitionOrder(db, deliveredId, "admin", true, {{"status", "delivered"}});
        check(delivered["statusHistory"].size() == 4 &&
                  delivered["shippingAddress"]["line1"] == "1 Test Street",
              "Fulfillment snapshot/history invalid");
        std::cout << "PASS emulator: complete fulfillment with immutable address\n";
        const auto quoteUser = "quote-user-" + suffix, quoteProduct = "quote-product-" + suffix;
        auto priced = product;
        priced["price"] = 650;
        priced["stock"] = 3;
        db.createDocument("products", quoteProduct, priced);
        commerce::mutateCart(db, quoteUser, "add", {{"productId", quoteProduct}});
        json quoteIntent = {{"quoteVersion", commerce::getCart(db, quoteUser)[0]["quoteVersion"]},
                            {"shippingAddress", address}};
        priced["price"] = 900;
        db.setDocument("products", quoteProduct, priced);
        bool priceRejected = false;
        try
        {
            checkoutDelivery(db, quoteUser, "old-price-" + suffix, quoteIntent);
        }
        catch (const ApiError &e)
        {
            priceRejected = e.code == "PRICE_CHANGED";
        }
        check(priceRejected && db.getDocument("products", quoteProduct)["stock"] == 3 &&
                  commerce::getCart(db, quoteUser).size() == 1,
              "Stale price modified checkout data");
        quoteIntent["quoteVersion"] = commerce::getCart(db, quoteUser)[0]["quoteVersion"];
        auto quotedOrder = checkoutDelivery(db, quoteUser, "new-price-" + suffix, quoteIntent);
        check(quotedOrder["totalMinor"] == 90000 &&
                  checkoutDelivery(db, quoteUser, "new-price-" + suffix, quoteIntent) == quotedOrder,
              "Fresh price confirmation or retry failed");
        std::cout << "PASS emulator: changed price rejected, fresh review accepted and replay preserved\n";
        quotedOrder["expiresAt"] = commerce::nowSeconds() - 1;
        quotedOrder["pendingExpiresAt"] = quotedOrder["expiresAt"];
        const std::string expiredId = quotedOrder["id"];
        db.setDocument("orders", expiredId, quotedOrder);
        const auto due = db.dueOrders(commerce::nowSeconds());
        check(std::any_of(due.begin(), due.end(), [&](const json &row) { return row["id"] == expiredId; }),
              "Indexed expiry query missed order");
        check(db.dueOrders(commerce::nowSeconds(), due.back()).empty(), "Expiry cursor did not advance");
        auto expireA = std::async(std::launch::async, [&] { return commerce::expireOrder(db, expiredId); });
        auto expireB = std::async(std::launch::async, [&] { return commerce::expireOrder(db, expiredId); });
        check(expireA.get()["status"] == "expired" && expireB.get()["status"] == "expired" &&
                  db.getDocument("products", quoteProduct)["stock"] == 3,
              "Concurrent expiry double-restocked");
        const auto afterExpiry = db.dueOrders(commerce::nowSeconds());
        check(std::none_of(afterExpiry.begin(), afterExpiry.end(),
                           [&](const json &row) { return row["id"] == expiredId; }),
              "Expired marker stayed in query");
        std::cout << "PASS emulator: indexed expiry query, cursor and concurrent one-time stock release\n";
        commerce::saveShipping(db, "admin",
                               {{"version", "initial"},
                                {"methods", json::array({{{"id", "standard"},
                                                          {"name", "Standard"},
                                                          {"fee", 12.5},
                                                          {"active", true},
                                                          {"countries", json::array({"IN"})}}})}});
        auto variantInput = product;
        variantInput["stock"] = 2;
        variantInput["price"] = 10;
        variantInput["sku"] = "SKU-" + suffix;
        variantInput["variantLabel"] = "Blue / Large";
        const std::string variantId = commerce::createVariant(db, productId, "admin", variantInput)["id"];
        commerce::mutateCart(db, uid, "add", {{"productId", variantId}});
        json intent4 = {
            {"shippingAddress", address}, {"shippingMethodId", "standard"}, {"shippingFeeMinor", 1250}};
        intent4["quoteVersion"] = commerce::getCart(db, uid)[0]["quoteVersion"];
        const auto variantOrder = checkoutDelivery(db, uid, "operations-" + suffix, intent4);
        const std::string variantOrderId = variantOrder["id"];
        check(variantOrder["totalMinor"] == 2250 &&
                  variantOrder["items"][0]["variantLabel"] == "Blue / Large",
              "Variant/shipping snapshot invalid");
        check(commerce::variants(db, productId).size() == 1, "Variant listing failed");
        std::cout << "PASS emulator: shipping fee and variant checkout\n";
        commerce::transitionOrder(db, variantOrderId, "admin", true, {{"status", "paid"}});
        const json tracking = {{"carrier", "Test Carrier"},
                               {"trackingNumber", "TRACK-123"},
                               {"trackingUrl", "https://example.test/track/123"},
                               {"version", "initial"}};
        commerce::saveShipment(db, variantOrderId, "admin", tracking);
        commerce::saveShipment(db, variantOrderId, "admin", tracking);
        int updates = 0;
        for (const auto &n : db.listDocuments("notifications"))
        {
            if (n["orderId"] == variantOrderId && n["event"] == "tracking_updated")
            {
                ++updates;
                commerce::markNotificationRead(db, n["id"], uid);
                check(db.getDocument("notifications", n["id"])["read"] == true,
                      "Notification acknowledgement failed");
            }
        }
        check(updates == 1, "Tracking replay duplicated notification");
        std::cout << "PASS emulator: tracking notifications and read acknowledgement\n";
        commerce::transitionOrder(db, variantOrderId, "admin", true, {{"status", "cancelled"}});
        check(db.getDocument("products", variantId)["stock"] == 2, "Variant cancellation failed to restock");
        int auditRows = 0;
        for (const auto &a : db.listDocuments("auditLogs"))
            if (a["targetId"] == variantOrderId)
                ++auditRows;
        check(auditRows == 3, "Missing status/tracking audit records");
        std::cout << "PASS emulator: variant cancellation and admin audit\n";
        const auto uploadDir =
            (std::filesystem::temp_directory_path() / ("studio-emulator-uploads-" + suffix)).string();
        const auto bytes = drogon::utils::base64Decode(
            "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAGHRFWHRDb21tZW50AHByaXZhdGUtbWV0YWRhdGFvg3uxAAAA"
            "DUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
        const auto uploaded = commerce::uploadImage(db, "admin", bytes, uploadDir);
        check(commerce::uploadImage(db, "admin", bytes, uploadDir) == uploaded,
              "Upload metadata replay failed");
        check(db.getDocument("uploads", uploaded["id"])["url"] == uploaded["url"], "Upload record missing");
        std::filesystem::remove_all(uploadDir);
        std::cout << "PASS emulator: sanitized upload metadata and replay\n";
        auto stale = validation::productView(db.getDocument("products", variantId), variantId);
        auto details = stale;
        details.erase("stock");
        details["name"] = "Renamed variant";
        commerce::adjustStock(db, variantId,
                              {{"version", stale["version"]}, {"delta", 3}, {"reason", "Received"}}, "admin");
        bool staleRejected = false;
        try
        {
            commerce::saveProduct(db, variantId, details, false, "admin");
        }
        catch (const ApiError &e)
        {
            staleRejected = e.code == "PRODUCT_CHANGED";
        }
        check(staleRejected && db.getDocument("products", variantId)["stock"] == 5,
              "Stale variant edit overwrote stock");
        details["version"] = db.getDocument("products", variantId)["version"];
        commerce::saveProduct(db, variantId, details, false, "admin");
        check(db.getDocument("products", variantId)["stock"] == 5, "Fresh details edit changed stock");
        std::cout << "PASS emulator: versioned variant edits and separate stock adjustments\n";
        const auto stockVersion = db.getDocument("products", variantId)["version"];
        auto adjust = [&] {
            try
            {
                commerce::adjustStock(db, variantId,
                                      {{"version", stockVersion}, {"delta", -1}, {"reason", "Damaged"}},
                                      "admin");
                return 1;
            }
            catch (const ApiError &e)
            {
                if (e.status == 409)
                    return 0;
                throw;
            }
        };
        auto adj1 = std::async(std::launch::async, adjust), adj2 = std::async(std::launch::async, adjust);
        check(adj1.get() + adj2.get() == 1 && db.getDocument("products", variantId)["stock"] == 4,
              "Concurrent stock adjustment duplicated");
        std::cout << "PASS emulator: concurrent stock adjustments\n";
        // Clean up only role-test fixtures in this fixed disposable demo project, including interrupted runs.
        for (const auto &user : db.listDocuments("users"))
            if (user.at("id").get<std::string>().rfind("role-test-", 0) == 0)
                db.setDocument("users", user["id"], {{"role", "customer"}});
        const std::string adminA = "role-test-a-" + suffix, adminB = "role-test-b-" + suffix;
        for (bool cross : {false, true})
        {
            db.setDocument("users", adminA, {{"role", "admin"}});
            db.setDocument("users", adminB, {{"role", "admin"}});
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
            auto d1 = std::async(std::launch::async, demote, adminA, cross ? adminB : adminA);
            auto d2 = std::async(std::launch::async, demote, adminB, cross ? adminA : adminB);
            check(d1.get() + d2.get() == 1, "Concurrent demotions did not preserve last admin");
            check((db.getDocument("users", adminA)["role"] == "admin") !=
                      (db.getDocument("users", adminB)["role"] == "admin"),
                  "Last admin was lost");
            std::cout << "PASS emulator: concurrent " << (cross ? "cross" : "self")
                      << " demotions preserve last admin\n";
        }
        db.setDocument("users", adminA, {{"role", "customer"}});
        db.setDocument("users", adminB, {{"role", "customer"}});
        const auto pageUser = "page-" + suffix;
        std::vector<std::string> pageIds;
        for (int i = 0; i < 7; ++i)
        {
            auto fixture = order;
            fixture["userId"] = pageUser;
            fixture["createdAt"] = 100;
            const auto id = "page-" + suffix + "-" + std::to_string(i);
            db.setDocument("orders", id, fixture);
            pageIds.push_back(id);
        }
        auto other = order;
        other["userId"] = pageUser + "-other";
        other["createdAt"] = 200;
        db.setDocument("orders", pageUser + "-other", other);
        const auto pageOne = db.historyPage("orders", 3, "", "userId", pageUser);
        check(pageOne["items"].size() == 3 && pageOne["items"][0]["id"] == pageIds[6],
              "History first page/tie ordering failed");
        const auto cursorOne = pageOne["nextCursor"].get<std::string>();
        db.deleteDocument("orders", pageIds[4]);
        auto newest = order;
        newest["userId"] = pageUser;
        newest["createdAt"] = 300;
        db.setDocument("orders", pageUser + "-new", newest);
        const auto pageTwo = db.historyPage("orders", 3, cursorOne, "userId", pageUser);
        check(pageTwo["items"].size() == 3 && pageTwo["items"][0]["id"] == pageIds[3] &&
                  pageTwo["items"][2]["id"] == pageIds[1],
              "Cursor skipped or repeated records after deletion/insertion");
        const auto pageThree = db.historyPage("orders", 3, pageTwo["nextCursor"], "userId", pageUser);
        check(pageThree["items"].size() == 1 && pageThree["items"][0]["id"] == pageIds[0] &&
                  pageThree["nextCursor"].is_null(),
              "Final page failed");
        const auto refreshed = db.historyPage("orders", 3, "", "userId", pageUser);
        check(refreshed["items"][0]["id"] == pageUser + "-new", "Refresh missed new records");
        bool scopeRejected = false;
        try
        {
            db.historyPage("orders", 3, cursorOne, "userId", pageUser + "-other");
        }
        catch (const ApiError &e)
        {
            scopeRejected = e.status == 400;
        }
        check(scopeRejected, "History cursor crossed customer scope");
        for (const auto &id : pageIds)
            db.deleteDocument("orders", id);
        db.deleteDocument("orders", pageUser + "-other");
        db.deleteDocument("orders", pageUser + "-new");
        std::cout
            << "PASS emulator: bounded history pages, customer isolation, ties, deletion and insertion\n";
        std::cout << "21 Firestore emulator integration scenarios passed\n";
    }
    catch (const DatabaseError &e)
    {
        std::cerr << "FAIL database: " << e.status << " " << e.code << '\n';
        return 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
