#include "../jai_test.hpp"
#include "../../include/jaiscript/jaiscript.hpp"
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(FunctionDebugSimple)

JAI_TEST(test_def_syntax) {
    engine engine;
    
    std::cout << "Testing def syntax (JavaScript-like):\n";
    try {
        auto result = engine.execute(R"(
            def add(a, b) {
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