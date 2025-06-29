#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SimpleTest)

JAI_TEST(basic_capture_test) {
    engine engine;
    
    std::string script = R"(
        auto x = 10;
        auto lambda = [x](auto bonus) -> auto {
            return x + bonus;
        };
        lambda(5)
    )";
    
    try {
        script_value result = engine.execute(script);
        std::cout << "Result: " << result.to_string() << std::endl;
        expect_eq(result.as<script_int>(), 15);
    } catch (const std::exception& e) {
        std::cout << "Exception: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()
