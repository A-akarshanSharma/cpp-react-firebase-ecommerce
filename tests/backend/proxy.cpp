#include "runtime/Proxy.h"
#include <iostream>
void check(bool ok) { if (!ok) throw std::runtime_error("Proxy identity check failed"); }
int main()
{
    try
    {
        check(runtime::clientIp("192.0.2.1", "", "198.51.100.1") == "192.0.2.1");
        check(runtime::clientIp("192.0.2.1", "192.0.2.2", "198.51.100.1") == "192.0.2.1");
        check(runtime::clientIp("192.0.2.1", "192.0.2.1", "198.51.100.1") == "198.51.100.1");
        check(runtime::clientIp("::1", "::1", "2001:0db8::1") == "2001:db8::1");
        for (const auto value : {"", "198.51.100.1, 192.0.2.1", "host.example", "1.2.3.4:80"})
        {
            bool rejected = false;
            try { runtime::clientIp("::1", "::1", value); }
            catch (const ApiError &e) { rejected = e.status == 400; }
            check(rejected);
        }
        std::cout << "PASS proxy: direct peers, untrusted headers, trusted IPv4/IPv6 and malformed headers\n";
    }
    catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
