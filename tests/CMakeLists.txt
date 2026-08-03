add_subdirectory(unit)

add_executable(risk_engine_test
    unit/risk_engine_test.cpp  
)

target_link_libraries(risk_engine_test
    PRIVATE
        QuantXCore_risk
        QuantXCore_alpha
        gtest 
        gtest_main
)

include(GoogleTest)
gtest_discover_tests(risk_engine_test)
