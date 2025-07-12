#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <cmath>

namespace jai::foundry::tests {

class script_class_tests : public suite {
public:
    script_class_tests() : suite("Script Class Tests") {}
    
    void forge_tests() override {
        test("basic_class_with_fields", [this]() {
            auto js_engine = engine::make();
            
            const char* script = R"(
                class Point {
                    int x = 0;
                    int y = 0;
                }
                
                auto p = Point();
                p.x = 10;
                p.y = 20;
                p.x + p.y
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_int());
            check_eq(result.as_int(), 30);
        });
        
        test("class_with_constructor", [this]() {
            auto js_engine = engine::make();
            
            const char* script = R"(
                class Point {
                    int x = 0;
                    int y = 0;
                    
                    Point(int x, int y) {
                        this.x = x;
                        this.y = y;
                    }
                }
                
                auto p = Point(5, 7);
                p.x * p.y
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_int());
            check_eq(result.as_int(), 35);
        });
        
        test("class_with_methods", [this]() {
            auto js_engine = engine::make();
            
            const char* script = R"(
                class Rectangle {
                    int width = 0;
                    int height = 0;
                    
                    Rectangle(int w, int h) {
                        this.width = w;
                        this.height = h;
                    }
                    
                    int area() {
                        return this.width * this.height;
                    }
                    
                    int perimeter() {
                        return 2 * (this.width + this.height);
                    }
                }
                
                auto rect = Rectangle(4, 3);
                rect.area() + rect.perimeter()
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_int());
            check_eq(result.as_int(), 12 + 14); // area=12, perimeter=14
        });
        
        test("script_cpp_interop", [this]() {
            auto js_engine = engine::make();
            
            // Register sqrt function directly  
            js_engine->add_function("sqrt", [](double x) { return std::sqrt(x); });
            
            // Register a C++ class
            struct Vector2D {
                double x, y;
                Vector2D(double x = 0, double y = 0) : x(x), y(y) {}
                double magnitude() const { return std::sqrt(x*x + y*y); }
            };
            
            class_builder<Vector2D>(*js_engine, "Vector2D")
                .constructor<double, double>()
                .property("x", &Vector2D::x)
                .property("y", &Vector2D::y)
                .method("magnitude", &Vector2D::magnitude)
                .build();
            
            const char* script = R"(
                class Point3D {
                    Vector2D position = null;
                    int z = 0;
                    
                    Point3D(double x, double y, int z) {
                        this.position = Vector2D(x, y);
                        this.z = z;
                    }
                    
                    double magnitude3D() {
                        auto mag2D = this.position.magnitude();
                        return sqrt(mag2D * mag2D + this.z * this.z);
                    }
                }
                
                auto p = Point3D(3, 4, 12);
                p.magnitude3D()
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_float());
            check_eq(result.as_float(), 13.0); // sqrt(3²+4²+12²) = sqrt(169) = 13
        });
        
        test("script_inherits_from_cpp", [this]() {
            auto js_engine = engine::make();
            
            // Register a C++ base class
            class Creature {
            public:
                std::string name;
                int health;
                
                Creature(const std::string& n, int h = 100) : name(n), health(h) {}
                
                virtual std::string attack() {
                    return name + " attacks!";
                }
                
                int getHealth() const { return health; }
            };
            
            class_builder<Creature>(*js_engine, "Creature")
                .constructor<std::string>()
                .constructor<std::string, int>()
                .property("name", &Creature::name)
                .property("health", &Creature::health)
                .method("attack", &Creature::attack)
                .method("getHealth", &Creature::getHealth)
                .build();
            
            const char* script = R"(
                // Script class inheriting from C++ class
                class Dragon : Creature {
                    int firepower = 50;
                    
                    Dragon(std::string name) : super(name, 200) {
                        // Dragon constructor
                    }
                    
                    std::string attack() override {
                        return super::attack() + " Dragon breathes fire!";
                    }
                    
                    int getDamage() {
                        return this.firepower + 10;
                    }
                }
                
                auto dragon = Dragon("Smaug");
                dragon.getDamage()
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_int());
            check_eq(result.as_int(), 60); // firepower(50) + 10
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using script_class_tests = jai::foundry::tests::script_class_tests;
FOUNDRY_REGISTER(script_class_tests)