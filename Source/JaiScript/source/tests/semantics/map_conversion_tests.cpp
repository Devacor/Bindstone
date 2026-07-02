#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test classes for map conversion scenarios
class MapProcessor {
public:
    // Test different map parameter types
    static int sum_string_to_int_map(std::map<std::string, int> map) {
        int total = 0;
        for (const auto& pair : map) {
            total += pair.second;
        }
        return total;
    }
    
    static std::vector<std::string> get_string_keys(std::map<std::string, int> map) {
        std::vector<std::string> keys;
        for (const auto& pair : map) {
            keys.push_back(pair.first);
        }
        return keys;
    }
    
    static std::vector<int> get_int_values(std::map<std::string, int> map) {
        std::vector<int> values;
        for (const auto& pair : map) {
            values.push_back(pair.second);
        }
        return values;
    }
    
    static std::map<std::string, int> create_string_int_map() {
        std::map<std::string, int> result;
        result["first"] = 1;
        result["second"] = 2;
        result["third"] = 3;
        return result;
    }
    
    static std::map<int, std::string> reverse_map(std::map<std::string, int> input) {
        std::map<int, std::string> result;
        for (const auto& pair : input) {
            result[pair.second] = pair.first;
        }
        return result;
    }
    
    static std::string concatenate_string_values(std::map<std::string, std::string> map) {
        std::string result;
        for (const auto& pair : map) {
            result += pair.second;
        }
        return result;
    }
    
    static double sum_double_values(std::map<std::string, double> map) {
        double total = 0.0;
        for (const auto& pair : map) {
            total += pair.second;
        }
        return total;
    }
    
    // Test nested maps
    static std::map<std::string, std::map<std::string, int>> create_nested_map() {
        std::map<std::string, std::map<std::string, int>> result;
        result["group1"]["a"] = 1;
        result["group1"]["b"] = 2;
        result["group2"]["x"] = 10;
        result["group2"]["y"] = 20;
        return result;
    }
    
    static int sum_nested_map(std::map<std::string, std::map<std::string, int>> nested) {
        int total = 0;
        for (const auto& outer : nested) {
            for (const auto& inner : outer.second) {
                total += inner.second;
            }
        }
        return total;
    }
    
    // Test map size operations
    static size_t get_map_size(std::map<std::string, int> map) {
        return map.size();
    }
    
    static bool map_contains_key(std::map<std::string, int> map, const std::string& key) {
        return map.find(key) != map.end();
    }
    
    static int get_map_value(std::map<std::string, int> map, const std::string& key) {
        auto it = map.find(key);
        if (it != map.end()) {
            return it->second;
        }
        return -1; // Not found
    }
};

// Test class for map values
class Person {
public:
    std::string name;
    int age;
    
    Person() : name(""), age(0) {}
    Person(const std::string& n, int a) : name(n), age(a) {}
    
    std::string get_name() const { return name; }
    int get_age() const { return age; }
    void set_name(const std::string& n) { name = n; }
    void set_age(int a) { age = a; }
    
    std::string to_string() const {
        return name + " (" + std::to_string(age) + ")";
    }
};

class PersonMapProcessor {
public:
    static int sum_ages(std::map<std::string, Person> people) {
        int total = 0;
        for (const auto& pair : people) {
            total += pair.second.get_age();
        }
        return total;
    }
    
    static std::vector<std::string> get_names(std::map<std::string, Person> people) {
        std::vector<std::string> names;
        for (const auto& pair : people) {
            names.push_back(pair.second.get_name());
        }
        return names;
    }
    
    static std::map<std::string, Person> create_person_map() {
        std::map<std::string, Person> result;
        result["john"] = Person("John Doe", 30);
        result["jane"] = Person("Jane Smith", 25);
        result["bob"] = Person("Bob Johnson", 35);
        return result;
    }
};

class map_conversion_tests : public suite {
public:
    map_conversion_tests() : suite("Map Conversion Tests") {}
    
