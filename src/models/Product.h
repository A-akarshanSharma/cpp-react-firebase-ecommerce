#pragma once
#include <nlohmann/json.hpp>
#include <string>

// Plain data shape for a product. No logic here on purpose -
// ProductController owns the behavior, this just describes the fields.
struct Product
{
    std::string id;          // Firestore doc id - empty when creating a new one
    std::string name;
    double price = 0.0;
    std::string description;
    std::string imageUrl;
    int stock = 0;
    std::string category;

    // Converts to the plain JSON shape stored in / sent to Firestore
    // (doesn't include "id" - Firestore tracks that separately as the doc name).
    nlohmann::json toFields() const
    {
        return {
            {"name", name},
            {"price", price},
            {"description", description},
            {"imageUrl", imageUrl},
            {"stock", stock},
            {"category", category}};
    }

    // Builds a Product from Firestore's plain-fields JSON (already includes "id" from listDocuments/getDocument).
    static Product fromJson(const nlohmann::json &j)
    {
        Product p;
        p.id = j.value("id", "");
        p.name = j.value("name", "");
        p.price = j.value("price", 0.0);
        p.description = j.value("description", "");
        p.imageUrl = j.value("imageUrl", "");
        p.stock = j.value("stock", 0);
        p.category = j.value("category", "");
        return p;
    }
};
