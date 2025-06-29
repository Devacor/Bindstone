#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SyntaxTests)

JAI_TEST(function_with_colon_parameters) {
    engine engine;
    script_value result = engine.execute(R"(
        function add(:a, :b) -> {
            return a + b;
        }
        add(3, 4);
    )");
    expect_eq(result.as_int(), script_int(7));
}

JAI_TEST(lambda_with_colon_parameters) {
    engine engine;
    script_value result = engine.execute(R"(
        var multiply = [](:a, :b) -> { return a * b; };
        multiply(5, 6);
    )");
    expect_eq(result.as_int(), script_int(30));
}

JAI_TEST(lambda_with_auto_parameters) {
    engine engine;
    script_value result = engine.execute(R"(
        var divide = [](auto a, auto b) -> { return a / b; };
        divide(20, 4);
    )");
    expect_eq(result.as_float(), script_float(5.0));
}

JAI_TEST(simple_arithmetic) {
    engine engine;
    script_value result = engine.execute("2 + 2;");
    expect_eq(result.as_int(), script_int(4));
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()