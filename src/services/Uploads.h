#pragma once
#include "Operations.h"
namespace commerce
{
constexpr size_t MaxUploadBytes = 4 * 1024 * 1024;
std::string sanitizedPng(const std::string &bytes);
std::string mediaFile(const std::string &directory, const std::string &name);
json uploadImage(FirestoreClient &db, const std::string &actor, const std::string &bytes,
                 const std::string &directory);
} // namespace commerce
