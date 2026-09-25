#include "FirestoreClient.h"
#include "Validation.h"
#include "runtime/Runtime.h"
#include <charconv>
#include <cmath>
#include <drogon/HttpClient.h>
#include <drogon/utils/Utilities.h>
#include <future>
#include <set>
#include <thread>
#include <trantor/net/EventLoopThread.h>
using json = nlohmann::json;

FirestoreClient::Transport FirestoreClient::httpTransport(std::string baseUrl,
                                                          std::function<std::string()> tokenProvider)
{
    return [baseUrl = std::move(baseUrl), tokenProvider = std::move(tokenProvider)](
               const std::string &method, const std::string &path, const json &body) {
        // A dedicated I/O loop keeps synchronous controller waits off their own loop.
        static trantor::EventLoopThread io;
        static const bool started = [] {
            io.run();
            return true;
        }();
        (void)started;
        auto client = drogon::HttpClient::newHttpClient(baseUrl, io.getLoop());
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(method == "GET" ? drogon::Get : method == "DELETE" ? drogon::Delete : drogon::Post);
        req->setPath(path);
        req->setPathEncode(false);
        req->addHeader("Authorization", "Bearer " + tokenProvider());
        if (!body.is_null())
        {
            req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
            req->setBody(body.dump());
        }
        auto promise = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
        auto future = promise->get_future();
        const auto budget = runtime::remaining();
        client->sendRequest(
            req,
            [promise](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
                promise->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
            },
            std::chrono::duration<double>(budget).count());
        if (future.wait_for(budget) != std::future_status::ready)
            throw DatabaseError(503, "UNAVAILABLE");
        auto response = future.get();
        if (!response)
            throw DatabaseError(503, "UNAVAILABLE");
        json parsed =
            response->getBody().empty() ? json::object() : json::parse(response->getBody(), nullptr, false);
        if (parsed.is_discarded() || (!parsed.is_object() && !parsed.is_array()))
            throw DatabaseError(502, "INVALID_RESPONSE");
        return Response{static_cast<int>(response->getStatusCode()), parsed};
    };
}
FirestoreClient::FirestoreClient(const std::string &projectId, FirebaseAuth &auth)
    : FirestoreClient(projectId, httpTransport("https://firestore.googleapis.com",
                                               [&auth] { return auth.getAccessToken(); }))
{
}
FirestoreClient::FirestoreClient(const std::string &projectId, Transport transport)
    : root_("projects/" + projectId + "/databases/(default)/documents"), transport_(std::move(transport))
{
}
std::string FirestoreClient::segment(const std::string &value)
{
    if (value.empty() || value == "." || value == ".." || value.find('/') != std::string::npos)
        throw ApiError(400, "INVALID_INPUT", "Invalid document path");
    return drogon::utils::urlEncodeComponent(value);
}
json FirestoreClient::call(const std::string &method, const std::string &path, const json &body,
                           bool allowMissing)
{
    runtime::remaining();
    bool changesProducts = method == "DELETE" && path.rfind("/products/", 0) == 0;
    if (path == ":commit" && body.contains("writes"))
        for (const auto &write : body["writes"])
            if (write.contains("update") &&
                write["update"].value("name", "").rfind(root_ + "/products/", 0) == 0)
                changesProducts = true;
    // Invalidate on both sides of an attempted write, including uncertain commits.
    // Generation changes prevent a concurrent old read from refilling the cache.
    struct Invalidate
    {
        CatalogCache *cache;
        explicit Invalidate(CatalogCache *c) : cache(c)
        {
            if (cache)
                cache->invalidate();
        }
        ~Invalidate()
        {
            if (cache)
                cache->invalidate();
        }
    } invalidation(changesProducts ? &catalogCache_ : nullptr);
    auto response = transport_(method, "/v1/" + root_ + path, body);
    if (allowMissing && response.status == 404)
        return nullptr;
    if (response.status < 200 || response.status >= 300)
    {
        std::string code = "DATABASE_ERROR";
        if (response.body.is_object() && response.body.contains("error") &&
            response.body["error"].is_object() && response.body["error"].contains("status") &&
            response.body["error"]["status"].is_string())
            code = response.body["error"]["status"].get<std::string>();
        throw DatabaseError(response.status, code);
    }
    if (!response.body.is_object() &&
        !((path == ":batchGet" || path == ":runQuery") && response.body.is_array()))
        throw DatabaseError(502, "INVALID_RESPONSE");
    return response.body;
}
json toValue(const json &val)
{
    if (val.is_string())
        return {{"stringValue", val.get<std::string>()}};
    else if (val.is_boolean())
        return {{"booleanValue", val.get<bool>()}};
    else if (val.is_number_integer())
        return {{"integerValue", std::to_string(val.get<long long>())}};
    else if (val.is_number_float())
        return {{"doubleValue", val.get<double>()}};
    else if (val.is_null())
        return {{"nullValue", nullptr}};
    else if (val.is_array())
    {
        json values = json::array();
        for (const auto &item : val)
            values.push_back(toValue(item));
        return {{"arrayValue", {{"values", values}}}};
    }
    else if (val.is_object())
    {
        json mapFields = json::object();
        for (auto it = val.begin(); it != val.end(); ++it)
            mapFields[it.key()] = toValue(it.value());
        return {{"mapValue", {{"fields", mapFields}}}};
    }
    else
        return {{"stringValue", val.dump()}}; // fallback
}

