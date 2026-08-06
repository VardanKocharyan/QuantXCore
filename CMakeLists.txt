cmake_minimum_required(VERSION 3.20)

project(
    QuantXCore
    VERSION 0.1.0
    LANGUAGES CXX
)


# --------------------------------------------------
# Global Position Independent Code (-fPIC for dynamic libraries)
# --------------------------------------------------
set(CMAKE_POSITION_INDEPENDENT_CODE ON)


# --------------------------------------------------
# Find System Dependencies
# --------------------------------------------------
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(Boost 1.75 REQUIRED COMPONENTS system)
find_package(SQLite3 REQUIRED)


# --------------------------------------------------
# C++ Configuration
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(
        CMAKE_BUILD_TYPE
        Release
        CACHE STRING ""
        FORCE
    )
endif()

option(
    QUANTXCORE_BUILD_TESTS
    "Build tests"
    ON
)

option(
    QUANTXCORE_BUILD_BENCHMARKS
    "Build benchmarks"
    OFF
)

# --------------------------------------------------
# Compiler flags (only project)
# --------------------------------------------------

add_compile_options(
    -Wall
    -Wextra
    -Werror=return-type
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wshadow
    )
endif()

# --------------------------------------------------
# Include
# --------------------------------------------------

include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# --------------------------------------------------
# Config header
# --------------------------------------------------

configure_file(
    ${CMAKE_SOURCE_DIR}/config/project_config.hpp.in
    ${CMAKE_BINARY_DIR}/generated/project_config.hpp
)

# --------------------------------------------------
# Dependencies via FetchContent
# --------------------------------------------------

include(FetchContent)

# ============================
# nlohmann_json
# ============================

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

FetchContent_MakeAvailable(nlohmann_json)

# ============================
# hiredis
# ============================

set(ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(DISABLE_TESTS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    hiredis
    GIT_REPOSITORY https://github.com/redis/hiredis.git
    GIT_TAG v1.2.0
)

FetchContent_MakeAvailable(hiredis)

# ==================================================
# hiredis compatibility headers
# ==================================================

set(
    HIREDIS_COMPAT_INCLUDE
    ${CMAKE_SOURCE_DIR}/third_party/hiredis_compat/include
)

file(
    MAKE_DIRECTORY
    ${HIREDIS_COMPAT_INCLUDE}/hiredis
)

file(
    COPY
    ${hiredis_SOURCE_DIR}/
    DESTINATION
    ${HIREDIS_COMPAT_INCLUDE}/hiredis
    FILES_MATCHING
    PATTERN "*.h"
)

set(
    HIREDIS_HEADER
    ${HIREDIS_COMPAT_INCLUDE}
    CACHE PATH
    "hiredis include directory"
    FORCE
)

# ==================================================
# redis-plus-plus
# ==================================================

set(
    REDIS_PLUS_PLUS_BUILD_TEST
    OFF
    CACHE BOOL ""
    FORCE
)

set(
    REDIS_PLUS_PLUS_BUILD_STATIC
    ON
    CACHE BOOL ""
    FORCE
)

set(
    REDIS_PLUS_PLUS_BUILD_SHARED
    OFF
    CACHE BOOL ""
    FORCE
)

set(
    REDIS_PLUS_PLUS_USE_HIREDIS
    ON
    CACHE BOOL ""
    FORCE
)

FetchContent_Declare(
    redis_plus_plus
    GIT_REPOSITORY https://github.com/sewenew/redis-plus-plus.git
    GIT_TAG 1.3.14
)

FetchContent_MakeAvailable(redis_plus_plus)

# Third party compatibility
if(TARGET redis++_static)
    target_compile_options(
        redis++_static
        PRIVATE
        -Wno-error=pedantic
        -Wno-error=sign-conversion
    )
endif()

# --------------------------------------------------
# Project sources 
# --------------------------------------------------

add_subdirectory(src)

if(TARGET QuantXCore_execution)
    target_link_libraries(QuantXCore_execution PRIVATE SQLite::SQLite3)
endif()

# --------------------------------------------------
# Link Libraries to quantxcore
# --------------------------------------------------

if(TARGET quantxcore)
    target_link_libraries(quantxcore PRIVATE
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        Threads::Threads
    )
endif()

# --------------------------------------------------
# Tests
# --------------------------------------------------

if(QUANTXCORE_BUILD_TESTS)
    enable_testing()

    set(
        CTEST_USE_LAUNCHERS
        FALSE
    )

    add_subdirectory(tests)
endif()

if(QUANTXCORE_BUILD_TESTS)
    set_property(DIRECTORY ${hiredis_BINARY_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()

# --------------------------------------------------
# Install
# --------------------------------------------------

install(
    DIRECTORY
        include/QuantXCore/
    DESTINATION
        include/QuantXCore
)

# --------------------------------------------------
# Summary
# --------------------------------------------------

message(STATUS "============================================")
message(STATUS "Project      : ${PROJECT_NAME}")
message(STATUS "Version      : ${PROJECT_VERSION}")
message(STATUS "Build Type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard : ${CMAKE_CXX_STANDARD}")
message(STATUS "Tests        : ${QUANTXCORE_BUILD_TESTS}")
message(STATUS "============================================")
