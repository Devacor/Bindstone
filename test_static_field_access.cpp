#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();
    jai::stdlib::register_all(*engine);

    try {
        // Test 1: Static field access in static method
        std::cout << "Test 1: Static field in static method\n";
        engine->execute(R"(
            class Counter {
                static int total = 0;

                static auto add(int v) {
                    print("Before: total = " + to_string(total));
                    total = total + v;
                    print("After: total = " + to_string(total));
                    return total;
                }
            }
        )");

        auto result1 = engine->execute("Counter::add(10)");
        std::cout << "Result: " << result1.to_string() << std::endl;

        // Test 2: Static field access in constructor
        std::cout << "\nTest 2: Static field in constructor\n";
        engine->execute(R"(
            class Config {
                static int count = 0;
                int id;

                Config() {
                    print("Constructor: count = " + to_string(count));
                    count = count + 1;
                    id = count;
                    print("Constructor: new count = " + to_string(count));
                }

                auto getId() {
                    return id;
                }
            }
        )");

        auto result2 = engine->execute("auto c = Config(); c.getId()");
        std::cout << "ID: " << result2.to_string() << std::endl;

        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
