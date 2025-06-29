#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/stdlib/json.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(DirectJSONTests)

JAI_TEST(direct_to_json_call) {
    // Test direct to_json_impl call without going through engine
    
    std::cout << "Creating integer value..." << std::endl;
    script_value intVal(42);
    std::cout << "script_value created. Type: " << static_cast<int>(intVal.type()) << std::endl;
    
    std::cout << "Calling to_json_impl..." << std::endl;
    std::string result = stdlib::to_json_impl(intVal, -1, 0);
    std::cout << "Result: " << result << std::endl;
    
    expect_eq(result, "42");
}

JAI_TEST(direct_to_json_string) {
    script_value strVal("hello");
    std::string result = stdlib::to_json_impl(strVal, -1, 0);
    expect_eq(result, "\"hello\"");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()