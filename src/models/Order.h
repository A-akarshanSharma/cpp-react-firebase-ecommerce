#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// One line item as it existed AT THE TIME of the order - price is a snapshot,
// not a live lookup, so an order total never changes even if the product's price does later.
struct OrderItem
{
    std::string productId;
    std::string name;
    double price = 0.0;
    int quantity = 0;

    nlohmann::json toJson() const
    {
        return {{"productId", productId}, {"name", name}, {"price", price}, {"quantity", quantity}};
    }

    static OrderItem fromJson(const nlohmann::json &j)
    {
        OrderItem item;
        item.productId = j.value("productId", "");
        item.name = j.value("name", "");
        item.price = j.value("price", 0.0);
        item.quantity = j.value("quantity", 0);
        return item;
    }
};

struct Order
{
    std::string id;
    std::string userId;
    std::vector<OrderItem> items;
    double total = 0.0;
    std::string status; // "pending" | "paid" | "shipped" | "cancelled" - extend as needed in Phase 3 (payments)
    long long createdAt = 0; // epoch seconds

    nlohmann::json toFields() const
    {
        nlohmann::json itemsArray = nlohmann::json::array();
        for (const auto &item : items)
            itemsArray.push_back(item.toJson());

        return {
            {"userId", userId},
            {"items", itemsArray},
            {"total", total},
            {"status", status},
            {"createdAt", createdAt}};
    }

    static Order fromJson(const nlohmann::json &j)
    {
        Order o;
        o.id = j.value("id", "");
        o.userId = j.value("userId", "");
        o.total = j.value("total", 0.0);
        o.status = j.value("status", "");
        o.createdAt = j.value("createdAt", 0LL);
        if (j.contains("items") && j["items"].is_array())
            for (const auto &item : j["items"])
                o.items.push_back(OrderItem::fromJson(item));
        return o;
    }
};
