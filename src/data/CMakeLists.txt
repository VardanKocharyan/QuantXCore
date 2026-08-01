# Data library with both parser and streamer
add_library(QuantXCore_data STATIC
    data.cpp
    redis_streamer.cpp
)

target_include_directories(QuantXCore_data PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# PUBLIC link ensures that dependents (unit tests) get headers and dependencies automatically.
# hiredis_static MUST be linked after redis++_static for C symbols resolution.
target_link_libraries(QuantXCore_data PUBLIC
    nlohmann_json::nlohmann_json
    redis++_static
    hiredis
)

# Thread support for RedisStreamer background thread
find_package(Threads REQUIRED)
target_link_libraries(QuantXCore_data PUBLIC
    Threads::Threads
)

# Alias for internal use
add_library(QuantXCore::data ALIAS QuantXCore_data)
