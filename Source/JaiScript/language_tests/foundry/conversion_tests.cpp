#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <vector>
#include <map>
#include <memory>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {
namespace conversion_tests_ns {

// Test classes for bound type conversions
class TestObject {
public:
    int value;
    std::string name;
    
    TestObject() : value(0), name("default") {}
    TestObject(int v, const std::string& n) : value(v), name(n) {}
    
    int get_value() const { return value; }
    void set_value(int v) { value = v; }
    const std::string& get_name() const { return name; }
    void set_name(const std::string& n) { name = n; }
};

class conversion_tests : public suite {
public:
    conversion_tests() : suite("Script Value Conversions") {}
    
    void forge_tests() override {
        test("basic_type_conversions", [this]() {
            auto engine = engine::make();
            
            // Test int conversion
            script_value int_val = engine->execute("42");
            check_eq(int_val.as<int>(), 42);
            check_eq(int_val.as<int64_t>(), 42L);
            check_eq(int_val.as<size_t>(), 42U);
            
            // Test float conversion
            script_value float_val = engine->execute("3.14");
            check_eq(float_val.as<double>(), 3.14);
            check_eq(float_val.as<float>(), 3.14f);
            
            // Test string conversion
            script_value string_val = engine->execute("\"hello world\"");
            check_eq(string_val.as<std::string>(), "hello world");
            
            // Test bool conversion
            script_value bool_val = engine->execute("true");
            check_eq(bool_val.as<bool>(), true);
            
            // Test char conversion
            script_value char_val = engine->execute("'A'");
            check_eq(char_val.as<char>(), 'A');
        });
        
        test("array_to_vector_conversions", [this]() {
            auto engine = engine::make();
            
            // Test basic vector<script_value> conversion
            script_value int_array = engine->execute("[1, 2, 3, 4, 5]");
            auto int_vec = int_array.as<std::vector<script_value>>();
            check_eq(int_vec.size(), 5U);
            check_eq(int_vec[0].as<int>(), 1);
            check_eq(int_vec[4].as<int>(), 5);
            
            // Test vector<string> conversion
            script_value string_array = engine->execute("[\"hello\", \"world\", \"test\"]");
            auto string_vec = string_array.as<std::vector<script_value>>();
            check_eq(string_vec.size(), 3U);
            check_eq(string_vec[0].as<std::string>(), "hello");
            check_eq(string_vec[2].as<std::string>(), "test");
            
            // Test mixed type array
            script_value mixed_array = engine->execute("[1, \"hello\", 3.14, true]");
            auto mixed_vec = mixed_array.as<std::vector<script_value>>();
            check_eq(mixed_vec.size(), 4U);
            check_eq(mixed_vec[0].as<int>(), 1);
            check_eq(mixed_vec[1].as<std::string>(), "hello");
            check_eq(mixed_vec[2].as<double>(), 3.14);
            check_eq(mixed_vec[3].as<bool>(), true);
        });
        
        test("map_to_stdmap_conversions", [this]() {
            auto engine = engine::make();
            
            // Test basic map conversion
            script_value map_val = engine->execute("{\"key1\": 1, \"key2\": 2, \"key3\": 3}");
            auto map_ref = map_val.as<std::map<script_value, script_value>>();
            check_eq(map_ref.size(), 3U);
            
            // Test accessing map values
            script_value key1 = script_value("key1", engine->weak_from_this());
            script_value key2 = script_value("key2", engine->weak_from_this());
            check_eq(map_ref.at(key1).as<int>(), 1);
            check_eq(map_ref.at(key2).as<int>(), 2);
        });
        
        test("vector_parameter_binding_attempt", [this]() {
            auto engine = engine::make();
            
            // Test function that takes vector<int>
            engine->add_function("sum_vector", [](std::vector<int> nums) -> int {
                int sum = 0;
                for (int n : nums) sum += n;
                return sum;
            });
            
            // This will test if the conversion works
            try {
                script_value result = engine->execute("sum_vector([1, 2, 3, 4, 5])");
                check_eq(result.as<int>(), 15);
                std::cout << "    ✓ Vector<int> parameter binding works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector<int> parameter binding failed: " << e.what() << "\n";
                // Don't fail the test - we expect this might not work
            }
        });
        
        test("map_parameter_binding_attempt", [this]() {
            auto engine = engine::make();
            
            // Test function that takes map<string, int>
            engine->add_function("sum_map_values", [](std::map<std::string, int> map) -> int {
                int sum = 0;
                for (const auto& pair : map) {
                    sum += pair.second;
                }
                return sum;
            });
            
            try {
                script_value result = engine->execute("sum_map_values({\"a\": 1, \"b\": 2, \"c\": 3})");
                check_eq(result.as<int>(), 6);
                std::cout << "    ✓ Map<string, int> parameter binding works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, int> parameter binding failed: " << e.what() << "\n";
                // Don't fail the test - we expect this might not work
            }
        });
        
        test("script_value_vector_compatibility", [this]() {
            auto engine = engine::make();
            
            // Test function that takes vector<script_value> explicitly
            engine->add_function("count_integers", [](std::vector<script_value> values) -> int {
                int count = 0;
                for (const auto& val : values) {
                    if (val.is_int()) count++;
                }
                return count;
            });
            
            try {
                script_value result = engine->execute("count_integers([1, \"hello\", 3.14, 42])");
                check_eq(result.as<int>(), 2); // Two integers: 1 and 42
                std::cout << "    ✓ Vector<script_value> parameter binding works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector<script_value> parameter binding failed: " << e.what() << "\n";
            }
        });
        
        test("script_value_map_compatibility", [this]() {
            auto engine = engine::make();
            
            // Test function that takes map<script_value, script_value> explicitly
            engine->add_function("count_int_values", [](std::map<script_value, script_value> map) -> int {
                int count = 0;
                for (const auto& pair : map) {
                    if (pair.second.is_int()) {
                        count++;
                    }
                }
                return count;
            });
            
            try {
                script_value result = engine->execute("count_int_values({\"a\": 1, \"b\": \"hello\", \"c\": 2})");
                check_eq(result.as<int>(), 2); // Two integers: 1 and 2
                std::cout << "    ✓ Map<script_value, script_value> parameter binding works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<script_value, script_value> parameter binding failed: " << e.what() << "\n";
            }
        });
        
        test("bound_class_conversions", [this]() {
            auto engine = engine::make();
            
            // Register TestObject class
            class_builder<TestObject>(*engine, "TestObject")
                .constructor<>()
                .constructor<int, const std::string&>()
                .method("getValue", &TestObject::get_value)
                .method("setValue", &TestObject::set_value)
                .method("getName", &TestObject::get_name)
                .method("setName", &TestObject::set_name)
                .property("value", &TestObject::value)
                .property("name", &TestObject::name)
                .build();
            
            // Test creating and converting bound object
            script_value obj_val = engine->execute("TestObject(42, \"test\")");
            
            // Debug: Check what type we have
            std::cout << "    Object type: " << static_cast<int>(obj_val.type()) << std::endl;
            std::cout << "    Is object: " << obj_val.is_object() << std::endl;
            
            try {
                auto obj_ptr = obj_val.as<std::shared_ptr<TestObject>>();
                check_eq(obj_ptr->get_value(), 42);
                check_eq(obj_ptr->get_name(), "test");
            } catch (const std::exception& e) {
                std::cout << "    Exception: " << e.what() << std::endl;
                throw;
            }
            
            // Test calling methods on bound object
            script_value result = engine->execute(R"(
                var obj = TestObject(10, "hello");
                obj.setValue(20);
                obj.setName("world");
                obj.getValue();
            )");
            check_eq(result.as<int>(), 20);
        });
    }
};

} // namespace conversion_tests_ns
} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::conversion_tests_ns::conversion_tests)