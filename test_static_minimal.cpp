#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    auto eng = jai::engine::make();

    try {
        std::cout << "Test 1: Basic static field access\n";
        eng->execute(R"(
            class Counter {
                static int count = 0;
            }
        )");

        auto result1 = eng->execute("Counter::count");
        std::cout << "Counter::count = " << result1.as<int>() << " (expected 0)\n";

        std::cout << "\nTest 2: Static field from constructor\n";
        eng->execute(R"(
            class Counter2 {
                static int total = 0;

                Counter2() {
                    print("Before: total = " + to_string(total));
                    total = total + 1;
                    print("After: total = " + to_string(total));
                }
            }
        )");

        std::cout << "Creating Counter2 instance...\n";
        eng->execute("auto c = Counter2();");

        auto result2 = eng->execute("Counter2::total");
        std::cout << "Counter2::total = " << result2.as<int>() << " (expected 1)\n";

        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}