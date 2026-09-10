#include "FirestoreClient.h"
#include <drogon/HttpClient.h>
#include <future>
#include <iostream>

using json = nlohmann::json;

FirestoreClient::FirestoreClient(const std::string &projectId, FirebaseAuth &auth)
    : projectId_(projectId), auth_(auth)
{
}

std::string FirestoreClient::baseUrl()
{
    return "https://firestore.googleapis.com";
}

// Converts a single plain JSON value into Firestore's typed wrapper.
// Handles nesting - arrays and objects recurse, so an array of objects
// (e.g. cart items) round-trips correctly.
json FirestoreClient::toFirestoreValue(const json &val)
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
            values.push_back(toFirestoreValue(item));
        return {{"arrayValue", {{"values", values}}}};
    }
    else if (val.is_object())
    {
        json mapFields = json::object();
        for (auto it = val.begin(); it != val.end(); ++it)
            mapFields[it.key()] = toFirestoreValue(it.value());
        return {{"mapValue", {{"fields", mapFields}}}};
    }
    else
        return {{"stringValue", val.dump()}}; // fallback
}

// Converts a single Firestore typed wrapper back into a plain JSON value.
json FirestoreClient::fromFirestoreValue(const json &wrapped)
{
    if (wrapped.contains("stringValue"))
        return wrapped["stringValue"].get<std::string>();
    else if (wrapped.contains("booleanValue"))
        return wrapped["booleanValue"].get<bool>();
    else if (wrapped.contains("integerValue"))
        return std::stoll(wrapped["integerValue"].get<std::string>());
    else if (wrapped.contains("doubleValue"))
        return wrapped["doubleValue"].get<double>();
    else if (wrapped.contains("nullValue"))
        return nullptr;
    else if (wrapped.contains("arrayValue"))
    {
        json result = json::array();
        if (wrapped["arrayValue"].contains("values"))
            for (const auto &item : wrapped["arrayValue"]["values"])
                result.push_back(fromFirestoreValue(item));
        return result;
    }
    else if (wrapped.contains("mapValue"))
    {
        json result = json::object();
        if (wrapped["mapValue"].contains("fields"))
            for (auto it = wrapped["mapValue"]["fields"].begin(); it != wrapped["mapValue"]["fields"].end(); ++it)
                result[it.key()] = fromFirestoreValue(it.value());
        return result;
    }
    return nullptr; // unhandled type (timestamp, geopoint, reference) - extend if you need these
}

json FirestoreClient::toFirestoreFields(const json &plain)
{
    json fields = json::object();
    for (auto it = plain.begin(); it != plain.end(); ++it)
        fields[it.key()] = toFirestoreValue(it.value());
    return fields;
}

json FirestoreClient::fromFirestoreFields(const json &firestoreDoc)
{
    json result = json::object();
    if (!firestoreDoc.contains("fields"))
        return result;

    for (auto it = firestoreDoc["fields"].begin(); it != firestoreDoc["fields"].end(); ++it)
        result[it.key()] = fromFirestoreValue(it.value());

    return result;
}

bool FirestoreClient::setDocument(const std::string &collection,
                                   const std::string &docId,
                                   const json &fields)
{
    std::string token = auth_.getAccessToken();
    std::string path = "/v1/projects/" + projectId_ +
                        "/databases/(default)/documents/" + collection + "/" + docId;

    json body = {{"fields", toFirestoreFields(fields)}};

    auto client = drogon::HttpClient::newHttpClient(baseUrl());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Patch);
    req->setPath(path);
    req->addHeader("Authorization", "Bearer " + token);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    req->setBody(body.dump());

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp)
    {
        std::cerr << "setDocument: network error\n";
        return false;
    }

    if (resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "setDocument failed [" << resp->getStatusCode() << "]: "
                   << resp->getBody() << "\n";
        return false;
    }
    return true;
}

json FirestoreClient::getDocument(const std::string &collection, const std::string &docId)
{
    std::string token = auth_.getAccessToken();
    std::string path = "/v1/projects/" + projectId_ +
                        "/databases/(default)/documents/" + collection + "/" + docId;

    auto client = drogon::HttpClient::newHttpClient(baseUrl());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(path);
    req->addHeader("Authorization", "Bearer " + token);

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "getDocument failed: " << (resp ? std::string(resp->getBody()) : "network error") << "\n";
        return json::object();
    }

    json firestoreDoc = json::parse(resp->getBody(), nullptr, false);
    if (firestoreDoc.is_discarded())
        return json::object();

    return fromFirestoreFields(firestoreDoc);
}

json FirestoreClient::listDocuments(const std::string &collection)
{
    std::string token = auth_.getAccessToken();
    std::string path = "/v1/projects/" + projectId_ +
                        "/databases/(default)/documents/" + collection;

    auto client = drogon::HttpClient::newHttpClient(baseUrl());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Get);
    req->setPath(path);
    req->addHeader("Authorization", "Bearer " + token);

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "listDocuments failed: " << (resp ? std::string(resp->getBody()) : "network error") << "\n";
        return json::array();
    }

    json parsed = json::parse(resp->getBody(), nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("documents"))
        return json::array(); // empty collection returns no "documents" key - not an error

    json result = json::array();
    for (const auto &doc : parsed["documents"])
    {
        json item = fromFirestoreFields(doc);
        // doc["name"] looks like ".../documents/products/abc123" - pull the last segment as the id
        std::string name = doc.value("name", "");
        auto lastSlash = name.find_last_of('/');
        item["id"] = (lastSlash != std::string::npos) ? name.substr(lastSlash + 1) : "";
        result.push_back(item);
    }
    return result;
}

std::string FirestoreClient::addDocument(const std::string &collection, const json &fields)
{
    std::string token = auth_.getAccessToken();
    std::string path = "/v1/projects/" + projectId_ +
                        "/databases/(default)/documents/" + collection;

    json body = {{"fields", toFirestoreFields(fields)}};

    auto client = drogon::HttpClient::newHttpClient(baseUrl());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Post);
    req->setPath(path);
    req->addHeader("Authorization", "Bearer " + token);
    req->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    req->setBody(body.dump());

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "addDocument failed: " << (resp ? std::string(resp->getBody()) : "network error") << "\n";
        return "";
    }

    json parsed = json::parse(resp->getBody(), nullptr, false);
    if (parsed.is_discarded())
        return "";

    std::string name = parsed.value("name", "");
    auto lastSlash = name.find_last_of('/');
    return (lastSlash != std::string::npos) ? name.substr(lastSlash + 1) : "";
}

bool FirestoreClient::deleteDocument(const std::string &collection, const std::string &docId)
{
    std::string token = auth_.getAccessToken();
    std::string path = "/v1/projects/" + projectId_ +
                        "/databases/(default)/documents/" + collection + "/" + docId;

    auto client = drogon::HttpClient::newHttpClient(baseUrl());
    auto req = drogon::HttpRequest::newHttpRequest();
    req->setMethod(drogon::Delete);
    req->setPath(path);
    req->addHeader("Authorization", "Bearer " + token);

    auto prom = std::make_shared<std::promise<drogon::HttpResponsePtr>>();
    auto fut = prom->get_future();
    client->sendRequest(req, [prom](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
        prom->set_value(result == drogon::ReqResult::Ok ? resp : nullptr);
    });

    auto resp = fut.get();
    if (!resp || resp->getStatusCode() != drogon::k200OK)
    {
        std::cerr << "deleteDocument failed: " << (resp ? std::string(resp->getBody()) : "network error") << "\n";
        return false;
    }
    return true;
}
