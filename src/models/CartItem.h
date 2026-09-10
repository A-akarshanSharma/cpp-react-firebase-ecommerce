#pragma once
#include <nlohmann/json.hpp>
#include <string>

// One line item inside a user's cart document (carts/{uid}, field "items": array of these).
struct CartItem
{
    std::string productId;
    int quantity = 0;

    nlohmann::json toJson() const
    {
        return {{"productId", productId}, {"quantity", quantity}};
    }

    static CartItem fromJson(const nlohmann::json &j)
    {
        CartItem item;
        item.productId = j.value("productId", "");
        item.quantity = j.value("quantity", 0);
        return item;
    }
};
