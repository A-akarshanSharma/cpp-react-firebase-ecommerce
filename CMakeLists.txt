cmake_minimum_required(VERSION 3.10)
project(phase1_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PNG REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/FirebaseAuth.cpp
    src/FirestoreClient.cpp
    src/services/Commerce.cpp src/services/Ordering.cpp src/services/Operations.cpp src/services/Uploads.cpp
    src/controllers/ProductController.cpp
    src/controllers/AuthMiddleware.cpp
    src/controllers/UserController.cpp
    src/controllers/CartController.cpp
    src/controllers/OperationsController.cpp
    src/controllers/OrderController.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto PNG::PNG)

include(CTest)
if(BUILD_TESTING)
    add_executable(backend_proxy_tests tests/backend/proxy.cpp)
    target_include_directories(backend_proxy_tests PRIVATE src)
    add_test(NAME backend_proxy COMMAND backend_proxy_tests)
    add_executable(backend_catalog_cache_tests tests/backend/catalog_cache.cpp src/FirestoreClient.cpp src/FirebaseAuth.cpp)
    target_include_directories(backend_catalog_cache_tests PRIVATE src)
    target_link_libraries(backend_catalog_cache_tests PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME backend_catalog_cache COMMAND backend_catalog_cache_tests)
    add_executable(backend_pagination_tests tests/backend/pagination.cpp src/FirestoreClient.cpp src/FirebaseAuth.cpp)
    target_include_directories(backend_pagination_tests PRIVATE src)
    target_link_libraries(backend_pagination_tests PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME backend_pagination COMMAND backend_pagination_tests)
    add_executable(backend_runtime_tests tests/backend/runtime.cpp)
    target_include_directories(backend_runtime_tests PRIVATE src)
    target_link_libraries(backend_runtime_tests PRIVATE Drogon::Drogon)
    add_test(NAME backend_runtime COMMAND backend_runtime_tests)
    add_executable(backend_runtime_server tests/backend/runtime_server.cpp)
    target_include_directories(backend_runtime_server PRIVATE src)
    target_link_libraries(backend_runtime_server PRIVATE Drogon::Drogon)
    add_executable(backend_authentication_tests tests/backend/authentication.cpp src/controllers/AuthMiddleware.cpp src/services/Commerce.cpp src/services/Ordering.cpp src/services/Operations.cpp src/FirestoreClient.cpp src/FirebaseAuth.cpp)
    target_include_directories(backend_authentication_tests PRIVATE src)
    target_link_libraries(backend_authentication_tests PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)
    add_test(NAME backend_authentication COMMAND backend_authentication_tests)
    add_executable(backend_correctness_tests tests/backend/correctness.cpp src/services/Commerce.cpp src/services/Ordering.cpp src/services/Operations.cpp src/services/Uploads.cpp src/FirestoreClient.cpp src/FirebaseAuth.cpp)
    target_include_directories(backend_correctness_tests PRIVATE src)
    target_link_libraries(backend_correctness_tests PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto PNG::PNG)
    add_test(NAME backend_correctness COMMAND backend_correctness_tests)
    add_executable(backend_emulator_tests tests/backend/emulator.cpp src/services/Commerce.cpp src/services/Ordering.cpp src/services/Operations.cpp src/services/Uploads.cpp src/FirestoreClient.cpp src/FirebaseAuth.cpp)
    target_include_directories(backend_emulator_tests PRIVATE src)
    target_link_libraries(backend_emulator_tests PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto PNG::PNG)
    # Explicitly run this executable against a disposable emulator on port 8189.
    # It is not a default ctest prerequisite and cannot target a production host.
endif()
