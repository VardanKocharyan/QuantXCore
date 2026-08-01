# Build subdirectories
add_subdirectory(core)
add_subdirectory(data)

# Main executable
add_executable(quantxcore main.cpp)
target_link_libraries(quantxcore PRIVATE
    QuantXCore::core
    QuantXCore::data
)

# Install executable
install(TARGETS quantxcore DESTINATION bin)

add_subdirectory(alpha)

