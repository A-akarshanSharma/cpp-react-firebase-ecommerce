#include "FirestoreClient.h"
#include <drogon/utils/Utilities.h>
#include <iostream>
using json = nlohmann::json;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void invalid(F fn)
{
    try
    {
        fn();
    }
    catch (const ApiError &e)
    {
        check(e.status == 400, "Wrong validation status");
        return;
    }
    throw std::runtime_error("Expected bad-input rejection");
}
int main()
{
    try
    {
        const std::string root = "projects/demo-pagination/databases/(default)/documents/";
        json body, response = json::array();
        int calls = 0;
        FirestoreClient db("demo-pagination",
                           [&](const std::string &method, const std::string &path, const json &input) {
                               check(method == "POST" && path.find(":runQuery") != std::string::npos,
                                     "History used collection scan");
                               ++calls;
                               body = input;
                               return FirestoreClient::Response{200, response};
                           });
        const auto record = [&](std::string id, int time, std::string owner = "alice") {
            return json{{"document",
                         {{"name", root + "orders/" + id},
                          {"fields", FirestoreClient::toFields({{"userId", owner}, {"createdAt", time}})}}}};
        };
        response = {record("c", 100), record("b", 100), record("a", 99)};
        auto first = db.historyPage("orders", 2, "", "userId", "alice");
        check(first["items"].size() == 2 && first["items"][1]["id"] == "b", "Wrong page boundary");
        check(body["structuredQuery"]["limit"] == 3, "Query not bounded with one lookahead");
        check(body["structuredQuery"]["where"]["fieldFilter"]["value"]["stringValue"] == "alice",
              "Missing owner filter");
        check(body["structuredQuery"]["orderBy"].size() == 2, "No stable tie breaker");
        const auto cursor = first["nextCursor"].get<std::string>();
        response = {record("a", 99)};
        auto last = db.historyPage("orders", 2, cursor, "userId", "alice");
        check(last["items"].size() == 1 && last["nextCursor"].is_null(), "Last page incorrect");
        check(body["structuredQuery"]["startAt"]["before"] == false &&
                  body["structuredQuery"]["startAt"]["values"][1]["referenceValue"] == root + "orders/b",
              "Cursor not exclusive raw reference");
        std::cout << "PASS pagination: bounded filtered query and stable cursor\n";
        const auto before = calls;
        invalid([&] { db.historyPage("orders", 0); });
        invalid([&] { db.historyPage("orders", 101); });
        invalid([&] { db.historyPage("orders", 25, "garbage"); });
        invalid([&] { db.historyPage("orders", 25, std::string(4097, 'a')); });
        invalid([&] { db.historyPage("orders", 2, cursor, "userId", "bob"); });
        invalid([&] { db.historyPage("notifications", 2, cursor, "userId", "alice"); });
        check(calls == before, "Invalid input performed database work");
        std::cout << "PASS pagination: invalid sizes/cursors and cross-user/list rejection\n";
        response = json::array({{{"readTime", "2026-09-21T00:00:00Z"}}});
        auto empty = db.historyPage("orders", 25, "", "userId", "alice");
        check(empty["items"].empty() && empty["nextCursor"].is_null(), "Empty page malformed");
        std::cout << "PASS pagination: empty result\n";
        for (const auto &bad : json::array(
                 {json::object(), json::array({42}), json::array({json::object()}),
                  json::array({record("x", 1, "bob")}),
                  json::array({{{"document", {{"name", root + "users/x"}, {"fields", json::object()}}}}})}))
        {
            response = bad;
            bool rejected = false;
            try
            {
                db.historyPage("orders", 2, "", "userId", "alice");
            }
            catch (const DatabaseError &)
            {
                rejected = true;
            }
            check(rejected, "Malformed or foreign records accepted");
        }
        std::cout << "PASS pagination: malformed/foreign upstream records rejected\n4 pagination scenarios "
                     "passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
