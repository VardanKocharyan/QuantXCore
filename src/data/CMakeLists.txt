add_library(QuantXCore_data
    BinanceStreamer.cpp
    CsvTickParser.cpp
    BinanceRestClient.cpp
    data.cpp
    OrderBook.cpp
    OrderBookSync.cpp
    redis_streamer.cpp
)

# Alias target
add_library(QuantXCore::data ALIAS QuantXCore_data)

target_include_directories(QuantXCore_data PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QuantXCore_data PUBLIC
    nlohmann_json::nlohmann_json
    Boost::headers
    boost_system
    OpenSSL::SSL
    OpenSSL::Crypto
    redis++_static
    hiredis
)
