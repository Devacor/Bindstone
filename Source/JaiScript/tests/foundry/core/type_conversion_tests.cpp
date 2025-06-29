#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <cstdint>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class type_conversion_tests : public suite {
public:
    type_conversion_tests() : suite("Type Conversions") {}
    
    void forge_tests() override {
        test("integer_type_conversions", [this]() {
            engine engine;
            
            script_value result = engine.execute("42;");
            check_eq(result.as<script_int>(), 42);
            check_eq(result.as<int>(), 42);
            check_eq(result.as<int32_t>(), 42);
            check_eq(result.as<int64_t>(), 42);
        });
        
        test("integer_size_conversions", [this]() {
            engine engine;
            
            // Test various integer sizes
            script_value small_int = engine.execute("127;");
            check_eq(static_cast<int8_t>(small_int.as<script_int>()), static_cast<int8_t>(127));
            
            script_value medium_int = engine.execute("32000;");
            check_eq(static_cast<int16_t>(medium_int.as<script_int>()), static_cast<int16_t>(32000));
            
            script_value large_int = engine.execute("123456;");
            check_eq(static_cast<int32_t>(large_int.as<script_int>()), static_cast<int32_t>(123456));
            
            script_value huge_int = engine.execute("9876543210;");
            check_eq(huge_int.as<script_int>(), static_cast<int64_t>(9876543210));
        });
        
        test("unsigned_integer_conversions", [this]() {
            engine engine;
            
            script_value byte_val = engine.execute("255;");
            check_eq(static_cast<uint8_t>(byte_val.as<script_int>()), static_cast<uint8_t>(255));
            
            script_value word_val = engine.execute("65000;");
            check_eq(static_cast<uint16_t>(word_val.as<script_int>()), static_cast<uint16_t>(65000));
            
            script_value dword_val = engine.execute("4000000000;");
            check_eq(static_cast<uint32_t>(dword_val.as<script_int>()), static_cast<uint32_t>(4000000000));
            
            // Test max int64 value
            script_value max_val = engine.execute("9223372036854775807;");
            check_eq(static_cast<uint64_t>(max_val.as<script_int>()), static_cast<uint64_t>(9223372036854775807LL));
        });
        
        test("float_type_conversions", [this]() {
            engine engine;
            
            script_value pi = engine.execute("3.14159;");
            check_near(pi.as<script_float>(), 3.14159, 0.00001);
            check_near(static_cast<float>(pi.as<script_float>()), 3.14159f, 0.00001f);
            check_near(pi.as<double>(), 3.14159, 0.00001);
        });
        
        test("string_conversions", [this]() {
            engine engine;
            
            script_value str = engine.execute("\"hello world\";");
            check_eq(str.as_string(), "hello world");
            check_eq(str.as<std::string>(), "hello world");
            
            // Test string with special characters
            script_value special = engine.execute("\"line1\\nline2\\ttab\";");
            check_eq(special.as_string(), "line1\nline2\ttab");
        });
        
        test("boolean_conversions", [this]() {
            engine engine;
            
            script_value true_val = engine.execute("true;");
            check_eq(true_val.as<bool>(), true);
            check_eq(true_val.as<script_bool>(), true);
            
            script_value false_val = engine.execute("false;");
            check_eq(false_val.as<bool>(), false);
            check_eq(false_val.as<script_bool>(), false);
        });
        
        test("implicit_numeric_conversions", [this]() {
            engine engine;
            
            // Test int to float conversion
            script_value result = engine.execute(R"(
                var x = 5;
                var y = 2.5;
                x + y;
            )");
            check_near(result.as<double>(), 7.5, 0.001);
            
            // Test float to int truncation
            result = engine.execute(R"(
                var x = 7.8;
                script_int(x);
            )");
            check_eq(result.as<int>(), 7);
        });
        
        test("array_element_conversions", [this]() {
            engine engine;
            
            script_value arr = engine.execute("[42, 3.14, \"hello\", true];");
            auto array = arr.as_array();
            
            check_eq(array[0].as<int>(), 42);
            check_near(array[1].as<double>(), 3.14, 0.01);
            check_eq(array[2].as_string(), "hello");
            check_eq(array[3].as<bool>(), true);
        });
        
        test("map_value_conversions", [this]() {
            engine engine;
            
            script_value map = engine.execute(R"(
                {
                    "int": 42,
                    "float": 3.14,
                    "string": "hello",
                    "bool": true
                }
            )");
            
            auto m = map.as_map();
            check_eq(m[script_value("int")].as<int>(), 42);
            check_near(m[script_value("float")].as<double>(), 3.14, 0.01);
            check_eq(m[script_value("string")].as_string(), "hello");
            check_eq(m[script_value("bool")].as<bool>(), true);
        });
        
        test("null_conversions", [this]() {
            engine engine;
            
            script_value null_val = engine.execute("null;");
            check(null_val.is_null());
            check_eq(null_val.type(), script_value_type::jai_null_type);
        });
        
        test("type_coercion_in_operations", [this]() {
            engine engine;
            
            // String concatenation with number
            script_value result = engine.execute("\"Number: \" + 42;");
            check_eq(result.as_string(), "Number: 42");
            
            // Boolean in arithmetic
            result = engine.execute("true + 5;");
            check_eq(result.as<int>(), 6);
            
            result = engine.execute("false + 10;");
            check_eq(result.as<int>(), 10);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using type_conversion_tests = jai::foundry::tests::type_conversion_tests;
FOUNDRY_REGISTER(type_conversion_tests)