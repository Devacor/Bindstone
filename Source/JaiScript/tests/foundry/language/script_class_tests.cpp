#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
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

                    Dragon(string name) : super(name, 200) {
                        // Dragon constructor
                    }

                    string attack() override {
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
        
        test("class_destructor_basic", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);
            
            // Add a counter to track destructor calls
            int destructor_count = 0;

            // Use getter/setter functions instead of direct variable binding
            js_engine->add_function("get_destructor_count", [&destructor_count]() { return destructor_count; });
            js_engine->add_function("inc_destructor_count", [&destructor_count]() { destructor_count++; });
            
            const char* script = R"(
                class Resource {
                    string name = "";
                    
                    Resource(string n) {
                        name = n;
                        print("Resource " + name + " created");
                    }
                    
                    ~Resource() {
                        print("Resource " + name + " destroyed");
                        inc_destructor_count();
                    }
                }

                // Test 1: Simple destruction when variable goes out of scope
                {
                    auto r1 = Resource("file1");
                    auto r2 = Resource("file2");
                }

                // Check that both destructors were called
                auto count_after_scope = get_destructor_count();

                // Test 2: Destruction on reassignment
                auto r = Resource("temp");
                r = Resource("replacement"); // Should destroy "temp"
                auto count_after_reassign = get_destructor_count();

                // Test 3: Destruction on null assignment
                r = null; // Should destroy "replacement"
                auto count_after_null = get_destructor_count();
                
                [count_after_scope, count_after_reassign, count_after_null]
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_int(), 2); // Two destructors after scope
            check_eq(arr[1].as_int(), 3); // One more after reassignment
            check_eq(arr[2].as_int(), 4); // One more after null assignment
        });
        
        test("class_destructor_polymorphic", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);
            
            // Counters for different destructor types
            static int base_destructor_count = 0;
            static int derived_destructor_count = 0;
            base_destructor_count = 0;
            derived_destructor_count = 0;
            
            js_engine->add_global_ref("base_destructor_count", base_destructor_count);
            js_engine->add_global_ref("derived_destructor_count", derived_destructor_count);
            
            const char* script = R"(
                class BaseResource {
                    string type = "base";
                    
                    BaseResource() {
                        print("BaseResource created");
                    }
                    
                    ~BaseResource() {
                        print("BaseResource destroyed");
                        base_destructor_count = base_destructor_count + 1;
                    }
                }
                
                class FileResource : BaseResource {
                    string filename = "";
                    
                    FileResource(string name) : super() {
                        type = "file";
                        filename = name;
                        print("FileResource " + filename + " created");
                    }
                    
                    ~FileResource() {
                        print("FileResource " + filename + " destroyed");
                        derived_destructor_count = derived_destructor_count + 1;
                        // Note: Base destructor should be called automatically after this
                    }
                }
                
                // Test polymorphic destruction through base pointer
                {
                    auto base_ref = BaseResource();
                    auto file_ref = FileResource("data.txt");
                    
                    // Store derived as base type
                    auto polymorphic = file_ref;
                    polymorphic = null; // Should call derived destructor then base
                    
                    // Check counts mid-test
                    auto mid_base = base_destructor_count;
                    auto mid_derived = derived_destructor_count;
                    
                    // These will be destroyed at scope end
                }
                
                auto final_base = base_destructor_count;
                auto final_derived = derived_destructor_count;
                
                [mid_base, mid_derived, final_base, final_derived]
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 4);
            
            // Mid-test: polymorphic destruction should have called both destructors
            check_eq(arr[0].as_int(), 1); // Base destructor called once
            check_eq(arr[1].as_int(), 1); // Derived destructor called once
            
            // Final: all objects destroyed (base_ref and file_ref from scope)
            check_eq(arr[2].as_int(), 3); // Total 3 base destructors (1 polymorphic + 2 scope)
            check_eq(arr[3].as_int(), 2); // Total 2 derived destructors (1 polymorphic + 1 scope)
        });
        
        // TODO: Enable this test once destructor support is fully implemented
        /*
        test("class_destructor_in_containers", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);
            
            static int object_count = 0;
            object_count = 0;
            js_engine->add_global("object_count", js_engine->make_value(&object_count));
            
            const char* script = R"(
                class CountedObject {
                    int id = 0;
                    
                    CountedObject(int i) {
                        id = i;
                        object_count = object_count + 1;
                        print("Object " + to_string(id) + " created (count=" + to_string(object_count) + ")");
                    }
                    
                    ~CountedObject() {
                        object_count = object_count - 1;
                        print("Object " + to_string(id) + " destroyed (count=" + to_string(object_count) + ")");
                    }
                }
                
                // Test destruction in arrays
                auto initial_count = object_count; // Should be 0
                
                {
                    auto objects = [
                        CountedObject(1),
                        CountedObject(2),
                        CountedObject(3)
                    ];
                    auto count_with_array = object_count; // Should be 3
                    
                    // Clear array
                    objects = [];
                    auto count_after_clear = object_count; // Should be 0
                }
                
                // Test destruction in maps
                {
                    auto object_map = {
                        "first": CountedObject(10),
                        "second": CountedObject(20)
                    };
                    auto count_with_map = object_count; // Should be 2
                }
                
                auto final_count = object_count; // Should be 0
                
                [initial_count, count_with_array, count_after_clear, count_with_map, final_count]
            )";
            
            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 5);
            
            check_eq(arr[0].as_int(), 0); // Initial count
            check_eq(arr[1].as_int(), 3); // 3 objects in array
            check_eq(arr[2].as_int(), 0); // All destroyed after clear
            check_eq(arr[3].as_int(), 2); // 2 objects in map
            check_eq(arr[4].as_int(), 0); // All destroyed after scope
        });
        */
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using script_class_tests = jai::foundry::tests::script_class_tests;
FOUNDRY_REGISTER(script_class_tests)