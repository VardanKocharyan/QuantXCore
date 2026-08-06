# Build subdirectories
add_subdirectory(core)
add_subdirectory(data)
add_subdirectory(alpha)
add_subdirectory(risk)
add_subdirectory(execution)
add_subdirectory(portfolio)
add_subdirectory(analytics)

# Main executable
add_executable(quantxcore 
    main.cpp
)

target_link_libraries(quantxcore PRIVATE
    QuantXCore::core
    QuantXCore::data
    QuantXCore_alpha
    QuantXCore_risk
    QuantXCore_execution
    QuantXCore::portfolio
    SQLite::SQLite3  # <-- Ուղղված է (SQLite::SQLite3)
)

install(TARGETS quantxcore DESTINATION bin)
