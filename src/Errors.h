#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

class ApiError : public std::runtime_error
{
  public:
    int status;
    std::string code;
    nlohmann::json details;
    ApiError(int status, std::string code, std::string message,
             nlohmann::json details = nlohmann::json::object())
        : std::runtime_error(std::move(message)), status(status), code(std::move(code)),
          details(std::move(details))
    {
    }
};

class DatabaseError : public std::runtime_error
{
  public:
    int status;
    std::string code;
    DatabaseError(int status, std::string code)
        : std::runtime_error("Database request failed"), status(status), code(std::move(code))
    {
    }
    bool conflict() const
    {
        return code == "ABORTED";
    }
};
