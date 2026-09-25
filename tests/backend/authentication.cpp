#include "controllers/ControllerSupport.h"
#include <iostream>
#include <jwt-cpp/jwt.h>
#include <openssl/pem.h>
using json = nlohmann::json;
FirestoreClient *g_firestoreClient = nullptr;
AuthMiddleware *g_authMiddleware = nullptr;
void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
std::pair<std::string, std::string> keys()
{
    auto ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    check(ctx && EVP_PKEY_keygen_init(ctx) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048) == 1,
          "Key setup failed");
    EVP_PKEY *key = nullptr;
    check(EVP_PKEY_keygen(ctx, &key) == 1, "Key generation failed");
    auto pub = BIO_new(BIO_s_mem()), priv = BIO_new(BIO_s_mem());
    check(PEM_write_bio_PUBKEY(pub, key) == 1 &&
              PEM_write_bio_PrivateKey(priv, key, nullptr, nullptr, 0, nullptr, nullptr) == 1,
          "PEM failed");
    auto read = [](BIO *bio) {
        char *bytes;
        const auto size = BIO_get_mem_data(bio, &bytes);
        return std::string(bytes, size);
    };
    auto result = std::make_pair(read(pub), read(priv));
    BIO_free(pub);
    BIO_free(priv);
    EVP_PKEY_free(key);
    EVP_PKEY_CTX_free(ctx);
    return result;
}
json claims()
{
    const auto now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    return {{"sub", "u"},
            {"email", "u@example.test"},
            {"iss", "https://securetoken.google.com/test-project"},
            {"aud", "test-project"},
            {"iat", now},
            {"auth_time", now - 100},
            {"exp", now + 3600}};
}
std::string token(const json &payload, const std::string &key, const std::string &kid = "test-key")
{
    auto builder = jwt::create().set_type("JWT").set_key_id(kid);
    for (auto it = payload.begin(); it != payload.end(); ++it)
    {
        picojson::value value;
        check(picojson::parse(value, it.value().dump()).empty(), "Claim conversion failed");
        builder.set_payload_claim(it.key(), jwt::claim(value));
    }
    return builder.sign(jwt::algorithm::rs256("", key, "", ""));
}
struct Fixture
{
    int lookups = 0, databaseCalls = 0, keyCalls = 0;
    bool accountFailure = false, keyFailure = false;
    json account = {{"localId", "u"}, {"disabled", false}, {"validSince", "0"}};
    std::chrono::steady_clock::time_point clock = std::chrono::steady_clock::now();
    json keyReply;
    FirestoreClient::Response reply = {200, nullptr};
    json profile = {{"role", "admin"}, {"email", "u@example.test"}};
    FirestoreClient db;
    AuthMiddleware auth;
    Fixture(const std::string &publicKey)
        : keyReply({{"test-key", publicKey}}),
          db("test-project",
             [&](const auto &, const std::string &path, const json &body) -> FirestoreClient::Response {
                 ++databaseCalls;
                 if (path.find(":beginTransaction") != std::string::npos)
                     return {200, {{"transaction", "tx"}}};
                 if (path.find(":batchGet") != std::string::npos)
                 {
                     const auto name = body["documents"][0];
                     return {200,
                             json::array({profile.is_null()
                                              ? json{{"missing", name}}
                                              : json{{"found",
                                                      {{"name", name},
                                                       {"fields", FirestoreClient::toFields(profile)}}}}})};
                 }
                 if (path.find(":commit") != std::string::npos)
                 {
                     for (const auto &write : body["writes"])
                         profile = FirestoreClient::fromFields(write["update"]);
                     return {200,
                             {{"writeResults",
                               std::vector<json>(body["writes"].size(), {{"updateTime", "now"}})}}};
                 }
                 if (path.find(":rollback") != std::string::npos)
                     return {200, json::object()};
                 throw std::runtime_error("Unexpected database call");
             }),
          auth(
              db, "test-project",
              [&] {
                  ++keyCalls;
                  if (keyFailure)
                      throw std::runtime_error("offline");
                  return keyReply;
              },
              [&](const std::string &method, const std::string &path,
                  const json &body) -> FirestoreClient::Response {
                  ++lookups;
                  check(method == "POST" && path == "/v1/projects/test-project/accounts:lookup" &&
                            body == json{{"localId", json::array({"u"})}},
                        "Wrong account lookup contract");
                  if (accountFailure)
                      throw DatabaseError(503, "UNAVAILABLE");
                  return reply.body.is_null()
                             ? FirestoreClient::Response{200, {{"users", json::array({account})}}}
                             : reply;
              },
              [&] { return clock; })
    {
        g_authMiddleware = &auth;
    }
    drogon::HttpResponsePtr request(const std::string &bearer, bool admin = false, bool checkout = false)
    {
        auto req = drogon::HttpRequest::newHttpRequest();
        if (checkout)
        {
            req->setMethod(drogon::Post);
            req->setPath("/orders");
        }
        if (!bearer.empty())
            req->addHeader("Authorization", "Bearer " + bearer);
        drogon::HttpResponsePtr response;
        controller::respond([&](auto r) { response = r; },
                            [&]() -> json {
                                const auto user = controller::auth(req, admin);
                                return {{"uid", user.uid}, {"admin", user.isAdmin}};
                            });
        return response;
    }
    void rejected(const std::string &bearer, int status)
    {
        const auto calls = databaseCalls;
        const auto r = request(bearer);
        check(r->getStatusCode() == status, "Unexpected rejection status");
        check(databaseCalls == calls, "Rejected account reached Firestore");
        check(json::parse(r->getBody())["code"] == (status == 503 ? "AUTH_UNAVAILABLE" : "UNAUTHENTICATED"),
              "Wrong public error code");
    }
};
int main()
{
    int passed = 0;
    auto test = [&](const char *name, auto work) {
        work();
        ++passed;
        std::cout << "PASS " << name << '\n';
    };
    try
    {
        const auto [pub, priv] = keys();
        const auto valid = token(claims(), priv);
        test("valid signed sessions preserve admin and customer role checks", [&] {
            Fixture f(pub);
            check(f.request(valid, true)->getStatusCode() == 200, "Admin denied");
            f.profile["role"] = "customer";
            check(f.request(valid)->getStatusCode() == 200 && f.request(valid, true)->getStatusCode() == 403,
                  "Role rules changed");
            check(f.lookups == 3 && f.keyCalls == 1, "Account state cached or keys not cached");
        });
        test("first sign-in provisions only an enabled verified account", [&] {
            Fixture f(pub);
            f.profile = nullptr;
            check(f.request(valid)->getStatusCode() == 200 && f.profile["role"] == "customer",
                  "Profile provisioning failed");
        });
        test("disabled and deleted accounts never reach profile reads or writes", [&] {
            Fixture f(pub);
            f.account["disabled"] = true;
            f.rejected(valid, 401);
            f.reply = {200, {{"users", json::array()}}};
            f.rejected(valid, 401);
            f.reply = {200, json::object()};
            f.rejected(valid, 401);
        });
        test("disablement after a successful request takes effect on the next request", [&] {
            Fixture f(pub);
            check(f.request(valid)->getStatusCode() == 200, "Valid denied");
            f.account["disabled"] = true;
            f.rejected(valid, 401);
        });
        test("revocation uses original auth_time including refreshed tokens and boundary", [&] {
            Fixture f(pub);
            auto payload = claims();
            const auto authTime = payload["auth_time"].get<int64_t>();
            f.account["validSince"] = std::to_string(authTime + 1);
            f.rejected(token(payload, priv), 401); // iat is recent but original session is revoked.
            payload["auth_time"] = authTime + 1;
            check(f.request(token(payload, priv))->getStatusCode() == 200, "Boundary sign-in rejected");
            f.account["validSince"] = std::to_string(authTime + 2);
            f.rejected(token(payload, priv), 401);
        });
        test("missing optional account fields follow Firebase defaults", [&] {
            Fixture f(pub);
            f.account = {{"localId", "u"}};
            check(f.request(valid)->getStatusCode() == 200, "Optional defaults rejected");
        });
        test("Firebase outages and permission errors fail closed as temporary errors", [&] {
            Fixture f(pub);
            f.accountFailure = true;
            f.rejected(valid, 503);
            f.accountFailure = false;
            for (int status : {400, 401, 403, 429, 500, 503})
            {
                f.reply = {status, {{"error", {{"message", "provider detail"}}}}};
                f.rejected(valid, 503);
            }
        });
        test("malformed or mismatched account responses fail closed", [&] {
            for (auto account :
                 json::array({json{{"localId", "other"}}, json{{"localId", "u"}, {"disabled", "false"}},
                              json{{"localId", "u"}, {"validSince", "bad"}},
                              json{{"localId", "u"}, {"validSince", "1tail"}},
                              json{{"localId", "u"}, {"validSince", "999999999999999999999999"}},
                              json{{"localId", "u"}, {"validSince", -1}}, json::object()}))
            {
                Fixture f(pub);
                f.account = account;
                f.rejected(valid, 503);
            }
            Fixture f(pub);
            f.reply = {200, {{"users", "bad"}}};
            f.rejected(valid, 503);
        });
        test("invalid signatures and unknown signing keys never reach account lookup", [&] {
            Fixture f(pub);
            const auto wrong = keys();
            f.rejected(token(claims(), wrong.second), 401);
            f.rejected(token(claims(), priv, "unknown"), 401);
            check(f.lookups == 0, "Unverified token reached account lookup");
        });
        test("expired wrong-project and incomplete tokens are rejected", [&] {
            Fixture f(pub);
            for (const auto *field : {"exp", "iat", "auth_time", "sub"})
            {
                auto p = claims();
                p.erase(field);
                f.rejected(token(p, priv), 401);
            }
            for (auto pair : json::array({json::array({"exp", 1}), json::array({"auth_time", "123"}),
                                          json::array({"sub", ""}), json::array({"aud", "other"}),
                                          json::array({"iss", "https://example.test"})}))
            {
                auto p = claims();
                p[pair[0].get<std::string>()] = pair[1];
                f.rejected(token(p, priv), 401);
            }
            f.rejected("", 401);
            f.rejected("not-a-jwt", 401);
            check(f.lookups == 0, "Invalid token reached account lookup");
        });
        test("key service outages return 503 and allow recovery", [&] {
            Fixture f(pub);
            f.keyFailure = true;
            f.rejected(valid, 503);
            f.keyFailure = false;
            f.clock += std::chrono::seconds(31);
            check(f.request(valid)->getStatusCode() == 200, "Recovery failed");
            f.keyFailure = true;
            f.clock += std::chrono::seconds(31);
            f.rejected(token(claims(), priv, "rotated"), 503);
        });
        test("malformed key responses fail closed", [&] {
            Fixture f(pub);
            f.clock += std::chrono::seconds(31);
            f.keyReply = json::array();
            f.rejected(valid, 503);
            f.clock += std::chrono::seconds(31);
            f.keyReply = {{"test-key", 5}};
            f.rejected(valid, 503);
            f.clock += std::chrono::seconds(31);
            f.keyReply = {{"test-key", "not a PEM key"}};
            f.rejected(valid, 503);
            f.clock += std::chrono::seconds(31);
            f.keyReply = {{"test-key", pub}};
            check(f.request(valid)->getStatusCode() == 200, "Malformed keys poisoned the cache");
        });
        test("unknown key floods and key outages have bounded refresh frequency", [&] {
            Fixture f(pub);
            check(f.request(valid)->getStatusCode() == 200, "Initial authentication failed");
            for (int i = 0; i < 20; ++i)
                f.rejected(token(claims(), priv, "unknown-" + std::to_string(i)), 401);
            check(f.keyCalls == 1 && f.lookups == 1, "Unknown keys amplified provider requests");
            f.clock += std::chrono::seconds(31);
            f.keyFailure = true;
            f.rejected(token(claims(), priv, "unknown"), 503);
            for (int i = 0; i < 20; ++i)
                f.rejected(token(claims(), priv, "unknown"), 401);
            check(f.keyCalls == 2, "Failed refresh was not backed off");
        });
        test("verified account limits run before remote account and database work", [&] {
            Fixture f(pub);
            runtime::Limits limits;
            runtime::limits = &limits;
            struct Reset
            {
                ~Reset()
                {
                    runtime::limits = nullptr;
                }
            } reset;
            for (int i = 0; i < 3; ++i)
                check(f.request(valid, false, true)->getStatusCode() == 200, "Normal checkout denied");
            const auto lookups = f.lookups, reads = f.databaseCalls;
            const auto reply = f.request(valid, false, true);
            check(reply->getStatusCode() == 429 && !reply->getHeader("Retry-After").empty(),
                  "Account limit missing");
            check(f.lookups == lookups && f.databaseCalls == reads,
                  "Rejected request performed expensive work");
        });
        test("signing key rotation refreshes keys without bypassing signatures", [&] {
            Fixture f(pub);
            check(f.request(valid)->getStatusCode() == 200, "Initial token denied");
            f.clock += std::chrono::seconds(31);
            f.keyReply["rotated"] = pub;
            check(f.request(token(claims(), priv, "rotated"))->getStatusCode() == 200 && f.keyCalls == 2,
                  "Signing key rotation failed");
        });
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << passed << " authentication scenarios passed\n";
}
