add_executable(quantxcore_unit_tests
    engine_test.cpp
    data_parser_test.cpp
    oracle_test.cpp
    #redis_streamer_test.cpp
    alpha_strategy_test.cpp
    alpha_engine_test.cpp
    rsi_strategy_test.cpp
    binance_streamer_test.cpp
    orderbook_test.cpp
    binance_rest_test.cpp
    binance_oms_test.cpp
)
target_include_directories(quantxcore_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(quantxcore_unit_tests PRIVATE
    QuantXCore_core
    QuantXCore_data
    QuantXCore_alpha
    QuantXCore_execution
    gtest
    gtest_main
)

include(GoogleTest)
gtest_discover_tests(quantxcore_unit_tests)

# Backtest Engine unit test target
add_executable(backtest_engine_test backtest_engine_test.cpp)
target_include_directories(backtest_engine_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(backtest_engine_test PRIVATE
    QuantXCore_execution
    QuantXCore_data
    QuantXCore_alpha
    QuantXCore_risk
    gtest
    gtest_main
)

target_sources(quantxcore_unit_tests PRIVATE
    orderbook_sync_test.cpp
)

gtest_discover_tests(backtest_engine_test)
