#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ArithmeticSimple)

JAI_TEST(basic_arithmetic) {
    engine engine;
    
    std::cout << "Test 1: Simple addition\n";
    try {
        auto result = engine.execute("1 + 2");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 2: Loop arithmetic\n";
    try {
        auto result = engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 10; i = i + 1) {
                sum = sum + i;
            }
            sum
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 3: Function test\n";
    try {
        auto result = engine.execute(R"(
            auto add(auto a, auto b) -> auto {
                return a + b;
            }
            add(5, 3)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()