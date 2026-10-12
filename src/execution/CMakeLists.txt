add_library(QuantXCore_execution
    BacktestEngine.cpp
    PositionTracker.cpp
    PnLTracker.cpp
    BinanceOmsClient.cpp
    LiveEngine.cpp
)

target_include_directories(QuantXCore_execution PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QuantXCore_execution PUBLIC
    QuantXCore_data
    QuantXCore_alpha
    QuantXCore_risk
    boost_system
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

# Alias for internal use
add_library(QuantXCore::execution ALIAS QuantXCore_execution)