// Converts a single Firestore typed wrapper back into a plain JSON value.
json fromValue(const json &wrapped)
{
    if (!wrapped.is_object() || wrapped.size() != 1)
        throw DatabaseError(502, "INVALID_FIELD_TYPE");
    if (wrapped.contains("stringValue"))
        return wrapped["stringValue"].get<std::string>();
    else if (wrapped.contains("booleanValue"))
        return wrapped["booleanValue"].get<bool>();
    else if (wrapped.contains("integerValue"))
    {
        const auto value = wrapped["integerValue"].get<std::string>();
        int64_t number = 0;
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
            throw DatabaseError(502, "INVALID_FIELD_TYPE");
        return number;
    }
    else if (wrapped.contains("doubleValue"))
    {
        if (!wrapped["doubleValue"].is_number())
            throw DatabaseError(502, "INVALID_FIELD_TYPE");
        const auto number = wrapped["doubleValue"].get<double>();
        if (!std::isfinite(number))
            throw DatabaseError(502, "INVALID_FIELD_TYPE");
        return number;
    }
    else if (wrapped.contains("nullValue"))
        return nullptr;
    else if (wrapped.contains("arrayValue"))
    {
        if (!wrapped["arrayValue"].is_object() ||
            (wrapped["arrayValue"].contains("values") && !wrapped["arrayValue"]["values"].is_array()))
            throw DatabaseError(502, "INVALID_FIELD_TYPE");
        json result = json::array();
        if (wrapped["arrayValue"].contains("values"))
            for (const auto &item : wrapped["arrayValue"]["values"])
                result.push_back(fromValue(item));
        return result;
    }
    else if (wrapped.contains("mapValue"))
    {
        if (!wrapped["mapValue"].is_object() ||
            (wrapped["mapValue"].contains("fields") && !wrapped["mapValue"]["fields"].is_object()))
            throw DatabaseError(502, "INVALID_FIELD_TYPE");
        json result = json::object();
        if (wrapped["mapValue"].contains("fields"))
            for (auto it = wrapped["mapValue"]["fields"].begin(); it != wrapped["mapValue"]["fields"].end();
                 ++it)
                result[it.key()] = fromValue(it.value());
        return result;
    }
    throw DatabaseError(502, "INVALID_FIELD_TYPE");
}

json FirestoreClient::toFields(const json &plain)
{
    json fields = json::object();
    for (auto it = plain.begin(); it != plain.end(); ++it)
        fields[it.key()] = toValue(it.value());
    return fields;
}

json FirestoreClient::fromFields(const json &firestoreDoc)
{
    json result = json::object();
    if (!firestoreDoc.contains("fields"))
        return result;
    if (!firestoreDoc["fields"].is_object())
        throw DatabaseError(502, "INVALID_RESPONSE");

    for (auto it = firestoreDoc["fields"].begin(); it != firestoreDoc["fields"].end(); ++it)
        result[it.key()] = fromValue(it.value());

    return result;
}

