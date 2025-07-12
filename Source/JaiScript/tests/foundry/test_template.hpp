// Template for new foundry test files - NO MAIN FUNCTION!
#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <memory>

namespace jai::foundry::tests {

// Your test suite class
class my_feature_tests : public suite {
public:
    my_feature_tests() : suite("My Feature Tests") {}
    
    void forge_tests() override {
        test("example_test", [this]() {
            auto engine = engine::make();
            auto result = engine->execute("2 + 2");
            check_eq(result.as<int>(), 4);
        });
    }
    
    // Optional setup/teardown
    void pre_test() override {}
    void post_test() override {}
    
private:
    // Test data members
};

// Factory function for the test runner
std::unique_ptr<suite> create_my_feature_tests() {
    return std::make_unique<my_feature_tests>();
}

} // namespace jai::foundry::tests