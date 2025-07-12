#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class Vec2 {
public:
    float x, y;
    Vec2(float x_ = 0, float y_ = 0) : x(x_), y(y_) {}
    
    Vec2 operator+(const Vec2& other) const {
        return Vec2(x + other.x, y + other.y);
    }
    
    Vec2 operator*(float scalar) const {
        return Vec2(x * scalar, y * scalar);
    }
};

int main() {
    try {
        auto engine = engine::make();
        
        // Register Vec2 class
        class_builder<Vec2>(*engine, "Vec2")
            .constructor<float, float>()
            .property("x", &Vec2::x)
            .property("y", &Vec2::y)
            .build();
            
        // Get the class definition
        auto vec2_def = engine->get_class_definition("Vec2");
        
        std::cout << "Test 1: Basic property access (should work)\n";
        {
            auto result = engine->execute("var v = Vec2(2.0, 3.0); v.x;");
            std::cout << "  SUCCESS: v.x = " << result.as<float>() << "\n\n";
        }
        
        std::cout << "Test 2: Operator returning Vec2 by value (old - crashes)\n";
        engine->add_function("+_old", [](const Vec2& a, const Vec2& b) -> Vec2 {
            return a + b;
        });
        
        try {
            auto result = engine->execute(R"(
                var v1 = Vec2(1.0, 2.0);
                var v2 = Vec2(3.0, 4.0);
                var sum = v1 +_old v2;
                sum.x;
            )");
            std::cout << "  By-value SUCCESS: sum.x = " << result.as<float>() << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "  By-value FAILED: " << e.what() << "\n\n";
        }
        
        std::cout << "Test 3: Operator returning properly wrapped class_instance (new - should work)\n";
        
        // Helper lambda to wrap C++ objects in class_instance
        auto wrap_vec2 = [engine, vec2_def](const Vec2& vec) -> script_value {
            auto instance = vec2_def->create_instance();
            auto cpp_obj = std::make_shared<Vec2>(vec);
            instance->set_field(class_constants::CPP_OBJECT_FIELD, 
                script_value::make_cpp_object("Vec2", cpp_obj, engine));
            return script_value::make_object("Vec2", instance, engine);
        };
        
        // Register operators that return properly wrapped class_instance objects
        engine->add_function("+", [wrap_vec2](const Vec2& a, const Vec2& b) -> script_value {
            return wrap_vec2(a + b);
        });
        
        engine->add_function("*", [wrap_vec2](const Vec2& v, float s) -> script_value {
            return wrap_vec2(v * s);
        });
        
        try {
            auto result = engine->execute(R"(
                var v1 = Vec2(1.0, 2.0);
                var v2 = Vec2(3.0, 4.0);
                var sum = v1 + v2;
                sum.x;
            )");
            std::cout << "  Wrapped SUCCESS: sum.x = " << result.as<float>() << "\n\n";
        } catch (const std::exception& e) {
            std::cout << "  Wrapped FAILED: " << e.what() << "\n\n";
        }
        
        std::cout << "Test 4: Chained operations with wrapped objects\n";
        try {
            auto result = engine->execute(R"(
                var v1 = Vec2(2.0, 3.0);
                var v2 = Vec2(1.0, 1.0);
                var result = (v1 + v2) * 2.0;
                result.x + result.y;
            )");
            std::cout << "  Chained SUCCESS: result = " << result.as<double>() << "\n";
            std::cout << "  Expected: 14.0 ((3,4) * 2 = (6,8), 6+8=14)\n";
        } catch (const std::exception& e) {
            std::cout << "  Chained FAILED: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}