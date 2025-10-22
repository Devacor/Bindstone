#include <iostream>
#include <vector>
#include <string>
#include "jaiscript/core/engine.hpp"

int main() {
    auto engine = jai::engine::make();

    std::vector<std::string> log_messages;
    int call_count = 0;

    engine->add_function("log_action", [&log_messages, &call_count, engine](const std::string& msg) {
        call_count++;
        log_messages.push_back(msg);
        std::cout << "LOG #" << call_count << ": " << msg << std::endl;
        return jai::script_value(std::monostate{}, engine->weak_from_this());
    });

    // Create initial class with v1 method
    std::cout << "=== Step 1: Creating initial class with v1 method ===" << std::endl;
    engine->execute(R"(
        auto global_obj = null;

        class TestClass {
            auto dummy_field = 0;

            void test_method() {
                log_action("v1 called");
            }
        }

        global_obj = TestClass();
    )");

    std::cout << "\n=== Step 2: Calling v1 method ===" << std::endl;
    engine->execute(R"(
        global_obj.test_method();
    )");

    std::cout << "\n=== Step 3: Redefining class with v2 method ===" << std::endl;
    log_messages.clear();
    call_count = 0;

    engine->execute(R"(
        class TestClass {
            auto dummy_field = 0;

            void test_method() {
                log_action("v2 called");
            }
        }
    )");

    std::cout << "\n=== Step 4: Calling method on SAME instance (should be v2) ===" << std::endl;
    engine->execute(R"(
        global_obj.test_method();
    )");

    std::cout << "\n=== Step 5: Creating NEW instance and calling (should be v2) ===" << std::endl;
    engine->execute(R"(
        auto new_obj = TestClass();
        new_obj.test_method();
    )");

    std::cout << "\n=== Results ===" << std::endl;
    std::cout << "Total method calls after redefinition: " << call_count << std::endl;
    std::cout << "Messages logged:" << std::endl;
    for (size_t i = 0; i < log_messages.size(); i++) {
        std::cout << "  [" << (i+1) << "] " << log_messages[i] << std::endl;
    }

    // Expected: 2 calls, both "v2 called"
    bool all_v2 = true;
    for (const auto& msg : log_messages) {
        if (msg != "v2 called") {
            all_v2 = false;
            break;
        }
    }

    std::cout << "\n";
    if (all_v2 && log_messages.size() == 2) {
        std::cout << "✓ SUCCESS: Both old and new instances use v2!" << std::endl;
        return 0;
    } else if (log_messages.size() == 2 && log_messages[0] != "v2 called" && log_messages[1] == "v2 called") {
        std::cout << "✗ PARTIAL: Old instance uses v1, new instance uses v2 (instance not updated)" << std::endl;
        return 1;
    } else {
        std::cout << "✗ FAILURE: Unexpected behavior!" << std::endl;
        return 1;
    }
}
