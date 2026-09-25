#pragma once
#include "../Errors.h"
#include <arpa/inet.h>
#include <string>
namespace runtime
{
inline std::string canonicalIp(const std::string &value)
{
    unsigned char address[sizeof(in6_addr)];
    char output[INET6_ADDRSTRLEN];
    for (const int family : {AF_INET, AF_INET6})
        if (inet_pton(family, value.c_str(), address) == 1 &&
            inet_ntop(family, address, output, sizeof(output)))
            return output;
    return "";
}
inline std::string clientIp(const std::string &peer, const std::string &trusted,
                            const std::string &realIp)
{
    if (trusted.empty() || canonicalIp(peer) != trusted)
        return peer;
    const auto client = canonicalIp(realIp);
    if (client.empty())
        throw ApiError(400, "INVALID_PROXY_HEADER", "Invalid proxy client address");
    return client;
}
} // namespace runtime
