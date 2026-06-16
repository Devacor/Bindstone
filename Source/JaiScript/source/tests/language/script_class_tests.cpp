#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
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
            
            js_engine->add_function("sqrt", [](double x) { return std::sqrt(x); });

            struct Vector2D {
                double x, y;
                Vector2D(double x = 0, double y = 0) : x(x), y(y) {}
                double magnitude() const { return std::sqrt(x*x + y*y); }
            };
            
            dynamic_binder<Vector2D>(*js_engine, "Vector2D")
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
            
            dynamic_binder<Creature>(*js_engine, "Creature")
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

            std::cout << "Destructor counts: [" << arr[0].as_int() << ", " << arr[1].as_int() << ", " << arr[2].as_int() << "]" << std::endl;
            std::cout << "Expected: [2, 3, 4]" << std::endl;

            check_eq(arr[0].as_int(), 2); // Two destructors after scope
            check_eq(arr[1].as_int(), 3); // One more after reassignment
            check_eq(arr[2].as_int(), 4); // One more after null assignment
        });
        
        test("class_destructor_polymorphic", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);

            const char* script = R"JAI(
                auto base_destructor_count = 0;
                auto derived_destructor_count = 0;

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
                auto mid_base = 0;
                auto mid_derived = 0;

                {
                    print("=== Test REVERSED order ===");
                    print("Creating file_ref FIRST");
                    auto file_ref = FileResource("data.txt");
                    print("Creating base_ref SECOND");
                    auto base_ref = BaseResource();
                    print("About to exit scope - should destroy base_ref then file_ref");
                }

                print("After scope exit - base: " + to_string(base_destructor_count) + ", derived: " + to_string(derived_destructor_count));
                auto final_base = base_destructor_count;
                auto final_derived = derived_destructor_count;

                [final_base, final_derived]
            )JAI";
            
            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 2);

            // After scope exit: both objects should be destroyed
            // base_ref: 1 BaseResource destructor
            // file_ref: 1 FileResource destructor + 1 BaseResource (base part) destructor
            // Total: 2 base destructors, 1 derived destructor
            check_eq(arr[0].as_int(), 2); // Base destructor called twice
            check_eq(arr[1].as_int(), 1); // Derived destructor called once
        });

        test("class_destructor_nested_scopes", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);

            std::string order_log;
            js_engine->add_function("log_event", [&order_log](const std::string& event) {
                if (!order_log.empty()) order_log += ",";
                order_log += event;
            });

            const char* script = R"(
                class Tracker {
                    string name = "";

                    Tracker(string n) {
                        name = n;
                        log_event(name + "_ctor");
                    }

                    ~Tracker() {
                        log_event(name + "_dtor");
                    }
                }

                // Test nested scopes with proper LIFO destruction order
                {
                    auto outer1 = Tracker("outer1");
                    {
                        auto inner1 = Tracker("inner1");
                        auto inner2 = Tracker("inner2");
                        {
                            auto deep1 = Tracker("deep1");
                        } // deep1 destroyed here
                        auto inner3 = Tracker("inner3");
                    } // inner3, inner2, inner1 destroyed here (LIFO)
                    auto outer2 = Tracker("outer2");
                } // outer2, outer1 destroyed here (LIFO)

                // Test multiple objects in same scope
                {
                    auto a = Tracker("a");
                    auto b = Tracker("b");
                    auto c = Tracker("c");
                } // c, b, a destroyed in reverse order (LIFO)

                "done"
            )";

            auto result = js_engine->execute(script);
            check(result.is_string());
            check_eq(result.as_string(), "done");

            // Verify exact destruction order
            std::string expected =
                "outer1_ctor,"
                "inner1_ctor,inner2_ctor,"
                "deep1_ctor,deep1_dtor,"
                "inner3_ctor,"
                "inner3_dtor,inner2_dtor,inner1_dtor,"
                "outer2_ctor,"
                "outer2_dtor,outer1_dtor,"
                "a_ctor,b_ctor,c_ctor,"
                "c_dtor,b_dtor,a_dtor";

            std::cout << "Destruction order log:\n" << order_log << std::endl;
            std::cout << "Expected:\n" << expected << std::endl;

            check_eq(order_log, expected);
        });

        test("class_destructor_in_containers", [this]() {
            auto js_engine = engine::make();
            stdlib::register_all(*js_engine);

            const char* script = R"JAI(
                auto global_count = 0;

                class CountedObject {
                    int id = 0;

                    CountedObject(int i) {
                        id = i;
                        global_count = global_count + 1;
                        print("Object " + to_string(id) + " created (count=" + to_string(global_count) + ")");
                    }

                    ~CountedObject() {
                        global_count = global_count - 1;
                        print("Object " + to_string(id) + " destroyed (count=" + to_string(global_count) + ")");
                    }
                }

                // Test destruction in arrays
                auto initial_count = global_count; // Should be 0

                auto count_with_array = 0;
                auto count_after_clear = 0;

                {
                    auto objects = [
                        CountedObject(1),
                        CountedObject(2),
                        CountedObject(3)
                    ];
                    count_with_array = global_count; // Should be 3

                    // Clear array
                    objects = [];
                    count_after_clear = global_count; // Should be 0
                }

                // Test destruction in maps
                auto count_with_map = 0;
                {
                    auto object_map = {
                        "first": CountedObject(10),
                        "second": CountedObject(20)
                    };
                    count_with_map = global_count; // Should be 2
                }

                auto final_count = global_count; // Should be 0

                [initial_count, count_with_array, count_after_clear, count_with_map, final_count]
            )JAI";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 5);

            std::cout << "Container destructor counts: [" << arr[0].as_int() << ", " << arr[1].as_int() << ", "
                      << arr[2].as_int() << ", " << arr[3].as_int() << ", " << arr[4].as_int() << "]" << std::endl;
            std::cout << "Expected: [0, 3, 0, 2, 0]" << std::endl;

            check_eq(arr[0].as_int(), 0); // Initial count
            check_eq(arr[1].as_int(), 3); // 3 objects in array
            check_eq(arr[2].as_int(), 0); // All destroyed after clear
            check_eq(arr[3].as_int(), 2); // 2 objects in map
            check_eq(arr[4].as_int(), 0); // All destroyed after scope
        });

        test("multiple_inheritance_basic_fields", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    int x = 1;
                }

                class B {
                    int y = 2;
                }

                class C : A, B {
                    int z = 3;
                }

                auto c = C();
                [c.x, c.y, c.z]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_int(), 1);
            check_eq(arr[1].as_int(), 2);
            check_eq(arr[2].as_int(), 3);
        });

        test("multiple_inheritance_method_lookup", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    auto get_a() { return "from A"; }
                }

                class B {
                    auto get_b() { return "from B"; }
                }

                class C : A, B {
                    auto get_c() { return "from C"; }
                }

                auto c = C();
                [c.get_a(), c.get_b(), c.get_c()]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_string(), "from A");
            check_eq(arr[1].as_string(), "from B");
            check_eq(arr[2].as_string(), "from C");
        });

        test("multiple_inheritance_field_conflict_detected", [this]() {
            auto js_engine = engine::make();

            // C++ semantics: ambiguous field access should error
            // If both parent classes have a field with the same name,
            // and the derived class doesn't redefine it, that's an error
            const char* script = R"(
                class A {
                    int value = 10;
                    auto get_name() { return "A"; }
                }

                class B {
                    int value = 20;
                    auto get_name() { return "B"; }
                }

                class C : A, B {
                    // ERROR: 'value' and 'get_name' are ambiguous from A and B
                }

                auto c = C();
                c.value
            )";

            // Should throw an error about ambiguous field 'value'
            check_throws([&]() {
                js_engine->execute(script);
            }, "Expected error for ambiguous field in multiple inheritance");
        });

        test("multiple_inheritance_override_in_derived", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    int value = 10;
                }

                class B {
                    int value = 20;
                }

                class C : A, B {
                    int value = 30;  // Derived class overrides
                }

                auto c = C();
                c.value
            )";

            auto result = js_engine->execute(script);
            check(result.is_int());
            check_eq(result.as_int(), 30);  // Derived class wins
        });

        test("multiple_inheritance_constructors", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    int x = 0;
                    A(int val) { this.x = val; }
                }

                class B {
                    int y = 0;
                    B(int val) { this.y = val; }
                }

                class C : A, B {
                    int z = 0;
                    C(int a, int b, int c) {
                        // Note: In current implementation, parent constructors
                        // are not automatically called. Fields get default values.
                        this.x = a;
                        this.y = b;
                        this.z = c;
                    }
                }

                auto c = C(1, 2, 3);
                [c.x, c.y, c.z]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_int(), 1);
            check_eq(arr[1].as_int(), 2);
            check_eq(arr[2].as_int(), 3);
        });

        test("multiple_inheritance_destructors", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                auto log = "";

                class A {
                    ~A() { log = log + "A"; }
                }

                class B {
                    ~B() { log = log + "B"; }
                }

                class C : A, B {
                    ~C() { log = log + "C"; }
                }

                {
                    auto c = C();
                }

                log
            )";

            auto result = js_engine->execute(script);
            check(result.is_string());
            // Destructors should be called: C, then A, then B (derived first, then parents left-to-right)
            check_eq(result.as_string(), "CAB");
        });

        test("multiple_inheritance_three_levels", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    int a = 1;
                }

                class B : A {
                    int b = 2;
                }

                class C {
                    int c = 3;
                }

                class D : B, C {
                    int d = 4;
                }

                auto obj = D();
                [obj.a, obj.b, obj.c, obj.d]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 4);
            check_eq(arr[0].as_int(), 1);  // From A (via B)
            check_eq(arr[1].as_int(), 2);  // From B
            check_eq(arr[2].as_int(), 3);  // From C
            check_eq(arr[3].as_int(), 4);  // From D
        });

        test("multiple_inheritance_mixed_fields", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    int a = 100;
                }

                class B {
                    int b = 200;
                }

                class C : A, B {
                    int c = 300;
                }

                auto obj = C();
                [obj.a, obj.b, obj.c]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_int(), 100);
            check_eq(arr[1].as_int(), 200);
            check_eq(arr[2].as_int(), 300);
        });

        test("static_members_not_inherited", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class A {
                    static int static_a = 100;
                }

                class B {
                    static int static_b = 200;
                }

                class C : A, B {
                    static int static_c = 300;
                }

                // Static members are NOT inherited (C++ semantics)
                // Must access via the class that owns them using :: operator
                [A::static_a, B::static_b, C::static_c]
            )";

            auto result = js_engine->execute(script);
            check(result.is_array());
            auto arr = result.as_array();
            check_eq(arr.size(), 3);
            check_eq(arr[0].as_int(), 100);  // A's static
            check_eq(arr[1].as_int(), 200);  // B's static
            check_eq(arr[2].as_int(), 300);  // C's static
        });

        test("diamond_inheritance_should_fail", [this]() {
            auto js_engine = engine::make();

            const char* script = R"(
                class Base {
                    int x = 1;
                }

                class A : Base {
                }

                class B : Base {
                }

                class Diamond : A, B {
                }

                auto d = Diamond();
                d.x
            )";

            // This should fail at class definition time due to diamond detection
            bool exception_thrown = false;
            try {
                auto result = js_engine->execute(script);
                // If we get here, diamond was not rejected - test should fail
            } catch (const std::exception&) {
                // Expected to throw/error
                exception_thrown = true;
            }

            check(exception_thrown);  // Verify diamond inheritance was rejected
        });
    }
};

} // namespace jai::foundry::tests

using script_class_tests = jai::foundry::tests::script_class_tests;
FOUNDRY_REGISTER(script_class_tests)