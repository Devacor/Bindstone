#include "../jai_test.hpp"
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::test;

JAI_TEST_SUITE(FromJSONTests)

JAI_TEST(from_json_primitives) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test null
    expect_true(engine.execute("from_json(\"null\")").is_null());
    
    // Test boolean
    expect_eq(engine.execute("from_json(\"true\")").as_bool(), true);
    expect_eq(engine.execute("from_json(\"false\")").as_bool(), false);
    
    // Test integers
    expect_eq(engine.execute("from_json(\"42\")").as_int(), 42);
    expect_eq(engine.execute("from_json(\"-17\")").as_int(), -17);
    expect_eq(engine.execute("from_json(\"0\")").as_int(), 0);
    
    // Test floats
    expect_near(engine.execute("from_json(\"3.14\")").as_float(), 3.14, 0.0001);
    expect_near(engine.execute("from_json(\"100.0\")").as_float(), 100.0, 0.0001);
    expect_near(engine.execute("from_json(\"-2.5\")").as_float(), -2.5, 0.0001);
    
    // Test strings
    expect_eq(engine.execute("from_json(\"\\\"hello\\\"\")").as_string(), "hello");
    expect_eq(engine.execute("from_json(\"\\\"hello\\\\nworld\\\"\")").as_string(), "hello\nworld");
    expect_eq(engine.execute("from_json(\"\\\"quote: \\\\\\\"test\\\\\\\"\\\"\")").as_string(), "quote: \"test\"");
}

JAI_TEST(from_json_arrays) {
    engine engine;
    stdlib::register_all(engine);
    
    // Empty array
    script_value emptyArr = engine.execute("from_json(\"[]\")");
    expect_true(emptyArr.is_array());
    expect_eq(emptyArr.as_array().size(), 0u);
    
    // Simple array
    script_value simpleArr = engine.execute("from_json(\"[1, 2, 3]\")");
    expect_true(simpleArr.is_array());
    expect_eq(simpleArr.as_array().size(), 3u);
    expect_eq(simpleArr.as_array()[0].as_int(), 1);
    expect_eq(simpleArr.as_array()[1].as_int(), 2);
    expect_eq(simpleArr.as_array()[2].as_int(), 3);
    
    // Mixed type array
    script_value mixedArr = engine.execute("from_json(\"[1, \\\"hello\\\", true, null]\")");
    expect_true(mixedArr.is_array());
    expect_eq(mixedArr.as_array().size(), 4u);
    expect_eq(mixedArr.as_array()[0].as_int(), 1);
    expect_eq(mixedArr.as_array()[1].as_string(), "hello");
    expect_eq(mixedArr.as_array()[2].as_bool(), true);
    expect_true(mixedArr.as_array()[3].is_null());
}

JAI_TEST(from_json_objects) {
    engine engine;
    stdlib::register_all(engine);
    
    // Empty object
    script_value emptyObj = engine.execute("from_json(\"{}\")");
    expect_true(emptyObj.is_map());
    expect_eq(emptyObj.as_map().size(), 0u);
    
    // Simple object
    script_value simpleObj = engine.execute("from_json(\"{\\\"name\\\": \\\"John\\\", \\\"age\\\": 30}\")");
    expect_true(simpleObj.is_map());
    expect_eq(simpleObj.as_map().size(), 2u);
    expect_eq(simpleObj.as_map().at(script_value("name")).as_string(), "John");
    expect_eq(simpleObj.as_map().at(script_value("age")).as_int(), 30);
    
    // Nested object
    script_value nestedObj = engine.execute(R"(
        from_json("{\"user\": {\"id\": 123, \"active\": true}, \"count\": 5}")
    )");
    expect_true(nestedObj.is_map());
    script_value userObj = nestedObj.as_map().at(script_value("user"));
    expect_true(userObj.is_map());
    expect_eq(userObj.as_map().at(script_value("id")).as_int(), 123);
    expect_eq(userObj.as_map().at(script_value("active")).as_bool(), true);
    expect_eq(nestedObj.as_map().at(script_value("count")).as_int(), 5);
}

JAI_TEST(from_json_round_trip) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test round-trip conversion
    std::string script = R"(
        var original = {
            "name": "Test",
            "values": [1, 2.5, true, null],
            "nested": {
                "key": "value"
            }
        };
        var json = to_json(original);
        var parsed = from_json(json);
        to_json(parsed)
    )";
    
    script_value result = engine.execute(script);
    std::string jsonStr = result.as_string();
    
    // Should contain all the original data
    expect_true(jsonStr.find("\"name\":\"Test\"") != std::string::npos);
    expect_true(jsonStr.find("\"values\":[1,2.5,true,null]") != std::string::npos);
    expect_true(jsonStr.find("\"key\":\"value\"") != std::string::npos);
}

JAI_TEST(from_json_error_handling) {
    engine engine;
    stdlib::register_all(engine);
    
    // Test invalid JSON
    try {
        engine.execute("from_json(\"{invalid}\")");
        expect_true(false); // Should not reach here
    } catch (const std::exception& e) {
        expect_true(std::string(e.what()).find("Expected") != std::string::npos);
    }
    
    // Test incomplete JSON
    try {
        engine.execute("from_json(\"{\\\"key\\\": }\")");
        expect_true(false); // Should not reach here
    } catch (const std::exception& e) {
        expect_true(std::string(e.what()).find("Unexpected") != std::string::npos);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()