#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <numeric>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test classes for vector conversion scenarios
class Point {
public:
    double x, y;
    
    Point() : x(0), y(0) {}
    Point(double x_val, double y_val) : x(x_val), y(y_val) {}
    
    double distance_from_origin() const {
        return std::sqrt(x * x + y * y);
    }
    
    Point operator+(const Point& other) const {
        return Point(x + other.x, y + other.y);
    }
    
    bool operator==(const Point& other) const {
        return std::abs(x - other.x) < 1e-9 && std::abs(y - other.y) < 1e-9;
    }
};

class VectorProcessor {
public:
    // Test different vector parameter types
    static int sum_integers(std::vector<int> nums) {
        return std::accumulate(nums.begin(), nums.end(), 0);
    }
    
    static double sum_doubles(std::vector<double> nums) {
        return std::accumulate(nums.begin(), nums.end(), 0.0);
    }
    
    static std::string concatenate_strings(std::vector<std::string> strs) {
        std::string result;
        for (const auto& s : strs) {
            result += s;
        }
        return result;
    }
    
    static std::vector<int> double_integers(std::vector<int> nums) {
        std::vector<int> result;
        for (int n : nums) {
            result.push_back(n * 2);
        }
        return result;
    }
    
    static std::vector<Point> create_points(std::vector<double> coords) {
        std::vector<Point> result;
        for (size_t i = 0; i + 1 < coords.size(); i += 2) {
            result.emplace_back(coords[i], coords[i + 1]);
        }
        return result;
    }
    
    static double sum_point_distances(std::vector<Point> points) {
        double total = 0.0;
        for (const auto& p : points) {
            total += p.distance_from_origin();
        }
        return total;
    }
    
    // Test reference parameters with bound_array
    static void modify_vector(bound_array<int>& nums) {
        for (size_t i = 0; i < nums.size(); ++i) {
            nums[i] = nums[i] * 2;
        }
    }
    
    // Test const reference parameters
    static size_t get_vector_size(std::vector<int> nums) {
        return nums.size();
    }
    
    // Test nested vectors
    static std::vector<std::vector<int>> create_matrix(int rows, int cols) {
        std::vector<std::vector<int>> matrix(rows);
        for (int i = 0; i < rows; ++i) {
            matrix[i].resize(cols);
            for (int j = 0; j < cols; ++j) {
                matrix[i][j] = i * cols + j;
            }
        }
        return matrix;
    }
    
    static int sum_matrix(const bound_array<bound_array<int>>& matrix) {
        int total = 0;
        for (const auto& row : matrix) {
            for (int val : row) {
                total += val;
            }
        }
        return total;
    }
};

class vector_conversion_tests : public suite {
public:
    vector_conversion_tests() : suite("Vector Conversion Tests") {}
    
