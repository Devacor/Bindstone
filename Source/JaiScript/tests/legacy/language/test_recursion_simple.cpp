#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(RecursionSimple)

JAI_TEST(simple_recursion) {
    engine engine;
    
    std::cout << "Test 1: Simple factorial\n";
    try {
        auto result = engine.execute(R"(
            function fact(auto n) -> auto {
                if (n <= 1) { return 1; }
                return n * fact(n - 1);
            }
            fact(2)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 2: Simple countdown\n";
    try {
        auto result = engine.execute(R"(
            function countdown(auto n) -> auto {
                if (n <= 0) { return 0; }
                return n + countdown(n - 1);
            }
            countdown(1)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()