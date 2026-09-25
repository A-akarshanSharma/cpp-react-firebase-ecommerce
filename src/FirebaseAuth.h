#pragma once
#include <chrono>
#include <mutex>
#include <string>

// Handles getting a Google OAuth2 access token from a service account.
// This replaces what the Firebase Admin SDK does automatically in other languages.
class FirebaseAuth
{
  public:
    explicit FirebaseAuth(const std::string &serviceAccountPath);

    // Returns a valid access token, refreshing it if expired.
    // Runs on bounded request workers; waits share the current request deadline.
    std::string getAccessToken();

  private:
    std::timed_mutex mutex_;
    std::string signedJwt();   // builds + signs the JWT assertion
    void refreshAccessToken(); // exchanges JWT for an access token

    std::string clientEmail_;
    std::string privateKey_;
    std::string tokenUri_;

    std::string cachedToken_;
    std::chrono::steady_clock::time_point tokenExpiry_;
};