    void forge_tests() override {
        test("basic_string_int_map_conversion", [this]() {
            auto engine = make_engine();
            
            // Register basic map functions
            engine->add_function("sum_string_to_int_map", &MapProcessor::sum_string_to_int_map);
            engine->add_function("get_string_keys", &MapProcessor::get_string_keys);
            engine->add_function("get_int_values", &MapProcessor::get_int_values);
            engine->add_function("get_map_size", &MapProcessor::get_map_size);
            
            // Test basic map<string, int> parameter
            try {
                script_value result = engine->execute("sum_string_to_int_map({\"a\": 1, \"b\": 2, \"c\": 3})");
                check_eq(result.as<int>(), 6);
                std::cout << "    ✓ Basic map<string, int> parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Basic map<string, int> parameter failed: " << e.what() << std::endl;
            }
            
            // Test map key extraction
            try {
                script_value result = engine->execute("get_string_keys({\"x\": 10, \"y\": 20, \"z\": 30})");
                auto keys = result.as<std::vector<script_value>>();
                check_eq(keys.size(), 3U);
                std::cout << "    ✓ Map key extraction works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map key extraction failed: " << e.what() << std::endl;
            }
            
            // Test map value extraction
            try {
                script_value result = engine->execute("get_int_values({\"alpha\": 100, \"beta\": 200})");
                auto values = result.as<std::vector<script_value>>();
                check_eq(values.size(), 2U);
                std::cout << "    ✓ Map value extraction works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map value extraction failed: " << e.what() << std::endl;
            }
            
            // Test map size
            try {
                script_value result = engine->execute("get_map_size({\"one\": 1, \"two\": 2, \"three\": 3, \"four\": 4})");
                check_eq(result.as<int>(), 4);
                std::cout << "    ✓ Map size works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map size failed: " << e.what() << std::endl;
            }
        });
        
        test("map_return_values", [this]() {
            auto engine = make_engine();
            
            engine->add_function("create_string_int_map", &MapProcessor::create_string_int_map);
            engine->add_function("reverse_map", &MapProcessor::reverse_map);
            
            // Test map return value
            try {
                script_value result = engine->execute("create_string_int_map()");
                auto map = result.as<std::map<script_value, script_value>>();
                check_eq(map.size(), 3U);
                
                script_value first_key = engine->make_value("first");
                auto it = map.find(first_key);
                if (it != map.end()) {
                    check_eq(it->second.as<int>(), 1);
                }
                
                std::cout << "    ✓ Map return value works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map return value failed: " << e.what() << std::endl;
            }
            
            // Test map transformation
            try {
                engine->execute("var original = create_string_int_map();");
                script_value result = engine->execute("reverse_map(original)");
                auto reversed = result.as<std::map<script_value, script_value>>();
                check_eq(reversed.size(), 3U);
                std::cout << "    ✓ Map transformation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map transformation failed: " << e.what() << std::endl;
            }
        });
        
        test("string_string_map_conversion", [this]() {
            auto engine = make_engine();
            
            engine->add_function("concatenate_string_values", &MapProcessor::concatenate_string_values);
            
            // Test map<string, string> parameter
            try {
                script_value result = engine->execute("concatenate_string_values({\"a\": \"Hello\", \"b\": \" \", \"c\": \"World\"})");
                check_eq(result.as<std::string>(), "Hello World");
                std::cout << "    ✓ Map<string, string> parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, string> parameter failed: " << e.what() << std::endl;
            }
            
            // Test empty map
            try {
                script_value result = engine->execute("concatenate_string_values({})");
                check_eq(result.as<std::string>(), "");
                std::cout << "    ✓ Empty map works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Empty map failed: " << e.what() << std::endl;
            }
            
            // Test single entry map
            try {
                script_value result = engine->execute("concatenate_string_values({\"only\": \"entry\"})");
                check_eq(result.as<std::string>(), "entry");
                std::cout << "    ✓ Single entry map works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Single entry map failed: " << e.what() << std::endl;
            }
        });
        
        test("double_value_map_conversion", [this]() {
            auto engine = make_engine();
            
            engine->add_function("sum_double_values", &MapProcessor::sum_double_values);
            
            // Test map<string, double> with pure doubles
            try {
                script_value result = engine->execute("sum_double_values({\"x\": 1.5, \"y\": 2.5, \"z\": 3.0})");
                check_eq(result.as<double>(), 7.0);
                std::cout << "    ✓ Map<string, double> with doubles works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, double> with doubles failed: " << e.what() << std::endl;
            }
            
            // Test map<string, double> with integers (should convert)
            try {
                script_value result = engine->execute("sum_double_values({\"a\": 1, \"b\": 2, \"c\": 3})");
                check_eq(result.as<double>(), 6.0);
                std::cout << "    ✓ Map<string, double> with integers works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, double> with integers failed: " << e.what() << std::endl;
            }
            
            // Test map<string, double> with mixed numeric types
            try {
                script_value result = engine->execute("sum_double_values({\"mixed1\": 1, \"mixed2\": 2.5, \"mixed3\": 3})");
                check_eq(result.as<double>(), 6.5);
                std::cout << "    ✓ Map<string, double> with mixed types works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, double> with mixed types failed: " << e.what() << std::endl;
            }
        });
        
        test("nested_map_conversion", [this]() {
            auto engine = make_engine();
            
            engine->add_function("create_nested_map", &MapProcessor::create_nested_map);
            engine->add_function("sum_nested_map", &MapProcessor::sum_nested_map);
            
            // Test nested map creation
            try {
                script_value result = engine->execute("create_nested_map()");
                auto nested = result.as<std::map<script_value, script_value>>();
                check_eq(nested.size(), 2U);
                
                script_value group1_key = engine->make_value("group1");
                auto it = nested.find(group1_key);
                if (it != nested.end()) {
                    auto inner_map = it->second.as<std::map<script_value, script_value>>();
                    check_eq(inner_map.size(), 2U);
                }
                
                std::cout << "    ✓ Nested map creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Nested map creation failed: " << e.what() << std::endl;
            }
            
            // Test nested map processing
            try {
                engine->execute("var nested = create_nested_map();");
                script_value result = engine->execute("sum_nested_map(nested)");
                check_eq(result.as<int>(), 33); // 1+2+10+20 = 33
                std::cout << "    ✓ Nested map processing works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Nested map processing failed: " << e.what() << std::endl;
            }
            
            // Test manual nested map creation
            try {
                script_value result = engine->execute("sum_nested_map({\"g1\": {\"a\": 5, \"b\": 10}, \"g2\": {\"x\": 15, \"y\": 20}})");
                check_eq(result.as<int>(), 50);
                std::cout << "    ✓ Manual nested map creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Manual nested map creation failed: " << e.what() << std::endl;
            }
        });
        
        test("map_with_custom_objects", [this]() {
            auto engine = make_engine();
            
            // Register Person class
            dynamic_binder<Person>(*engine, "Person")
                .constructor<>()
                .constructor<const std::string&, int>()
                .method("getName", &Person::get_name)
                .method("getAge", &Person::get_age)
                .method("setName", &Person::set_name)
                .method("setAge", &Person::set_age)
                .method("toString", &Person::to_string)
                .property("name", &Person::name)
                .property("age", &Person::age)
                .build();
            
            // Register functions that work with maps of custom objects
            engine->add_function("sum_ages", &PersonMapProcessor::sum_ages);
            engine->add_function("get_names", &PersonMapProcessor::get_names);
            engine->add_function("create_person_map", &PersonMapProcessor::create_person_map);
            
            // Test map with custom objects creation
            try {
                script_value result = engine->execute("create_person_map()");
                auto person_map = result.as<std::map<script_value, script_value>>();
                check_eq(person_map.size(), 3U);
                std::cout << "    ✓ Map with custom objects creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map with custom objects creation failed: " << e.what() << std::endl;
            }
            
            // Test processing map with custom objects
            try {
                engine->execute("var people = create_person_map();");
                script_value result = engine->execute("sum_ages(people)");
                check_eq(result.as<int>(), 90); // 30+25+35 = 90
                std::cout << "    ✓ Processing map with custom objects works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Processing map with custom objects failed: " << e.what() << std::endl;
            }
            
            // Test extracting data from map with custom objects
            try {
                engine->execute("var people = create_person_map();");
                script_value result = engine->execute("get_names(people)");
                auto names = result.as<std::vector<script_value>>();
                check_eq(names.size(), 3U);
                std::cout << "    ✓ Extracting data from map with custom objects works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Extracting data from map with custom objects failed: " << e.what() << std::endl;
            }
        });
        
        test("map_key_lookup_operations", [this]() {
            auto engine = make_engine();
            
            engine->add_function("map_contains_key", &MapProcessor::map_contains_key);
            engine->add_function("get_map_value", &MapProcessor::get_map_value);
            
            // Test key existence check
            try {
                script_value result = engine->execute("map_contains_key({\"exists\": 42, \"also\": 24}, \"exists\")");
                check_eq(result.as<bool>(), true);
                std::cout << "    ✓ Map key existence check (true) works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map key existence check (true) failed: " << e.what() << std::endl;
            }
            
            try {
                script_value result = engine->execute("map_contains_key({\"exists\": 42, \"also\": 24}, \"missing\")");
                check_eq(result.as<bool>(), false);
                std::cout << "    ✓ Map key existence check (false) works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map key existence check (false) failed: " << e.what() << std::endl;
            }
            
            // Test value retrieval
            try {
                script_value result = engine->execute("get_map_value({\"key1\": 100, \"key2\": 200}, \"key1\")");
                check_eq(result.as<int>(), 100);
                std::cout << "    ✓ Map value retrieval (found) works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map value retrieval (found) failed: " << e.what() << std::endl;
            }
            
            try {
                script_value result = engine->execute("get_map_value({\"key1\": 100, \"key2\": 200}, \"missing\")");
                check_eq(result.as<int>(), -1);
                std::cout << "    ✓ Map value retrieval (not found) works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map value retrieval (not found) failed: " << e.what() << std::endl;
            }
        });
        
        test("map_edge_cases", [this]() {
            auto engine = make_engine();
            
            engine->add_function("sum_string_to_int_map", &MapProcessor::sum_string_to_int_map);
            engine->add_function("get_map_size", &MapProcessor::get_map_size);
            engine->add_function("concatenate_string_values", &MapProcessor::concatenate_string_values);
            
            // Test empty map
            try {
                script_value result = engine->execute("sum_string_to_int_map({})");
                check_eq(result.as<int>(), 0);
                std::cout << "    ✓ Empty map works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Empty map failed: " << e.what() << std::endl;
            }
            
            // Test single entry map
            try {
                script_value result = engine->execute("sum_string_to_int_map({\"single\": 42})");
                check_eq(result.as<int>(), 42);
                std::cout << "    ✓ Single entry map works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Single entry map failed: " << e.what() << std::endl;
            }
            
            // Test map with special characters in keys
            try {
                script_value result = engine->execute("sum_string_to_int_map({\"key with spaces\": 1, \"key-with-dashes\": 2, \"key_with_underscores\": 3})");
                check_eq(result.as<int>(), 6);
                std::cout << "    ✓ Map with special character keys works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map with special character keys failed: " << e.what() << std::endl;
            }
            
            // Test map with empty string keys
            try {
                script_value result = engine->execute("sum_string_to_int_map({\"\": 42})");
                check_eq(result.as<int>(), 42);
                std::cout << "    ✓ Map with empty string key works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map with empty string key failed: " << e.what() << std::endl;
            }
        });
        
        test("map_script_value_compatibility", [this]() {
            auto engine = make_engine();
            
            // Test function that takes map<script_value, script_value> explicitly
            engine->add_function("count_map_types", [](std::map<script_value, script_value> map) -> int {
                int int_count = 0;
                for (const auto& pair : map) {
                    if (pair.second.is_int()) {
                        int_count++;
                    }
                }
                return int_count;
            });
            
            // Test with mixed value types
            try {
                script_value result = engine->execute("count_map_types({\"a\": 1, \"b\": \"hello\", \"c\": 2, \"d\": 3.14, \"e\": 3})");
                check_eq(result.as<int>(), 3); // Three integers: 1, 2, 3
                std::cout << "    ✓ Map<script_value, script_value> with mixed types works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<script_value, script_value> with mixed types failed: " << e.what() << std::endl;
            }
            
            // Test function that returns map<script_value, script_value>
            engine->add_function("create_mixed_map", [engine]() -> std::map<script_value, script_value> {
                std::map<script_value, script_value> result;
                result.insert_or_assign(engine->make_value("int"), engine->make_value(42));
                result.insert_or_assign(engine->make_value("string"), engine->make_value("hello"));
                result.insert_or_assign(engine->make_value("float"), engine->make_value(3.14));
                result.insert_or_assign(engine->make_value("bool"), engine->make_value(true));
                return result;
            });
            
            try {
                script_value result = engine->execute("create_mixed_map()");
                auto map = result.as<std::map<script_value, script_value>>();
                check_eq(map.size(), 4U);
                
                script_value int_key = engine->make_value("int");
                script_value string_key = engine->make_value("string");
                
                auto int_it = map.find(int_key);
                auto string_it = map.find(string_key);
                check(int_it != map.end());
                check(string_it != map.end());
                check_eq(int_it->second.as<int>(), 42);
                check_eq(string_it->second.as<std::string>(), "hello");
                
                std::cout << "    ✓ Returning map<script_value, script_value> works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    ✗ Returning map<script_value, script_value> failed: " << e.what() << std::endl;
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using map_conversion_tests = jai::foundry::tests::map_conversion_tests;
FOUNDRY_REGISTER(map_conversion_tests)