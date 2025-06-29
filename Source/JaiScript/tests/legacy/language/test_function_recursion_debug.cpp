#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(FunctionRecursionDebug)

JAI_TEST(test_simple_function) {
    engine engine;
    
    std::cout << "Test 1: Simple function declaration and call\n";
    try {
        auto result = engine.execute(R"(
            int add(int a, int b) {
                return a + b;
            }
            add(5, 3)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 2: Function with conditional\n";
    try {
        auto result = engine.execute(R"(
            int abs(int n) {
                if (n < 0) { return -n; }
                return n;
            }
            abs(-5)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 3: Simple recursive function\n";
    try {
        auto result = engine.execute(R"(
            int factorial(int n) {
                if (n <= 1) { return 1; }
                return n * factorial(n - 1);
            }
            factorial(5)
        )");
        std::cout << "Result: " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "\nTest 4: Prime function\n";
    try {
        auto result = engine.execute(R"(
            bool isPrime(int n) {
                if (n < 2) { return false; }
                var i = 2;
                while (i * i <= n) {
                    if (n % i == 0) { return false; }
                    i = i + 1;
                }
                return true;
            }
            isPrime(7)
        )");
        std::cout << "Result: " << (result.as_bool() ? "true" : "false") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()