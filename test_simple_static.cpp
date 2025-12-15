#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto eng = jai::engine::make();

    try {
        std::cout << "=== Test 1: Define class with static field ===" << std::endl;
        eng->execute(R"(
            class Test {
                static int value = 42;
            }
        )");
        std::cout << "Class defined successfully\n";

        std::cout << "\n=== Test 2: Access static field ===" << std::endl;
        auto result = eng->execute("Test::value");
        std::cout << "Test::value = " << result.as<int>() << " (expected 42)\n";

        std::cout << "\n=== Test 3: Access from constructor ===" << std::endl;
        eng->execute(R"(
            class Counter {
                static int count = 0;

                Counter() {
                    print("count = " + to_string(count));
                }
            }
        )");
        eng->execute("auto c = Counter();");

        std::cout << "\n=== Test 4: Modify static field from constructor (CRITICAL TEST) ===" << std::endl;
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

        eng->execute("auto c1 = Counter2();");
        auto result2 = eng->execute("Counter2::total");
        std::cout << "Counter2::total after 1 instance = " << result2.as<int>() << " (expected 1)\n";

        eng->execute("auto c2 = Counter2();");
        auto result3 = eng->execute("Counter2::total");
        std::cout << "Counter2::total after 2 instances = " << result3.as<int>() << " (expected 2)\n";

        if (result3.as<int>() != 2) {
            std::cerr << "FAILED: Static field modification didn't work!\n";
            return 1;
        }

        std::cout << "\nAll tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
