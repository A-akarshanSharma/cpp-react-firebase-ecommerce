#pragma once
#include "../Validation.h"
#include <cstdint>
#include <string>
struct Product
{
    std::string id, name, description, imageUrl, category;
    int64_t priceMinor = 0;
    int stock = 0;
    nlohmann::json toFields() const
    {
        return {{"name", name},      {"priceMinor", priceMinor},   {"price", priceMinor / 100.0},
                {"currency", "INR"}, {"description", description}, {"imageUrl", imageUrl},
                {"stock", stock},    {"category", category}};
    }
    static Product fromJson(const nlohmann::json &j)
    {
        const auto p = validation::productView(j, j.value("id", ""));
        Product result;
        result.id = p["id"];
        result.name = p["name"];
        result.description = p["description"];
        result.imageUrl = p["imageUrl"];
        result.category = p["category"];
        result.priceMinor = p["priceMinor"];
        result.stock = p["stock"];
        return result;
    }
};
