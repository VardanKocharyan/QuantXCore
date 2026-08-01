#include "QuantXCore/core/engine.hpp"

#include <cstdlib>
#include <iostream>

int main() {
    quantxcore::core::Engine engine;
    engine.initialize();

    std::cout << "QuantXCore is ready!\n";

    engine.shutdown();
    return EXIT_SUCCESS;
}
