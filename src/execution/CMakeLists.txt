add_library(QuantXCore_execution STATIC
    PositionTracker.cpp
    BacktestEngine.cpp
    PnLTracker.cpp
)

target_include_directories(QuantXCore_execution PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QuantXCore_execution PRIVATE
    QuantXCore_core
    QuantXCore_data
    QuantXCore_risk
    CURL::libcurl
)

# Alias for internal use
add_library(QuantXCore::execution ALIAS QuantXCore_execution)