json FirestoreClient::getDocument(const std::string &collection, const std::string &id,
                                  const std::string &transaction)
{
    const auto url = "/" + segment(collection) + "/" + segment(id);
    json doc;
    if (transaction.empty())
        doc = call("GET", url, nullptr, true);
    else
    {
        // batchGet carries the opaque transaction token in JSON, including on the emulator.
        const auto name = root_ + "/" + collection + "/" + id;
        const auto rows =
            call("POST", ":batchGet", {{"documents", json::array({name})}, {"transaction", transaction}});
        if (!rows.is_array() || rows.size() != 1 || !rows[0].is_object())
            throw DatabaseError(502, "INVALID_RESPONSE");
        const auto &row = rows[0];
        if (row.contains("missing") && !row.contains("found") && row["missing"] == name)
            return nullptr;
        if (!row.contains("found") || row.contains("missing") || !row["found"].is_object() ||
            !row["found"].contains("name") || row["found"]["name"] != name)
            throw DatabaseError(502, "INVALID_RESPONSE");
        doc = row["found"];
    }
    if (doc.is_null())
        return nullptr;
    if (!doc.contains("name") || !doc["name"].is_string() ||
        (doc.contains("fields") && !doc["fields"].is_object()))
        throw DatabaseError(502, "INVALID_RESPONSE");
    try
    {
        return fromFields(doc);
    }
    catch (...)
    {
        throw DatabaseError(502, "INVALID_RESPONSE");
    }
}
json FirestoreClient::queryEqual(const std::string &collection, const std::string &field, const json &value,
                                 const std::string &transaction)
{
    segment(collection);
    segment(field);
    if (transaction.empty())
        throw ApiError(400, "INVALID_INPUT", "Query requires a transaction");
    const json filter = {{"field", {{"fieldPath", field}}},
                         {"op", "EQUAL"},
                         {"value", toFields({{"value", value}}).at("value")}};
    const json query = {{"from", json::array({{{"collectionId", collection}}})},
                        {"where", {{"fieldFilter", filter}}}};
    const auto rows = call("POST", ":runQuery", {{"transaction", transaction}, {"structuredQuery", query}});
    if (!rows.is_array())
        throw DatabaseError(502, "INVALID_RESPONSE");
    json result = json::array();
    for (const auto &row : rows)
    {
        if (!row.is_object())
            throw DatabaseError(502, "INVALID_RESPONSE");
        if (!row.contains("document"))
        {
            if (!row.contains("readTime") || !row["readTime"].is_string())
                throw DatabaseError(502, "INVALID_RESPONSE");
            continue;
        }
        const auto &doc = row["document"];
        if (!doc.is_object() || !doc.contains("name") || !doc["name"].is_string())
            throw DatabaseError(502, "INVALID_RESPONSE");
        auto item = fromFields(doc);
        const auto name = doc["name"].get<std::string>();
        const auto prefix = root_ + "/" + collection + "/";
        if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size() ||
            name.find('/', prefix.size()) != std::string::npos)
            throw DatabaseError(502, "INVALID_RESPONSE");
        item["id"] = name.substr(prefix.size());
        result.push_back(item);
    }
    return result;
}
json FirestoreClient::dueOrders(int64_t now, const json &after)
{
    json query = {
        {"from", json::array({{{"collectionId", "orders"}}})},
        {"where",
         {{"fieldFilter",
           {{"field", {{"fieldPath", "pendingExpiresAt"}}},
            {"op", "LESS_THAN_OR_EQUAL"},
            {"value", {{"integerValue", std::to_string(now)}}}}}}},
        {"orderBy", json::array({{{"field", {{"fieldPath", "pendingExpiresAt"}}}, {"direction", "ASCENDING"}},
                                 {{"field", {{"fieldPath", "__name__"}}}, {"direction", "ASCENDING"}}})},
        {"limit", 50}};
    if (!after.is_null())
        query["startAt"] = {
            {"before", false},
            {"values",
             json::array({{{"integerValue", std::to_string(after.at("pendingExpiresAt").get<int64_t>())}},
                          {{"referenceValue", root_ + "/orders/" + segment(after.at("id"))}}})}};
    const auto rows = call("POST", ":runQuery", {{"structuredQuery", query}});
    if (!rows.is_array())
        throw DatabaseError(502, "INVALID_RESPONSE");
    json result = json::array();
    for (const auto &row : rows)
    {
        if (!row.is_object())
            throw DatabaseError(502, "INVALID_RESPONSE");
        if (!row.contains("document"))
        {
            if (!row.contains("readTime") || !row["readTime"].is_string())
                throw DatabaseError(502, "INVALID_RESPONSE");
            continue;
        }
        auto doc = fromFields(row.at("document"));
        const auto name = row.at("document").at("name").get<std::string>();
        const auto prefix = root_ + "/orders/";
        if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size() ||
            name.find('/', prefix.size()) != std::string::npos || !doc.contains("pendingExpiresAt") ||
            !doc["pendingExpiresAt"].is_number_integer())
            throw DatabaseError(502, "INVALID_RESPONSE");
        doc["id"] = name.substr(prefix.size());
        result.push_back(doc);
    }
    return result;
}
json FirestoreClient::historyPage(const std::string &collection, int limit, const std::string &cursor,
                                  const std::string &field, const std::string &value)
{
    validation::require(limit >= 1 && limit <= 100, "Page size must be between 1 and 100");
    segment(collection);
    if (!field.empty())
        segment(field);
    const json scope = {collection, field, value};
    json query = {
        {"from", json::array({{{"collectionId", collection}}})},
        {"orderBy", json::array({{{"field", {{"fieldPath", "createdAt"}}}, {"direction", "DESCENDING"}},
                                 {{"field", {{"fieldPath", "__name__"}}}, {"direction", "DESCENDING"}}})},
        {"limit", limit + 1}};
    if (!field.empty())
        query["where"] = {
            {"fieldFilter",
             {{"field", {{"fieldPath", field}}}, {"op", "EQUAL"}, {"value", {{"stringValue", value}}}}}};
    if (!cursor.empty())
    {
        validation::require(cursor.size() <= 4096, "Invalid page cursor");
        json decoded;
        try
        {
            decoded = json::parse(drogon::utils::base64Decode(cursor));
        }
        catch (...)
        {
            throw ApiError(400, "INVALID_CURSOR", "Invalid page cursor. Refresh the list.");
        }
        validation::require(decoded.is_object() && decoded.value("scope", json()) == scope &&
                                decoded.value("version", json()) == 1,
                            "Cursor does not match this list");
        const auto time = validation::integer(decoded, "createdAt", 0, INT64_MAX);
        const auto id = validation::text(decoded, "id", 1500);
        segment(id);
        query["startAt"] = {
            {"before", false},
            {"values", json::array({{{"integerValue", std::to_string(time)}},
                                    {{"referenceValue", root_ + "/" + collection + "/" + id}}})}};
    }
    const auto rows = call("POST", ":runQuery", {{"structuredQuery", query}});
    if (!rows.is_array())
        throw DatabaseError(502, "INVALID_RESPONSE");
    json items = json::array();
    const auto prefix = root_ + "/" + collection + "/";
    for (const auto &row : rows)
    {
        if (!row.is_object())
            throw DatabaseError(502, "INVALID_RESPONSE");
        if (!row.contains("document"))
        {
            if (!row.contains("readTime") || !row["readTime"].is_string())
                throw DatabaseError(502, "INVALID_RESPONSE");
            continue;
        }
        const auto &doc = row["document"];
        if (!doc.is_object() || !doc.contains("name") || !doc["name"].is_string())
            throw DatabaseError(502, "INVALID_RESPONSE");
        const auto name = doc["name"].get<std::string>();
        if (name.rfind(prefix, 0) != 0 || name.size() == prefix.size() ||
            name.find('/', prefix.size()) != std::string::npos)
            throw DatabaseError(502, "INVALID_RESPONSE");
        auto item = fromFields(doc);
        if (!item.contains("createdAt") || !item["createdAt"].is_number_integer() || item["createdAt"] < 0 ||
            item["createdAt"] > INT64_MAX || (!field.empty() && item.value(field, json()) != value))
            throw DatabaseError(502, "INVALID_RESPONSE");
        item["id"] = name.substr(prefix.size());
        items.push_back(std::move(item));
        if (items.size() > static_cast<size_t>(limit + 1))
            throw DatabaseError(502, "INVALID_RESPONSE");
    }
    json next = nullptr;
    if (items.size() > static_cast<size_t>(limit))
    {
        items.erase(items.end() - 1);
        const auto &last = items.back();
        const json token = {
            {"version", 1}, {"scope", scope}, {"createdAt", last["createdAt"]}, {"id", last["id"]}};
        next = drogon::utils::base64Encode(token.dump());
    }
    return {{"items", items}, {"nextCursor", next}};
}
json FirestoreClient::catalogProducts()
{
    return catalogCache_.get([this] { return listDocuments("products"); });
}
json FirestoreClient::listDocuments(const std::string &collection)
{
    json result = json::array();
    std::string token;
    std::set<std::string> seen;
    do
    {
        auto page =
            call("GET", "/" + segment(collection) + "?pageSize=100" +
                            (token.empty() ? "" : "&pageToken=" + drogon::utils::urlEncodeComponent(token)));
        if (page.contains("documents"))
        {
            if (!page["documents"].is_array())
                throw DatabaseError(502, "INVALID_RESPONSE");
            for (const auto &doc : page["documents"])
            {
                if (!doc.is_object() || !doc.contains("name") || !doc["name"].is_string())
                    throw DatabaseError(502, "INVALID_RESPONSE");
                auto name = doc["name"].get<std::string>();
                try
                {
                    auto item = fromFields(doc);
                    item["id"] = name.substr(name.find_last_of('/') + 1);
                    result.push_back(item);
                }
                catch (...)
                {
                    throw DatabaseError(502, "INVALID_RESPONSE");
                }
            }
        }
        if (page.contains("nextPageToken") && !page["nextPageToken"].is_string())
            throw DatabaseError(502, "INVALID_RESPONSE");
        token = page.value("nextPageToken", "");
        if (!token.empty() && !seen.insert(token).second)
            throw DatabaseError(502, "INVALID_RESPONSE");
    } while (!token.empty());
    return result;
}
json FirestoreClient::writeJson(const Write &write) const
{
    // Resource names contain raw document IDs, while request URL segments are encoded.
    segment(write.collection);
    segment(write.id);
    json output = {
        {"update",
         {{"name", root_ + "/" + write.collection + "/" + write.id}, {"fields", toFields(write.fields)}}}};
    if (!write.mask.empty())
        output["updateMask"] = {{"fieldPaths", write.mask}};
    if (write.createOnly)
        output["currentDocument"] = {{"exists", false}};
    else if (write.existsOnly)
        output["currentDocument"] = {{"exists", true}};
    return output;
}
bool FirestoreClient::setDocument(const std::string &collection, const std::string &id, const json &fields)
{
    auto response =
        call("POST", ":commit", {{"writes", json::array({writeJson({collection, id, fields, {}})})}});
    if (!response.contains("writeResults") || !response["writeResults"].is_array() ||
        response["writeResults"].size() != 1)
        throw DatabaseError(502, "INVALID_RESPONSE");
    return true;
}
bool FirestoreClient::createDocument(const std::string &collection, const std::string &id, const json &fields)
{
    auto response =
        call("POST", ":commit", {{"writes", json::array({writeJson({collection, id, fields, {}, true})})}});
    if (!response.contains("writeResults") || !response["writeResults"].is_array() ||
        response["writeResults"].size() != 1)
        throw DatabaseError(502, "INVALID_RESPONSE");
    return true;
}
std::string FirestoreClient::addDocument(const std::string &collection, const json &fields)
{
    auto id = drogon::utils::getUuid();
    createDocument(collection, id, fields);
    return id;
}
bool FirestoreClient::deleteDocument(const std::string &collection, const std::string &id)
{
    call("DELETE", "/" + segment(collection) + "/" + segment(id), nullptr, true);
    return true;
}
json FirestoreClient::transact(
    const std::function<json(const std::string &, std::vector<Write> &)> &operation)
{
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        auto begin = call("POST", ":beginTransaction", {{"options", {{"readWrite", json::object()}}}});
        if (!begin.contains("transaction") || !begin["transaction"].is_string() ||
            begin["transaction"].get<std::string>().empty())
            throw DatabaseError(502, "INVALID_RESPONSE");
        const auto token = begin["transaction"].get<std::string>();
        const auto rollback = [&] {
            try
            {
                call("POST", ":rollback", {{"transaction", token}});
            }
            catch (...)
            {
            }
        };
        try
        {
            std::vector<Write> writes;
            auto result = operation(token, writes);
            if (writes.empty())
            {
                rollback();
                return result;
            }
            json encoded = json::array();
            for (const auto &write : writes)
                encoded.push_back(writeJson(write));
            auto response = call("POST", ":commit", {{"transaction", token}, {"writes", encoded}});
            if (!response.contains("writeResults") || !response["writeResults"].is_array() ||
                response["writeResults"].size() != writes.size())
                throw DatabaseError(502, "INVALID_RESPONSE");
            return result;
        }
        catch (const DatabaseError &e)
        {
            rollback();
            // Only a known ABORTED transaction is safe to retry automatically.
            // Timeouts/5xx may have committed: the caller must reuse its idempotency key.
            if (!e.conflict())
                throw;
            if (attempt == 4)
                throw ApiError(409, "CONCURRENT_CHANGE",
                               "The data changed during this request. Please retry.");
            std::this_thread::sleep_for(std::chrono::milliseconds(10 * (1 << attempt)));
        }
        catch (...)
        {
            rollback();
            throw;
        }
    }
    throw DatabaseError(503, "UNAVAILABLE");
}
