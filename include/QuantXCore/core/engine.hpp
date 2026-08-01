#pragma once

#include <string>

namespace quantxcore::core {

class Engine {
public:
    Engine();
    ~Engine();

    void initialize();
    void shutdown();

    // Public getters
    std::string get_version() const;
    bool is_initialized() const;

private:
    bool is_initialized_;
};

} // namespace quantxcore::core
