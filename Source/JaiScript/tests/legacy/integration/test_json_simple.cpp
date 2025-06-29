#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(SimpleJSONTests)

JAI_TEST(basic_to_json_call) {
    engine engine;
    
    std::cout << "About to register JSON functions..." << std::endl;
    stdlib::register_all(engine);
    std::cout << "JSON functions registered." << std::endl;
    
    // Test with a simple integer
    std::cout << "About to execute to_json(42)..." << std::endl;
    script_value result = engine.execute("to_json(42)");
    std::cout << "Result: " << result.to_string() << std::endl;
    
    expect_eq(result.as_string(), "42");
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()