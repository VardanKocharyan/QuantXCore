add_library(qxcore_portfolio STATIC
    Portfolio.cpp
)

target_include_directories(qxcore_portfolio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(qxcore_portfolio PUBLIC cxx_std_20)

set_target_properties(qxcore_portfolio PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

target_link_libraries(qxcore_portfolio
    PUBLIC
        QuantXCore_execution
        QuantXCore_risk
        QuantXCore_data
        QuantXCore_alpha
        nlohmann_json::nlohmann_json
)

if (MSVC)
    target_compile_options(qxcore_portfolio PRIVATE /W4)
else()
    target_compile_options(qxcore_portfolio PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(QuantXCore::portfolio ALIAS qxcore_portfolio)
