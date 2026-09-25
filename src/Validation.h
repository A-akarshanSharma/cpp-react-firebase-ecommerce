#pragma once
#include "Errors.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace validation
{
using json = nlohmann::json;
constexpr int MaxQuantity = 1000000;
constexpr size_t MaxCartLines = 100;
constexpr size_t MaxBodyBytes = 32768;
constexpr int64_t MaxMoney = 9000000000000000LL;
constexpr int64_t MaxPrice = 1000000000000LL;
inline void require(bool valid, const std::string &message)
{
    if (!valid)
        throw ApiError(400, "INVALID_INPUT", message);
}
inline void object(const json &value)
{
    require(value.is_object(), "Body must be a JSON object");
}
inline std::string text(const json &value, const std::string &key, size_t max, bool required = true)
{
    if (!value.contains(key))
    {
        require(!required, key + " is required");
        return "";
    }
    require(value[key].is_string(), key + " must be a string");
    auto result = value[key].get<std::string>();
    require(result.size() <= max, key + " is too long");
    if (required)
        require(result.find_first_not_of(" \t\r\n") != std::string::npos, key + " must not be empty");
    return result;
}
inline std::string id(const std::string &value)
{
    require(!value.empty() && value.size() <= 128 && value != "." && value != "..", "Invalid document ID");
    require(std::all_of(value.begin(), value.end(),
                        [](unsigned char c) {
                            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
                        }),
            "Invalid document ID");
    return value;
}
inline int64_t integer(const json &value, const std::string &key, int64_t min, int64_t max)
{
    require(value.contains(key) && value[key].is_number_integer(), key + " must be an integer");
    if (value[key].is_number_unsigned())
        require(value[key].get<uint64_t>() <= static_cast<uint64_t>(max), key + " is too large");
    auto n = value[key].get<int64_t>();
    require(n >= min && n <= max, key + " is outside its allowed range");
    return n;
}
inline int64_t minorUnits(const json &value, const std::string &key, int64_t max = MaxMoney)
{
    require(value.contains(key) && value[key].is_number(), key + " must be a number");
    const double major = value[key].get<double>();
    require(std::isfinite(major) && major >= 0 && major <= double(max) / 100.0,
            key + " is outside its allowed range");
    const long double scaled = static_cast<long double>(major) * 100;
    const auto rounded = std::round(scaled);
    // Tolerate binary floating point noise in legacy JSON, not fractional paise.
    require(std::fabs(scaled - rounded) <= 0.0001L, key + " must have at most two decimal places");
    return static_cast<int64_t>(rounded);
}
inline int64_t storedMoney(const json &value, const std::string &majorKey, const std::string &minorKey,
                           int64_t max = MaxMoney)
{
    if (value.contains("currency"))
        require(value["currency"] == "INR", "Unsupported stored currency");
    return value.contains(minorKey) ? integer(value, minorKey, 0, max) : minorUnits(value, majorKey, max);
}
inline int64_t lineTotal(int64_t price, int quantity)
{
    require(price >= 0 && price <= MaxPrice && quantity > 0 && quantity <= MaxQuantity, "Invalid order item");
    require(price == 0 || quantity <= MaxMoney / price, "Order amount is too large");
    return price * quantity;
}
inline std::string productVersion(const json &doc)
{
    return doc.contains("version") ? text(doc, "version", 128) : "initial";
}
inline json productInput(const json &input, bool includeStock = true)
{
    object(input);
    auto image = text(input, "imageUrl", 2048, false);
    require(image.empty() || image.rfind("https://", 0) == 0 || image.rfind("http://", 0) == 0 ||
                (image.size() == 75 && image.rfind("/media/", 0) == 0 && image.substr(71) == ".png" &&
                 std::all_of(image.begin() + 7, image.begin() + 71,
                             [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })),
            "imageUrl must be an HTTP(S) URL");
    auto price = minorUnits(input, "price", MaxPrice);
    json result = {{"name", text(input, "name", 160)},
                   {"description", text(input, "description", 5000, false)},
                   {"category", text(input, "category", 80)},
                   {"imageUrl", image},
                   {"priceMinor", price},
                   {"price", price / 100.0},
                   {"currency", "INR"}};
    if (includeStock)
        result["stock"] = integer(input, "stock", 0, 2147483647);
    else
        require(!input.contains("stock"), "Use the stock adjustment action to change inventory");
    return result;
}
inline json productView(const json &doc, const std::string &idValue)
{
    try
    {
        object(doc);
        auto price = storedMoney(doc, "price", "priceMinor", MaxPrice);
        json result = {{"id", idValue},
                       {"name", text(doc, "name", 160)},
                       {"description", text(doc, "description", 5000, false)},
                       {"category", text(doc, "category", 80, false)},
                       {"imageUrl", text(doc, "imageUrl", 2048, false)},
                       {"price", price / 100.0},
                       {"priceMinor", price},
                       {"currency", "INR"},
                       {"stock", integer(doc, "stock", 0, 2147483647)},
                       {"version", productVersion(doc)}};
        for (const auto *field : {"parentProductId", "variantLabel", "sku", "hasVariants"})
            if (doc.contains(field))
                result[field] = doc[field];
        return result;
    }
    catch (const ApiError &)
    {
        throw ApiError(500, "INVALID_STORED_PRODUCT",
                       "Product data needs attention from the store administrator");
    }
}
inline json orderView(json doc, const std::string &idValue)
{
    try
    {
        object(doc);
        text(doc, "userId", 128);
        text(doc, "status", 80);
        integer(doc, "createdAt", 0, MaxMoney);
        require(doc.contains("items") && doc["items"].is_array(), "Invalid order items");
        for (auto &item : doc["items"])
        {
            text(item, "productId", 128);
            text(item, "name", 160);
            integer(item, "quantity", 1, MaxQuantity);
            const auto price = storedMoney(item, "price", "priceMinor", MaxPrice);
            item["price"] = price / 100.0;
            item["priceMinor"] = price;
        }
        const auto total = storedMoney(doc, "total", "totalMinor");
        doc["id"] = idValue;
        doc["total"] = total / 100.0;
        doc["totalMinor"] = total;
        doc["currency"] = "INR";
        return doc;
    }
    catch (const ApiError &)
    {
        throw ApiError(500, "INVALID_STORED_ORDER",
                       "Order data needs attention from the store administrator");
    }
}
} // namespace validation
