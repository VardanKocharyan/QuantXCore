add_executable(quantxcore_unit_tests
    engine_test.cpp
    data_parser_test.cpp
    oracle_test.cpp
    redis_streamer_test.cpp
    alpha_strategy_test.cpp
    alpha_engine_test.cpp
    rsi_strategy_test.cpp
    oracle_test.cpp
)

target_include_directories(quantxcore_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(quantxcore_unit_tests PRIVATE
    QuantXCore_core
    QuantXCore_data
    QuantXCore_alpha
    gtest
    gtest_main
)

target_compile_features(quantxcore_unit_tests PRIVATE cxx_std_20)

include(GoogleTest)
gtest_discover_tests(quantxcore_unit_tests)
