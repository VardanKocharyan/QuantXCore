add_library(QuantXCore_analytics SHARED
    PerformanceAnalytics.cpp
)

target_include_directories(QuantXCore_analytics PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QuantXCore_analytics PUBLIC
    QuantXCore_execution
)
