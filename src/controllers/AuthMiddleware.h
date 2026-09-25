#pragma once
#include "../FirestoreClient.h"
#include <chrono>
#include <drogon/HttpRequest.h>
#include <map>
#include <mutex>
#include <string>

struct AuthResult
{
    bool valid = false; // false for invalid tokens, disabled/deleted accounts or revoked sessions
    std::string uid;    // Firebase user id (the "sub" claim)
    std::string email;
    bool isAdmin = false; // looked up from Firestore users/{uid}.role
    std::string errorMessage;
};

// Verifies the "Authorization: Bearer <token>" header on incoming requests
// against Google's public keys, then checks current account status and revocation.
class AuthMiddleware
{
  public:
    using KeyProvider = std::function<nlohmann::json()>;
    AuthMiddleware(FirestoreClient &firestore, const std::string &projectId, FirebaseAuth &credentials);
    // Injectable remote dependencies for tests; JWT verification always runs.
    AuthMiddleware(
        FirestoreClient &firestore, const std::string &projectId, KeyProvider keys,
        FirestoreClient::Transport accounts,
        std::function<std::chrono::steady_clock::time_point()> clock = std::chrono::steady_clock::now);

    // Call at the top of any controller handler that needs to know who's calling.
    // If result.valid is false, respond 401 and stop - don't proceed.
    AuthResult verify(const drogon::HttpRequestPtr &req);

  private:
    std::function<std::chrono::steady_clock::time_point()> clock_;
    KeyProvider keys_;
    FirestoreClient::Transport accounts_;
    std::timed_mutex mutex_;
    void refreshGoogleKeys();

    FirestoreClient &firestore_;
    std::string projectId_;

    std::map<std::string, std::string> googleCerts_; // kid -> PEM certificate
    std::chrono::steady_clock::time_point certsExpiry_;
    std::chrono::steady_clock::time_point nextRefresh_{};
};
