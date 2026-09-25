#pragma once
#include "../Validation.h"
#include <cstdint>
#include <string>
#include <vector>
struct OrderItem
{
    std::string productId, name;
    int64_t priceMinor = 0;
    int quantity = 0;
    nlohmann::json metadata = nlohmann::json::object();
    nlohmann::json toJson() const
    {
        auto result = metadata;
        result.update(nlohmann::json{{"productId", productId},
                                     {"name", name},
                                     {"priceMinor", priceMinor},
                                     {"price", priceMinor / 100.0},
                                     {"quantity", quantity}});
        return result;
    }
    static OrderItem fromJson(const nlohmann::json &j)
    {
        OrderItem result{validation::text(j, "productId", 128), validation::text(j, "name", 160),
                         validation::storedMoney(j, "price", "priceMinor", validation::MaxPrice),
                         static_cast<int>(validation::integer(j, "quantity", 1, validation::MaxQuantity))};
        result.metadata = j;
        for (const auto *field : {"productId", "name", "priceMinor", "price", "quantity"})
            result.metadata.erase(field);
        return result;
    }
};
struct Order
{
    std::string id, userId;
    std::vector<OrderItem> items;
    int64_t totalMinor = 0;
    nlohmann::json metadata = nlohmann::json::object();
    std::string status; // pending | paid | shipped | delivered | cancelled; lifecycle enforced by Ordering;
                        // payment confirmed separately.
    int64_t createdAt = 0; // Epoch seconds.
    nlohmann::json toFields() const
    {
        auto lines = nlohmann::json::array();
        for (const auto &item : items)
            lines.push_back(item.toJson());
        auto result = metadata;
        result.update(nlohmann::json{{"userId", userId},
                                     {"items", lines},
                                     {"totalMinor", totalMinor},
                                     {"total", totalMinor / 100.0},
                                     {"currency", "INR"},
                                     {"status", status},
                                     {"createdAt", createdAt}});
        return result;
    }
    static Order fromJson(const nlohmann::json &j)
    {
        const auto data = validation::orderView(j, j.value("id", ""));
        Order result;
        result.metadata = data;
        for (const auto *field :
             {"id", "userId", "items", "totalMinor", "total", "currency", "status", "createdAt"})
            result.metadata.erase(field);
        result.id = data["id"];
        result.userId = data["userId"];
        result.totalMinor = data["totalMinor"];
        result.status = data["status"];
        result.createdAt = data["createdAt"];
        for (const auto &item : data["items"])
            result.items.push_back(OrderItem::fromJson(item));
        return result;
    }
};
