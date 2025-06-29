#include "../jai_test.hpp"
#include <jaiscript/jaiscript.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(ClassInstantiation)

JAI_TEST(function_call_syntax) {
    engine engine;
    
    // Use add_variadic_function for functions that take std::vector<script_value>&
    engine.add_variadic_function("Point", [](const std::vector<script_value>& args) -> script_value {
        return script_value(static_cast<script_int>(42)); // Return a dummy value
    });
    
    script_value result = engine.execute("Point();");
    expect_eq(static_cast<script_int>(42), result.as_int());
}

JAI_TEST(variable_assignment) {
    engine engine;
    
    engine.add_variadic_function("Point", [](const std::vector<script_value>& args) -> script_value {
        return script_value(static_cast<script_int>(42));
    });
    
    script_value result = engine.execute("var p = Point(); p;");
    expect_eq(static_cast<script_int>(42), result.as_int());
}

JAI_TEST(auto_variable) {
    engine engine;
    
    engine.add_variadic_function("Point", [](const std::vector<script_value>& args) -> script_value {
        return script_value(static_cast<script_int>(42));
    });
    
    script_value result = engine.execute("auto p2 = Point(); p2;");
    expect_eq(static_cast<script_int>(42), result.as_int());
}

JAI_TEST(return_statement) {
    engine engine;
    
    engine.add_variadic_function("Point", [](const std::vector<script_value>& args) -> script_value {
        return script_value(static_cast<script_int>(42));
    });
    
    script_value result = engine.execute("auto p3 = Point(); return p3;");
    expect_eq(static_cast<script_int>(42), result.as_int());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()