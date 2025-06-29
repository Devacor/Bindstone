#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <set>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class map_tests : public suite {
public:
    map_tests() : suite("Map Operations") {}
    
    void forge_tests() override {
        test("map_creation_and_access", [this]() {
            engine engine;
            
            script_value result = engine.execute(R"(
                var m = {"a": 1, "b": 2, "c": 3};
                m["b"];
            )");
            check_eq(result.as<int>(), 2);
        });
        
        test("map_assignment", [this]() {
            engine engine;
            
            engine.execute(R"(
                var m = {"x": 10, "y": 20};
                m["x"] = 30;
                m["z"] = 40;
            )");
            
            script_value x = engine.execute("m[\"x\"];");
            check_eq(x.as<int>(), 30);
            
            script_value z = engine.execute("m[\"z\"];");
            check_eq(z.as<int>(), 40);
        });
        
        test("map_size_method", [this]() {
            engine engine;
            
            script_value result = engine.execute(R"(
                var m = {"a": 1, "b": 2, "c": 3};
                m.size();
            )");
            check_eq(result.as<int>(), 3);
        });
        
        test("map_empty_method", [this]() {
            engine engine;
            
            script_value empty_map = engine.execute(R"(
                var m = {};
                m.empty();
            )");
            check_eq(empty_map.as<bool>(), true);
            
            script_value non_empty = engine.execute(R"(
                var m2 = {"x": 1};
                m2.empty();
            )");
            check_eq(non_empty.as<bool>(), false);
        });
        
        test("map_clear_method", [this]() {
            engine engine;
            
            engine.execute(R"(
                var m = {"a": 1, "b": 2, "c": 3};
                m.clear();
            )");
            
            script_value size = engine.execute("m.size();");
            check_eq(size.as<int>(), 0);
            
            script_value empty = engine.execute("m.empty();");
            check_eq(empty.as<bool>(), true);
        });
        
        test("map_contains_method", [this]() {
            engine engine;
            
            engine.execute("var m = {\"foo\": 42, \"bar\": 99};");
            
            script_value has_foo = engine.execute("m.contains(\"foo\");");
            check_eq(has_foo.as<bool>(), true);
            
            script_value has_baz = engine.execute("m.contains(\"baz\");");
            check_eq(has_baz.as<bool>(), false);
        });
        
        test("map_erase_method", [this]() {
            engine engine;
            
            engine.execute(R"(
                var m = {"a": 1, "b": 2, "c": 3};
                m.erase("b");
            )");
            
            script_value size = engine.execute("m.size();");
            check_eq(size.as<int>(), 2);
            
            script_value has_b = engine.execute("m.contains(\"b\");");
            check_eq(has_b.as<bool>(), false);
        });
        
        test("map_keys_method", [this]() {
            engine engine;
            
            script_value keys = engine.execute(R"(
                var m = {"x": 10, "y": 20, "z": 30};
                m.keys();
            )");
            
            auto keys_array = keys.as_array();
            check_eq(keys_array.size(), size_t(3));
            
            // Keys might be in any order, so check that all expected keys exist
            std::set<std::string> key_set;
            for (const auto& key : keys_array) {
                key_set.insert(key.as_string());
            }
            check(key_set.count("x") == 1);
            check(key_set.count("y") == 1);
            check(key_set.count("z") == 1);
        });
        
        test("map_values_method", [this]() {
            engine engine;
            
            script_value values = engine.execute(R"(
                var m = {"a": 10, "b": 20, "c": 30};
                m.values();
            )");
            
            auto values_array = values.as_array();
            check_eq(values_array.size(), size_t(3));
            
            // Values might be in any order, so check that all expected values exist
            std::set<int> value_set;
            for (const auto& val : values_array) {
                value_set.insert(val.as<int>());
            }
            check(value_set.count(10) == 1);
            check(value_set.count(20) == 1);
            check(value_set.count(30) == 1);
        });
        
        test("map_mixed_types", [this]() {
            engine engine;
            
            engine.execute(R"(
                var m = {};
                m["int"] = 42;
                m["float"] = 3.14;
                m["string"] = "hello";
                m["bool"] = true;
                m["array"] = [1, 2, 3];
            )");
            
            script_value int_val = engine.execute("m[\"int\"];");
            check_eq(int_val.as<int>(), 42);
            
            script_value float_val = engine.execute("m[\"float\"];");
            check_eq(float_val.as<double>(), 3.14);
            
            script_value string_val = engine.execute("m[\"string\"];");
            check_eq(string_val.as_string(), "hello");
            
            script_value bool_val = engine.execute("m[\"bool\"];");
            check_eq(bool_val.as<bool>(), true);
            
            script_value array_val = engine.execute("m[\"array\"];");
            check_eq(array_val.as_array().size(), size_t(3));
        });
        
        test("map_nested_operations", [this]() {
            engine engine;
            
            script_value result = engine.execute(R"(
                var data = {
                    "users": {
                        "alice": {"age": 25, "city": "NYC"},
                        "bob": {"age": 30, "city": "LA"}
                    }
                };
                data["users"]["alice"]["age"];
            )");
            check_eq(result.as<int>(), 25);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using map_tests = jai::foundry::tests::map_tests;
FOUNDRY_REGISTER(map_tests)