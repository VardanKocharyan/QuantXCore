#include "QuantXCore/core/engine.hpp"

#include "project_config.hpp"

#include <iostream>
#include <stdexcept>

namespace quantxcore::core {

Engine::Engine() : is_initialized_(false) {
    // Constructor logic: nothing heavy here
}

Engine::~Engine() {
    // Ensure proper cleanup even if shutdown wasn't called explicitly
    if (is_initialized_) {
        shutdown();
    }
}

void Engine::initialize() {
    if (is_initialized_) {
        std::cerr << "Warning: Engine already initialized.\n";
        return;
    }

    std::cout << "QuantXCore Engine v" << QUANTXCORE_VERSION_STRING
              << " initializing...\n";

    // Perform initialization steps here
    // (e.g., allocate resources, set up logging, etc.)

    is_initialized_ = true;
    std::cout << "Engine initialization complete.\n";
}

void Engine::shutdown() {
    if (!is_initialized_) {
        std::cerr << "Warning: Engine not initialized, nothing to shutdown.\n";
        return;
    }

    std::cout << "QuantXCore Engine shutting down...\n";

    // Perform cleanup steps here
    // (e.g., release resources, flush logs, etc.)

    is_initialized_ = false;
    std::cout << "Engine shutdown complete.\n";
}

std::string Engine::get_version() const {
    return QUANTXCORE_VERSION_STRING;
}

bool Engine::is_initialized() const {
    return is_initialized_;
}

} // namespace quantxcore::core
