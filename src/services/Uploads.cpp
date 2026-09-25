#include "Uploads.h"
#include <drogon/utils/Utilities.h>
#include <filesystem>
#include <fstream>
#include <png.h>
namespace commerce
{
std::string sanitizedPng(const std::string &bytes)
{
    if (bytes.size() > MaxUploadBytes)
        throw ApiError(413, "UPLOAD_TOO_LARGE", "Image must be at most 4 MiB");
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
    {
        png_image_free(&image);
        throw ApiError(400, "INVALID_IMAGE", "Upload a valid PNG image");
    }
    if (!image.width || !image.height || image.width > 4096 || image.height > 4096 ||
        uint64_t(image.width) * image.height > 4194304)
    {
        png_image_free(&image);
        throw ApiError(400, "INVALID_IMAGE",
                       "Image must contain at most 4 megapixels and be at most 4096 pixels per side");
    }
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
    {
        png_image_free(&image);
        throw ApiError(400, "INVALID_IMAGE", "Image data is incomplete or invalid");
    }
    png_alloc_size_t size = 0;
    if (!png_image_write_to_memory(&image, nullptr, &size, 0, pixels.data(), 0, nullptr) ||
        size > 20 * 1024 * 1024)
    {
        png_image_free(&image);
        throw ApiError(400, "INVALID_IMAGE", "Image could not be prepared");
    }
    std::string output(size, '\0');
    if (!png_image_write_to_memory(&image, output.data(), &size, 0, pixels.data(), 0, nullptr))
    {
        png_image_free(&image);
        throw ApiError(400, "INVALID_IMAGE", "Image could not be prepared");
    }
    png_image_free(&image);
    output.resize(size);
    return output;
}
std::string mediaFile(const std::string &directory, const std::string &name)
{
    if (name.size() != 68 || name.substr(64) != ".png" ||
        !std::all_of(name.begin(), name.begin() + 64,
                     [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }))
        throw ApiError(404, "IMAGE_NOT_FOUND", "Image not found");
    return (std::filesystem::path(directory) / name).string();
}
json uploadImage(FirestoreClient &db, const std::string &actor, const std::string &bytes,
                 const std::string &directory)
{
    const auto normalized = sanitizedPng(bytes);
    const auto hash = fingerprint(normalized), name = hash + ".png";
    const auto path = mediaFile(directory, name);
    std::filesystem::create_directories(directory);
    if (!std::filesystem::exists(path))
    {
        const auto temporary = path + "." + drogon::utils::getUuid() + ".tmp";
        try
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out.write(normalized.data(), normalized.size());
            out.close();
            if (!out)
                throw std::runtime_error("Image storage failed");
            std::filesystem::rename(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary);
            throw;
        }
    }
    const json result = {{"url", "/media/" + name}, {"id", hash}};
    return db.transact([&](const std::string &tx, std::vector<FirestoreClient::Write> &writes) {
        if (db.getDocument("uploads", hash, tx).is_null())
        {
            writes.push_back({"uploads",
                              hash,
                              {{"url", result["url"]},
                               {"createdAt", nowSeconds()},
                               {"actorId", actor},
                               {"bytes", normalized.size()}},
                              {},
                              true});
            audit(writes, actor, "image_uploaded", hash);
        }
        return result;
    });
}
} // namespace commerce
