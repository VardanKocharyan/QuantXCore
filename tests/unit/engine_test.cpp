#include "QuantXCore/core/engine.hpp"

#include <gtest/gtest.h>

TEST(EngineTest, VersionIsSet) {
    quantxcore::core::Engine engine;
    EXPECT_FALSE(engine.get_version().empty());
}

TEST(EngineTest, CanInitializeAndShutdown) {
    quantxcore::core::Engine engine;
    engine.initialize();
    engine.shutdown();
}
