#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(JSONTests)

JAI_TEST(to_json_primitives) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test null
    expect_eq(engine.execute("to_json(null)").as_string(), "null");
    
    // Test boolean
    expect_eq(engine.execute("to_json(true)").as_string(), "true");
    expect_eq(engine.execute("to_json(false)").as_string(), "false");
    
    // Test integers
    expect_eq(engine.execute("to_json(42)").as_string(), "42");
    expect_eq(engine.execute("to_json(-17)").as_string(), "-17");
    expect_eq(engine.execute("to_json(0)").as_string(), "0");
    
    // Test floats
    expect_eq(engine.execute("to_json(3.14)").as_string(), "3.14");
    expect_eq(engine.execute("to_json(100.0)").as_string(), "100.0");
    
    // Test strings
    expect_eq(engine.execute("to_json(\"hello\")").as_string(), "\"hello\"");
    expect_eq(engine.execute("to_json(\"hello\\nworld\")").as_string(), "\"hello\\nworld\"");
    expect_eq(engine.execute("to_json(\"quote: \\\"test\\\"\")").as_string(), "\"quote: \\\"test\\\"\"");
}

JAI_TEST(to_json_arrays) {
    engine engine;
    stdlib::register_all(engine);
    
    // Empty array
    expect_eq(engine.execute("to_json([])").as_string(), "[]");
    
    // Simple array
    expect_eq(engine.execute("to_json([1, 2, 3])").as_string(), "[1,2,3]");
    
    // Mixed type array
    expect_eq(engine.execute("to_json([1, \"hello\", true, null])").as_string(), 
              "[1,\"hello\",true,null]");
    
    // Nested arrays
    expect_eq(engine.execute("to_json([[1, 2], [3, 4]])").as_string(), "[[1,2],[3,4]]");
}

JAI_TEST(to_json_maps) {
    engine engine;
    stdlib::register_all(engine);
    
    // Empty map
    expect_eq(engine.execute("to_json({})").as_string(), "{}");
    
    // Simple map (JSON style) - check for both possible orderings
    std::string mapJson = engine.execute("to_json({\"name\": \"John\", \"age\": 30})").as_string();
    expect_true(mapJson == "{\"name\":\"John\",\"age\":30}" || 
                mapJson == "{\"age\":30,\"name\":\"John\"}");
    
    // Map with various value types - just check that it contains the expected elements
    std::string complexMapJson = engine.execute("to_json({\"int\": 42, \"float\": 3.14, \"bool\": true, \"null\": null})").as_string();
    expect_true(complexMapJson.find("\"int\":42") != std::string::npos);
    expect_true(complexMapJson.find("\"float\":3.14") != std::string::npos);
    expect_true(complexMapJson.find("\"bool\":true") != std::string::npos);
    expect_true(complexMapJson.find("\"null\":null") != std::string::npos);
}

JAI_TEST(to_json_nested_structures) {
    engine engine;
    stdlib::register_all(engine);
    
    // Nested object
    std::string script = R"(
        var data = {
            "name": "Player",
            "stats": {
                "level": 10,
                "health": 100.0,
                "alive": true
            },
            "inventory": ["sword", "shield", "potion"]
        };
        to_json(data)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should contain all the elements (order might vary for maps)
    expect_true(json.find("\"name\":\"Player\"") != std::string::npos);
    expect_true(json.find("\"level\":10") != std::string::npos);
    expect_true(json.find("\"health\":100.0") != std::string::npos);
    expect_true(json.find("\"alive\":true") != std::string::npos);
    expect_true(json.find("[\"sword\",\"shield\",\"potion\"]") != std::string::npos);
}

JAI_TEST(to_json_pretty_printing) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test pretty printing with indentation
    std::string script = R"(
        var data = {"name": "test", "values": [1, 2, 3]};
        to_json(data, 2)
    )";
    
    script_value result = engine.execute(script);
    std::string json = result.as_string();
    
    // Should have newlines and indentation
    expect_true(json.find("\n") != std::string::npos);
    expect_true(json.find("  ") != std::string::npos); // 2-space indent
}

JAI_TEST(to_json_special_characters) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test escape sequences
    expect_eq(engine.execute("to_json(\"\\t\\r\\n\")").as_string(), "\"\\t\\r\\n\"");
    expect_eq(engine.execute("to_json(\"\\\\\")").as_string(), "\"\\\\\"");
    expect_eq(engine.execute("to_json(\"\\\"\")").as_string(), "\"\\\"\"");
}

// TODO: Add tests for C++ bound objects when we have a way to check for to_json methods

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()