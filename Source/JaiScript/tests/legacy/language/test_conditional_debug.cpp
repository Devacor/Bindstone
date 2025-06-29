#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ConditionalDebug)

JAI_TEST(test_comparison_operators) {
    engine engine;
    
    std::cout << "Test 1: Simple <= comparison\n";
    try {
        auto result = engine.execute("1 <= 1");
        std::cout << "1 <= 1 = " << (result.as_bool() ? "true" : "false") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 2: Zero <= one comparison\n";
    try {
        auto result = engine.execute("0 <= 1");
        std::cout << "0 <= 1 = " << (result.as_bool() ? "true" : "false") << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 3: Simple if statement\n";
    try {
        auto result = engine.execute(R"(
            auto n = 1;
            if (n <= 1) { 
                99
            } else { 
                0 
            }
        )");
        std::cout << "if (1 <= 1) result = " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 4: Zero if statement\n";
    try {
        auto result = engine.execute(R"(
            auto n = 0;
            if (n <= 1) { 
                88
            } else { 
                0 
            }
        )");
        std::cout << "if (0 <= 1) result = " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 5: Return statement test\n";
    try {
        auto result = engine.execute(R"(
            function testReturn(auto x) -> auto {
                if (x <= 1) { return 42; }
                return 0;
            }
            testReturn(1)
        )");
        std::cout << "testReturn(1) result = " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 6: Return statement with 0\n";
    try {
        auto result = engine.execute(R"(
            function testReturn(auto x) -> auto {
                if (x <= 1) { return 42; }
                return 0;
            }
            testReturn(0)
        )");
        std::cout << "testReturn(0) result = " << result.as_int() << "\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    std::cout << "Test 7: Return statement exit test\n";
    try {
        // Register print function first
        engine.add_function("print", [](const std::string& msg) {
            std::cout << "PRINT: " << msg << "\n";
        });
        
        auto result = engine.execute(R"(
            function testReturnExit() -> auto {
                return 42;
                print("This should NOT print!");
                return 99;
            }
            testReturnExit()
        )");
        std::cout << "testReturnExit() result = " << result.as_int() << " (should be 42, not 99)\n";
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()