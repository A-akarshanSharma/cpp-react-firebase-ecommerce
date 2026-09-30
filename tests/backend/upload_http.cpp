// Real Drogon request parsing + production PNG sanitizer; no Firebase or credentials.
#include "services/Uploads.h"
#include <drogon/drogon.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <png.h>
#include <random>
#include <thread>

static void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}

static std::string png(unsigned width, unsigned height, bool noisy = true)
{
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = width;
    image.height = height;
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    std::mt19937 random(42);
    for (auto &byte : pixels)
        byte = noisy ? static_cast<unsigned char>(random()) : 0;
    png_alloc_size_t size = 0;
    check(png_image_write_to_memory(&image, nullptr, &size, 0, pixels.data(), 0, nullptr), "PNG sizing failed");
    std::string bytes(size, '\0');
    check(png_image_write_to_memory(&image, bytes.data(), &size, 0, pixels.data(), 0, nullptr), "PNG encoding failed");
    bytes.resize(size);
    png_image_free(&image);
    return bytes;
}

int main(int argc, char **argv)
{
    // --legacy reproduces the previous configuration; it must fail on the large PNG.
    const bool legacy = argc > 1 && std::string(argv[1]) == "--legacy";
    const auto directory = std::filesystem::temp_directory_path() / ("upload-http-" + drogon::utils::getUuid());
    std::filesystem::create_directory(directory);
    // A file instead of a directory prevents spooling even when CI runs as root.
    const auto blocked = directory / "blocked";
    std::ofstream(blocked).put('x');
    drogon::app().setUploadPath(blocked.string());
    if (legacy)
        drogon::app().setClientMaxBodySize(commerce::MaxUploadBytes);
    else
        commerce::configureUploadBodyLimits();
    drogon::app().registerHandler("/admin/uploads", [](const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
        const auto bytes = req->getBody();
        auto response = drogon::HttpResponse::newHttpResponse();
        response->addHeader("X-Test-Body-Length", std::to_string(bytes.size()));
        response->addHeader("X-Test-Content-Type", req->getHeader("content-type"));
        try
        {
            response->setBody(commerce::sanitizedPng(std::string(bytes)));
            response->setContentTypeCode(drogon::CT_IMAGE_PNG);
        }
        catch (const ApiError &error)
        {
            response->setStatusCode(static_cast<drogon::HttpStatusCode>(error.status));
            response->setBody(error.what());
        }
        callback(response);
    }, {drogon::Post});
    std::promise<void> ready;
    drogon::app().registerBeginningAdvice([&] { ready.set_value(); });
    drogon::app().addListener("127.0.0.1", 0).setThreadNum(1);
    std::thread server([] { drogon::app().run(); });
    ready.get_future().wait();
    int result = 0;
    try
    {
        const auto port = drogon::app().getListeners().at(0).toPort();
        auto client = drogon::HttpClient::newHttpClient("http://127.0.0.1:" + std::to_string(port));
        auto send = [&](const std::string &body, int expected) {
            auto req = drogon::HttpRequest::newHttpRequest();
            req->setMethod(drogon::Post);
            req->setPath("/admin/uploads");
            req->setContentTypeCode(drogon::CT_IMAGE_PNG);
            req->setBody(body);
            const auto [status, response] = client->sendRequest(req, 10);
            check(status == drogon::ReqResult::Ok && response, "HTTP upload failed");
            std::cout << "sent=" << body.size() << " received=" << response->getHeader("x-test-body-length")
                      << " type=" << response->getHeader("x-test-content-type") << " status=" << response->statusCode() << '\n';
            check(response->statusCode() == expected, "Unexpected upload status");
            if (expected == 200)
            {
                check(response->getHeader("x-test-body-length") == std::to_string(body.size()), "Body truncated");
                check(response->getHeader("x-test-content-type") == "image/png", "Wrong content type");
                check(response->getBody() == commerce::sanitizedPng(body), "Sanitized HTTP bytes differ");
            }
        };
        send(png(16, 16), 200);
        const auto large = png(256, 128);
        check(large.size() > 128 * 1024, "Fixture must exceed old 64 KiB spill threshold");
        send(large, 200);
        auto boundary = large;
        boundary.resize(commerce::MaxUploadBytes, '\0');
        send(boundary, 200); // Valid PNG with trailing data; sanitizer removes it.
        send(std::string(commerce::MaxUploadBytes + 1, 'x'), 413);
        send(large.substr(0, 40), 400);
        send("not a PNG", 400);
        send(png(4097, 1), 400);
        send(png(2049, 2048, false), 400); // Compressed input within byte limit, over pixel limit.
        std::cout << "PASS: binary HTTP uploads and limits\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    drogon::app().quit();
    server.join();
    std::filesystem::remove_all(directory);
    return result;
}
