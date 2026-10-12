add_library(QuantXCore_alpha STATIC
    MovingAverageCrossover.cpp
    AlphaEngine.cpp
    RsiStrategy.cpp
    CompositeAlphaStrategy.cpp
)

target_include_directories(QuantXCore_alpha PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(QuantXCore_alpha PUBLIC
    QuantXCore_data
)

add_library(QuantXCore::alpha ALIAS QuantXCore_alpha)
