#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(CaptureParseTest)

JAI_TEST(explicit_capture_test) {
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
        std::cout << "Explicit capture result: " << result.to_string() << std::endl;
        expect_eq(result.as<script_int>(), 15);
    } catch (const std::exception& e) {
        std::cout << "Exception in explicit capture: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()