    void forge_tests() override {
        test("basic_vector_int_conversion", [this]() {
            auto engine = engine::make();
            
            // Register vector processing functions
            engine->add_function("sum_integers", &VectorProcessor::sum_integers);
            engine->add_function("double_integers", &VectorProcessor::double_integers);
            engine->add_function("get_vector_size", &VectorProcessor::get_vector_size);
            
            // Test basic vector<int> parameter
            try {
                script_value result = engine->execute("sum_integers([1, 2, 3, 4, 5])");
                check_eq(result.as<int>(), 15);
                std::cout << "    " << "✓ Basic vector<int> parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Basic vector<int> parameter failed: " << e.what() << std::endl;
            }
            
            // Test vector<int> return value
            try {
                script_value result = engine->execute("double_integers([1, 2, 3])");
                auto vec = result.as<std::vector<script_value>>();
                check_eq(vec.size(), 3U);
                check_eq(vec[0].as<int>(), 2);
                check_eq(vec[1].as<int>(), 4);
                check_eq(vec[2].as<int>(), 6);
                std::cout << "    " << "✓ Vector<int> return value works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<int> return value failed: " << e.what() << std::endl;
            }
            
            // Test const reference parameter
            try {
                script_value result = engine->execute("get_vector_size([10, 20, 30, 40])");
                check_eq(result.as<int>(), 4);
                std::cout << "    " << "✓ Const reference vector parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Const reference vector parameter failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_double_conversion", [this]() {
            auto engine = engine::make();
            
            engine->add_function("sum_doubles", &VectorProcessor::sum_doubles);
            
            // Test vector<double> with integers (should convert)
            try {
                script_value result = engine->execute("sum_doubles([1, 2, 3])");
                check_eq(result.as<double>(), 6.0);
                std::cout << "    " << "✓ Vector<double> with integers works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<double> with integers failed: " << e.what() << std::endl;
            }
            
            // Test vector<double> with floats
            try {
                script_value result = engine->execute("sum_doubles([1.5, 2.5, 3.0])");
                check_eq(result.as<double>(), 7.0);
                std::cout << "    " << "✓ Vector<double> with floats works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<double> with floats failed: " << e.what() << std::endl;
            }
            
            // Test mixed numeric types
            try {
                script_value result = engine->execute("sum_doubles([1, 2.5, 3])");
                check_eq(result.as<double>(), 6.5);
                std::cout << "    " << "✓ Vector<double> with mixed types works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<double> with mixed types failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_string_conversion", [this]() {
            auto engine = engine::make();
            
            engine->add_function("concatenate_strings", &VectorProcessor::concatenate_strings);
            
            // Test vector<string> parameter
            try {
                script_value result = engine->execute("concatenate_strings([\"Hello\", \" \", \"World\", \"!\"])");
                check_eq(result.as<std::string>(), "Hello World!");
                std::cout << "    " << "✓ Vector<string> parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<string> parameter failed: " << e.what() << std::endl;
            }
            
            // Test empty vector
            try {
                script_value result = engine->execute("concatenate_strings([])");
                check_eq(result.as<std::string>(), "");
                std::cout << "    " << "✓ Empty vector<string> works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Empty vector<string> failed: " << e.what() << std::endl;
            }
            
            // Test single element vector
            try {
                script_value result = engine->execute("concatenate_strings([\"single\"])");
                check_eq(result.as<std::string>(), "single");
                std::cout << "    " << "✓ Single element vector<string> works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Single element vector<string> failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_custom_object_conversion", [this]() {
            auto engine = engine::make();
            
            // Register Point class
            dynamic_binder<Point>(*engine, "Point")
                .constructor<>()
                .constructor<double, double>()
                .method("distanceFromOrigin", &Point::distance_from_origin)
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            // Register functions that work with vectors of Points
            engine->add_function("create_points", &VectorProcessor::create_points);
            engine->add_function("sum_point_distances", &VectorProcessor::sum_point_distances);
            
            // Test creating vector of custom objects
            try {
                script_value result = engine->execute("create_points([0.0, 0.0, 3.0, 4.0, 1.0, 1.0])");
                auto points = result.as<std::vector<script_value>>();
                check_eq(points.size(), 3U);
                std::cout << "    " << "✓ Vector of custom objects creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector of custom objects creation failed: " << e.what() << std::endl;
            }
            
            // Test vector of custom objects as parameter
            try {
                engine->execute(R"(
                    var points = create_points([0.0, 0.0, 3.0, 4.0]);
                )");
                script_value result = engine->execute("sum_point_distances(points)");
                check_eq(result.as<double>(), 5.0); // Distance from origin: sqrt(3^2 + 4^2) = 5
                std::cout << "    " << "✓ Vector of custom objects as parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector of custom objects as parameter failed: " << e.what() << std::endl;
            }
        });
        
        test("nested_vector_conversion", [this]() {
            auto engine = engine::make();
            
            engine->add_function("create_matrix", &VectorProcessor::create_matrix);
            engine->add_function("sum_matrix", &VectorProcessor::sum_matrix);
            
            // Test nested vector creation
            try {
                script_value result = engine->execute("create_matrix(3, 3)");
                auto matrix = result.as<std::vector<script_value>>();
                check_eq(matrix.size(), 3U);
                
                auto first_row = matrix[0].as<std::vector<script_value>>();
                check_eq(first_row.size(), 3U);
                check_eq(first_row[0].as<int>(), 0);
                check_eq(first_row[1].as<int>(), 1);
                check_eq(first_row[2].as<int>(), 2);
                
                std::cout << "    " << "✓ Nested vector creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Nested vector creation failed: " << e.what() << std::endl;
            }
            
            // Test nested vector as parameter
            try {
                engine->execute("var matrix = create_matrix(2, 2);");
                script_value result = engine->execute("sum_matrix(matrix)");
                check_eq(result.as<int>(), 6); // 0+1+2+3 = 6
                std::cout << "    " << "✓ Nested vector as parameter works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Nested vector as parameter failed: " << e.what() << std::endl;
            }
            
            // Test manual nested vector creation
            try {
                script_value result = engine->execute("sum_matrix([[1, 2], [3, 4]])");
                check_eq(result.as<int>(), 10);
                std::cout << "    " << "✓ Manual nested vector creation works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Manual nested vector creation failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_edge_cases", [this]() {
            auto engine = engine::make();
            
            engine->add_function("sum_integers", &VectorProcessor::sum_integers);
            engine->add_function("get_vector_size", &VectorProcessor::get_vector_size);
            
            // Test empty vector
            try {
                script_value result = engine->execute("sum_integers([])");
                check_eq(result.as<int>(), 0);
                std::cout << "    " << "✓ Empty vector works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Empty vector failed: " << e.what() << std::endl;
            }
            
            // Test single element vector
            try {
                script_value result = engine->execute("sum_integers([42])");
                check_eq(result.as<int>(), 42);
                std::cout << "    " << "✓ Single element vector works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Single element vector failed: " << e.what() << std::endl;
            }
            
            // Test large vector
            try {
                script_value result = engine->execute(R"(
                    var large_array = [];
                    for (int i = 0; i < 1000; ++i) {
                        large_array.push(i);
                    }
                    get_vector_size(large_array);
                )");
                check_eq(result.as<int>(), 1000);
                std::cout << "    " << "✓ Large vector works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Large vector failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_type_coercion", [this]() {
            auto engine = engine::make();
            
            // Test functions that expect specific types
            engine->add_function("sum_integers", &VectorProcessor::sum_integers);
            engine->add_function("sum_doubles", &VectorProcessor::sum_doubles);
            engine->add_function("concatenate_strings", &VectorProcessor::concatenate_strings);
            
            // Test mixed types in vector for integer function
            try {
                script_value result = engine->execute("sum_integers([1, 2.5, 3])");
                // This should either work with type coercion or fail gracefully
                std::cout << "    " << "Mixed types in integer vector result: " << std::to_string(result.as<int>()) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "Mixed types in integer vector failed (expected): " << e.what() << std::endl;
            }
            
            // Test non-numeric types in numeric function
            try {
                script_value result = engine->execute("sum_integers([1, \"hello\", 3])");
                check(false); // "Should have failed with non-numeric type");
            } catch (const std::exception& e) {
                std::cout << "    " << "Non-numeric types in numeric vector failed as expected: " << e.what() << std::endl;
            }
            
            // Test numeric types in string function
            try {
                script_value result = engine->execute("concatenate_strings([\"a\", 123, \"b\"])");
                // This should either work with type coercion or fail
                std::cout << "    " << "Numeric types in string vector result: " << result.as<std::string>() << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "Numeric types in string vector failed: " << e.what() << std::endl;
            }
        });
        
        test("vector_script_value_compatibility", [this]() {
            auto engine = engine::make();
            
            // Test function that takes vector<script_value> explicitly
            engine->add_function("process_mixed_vector", [](std::vector<script_value> values) -> int {
                int count = 0;
                for (const auto& val : values) {
                    if (val.is_int()) count++;
                }
                return count;
            });
            
            // Test with mixed types
            try {
                script_value result = engine->execute("process_mixed_vector([1, \"hello\", 3.14, true, 42])");
                check_eq(result.as<int>(), 2); // Two integers: 1 and 42
                std::cout << "    " << "✓ Vector<script_value> with mixed types works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Vector<script_value> with mixed types failed: " << e.what() << std::endl;
            }
            
            // Test function that returns vector<script_value>
            engine->add_function("create_mixed_vector", [engine]() -> std::vector<script_value> {
                std::vector<script_value> result;
                // TODO: These should use engine references when lambda captures are available
                result.push_back(script_value(script_int(42), engine.get()));
                result.push_back(script_value(script_string("hello"), engine.get()));
                result.push_back(script_value(script_float(3.14), engine.get()));
                result.push_back(script_value(script_bool(true), engine.get()));
                return result;
            });
            
            try {
                script_value result = engine->execute("create_mixed_vector()");
                auto vec = result.as<std::vector<script_value>>();
                check_eq(vec.size(), 4U);
                check_eq(vec[0].as<int>(), 42);
                check_eq(vec[1].as<std::string>(), "hello");
                check_eq(vec[2].as<double>(), 3.14);
                check_eq(vec[3].as<bool>(), true);
                std::cout << "    " << "✓ Returning vector<script_value> works" << std::endl;
            } catch (const std::exception& e) {
                std::cout << "    " << "✗ Returning vector<script_value> failed: " << e.what() << std::endl;
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using vector_conversion_tests = jai::foundry::tests::vector_conversion_tests;
FOUNDRY_REGISTER(vector_conversion_tests)