#include "assignment_tests.hpp"

namespace jai::foundry::tests {

void assignment_tests::forge_tests() {
    test("simple_variable_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto x = 42;");
        engine->execute("x = 99;");
        auto result = engine->execute("x");
        check_eq(result.as<int>(), 99);
    });
    
    test("array_subscript_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto arr = [1, 2, 3];");
        engine->execute("arr[1] = 999;");
        auto result = engine->execute("arr[1]");
        check_eq(result.as<int>(), 999);
    });
    
    test("empty_map_creation_and_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto myMap = {};");
        engine->execute("myMap[\"key\"] = 123;");
        auto result = engine->execute("myMap[\"key\"]");
        check_eq(result.as<int>(), 123);
    });
    
    test("map_integer_key_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto myMap = {};");
        engine->execute("myMap[456] = 789;");
        auto result = engine->execute("myMap[456]");
        check_eq(result.as<int>(), 789);
    });
    
    test("map_variable_key_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto myMap = {};");
        engine->execute("auto key = \"test\";");
        engine->execute("myMap[key] = 42;");
        auto result = engine->execute("myMap[key]");
        check_eq(result.as<int>(), 42);
    });
    
    test("non_empty_map_assignment", [this]() {
        auto engine = engine::make();
        engine->execute("auto myMap = {{\"existing\", 100}};");
        engine->execute("myMap[\"new\"] = 200;");
        auto existing = engine->execute("myMap[\"existing\"]");
        auto new_val = engine->execute("myMap[\"new\"]");
        check_eq(existing.as<int>(), 100);
        check_eq(new_val.as<int>(), 200);
    });
    
    test("mixed_key_types_in_map", [this]() {
        auto engine = engine::make();
        engine->execute("auto data = {};");
        engine->execute("data[\"string_key\"] = 111;");
        engine->execute("data[123] = 222;");
        engine->execute("data[45.6] = 333;");
        
        check_eq(engine->execute("data[\"string_key\"]").as<int>(), 111);
        check_eq(engine->execute("data[123]").as<int>(), 222);
        check_eq(engine->execute("data[45.6]").as<int>(), 333);
    });
    
    test("compound_assignment_on_array_element", [this]() {
        auto engine = engine::make();
        engine->execute("auto arr = [10, 20, 30];");
        engine->execute("arr[1] += 5;");
        auto result = engine->execute("arr[1]");
        check_eq(result.as<int>(), 25);
    });
    
    test("compound_assignment_on_map_element", [this]() {
        auto engine = engine::make();
        engine->execute("auto myMap = {{\"value\", 100}};");
        engine->execute("myMap[\"value\"] *= 2;");
        auto result = engine->execute("myMap[\"value\"]");
        check_eq(result.as<int>(), 200);
    });
    
    test("nested_container_assignment", [this]() {
        auto engine = engine::make();
        // Create a map of arrays
        engine->execute("auto nested = {{\"nums\", [1, 2, 3]}};");
        engine->execute("nested[\"nums\"][1] = 999;");
        auto result = engine->execute("nested[\"nums\"][1]");
        check_eq(result.as<int>(), 999);
    });
    
    test("reference_semantics_verification", [this]() {
        auto engine = engine::make();
        // This test verifies that subscript access returns a reference,
        // but when assigned to a variable, the variable gets a copy
        engine->execute("auto myMap = {{\"test\", 42}};");
        engine->execute("auto temp = myMap[\"test\"];"); // temp gets a copy of the value
        engine->execute("temp = 999;"); // This modifies temp, not the map
        auto result = engine->execute("myMap[\"test\"]");
        check_eq(result.as<int>(), 42); // Should still be 42 since temp is a separate copy
        
        // But direct assignment through subscript should work
        engine->execute("myMap[\"test\"] = 888;");
        auto result2 = engine->execute("myMap[\"test\"]");
        check_eq(result2.as<int>(), 888); // Direct assignment modifies the map
    });
    
    test("map_keyword_conflict_resolution", [this]() {
        auto engine = engine::make();
        // Verify that 'map' cannot be used as a variable name
        try {
            engine->execute("auto map = {};");
            throw test_failure("Should have thrown an error for using 'map' as variable name");
        } catch (const std::exception&) {
            // Expected - 'map' is a reserved keyword
        }
        
        // But 'myMap', 'mapData', etc. should work fine
        engine->execute("auto myMap = {};");
        engine->execute("auto mapData = {};");
        engine->execute("auto dataMap = {};");
        // If we get here without exceptions, the test passes
    });
    
    test("array_bounds_checking", [this]() {
        auto engine = engine::make();
        engine->execute("auto arr = [1, 2, 3];");
        
        // Valid assignment
        engine->execute("arr[2] = 99;");
        check_eq(engine->execute("arr[2]").as<int>(), 99);
        
        // Out of bounds should throw
        try {
            engine->execute("arr[5] = 100;");
            throw test_failure("Should have thrown out of bounds error");
        } catch (const std::exception& e) {
            check(std::string(e.what()).find("out of bounds") != std::string::npos);
        }
    });
    
    test("auto_reference_syntax", [this]() {
        auto engine = engine::make();
        
        // Test auto& creates a reference that can modify the container
        engine->execute("auto myMap = {{\"test\", 42}};");
        engine->execute("auto& refVal = myMap[\"test\"];");
        engine->execute("refVal = 999;");
        
        // Both should have the same value
        check_eq(engine->execute("myMap[\"test\"]").as<int>(), 999);
        check_eq(engine->execute("refVal").as<int>(), 999);
        
        // Test with arrays too
        engine->execute("auto arr = [10, 20, 30];");
        engine->execute("auto& arrRef = arr[1];");
        engine->execute("arrRef = 200;");
        
        check_eq(engine->execute("arr[1]").as<int>(), 200);
        check_eq(engine->execute("arrRef").as<int>(), 200);
        
        // Test auto (non-reference) creates a copy
        engine->execute("auto copyVal = myMap[\"test\"];");
        engine->execute("copyVal = 1234;");
        
        // Map should still have 999, only copyVal changed
        check_eq(engine->execute("myMap[\"test\"]").as<int>(), 999);
        check_eq(engine->execute("copyVal").as<int>(), 1234);
    });
    
    test("complete_workflow_test", [this]() {
        auto engine = engine::make();
        
        // Register print function for the test
        engine->add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
            // Just consume the args, don't actually print in tests
            return script_value(script_value::serialization_tag{}, std::monostate{});
        });
        
        // The exact workflow requested by the user
        auto result = engine->execute(R"(
            auto mapVal = {};
            mapVal["key"] = 5;
            print(mapVal["key"]);
            mapVal["key"]
        )");
        
        check_eq(result.as<int>(), 5);
    });
}

} // namespace jai::foundry::tests