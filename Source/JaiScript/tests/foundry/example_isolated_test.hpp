// Example showing how to use CONDITIONAL_ISOLATED_TEST
#pragma once

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class isolated_example : public suite {
public:
    isolated_example() : suite("Isolated Example") {}
    
    void forge_tests() override {
        test("demo_test", [this]() {
            engine engine;
            engine.add_function("greet", [](const script_string& name) {
                return "Hello, " + name + "!";
            });
            
            auto result = engine.execute("greet(\"World\")");
            check_eq(result.as<script_string>(), "Hello, World!");
        });
    }
};

// This macro does nothing in normal builds
// But when compiled with -DJAI_ISOLATED_TEST, it adds:
// int main() { isolated_example t; return t.forge(); }
CONDITIONAL_ISOLATED_TEST(isolated_example)

} // namespace