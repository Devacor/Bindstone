#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

using namespace jai;
using namespace jai::foundry;

class deep_copy_tests : public suite {
public:
    deep_copy_tests() : suite("Deep Copy Tests") {}
    
    void forge_tests() override {
        test("map_deep_copy", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                var map1 = {"a": 1, "b": 2};
                var map2 = map1;  // This should deep copy
                map2["c"] = 3;    // Only affects map2
                map1["a"] + map1.size() * 10  // Should be 1 + 2*10 = 21
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 21, "map1 should not be affected by map2 changes");
        });
        
        test("array_deep_copy", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                var arr1 = [1, 2, 3];
                var arr2 = arr1;  // This should deep copy
                arr2.push(4);     // Only affects arr2
                arr1.size()       // Should still be 3
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 3, "arr1 should not be affected by arr2 changes");
        });
        
        test("nested_container_deep_copy", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                var data1 = {
                    "array": [1, 2, 3],
                    "map": {"x": 10, "y": 20}
                };
                var data2 = data1;  // Deep copy
                
                // Modify nested structures in data2
                data2["array"].push(4);
                data2["map"]["z"] = 30;
                
                // Check original is unchanged
                data1["array"].size() + data1["map"].size() * 10  // 3 + 2*10 = 23
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 23, "Nested containers should be deep copied");
        });
        
        test("assignment_operator_deep_copy", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                var map1 = {"a": 1};
                var map2 = {"b": 2};
                map2 = map1;      // Assignment should also deep copy
                map2["c"] = 3;    // Only affects map2
                map1.size()       // Should still be 1
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 1, "Assignment operator should deep copy");
        });
        
        test("function_parameter_copy", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                // Function parameters should copy by value
                auto modify_map(auto m) -> auto {
                    m["modified"] = true;
                    return m.size();
                }
                
                var original = {"a": 1, "b": 2};
                var size = modify_map(original);
                original.size()  // Should still be 2 (not modified)
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 2, "Function parameters should be copied by value");
        });
        
        test("reference_semantics_preserved", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            
            std::string script = R"(
                // References should still work for explicit sharing
                var data = {"value": 42};
                auto modify_ref(auto& m) -> auto {
                    m["value"] = 100;
                }

                modify_ref(data);
                data["value"]  // Should be modified to 100
            )";
            
            script_value result = engine->execute(script);
            check_eq(result.as<script_int>(), 100, "Reference parameters should allow modification");
        });
    }
};

FOUNDRY_REGISTER(deep_copy_tests)