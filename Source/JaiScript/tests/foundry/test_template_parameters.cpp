#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

class template_parameter_tests : public suite {
public:
    template_parameter_tests() : suite("Template Parameter Tests") {}
    
    void forge_tests() override {
        test("array_parameter", [this]() {
            engine eng;
            
            // Function with array<float> parameter
            auto result = eng.execute(R"(
                function sum(array<float>: values) {
                    auto total = 0.0;
                    for (auto i = 0; i < values.size(); i = i + 1) {
                        total = total + values[i];
                    }
                    return total;
                }
                
                auto nums = [1.5, 2.5, 3.0];
                sum(nums)
            )");
            
            check_eq(result.as<script_float>(), script_float(7.0), "Array<float> parameter should work");
        });
        
        test("map_parameter", [this]() {
            engine eng;
            
            // Function with map<string, int> parameter
            auto result = eng.execute(R"(
                function getAge(map<string, int>: people, string: name) {
                    if (people.contains(name)) {
                        return people[name];
                    }
                    return -1;
                }
                
                auto ages = {{"Alice", 25}, {"Bob", 30}};
                getAge(ages, "Bob")
            )");
            
            check_eq(result.as<script_int>(), script_int(30), "Map<string, int> parameter should work");
        });
        
        test("auto_in_template", [this]() {
            engine eng;
            
            // Function with map<string, auto> parameter
            auto result = eng.execute(R"(
                function getValue(map<string, auto>: data, string: key) {
                    if (data.contains(key)) {
                        return data[key];
                    }
                    return null;
                }
                
                auto mixed = {{"count", 42}, {"name", "test"}, {"pi", 3.14}};
                getValue(mixed, "count")
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Map<string, auto> parameter should work");
        });
        
        test("nested_templates", [this]() {
            engine eng;
            
            // Function with array<array<int>> parameter
            auto result = eng.execute(R"(
                function sumMatrix(array<array<int>>: matrix) {
                    auto total = 0;
                    for (auto i = 0; i < matrix.size(); i = i + 1) {
                        for (auto j = 0; j < matrix[i].size(); j = j + 1) {
                            total = total + matrix[i][j];
                        }
                    }
                    return total;
                }
                
                auto row1 = [1, 2, 3];
                auto row2 = [4, 5, 6];
                auto matrix = [row1, row2];
                
                sumMatrix(matrix)
            )");
            
            check_eq(result.as<script_int>(), script_int(21), "Nested template parameters should work");
        });
        
        test("custom_type_template", [this]() {
            engine eng;
            
            // Register a simple Point class
            auto result = eng.execute(R"(
                // For now, test with built-in types since we don't have custom classes yet
                // This would be Point<float> in the future
                function distance(array<float>: point1, array<float>: point2) {
                    auto dx = point2[0] - point1[0];
                    auto dy = point2[1] - point1[1];
                    return (dx * dx + dy * dy);  // squared distance
                }
                
                auto p1 = [0.0, 0.0];
                auto p2 = [3.0, 4.0];
                
                distance(p1, p2)
            )");
            
            check_eq(result.as<script_float>(), script_float(25.0), "Template type parameters should work");
        });
        
        test("shared_ptr_parameter", [this]() {
            engine eng;
            
            // Test that template syntax is parsed correctly
            try {
                eng.execute(R"(
                    // Just test that the parameter syntax parses
                    function processPtr(shared_ptr<array<int>>: arr) {
                        return 42;
                    }
                    
                    // For now, just return a value to test parsing
                    42
                )");
                
                // If we get here, the syntax parsed correctly
                check(true, "shared_ptr template parameter syntax should parse");
            } catch (const std::exception& e) {
                std::cout << "    Note: shared_ptr parameters not yet fully supported: " << e.what() << "\n";
                check(false, "shared_ptr template parameter syntax failed to parse");
            }
        });
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(template_parameter_tests)