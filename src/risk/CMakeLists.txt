add_library(QuantXCore_risk STATIC
    RiskEngine.cpp
)

target_include_directories(QuantXCore_risk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(QuantXCore_risk PUBLIC cxx_std_20)

target_link_libraries(QuantXCore_risk
    PUBLIC
        QuantXCore_alpha
)

set_target_properties(QuantXCore_risk PROPERTIES
    OUTPUT_NAME "QuantXCore_risk"
    POSITION_INDEPENDENT_CODE ON
)
