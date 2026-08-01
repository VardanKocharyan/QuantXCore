# Core library (static) — Ուղղված տարբերակ
add_library(QuantXCore_core STATIC
    engine.cpp
    alpha_engine.cpp
)

# Make generated config available to the library
target_include_directories(QuantXCore_core PRIVATE
    ${CMAKE_BINARY_DIR}/generated
)

# Public include directories (for consumers)
target_include_directories(QuantXCore_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Alias for internal use
add_library(QuantXCore::core ALIAS QuantXCore_core)

# Install library
install(TARGETS QuantXCore_core
    EXPORT QuantXCoreTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Install public headers
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/QuantXCore/
    DESTINATION include/QuantXCore
    FILES_MATCHING PATTERN "*.hpp"
)

