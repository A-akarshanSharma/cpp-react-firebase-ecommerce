#pragma once
#include <drogon/HttpRequest.h>
#include <string>
#include <map>
#include <chrono>
#include "../FirestoreClient.h"

struct AuthResult
{
    bool valid = false;      // false if token missing/invalid/expired
    std::string uid;         // Firebase user id (the "sub" claim)
    std::string email;
    bool isAdmin = false;    // looked up from Firestore users/{uid}.role
    std::string errorMessage;
};

// Verifies the "Authorization: Bearer <token>" header on incoming requests
// against Google's public keys - this replaces what the Admin SDK's
// verifyIdToken() does automatically in other languages.
class AuthMiddleware
{
public:
    AuthMiddleware(FirestoreClient &firestore, const std::string &projectId);

    // Call at the top of any controller handler that needs to know who's calling.
    // If result.valid is false, respond 401 and stop - don't proceed.
    AuthResult verify(const drogon::HttpRequestPtr &req);

private:
    void refreshGoogleKeys();

    FirestoreClient &firestore_;
    std::string projectId_;

    std::map<std::string, std::string> googleCerts_; // kid -> PEM certificate
    std::chrono::steady_clock::time_point certsExpiry_;
};
