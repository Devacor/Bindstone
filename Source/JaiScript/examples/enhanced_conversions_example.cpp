#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/conversion_registry.hpp>
#include <iostream>
#include <vector>
#include <map>

using namespace jai;

// Example custom class
class Vector3D {
public:
    double x, y, z;
    
    Vector3D(double x = 0, double y = 0, double z = 0) : x(x), y(y), z(z) {}
    
    double magnitude() const {
        return std::sqrt(x*x + y*y + z*z);
    }
    
    Vector3D operator+(const Vector3D& other) const {
        return Vector3D(x + other.x, y + other.y, z + other.z);
    }
};

int main() {
    engine engine;
    
    std::cout << "=== JaiScript Enhanced Conversion System Demo ===\n\n";
    
    // 1. Register standard conversions for vectors and maps
    std::cout << "1. Registering standard conversions...\n";
    engine.add_standard_conversions();
    
    // 2. Register functions that take C++ vectors and maps
    std::cout << "2. Registering functions with vector/map parameters...\n";
    
    engine.add_function("sum_numbers", [](const std::vector<int>& numbers) -> int {
        int total = 0;
        for (int n : numbers) total += n;
        return total;
    });
    
    engine.add_function("join_words", [](const std::vector<std::string>& words, const std::string& separator) -> std::string {
        std::string result;
        for (size_t i = 0; i < words.size(); ++i) {
            if (i > 0) result += separator;
            result += words[i];
        }
        return result;
    });
    
    engine.add_function("get_score", [](const std::map<std::string, int>& scores, const std::string& name) -> int {
        auto it = scores.find(name);
        return (it != scores.end()) ? it->second : 0;
    });
    
    engine.add_function("calculate_average", [](const std::vector<double>& values) -> double {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum / values.size();
    });
    
    // 3. Test vector conversions
    std::cout << "3. Testing vector conversions:\n";
    
    try {
        auto result = engine.execute("sum_numbers([1, 2, 3, 4, 5])");
        std::cout << "   Sum of [1,2,3,4,5] = " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    try {
        auto result = engine.execute("join_words([\"Hello\", \"Enhanced\", \"Conversions\"], \" \")");
        std::cout << "   Joined words: \"" << result.as<std::string>() << "\"" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    try {
        // Test mixed int/float to double conversion
        auto result = engine.execute("calculate_average([1, 2.5, 3, 4.5])");
        std::cout << "   Average of mixed types: " << result.as<double>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    // 4. Test map conversions
    std::cout << "4. Testing map conversions:\n";
    
    try {
        auto result = engine.execute("get_score({\"Alice\": 95, \"Bob\": 87, \"Charlie\": 92}, \"Alice\")");
        std::cout << "   Alice's score: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    // 5. Register custom conversion for Vector3D
    std::cout << "5. Registering custom Vector3D conversion...\n";
    
    engine.add_custom_conversion<Vector3D>(
        // From script array to Vector3D
        [](const script_value& v) -> Vector3D {
            auto arr = v.as<std::vector<script_value>>();
            if (arr.size() != 3) {
                throw std::runtime_error("Vector3D requires exactly 3 components [x, y, z]");
            }
            return Vector3D(
                arr[0].as<double>(),
                arr[1].as<double>(),
                arr[2].as<double>()
            );
        },
        // From Vector3D to script array
        [](const Vector3D& vec) -> script_value {
            auto arr = script_value::make_array(nullptr);
            auto& script_arr = const_cast<std::vector<script_value>&>(arr.as_array());
            script_arr.push_back(script_value(vec.x));
            script_arr.push_back(script_value(vec.y));
            script_arr.push_back(script_value(vec.z));
            return arr;
        }
    );
    
    engine.add_function("vector_magnitude", [](const Vector3D& vec) -> double {
        return vec.magnitude();
    });
    
    engine.add_function("add_vectors", [](const Vector3D& a, const Vector3D& b) -> Vector3D {
        return a + b;
    });
    
    engine.add_function("sum_vector_magnitudes", [](const std::vector<Vector3D>& vectors) -> double {
        double total = 0.0;
        for (const auto& vec : vectors) {
            total += vec.magnitude();
        }
        return total;
    });
    
    // 6. Test custom conversions
    std::cout << "6. Testing custom Vector3D conversions:\n";
    
    try {
        auto result = engine.execute("vector_magnitude([3.0, 4.0, 0.0])");
        std::cout << "   Magnitude of [3,4,0]: " << result.as<double>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    try {
        auto result = engine.execute("add_vectors([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])");
        auto arr = result.as<std::vector<script_value>>();
        std::cout << "   Vector addition result: [" 
                  << arr[0].as<double>() << ", " 
                  << arr[1].as<double>() << ", " 
                  << arr[2].as<double>() << "]" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    try {
        auto result = engine.execute("sum_vector_magnitudes([[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [3.0, 4.0, 0.0]])");
        std::cout << "   Sum of vector magnitudes: " << result.as<double>() << std::endl;  // 1 + 1 + 5 = 7
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    // 7. Test bounds checking
    std::cout << "7. Testing int64_t -> int bounds checking:\n";
    
    engine.add_function("test_int_bounds", [](const std::vector<int>& numbers) -> int {
        return static_cast<int>(numbers.size());
    });
    
    try {
        // This should work (within int range)
        auto result = engine.execute("test_int_bounds([2147483647])");  // INT_MAX
        std::cout << "   INT_MAX works: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "   Error: " << e.what() << std::endl;
    }
    
    try {
        // This should fail (out of int range)
        engine.execute("test_int_bounds([9223372036854775807])");  // INT64_MAX
        std::cout << "   ERROR: INT64_MAX should have failed!\n";
    } catch (const std::exception& e) {
        std::cout << "   ✓ Bounds checking works: " << e.what() << std::endl;
    }
    
    std::cout << "\n=== Demo Complete! ===\n";
    
    return 0;
}