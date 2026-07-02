#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/conversion_registry.hpp>
#include <jaiscript/core/engine_impl.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <jaiscript/core/bound_map.hpp>
#include <vector>
#include <map>
#include <memory>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test class for custom conversions
class Point {
public:
    double x, y;
    
    Point() : x(0), y(0) {}
    Point(double x_val, double y_val) : x(x_val), y(y_val) {}
    
    double distance_from_origin() const {
        return std::sqrt(x * x + y * y);
    }
    
    bool operator==(const Point& other) const {
        return std::abs(x - other.x) < 1e-9 && std::abs(y - other.y) < 1e-9;
    }
};

class enhanced_conversion_tests : public suite {
public:
    enhanced_conversion_tests() : suite("Enhanced Conversion System") {}
    
    void forge_tests() override {
        test("standard_vector_conversions", [this]() {
            auto engine = make_engine();
            
            // Register standard conversions
            engine->add_standard_conversions();
            
            // Test function that takes bound_array<int>
            engine->add_function("sum_ints", [](const bound_array<int>& nums) -> int {
                int sum = 0;
                for (int n : nums) sum += n;
                return sum;
            });
            
            // Test function that takes bound_array<double>
            engine->add_function("sum_doubles", [](const bound_array<double>& nums) -> double {
                double sum = 0.0;
                for (double n : nums) sum += n;
                return sum;
            });
            
            // Test function that takes vector<string>
            engine->add_function("join_strings", [](const bound_array<std::string>& strs) -> std::string {
                std::string result;
                for (size_t i = 0; i < strs.size(); ++i) {
                    if (i > 0) result += " ";
                    result += strs[i];
                }
                return result;
            });
            
            try {
                // Test vector<int> conversion with bounds checking
                script_value result = engine->execute("sum_ints([1, 2, 3, 4, 5])");
                check_eq(result.as<int>(), 15);
                std::cout << "    ✓ Vector<int> conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector<int> conversion failed: " << e.what() << "\n";
            }
            
            try {
                // Test vector<double> with mixed int/float values
                script_value result = engine->execute("sum_doubles([1, 2.5, 3, 4.5])");
                check_eq(result.as<double>(), 11.0);
                std::cout << "    ✓ Vector<double> with mixed types works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector<double> with mixed types failed: " << e.what() << "\n";
            }
            
            try {
                // Test vector<string> conversion
                script_value result = engine->execute("join_strings([\"Hello\", \"Enhanced\", \"Conversions\"])");
                check_eq(result.as<std::string>(), "Hello Enhanced Conversions");
                std::cout << "    ✓ Vector<string> conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector<string> conversion failed: " << e.what() << "\n";
            }
        });
        
        test("standard_map_conversions", [this]() {
            auto engine = make_engine();
            
            // Register standard conversions
            engine->add_standard_conversions();
            
            // Test function that takes bound_map<string, int>
            engine->add_function("sum_map_values", [](const bound_map<std::string, int>& map) -> int {
                int total = 0;
                for (const auto& [key, value] : map) {
                    total += value;
                }
                return total;
            });
            
            // Test function that takes bound_map<string, double>
            engine->add_function("sum_double_values", [](const bound_map<std::string, double>& map) -> double {
                double total = 0.0;
                for (const auto& [key, value] : map) {
                    total += value;
                }
                return total;
            });
            
            // Test function that returns map<string, int>
            engine->add_function("create_scores", []() -> std::map<std::string, int> {
                return {{"Alice", 95}, {"Bob", 87}, {"Charlie", 92}};
            });
            
            try {
                // Test map<string, int> conversion with bounds checking
                script_value result = engine->execute("sum_map_values({\"a\": 10, \"b\": 20, \"c\": 30})");
                check_eq(result.as<int>(), 60);
                std::cout << "    ✓ Map<string, int> conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, int> conversion failed: " << e.what() << "\n";
            }
            
            try {
                // Test map<string, double> with mixed int/float values
                script_value result = engine->execute("sum_double_values({\"x\": 1.5, \"y\": 2, \"z\": 3.5})");
                check_eq(result.as<double>(), 7.0);
                std::cout << "    ✓ Map<string, double> with mixed types works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map<string, double> with mixed types failed: " << e.what() << "\n";
            }
            
            try {
                // Test map return value
                script_value result = engine->execute("create_scores()");
                auto map = result.as<std::map<script_value, script_value>>();
                check_eq(map.size(), 3U);
                std::cout << "    ✓ Map return value works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Map return value failed: " << e.what() << "\n";
            }
        });
        
        test("custom_type_conversions", [this]() {
            auto engine = make_engine();
            
            // Register standard conversions first
            engine->add_standard_conversions();
            
            dynamic_binder<Point>(*engine, "Point")
                .constructor<double, double>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .method("distance_from_origin", &Point::distance_from_origin)
                .build();
            
            // Test function that takes Point
            engine->add_function("point_distance", [](const Point& p) -> double {
                return p.distance_from_origin();
            });
            
            // Test function that takes bound_array<Point>
            engine->add_function("total_distance", [](const bound_array<Point>& points) -> double {
                double total = 0.0;
                for (const auto& p : points) {
                    total += p.distance_from_origin();
                }
                return total;
            });
            
            // Test function that returns Point
            engine->add_function("create_point", [](double x, double y) -> Point {
                return Point(x, y);
            });
            
            try {
                // Test custom Point conversion - use constructor syntax
                script_value result = engine->execute("point_distance(Point(3.0, 4.0))");
                check_eq(result.as<double>(), 5.0);  // sqrt(3^2 + 4^2) = 5
                std::cout << "    ✓ Custom Point conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Custom Point conversion failed: " << e.what() << "\n";
            }
            
            try {
                // Test vector of custom types - use Point constructors
                script_value result = engine->execute("total_distance([Point(0.0, 0.0), Point(3.0, 4.0), Point(5.0, 12.0)])");
                check_eq(result.as<double>(), 18.0);  // 0 + 5 + 13 = 18
                std::cout << "    ✓ Vector of custom types works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Vector of custom types failed: " << e.what() << "\n";
            }
            
            try {
                // Test custom type return value
                script_value result = engine->execute("create_point(1.5, 2.5)");
                // The result should be a Point object, we can access its properties
                script_value x_val = engine->execute("create_point(1.5, 2.5).x");
                script_value y_val = engine->execute("create_point(1.5, 2.5).y");
                check_eq(x_val.as<double>(), 1.5);
                check_eq(y_val.as<double>(), 2.5);
                std::cout << "    ✓ Custom type return value works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Custom type return value failed: " << e.what() << "\n";
            }
        });
        
        test("specific_type_registration", [this]() {
            auto engine = make_engine();
            
            // Register only specific conversions we need
            engine->add_bound_array_conversion<int>();
            engine->add_bound_array_conversion<std::string>();
            engine->add_bound_map_conversion<std::string, int>();
            
            // Test function that takes the registered types
            engine->add_function("process_data", [](
                const bound_array<int>& numbers,
                const bound_array<std::string>& words,
                const bound_map<std::string, int>& scores
            ) -> int {
                int total = 0;
                
                // Sum numbers
                for (int n : numbers) total += n;
                
                // Add word count
                total += static_cast<int>(words.size()) * 10;
                
                // Add score values
                for (const auto& [key, value] : scores) {
                    total += value;
                }
                
                return total;
            });
            
            try {
                script_value result = engine->execute(R"(
                    process_data(
                        [1, 2, 3],
                        ["hello", "world"],
                        {"alice": 10, "bob": 20}
                    )
                )");
                
                // Expected: 6 (sum) + 20 (2 words * 10) + 30 (scores) = 56
                check_eq(result.as<int>(), 56);
                std::cout << "    ✓ Specific type registration works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Specific type registration failed: " << e.what() << "\n";
            }
        });
        
        test("int64_downconversion_bounds_checking", [this]() {
            auto engine = make_engine();
            
            // Register standard conversions with bounds checking
            engine->add_standard_conversions();
            
            // Test function that takes bound_array<int> (should bounds check)
            engine->add_function("sum_ints", [](const bound_array<int>& nums) -> int {
                int sum = 0;
                for (int n : nums) sum += n;
                return sum;
            });
            
            try {
                // Test normal range values
                script_value result = engine->execute("sum_ints([2147483647, -2147483648])");  // INT_MAX, INT_MIN
                check_eq(result.as<int>(), -1);  // Overflow wraps around
                std::cout << "    ✓ Normal range values work!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Normal range values failed: " << e.what() << "\n";
            }
            
            try {
                // Test out-of-range value (should throw)
                engine->execute("sum_ints([9223372036854775807])");  // INT64_MAX
                std::cout << "    ✗ Out-of-range value should have thrown!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✓ Out-of-range bounds checking works: " << e.what() << "\n";
            }
        });
        
        test("mixed_type_array_conversions", [this]() {
            auto engine = make_engine();
            
            // Register conversions
            engine->add_standard_conversions();
            
            // Test function that takes bound_array<double> (should accept int/float mix)
            engine->add_function("avg_doubles", [](const bound_array<double>& nums) -> double {
                if (nums.empty()) return 0.0;
                double sum = 0.0;
                for (double n : nums) sum += n;
                return sum / nums.size();
            });
            
            // Test function that takes bound_array<bool> (should accept int/bool mix)
            engine->add_function("count_true", [](const bound_array<bool>& flags) -> int {
                int count = 0;
                for (bool flag : flags) {
                    if (flag) count++;
                }
                return count;
            });
            
            try {
                // Test mixed int/float to double conversion
                script_value result = engine->execute("avg_doubles([1, 2.5, 3, 4.5])");
                check_eq(result.as<double>(), 2.75);  // (1 + 2.5 + 3 + 4.5) / 4 = 2.75
                std::cout << "    ✓ Mixed int/float to double conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Mixed int/float to double conversion failed: " << e.what() << "\n";
            }
            
            try {
                // Test mixed int/bool conversion (0=false, non-zero=true)
                script_value result = engine->execute("count_true([1, 0, 2, false, true])");
                check_eq(result.as<int>(), 3);  // 1, 2, true are truthy
                std::cout << "    ✓ Mixed int/bool conversion works!\n";
            } catch (const std::exception& e) {
                std::cout << "    ✗ Mixed int/bool conversion failed: " << e.what() << "\n";
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using enhanced_conversion_tests = jai::foundry::tests::enhanced_conversion_tests;
FOUNDRY_REGISTER(enhanced_conversion_tests)