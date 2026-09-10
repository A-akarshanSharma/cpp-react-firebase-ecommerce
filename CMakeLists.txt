cmake_minimum_required(VERSION 3.10)
project(phase1_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/FirebaseAuth.cpp
    src/FirestoreClient.cpp
    src/controllers/TestController.cpp
    src/controllers/ProductController.cpp
    src/controllers/AuthMiddleware.cpp
    src/controllers/UserController.cpp
    src/controllers/CartController.cpp
    src/controllers/OrderController.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon OpenSSL::SSL OpenSSL::Crypto)
