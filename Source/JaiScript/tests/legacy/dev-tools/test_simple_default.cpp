#include "jai_test.hpp"
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SimpleDefaultCaptureTest)

JAI_TEST(default_capture_test) {
    engine engine;
    
    std::string script = R"(
        auto lambda = [=](auto bonus) -> auto {
            return bonus + 1;
        };
        lambda(5)
    )";
    
    try {
        script_value result = engine.execute(script);
        std::cout << "Default capture result: " << result.to_string() << std::endl;
        expect_eq(result.as<script_int>(), 6);
    } catch (const std::exception& e) {
        std::cout << "Exception in default capture: " << e.what() << std::endl;
        throw;
    }
}

JAI_TEST_SUITE_END()
JAI_TEST_MAIN()
