#include <iostream>
#include "jaiscript/core/engine.hpp"

int main() {
    auto engine = jai::engine::make();

    std::cout << "=== Test 1: Unqualified static field access in static method ===" << std::endl;
    try {
        engine->execute(R"(
            class Counter {
                static int count = 0;

                static function increment_unqualified() {
                    count = count + 1;  // Unqualified access
                }

                static function getCount() -> int {
                    return count;
                }
            }
        )");

        std::cout << "Initial count: " << engine->execute("Counter::getCount()").as<int>() << std::endl;
        engine->execute("Counter::increment_unqualified()");
        std::cout << "After increment (unqualified): " << engine->execute("Counter::getCount()").as<int>() << std::endl;
        std::cout << "✓ Unqualified access works!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ Unqualified access failed: " << e.what() << std::endl;
    }

    std::cout << "\n=== Test 2: Qualified static field access in static method ===" << std::endl;
    try {
        engine->execute(R"(
            class Counter2 {
                static int count = 0;

                static function increment_qualified() {
                    Counter2::count = Counter2::count + 1;  // Qualified access
                }

                static function getCount() -> int {
                    return Counter2::count;
                }
            }
        )");

        std::cout << "Initial count: " << engine->execute("Counter2::getCount()").as<int>() << std::endl;
        engine->execute("Counter2::increment_qualified()");
        std::cout << "After increment (qualified): " << engine->execute("Counter2::getCount()").as<int>() << std::endl;
        std::cout << "✓ Qualified access works!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ Qualified access failed: " << e.what() << std::endl;
    }

    return 0;
}
