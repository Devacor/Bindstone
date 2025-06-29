#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(CaptureDebugTest)

JAI_TEST(capture_by_value_test) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto lambda = [=](auto bonus) -> auto {
            return x + bonus;
        };
        x = 100;  // Change x after lambda creation
        lambda(5);  // Should return 15 (10 + 5), not 105
    )";
    
    try {
        script_value result = engine.execute(script);
        std::cout << "Capture by value result: " << result.to_string() << std::endl;
        expect_eq(result.as<script_int>(), 15);
    } catch (const std::exception& e) {
        std::cout << "Exception in capture by value: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST(capture_by_reference_test) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto lambda = [&](auto bonus) -> auto {
            x = x + bonus;
            return x;
        };
        lambda(5);  // Should modify x to 15
    )";
    
    try {
        script_value result = engine.execute(script);
        std::cout << "Capture by reference result: " << result.to_string() << std::endl;
        expect_eq(result.as<script_int>(), 15);
    } catch (const std::exception& e) {
        std::cout << "Exception in capture by reference: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()
