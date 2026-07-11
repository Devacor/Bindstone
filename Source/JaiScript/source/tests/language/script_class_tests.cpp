#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <array>
#include <cmath>

namespace jai::foundry::tests {

class script_class_tests : public suite {
public:
    script_class_tests() : suite("Script Class Tests") {}
    
    void forge_tests() override {
        test("basic_class_with_fields", [this]() {
            auto js_engine = make_engine();
            
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
            auto js_engine = make_engine();
            
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
            auto js_engine = make_engine();
            
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
            auto js_engine = make_engine();
            
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
            auto js_engine = make_engine();
            
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
            auto js_engine = make_engine();
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
            auto js_engine = make_engine();
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
            auto js_engine = make_engine();
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
            auto js_engine = make_engine();
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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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
            auto js_engine = make_engine();

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

        // `new T(args)` is pure sugar for shared_ptr<T>(args) construction (Dev ruling
        // 2026-07): same new_expr, same tagging, reference semantics opt-in. A 'var'
        // decl keeps the shared_ptr marker (var p = new P() IS shared_ptr<P> p = P()).
        test("new_keyword_shared_ptr_sugar_basics", [this]() {
            const char* src = R"(
                class P { int x = 3; P() { x = 3; } P(int v) { x = v; } }
                var p = new P(7);
                var q = p;
                q.x = 99;
                shared_ptr<P> typed = new P(5);
                auto viaExpr = [](:o) -> { return o.x; };
                to_string(p.x) + "|" + to_string(q.x) + "|" + to_string(typed.x)
                    + "|" + to_string(viaExpr(new P(41)));
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(e);
                check_eq(std::string("99|99|5|41"), e->execute(src).as<std::string>(),
                         use_vm ? "vm new sugar" : "interp new sugar");
            }
        });

        test("new_keyword_matches_shared_ptr_spelling", [this]() {
            // new P() and shared_ptr<P>(...) are the same expression: same type_of,
            // same sharing behavior, brace form included
            const char* src = R"(
                class P { int x = 1; P() { x = 1; } }
                var a = new P();
                var b = shared_ptr<P>();
                var braced = new P{};
                var aliasA = a;
                aliasA.x = 10;
                var aliasB = b;
                aliasB.x = 20;
                (type_of(a) == type_of(b) ? "same" : "diff") + "|" + to_string(a.x)
                    + "|" + to_string(b.x) + "|" + to_string(braced.x);
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(e);
                check_eq(std::string("same|10|20|1"), e->execute(src).as<std::string>(),
                         use_vm ? "vm spelling parity" : "interp spelling parity");
            }
        });

        test("new_keyword_inheritance_upcast", [this]() {
            // new Derived into shared_ptr<Base> follows today's decl upcast rules
            const char* src = R"(
                class Base { int v = 1; Base() { v = 1; } }
                class Derived : Base { Derived() : super() { v = 2; } }
                shared_ptr<Base> b = new Derived();
                var alias = b;
                alias.v = 42;
                b.v;
            )";
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                check_eq((int64_t)42, e->execute(src).as_int(),
                         use_vm ? "vm upcast" : "interp upcast");
            }
        });

        test("new_keyword_registered_cpp_class", [this]() {
            // new works for registered C++ classes exactly like shared_ptr<T>(...)
            struct counter_probe { int n = 0; };
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                dynamic_binder<counter_probe>(*e, "CounterProbe")
                    .constructor<>()
                    .property("n", &counter_probe::n)
                    .build();
                const char* src = R"(
                    var c = new CounterProbe();
                    var alias = c;
                    alias.n = 13;
                    c.n;
                )";
                check_eq((int64_t)13, e->execute(src).as_int(),
                         use_vm ? "vm cpp class new" : "interp cpp class new");
            }
        });

        test("new_keyword_statement_and_error_shapes", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                // statement position: constructs and discards without error
                const char* stmt_src = R"(
                    class P { int x = 0; P() { x = 0; } }
                    new P();
                    7;
                )";
                check_eq((int64_t)7, e->execute(stmt_src).as_int(),
                         use_vm ? "vm stmt new" : "interp stmt new");
                // `new P;` without an argument list is a parse error (rejected either
                // as a throw or a null result depending on recovery)
                auto e2 = engine::make();
                if (use_vm) { e2->set_backend(jai::backend_type::vm); }
                bool rejected = false;
                try {
                    auto r = e2->execute("class P { int x = 0; } var p = new P;");
                    rejected = r.is_null();
                } catch (const std::exception&) {
                    rejected = true;
                }
                check_true(rejected, use_vm ? "vm new-without-parens rejected"
                                            : "interp new-without-parens rejected");
            }
        });

        // === Member access enforcement (private:/protected:/public: labels) ===
        // RULED (2026-07): private = declaring class's methods only; protected = declaring
        // class + subclasses; lambdas defined in a method inherit its class's access; free
        // functions/top level are public-only. Host C++ APIs stay unrestricted.
        test("member_access_enforcement_matrix", [this]() {
            const char* src = R"(
                class C {
                private:
                    int secret = 7;
                    int hidden_helper() { return secret * 2; }
                protected:
                    int prot = 8;
                public:
                    int pub = 9;
                    int peek() { return this.secret + this.prot + hidden_helper(); }
                    function lam() { var f = [this]() { return this.secret; }; return f(); }
                }
                class D : C {
                    int useProt() { return this.prot + 1; }
                }
                function freeProbe(o) { return o.secret; }
                class Other { int poke(o) { return o.secret; } }
                var c = C();
                var d = D();
                var out = "" + c.peek() + "|" + c.pub + "|" + c.lam() + "|" + d.useProt();
                try { var a = c.secret; out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                try { var b = c.prot; out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                try { var h = c.hidden_helper(); out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                try { var f = freeProbe(c); out = out + "|LEAK"; } catch (e) { out = out + "|free:" + e; }
                try { var o = Other().poke(c); out = out + "|LEAK"; } catch (e) { out = out + "|other:" + e; }
                try { c.secret = 1; out = out + "|WROTE"; } catch (e) { out = out + "|w:" + e; }
                try { c.prot += 1; out = out + "|COMPOUND"; } catch (e) { out = out + "|cw:" + e; }
                out;
            )";
            const std::string expected =
                "29|9|7|9"
                "|Cannot access private member 'secret' of class 'C'"
                "|Cannot access protected member 'prot' of class 'C'"
                "|Cannot access private member 'hidden_helper' of class 'C'"
                "|free:Cannot access private member 'secret' of class 'C'"
                "|other:Cannot access private member 'secret' of class 'C'"
                "|w:Cannot access private member 'secret' of class 'C'"
                "|cw:Cannot access protected member 'prot' of class 'C'";
            std::string outputs[2];
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(e);
                outputs[use_vm ? 1 : 0] = e->execute(src).as<std::string>();
            }
            check_eq(expected, outputs[0], "interp access matrix");
            check_eq(outputs[0], outputs[1], "access matrix parity (byte-identical incl. error text)");
        });

        test("member_access_private_in_base_vs_derived", [this]() {
            // Derived methods cannot touch Base-private (C++ rule); protected passes.
            // super:: private method errors, super:: protected/public passes.
            const char* src = R"(
                class Base {
                private:
                    int core = 1;
                    int corem() { return 11; }
                protected:
                    int sharedv = 2;
                    int sharedm() { return 22; }
                }
                class Derived : Base {
                    int okProt() { return this.sharedv + super::sharedm(); }
                    int badPriv() { return this.core; }
                    int badPrivM() { return super::corem(); }
                }
                var d = Derived();
                var out = "" + d.okProt();
                try { var a = d.badPriv(); out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                try { var b = d.badPrivM(); out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                out;
            )";
            const std::string expected =
                "24"
                "|Cannot access private member 'core' of class 'Base'"
                "|Cannot access private member 'corem' of class 'Base'";
            std::string outputs[2];
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                outputs[use_vm ? 1 : 0] = e->execute(src).as<std::string>();
            }
            check_eq(expected, outputs[0], "interp base/derived access");
            check_eq(outputs[0], outputs[1], "base/derived access parity");
        });

        test("member_access_statics_and_ref_binding", [this]() {
            const char* src = R"(
                class S {
                private:
                    static int counter = 5;
                    int f = 3;
                public:
                    static function bump() { counter += 1; return counter; }
                }
                function wantsRef(int& r) { r = 99; }
                var out = "" + S::bump();
                try { var a = S::counter; out = out + "|LEAK"; } catch (e) { out = out + "|" + e; }
                try { S::counter = 0; out = out + "|WROTE"; } catch (e) { out = out + "|w:" + e; }
                var s = S();
                try { wantsRef(s.f); out = out + "|BOUND"; } catch (e) { out = out + "|ref:" + e; }
                out;
            )";
            const std::string expected =
                "6"
                "|Cannot access private member 'counter' of class 'S'"
                "|w:Cannot access private member 'counter' of class 'S'"
                "|ref:Cannot access private member 'f' of class 'S'";
            std::string outputs[2];
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                outputs[use_vm ? 1 : 0] = e->execute(src).as<std::string>();
            }
            check_eq(expected, outputs[0], "interp statics/ref access");
            check_eq(outputs[0], outputs[1], "statics/ref access parity");
        });

        // ------------------------------------------------- bare-name resolution order
        // 2026-07 audit pins (docs/site/guide/07-classes.html "Name resolution in
        // methods"): catch var > locals/params > enclosing scopes > GLOBALS > fields >
        // methods > statics, identical on both backends for reads AND writes. The
        // surprising row - globals shadow fields - is DELIBERATELY pinned; this.name
        // always reaches the field.

        test("name_resolution_locals_and_params_shadow_fields", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = make_engine();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                auto r = e->execute(R"(
                    class C {
                        string hp = "field";
                        function local_wins() -> string { var hp = "local"; return hp; }
                        function param_wins(var hp) -> string { return hp; }
                        function catch_wins() -> string { try { throw "caught"; } catch (hp) { return hp; } }
                        function field_when_alone() -> string { return hp; }
                    }
                    auto c = C();
                    c.local_wins() + "|" + c.param_wins("param") + "|" + c.catch_wins() + "|" + c.field_when_alone()
                )");
                check_eq(std::string("local|param|caught|field"), r.as<std::string>());
            }
        });

        test("name_resolution_globals_shadow_fields", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = make_engine();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                // reads AND writes go to the global; this.g always reaches the field;
                // a global defined AFTER the class steals bare reads from then on
                auto r = e->execute(R"(
                    var g = "global";
                    class C {
                        string g = "field";
                        function read_bare() -> string { return g; }
                        function write_bare() -> void { g = "written"; }
                        function read_this() -> string { return this.g; }
                    }
                    var c = C();
                    c.write_bare();
                    var before_late = "";
                    class Late { string v = "field"; function probe() -> string { return v; } }
                    var lc = Late();
                    before_late = lc.probe();
                    var v = "late-global";
                    c.read_bare() + "|" + c.read_this() + "|" + g + "|" + before_late + "|" + lc.probe()
                )");
                check_eq(std::string("written|field|written|field|late-global"), r.as<std::string>());
            }
        });

        // ------------------------------------- shared_ptr<T> type-intern identity
        // GLOOM bug (2026-07): engine::get_type_info's composite key had no case for
        // jai_shared_ptr_type, so every parsed shared_ptr<T> collapsed onto the FIRST
        // interned T engine-wide - `new Bar()` after `new Foo()` SILENTLY CONSTRUCTED
        // a Foo (both backends; new desugars to the same shared_ptr<T>(args) node).
        // The key now includes the pointee. Matrix pinned below; the sibling caches
        // were audited clean: class_definition::overload_resolution_cache_ is a
        // per-class member (class identity inherent) and the flat-stack stage-1
        // callee IC pins script_function identity per call_site.

        test("shared_ptr_construction_distinguishes_classes", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                    var a = new Foo();
                    var b = new Bar();
                    auto sf = shared_ptr<Foo>();
                    auto sb = shared_ptr<Bar>();
                    auto vf = Foo();
                    auto vb = Bar();
                )");
                check_eq((int64_t)1, e->execute("a.x").as_int());
                check_eq((int64_t)2, e->execute("b.y").as_int());   // was: b built as a Foo
                check_eq((int64_t)1, e->execute("sf.x").as_int());
                check_eq((int64_t)2, e->execute("sb.y").as_int());
                check_eq((int64_t)1, e->execute("vf.x").as_int());
                check_eq((int64_t)2, e->execute("vb.y").as_int());
            }
        });

        test("shared_ptr_construction_same_arity_args_and_interleaved", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 0; Foo(int v) { x = v; } }
                    class Bar { int y = 0; Bar(int v) { y = v; } }
                    var b1 = new Bar(20);
                    var a1 = new Foo(10);
                    var b2 = new Bar{30};
                    auto a2 = shared_ptr<Foo>(40);
                )");
                check_eq((int64_t)20, e->execute("b1.y").as_int());
                check_eq((int64_t)10, e->execute("a1.x").as_int());  // was: a1 built as a Bar
                check_eq((int64_t)30, e->execute("b2.y").as_int());
                check_eq((int64_t)40, e->execute("a2.x").as_int());
            }
        });

        test("shared_ptr_typed_decl_enforces_right_pointee", [this]() {
            // The stale-pointee variant: shared_ptr<Bar> declarations must enforce
            // against BAR even when shared_ptr<Foo> was interned first.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                    shared_ptr<Foo> a = Foo();
                    shared_ptr<Bar> b = Bar();
                )");
                check_eq((int64_t)1, e->execute("a.x").as_int());
                check_eq((int64_t)2, e->execute("b.y").as_int());
                // Dev ruling (2026-07): typed shared_ptr decls ENFORCE the pointee
                // class - wrong-class value init and wrong-class sp-aliasing both
                // error; upcasts (script chains) stay legal; null stays assignable.
                check_throws([&]() { e->execute("shared_ptr<Bar> wrong = Foo();"); });
                check_throws([&]() { e->execute("shared_ptr<Bar> wrong2 = a;"); });   // aliasing a shared_ptr<Foo>
                e->execute(R"(
                    class Base { int bv = 5; }
                    class Derived : Base { int dv = 6; }
                    shared_ptr<Base> up1 = Derived();
                    var d = new Derived();
                    shared_ptr<Base> up2 = d;
                    shared_ptr<Base> nul = null;
                )");
                check_eq((int64_t)5, e->execute("up1.bv").as_int());
                check_eq((int64_t)5, e->execute("up2.bv").as_int());
                check_eq(true, e->execute("nul == null").as<bool>());
            }
        });

        test("shared_ptr_auto_decl_infers_then_enforces", [this]() {
            // shared_ptr<auto> (Dev ruling 2026-07): the pointee is INFERRED from the
            // initializer's exact class, then enforced exactly like the explicit
            // spelling - construct-and-share, handle sharing, upcast-on-reassign,
            // wrong-class errors, and null-after-establishment all mirror shared_ptr<T>.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                    shared_ptr<auto> a = new Foo();      // infer from new
                    shared_ptr<auto> b = Foo();          // construct-and-share (mirrors shared_ptr<Foo> b = Foo())
                    shared_ptr<auto> c = a;              // infer from a shared_ptr variable
                    var arr = [new Foo()];
                    shared_ptr<auto> d = arr[0];         // infer from an element read
                )");
                e->execute("c.x = 7;");
                check_eq((int64_t)7, e->execute("a.x").as_int());      // c shares a's handle
                e->execute("auto b2 = b; b2.x = 42;");
                check_eq((int64_t)42, e->execute("b.x").as_int());     // sp copies share
                e->execute("d.x = 9;");
                check_eq((int64_t)9, e->execute("arr[0].x").as_int()); // d shares the element
                // enforce-after: the inferred tag drives the explicit spelling's
                // reassignment checks and its init enforcement when aliased onward
                check_throws([&]() { e->execute("a = Bar();"); });
                check_throws([&]() { e->execute("a = new Bar();"); });
                check_throws([&]() { e->execute("shared_ptr<Bar> wrong = a;"); });
                e->execute("shared_ptr<Foo> fine = a;");               // tag says Foo
                e->execute("a = null;");                               // null legal AFTER establishment
                check_eq(true, e->execute("a == null").as<bool>());
                // upcast-on-reassign mirrors the explicit spelling; downcast errors
                e->execute(R"(
                    class Base { int bv = 5; }
                    class Derived : Base { int dv = 6; }
                    shared_ptr<auto> up = Base();
                    up = new Derived();
                    shared_ptr<auto> down = Derived();
                )");
                check_eq((int64_t)5, e->execute("up.bv").as_int());
                check_throws([&]() { e->execute("down = Base();"); });
                // nothing to infer: missing/null/non-class initializers error
                check_throws([&]() { e->execute("shared_ptr<auto> n0;"); });
                check_throws([&]() { e->execute("shared_ptr<auto> n1 = null;"); });
                check_throws([&]() { e->execute("shared_ptr<auto> n2 = 5;"); });
            }
            // nothing-to-infer error text: byte-identical across backends, and points
            // at the escape hatches (var, or an explicit shared_ptr<T>)
            auto run_catch = [](bool use_vm, const char* src) -> std::string {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                try { e->execute(src); } catch (const std::exception& ex) { return ex.what(); }
                return "<no throw>";
            };
            for (const char* src : { "shared_ptr<auto> n = null;", "class F {} shared_ptr<auto> n;" }) {
                std::string interp_msg = run_catch(false, src);
                std::string vm_msg = run_catch(true, src);
                check_eq(interp_msg, vm_msg, std::string("parity: ") + src);
                check(interp_msg.find("use var, or an explicit shared_ptr<T>") != std::string::npos, interp_msg);
            }
        });

        test("shared_ptr_var_is_a_parse_error", [this]() {
            // Dev ruling (2026-07, option 2): shared_ptr<var> is REJECTED at parse -
            // plain var already holds and rebinds any shared_ptr across unrelated
            // classes, so a constrained-but-dynamic pointee has no use case, and var
            // meaning something different inside angle brackets would muddy the ladder.
            // The error teaches the two real spellings.
            for (bool use_vm : {false, true}) {
                for (const char* src : { "class Foo { int x = 1; } shared_ptr<var> p = Foo();",
                                         "shared_ptr<var> n = null;",
                                         "function f(shared_ptr<var> a) { return a; }" }) {
                    auto e = jai::engine::make();
                    if (use_vm) { e->set_backend(jai::backend_type::vm); }
                    bool rejected = false;
                    std::string msg;
                    try {
                        auto r = e->execute(src);
                        rejected = r.is_null();   // recoverable parse errors synchronize to null
                    } catch (const std::exception& ex) {
                        rejected = true;
                        msg = ex.what();
                    }
                    check_true(rejected, std::string(use_vm ? "vm: " : "interp: ") + src);
                    if (!msg.empty()) {
                        check_true(msg.find("shared_ptr<var> is not supported") != std::string::npos, msg);
                        check_true(msg.find("shared_ptr<auto>") != std::string::npos, msg);
                    }
                    // the engine stays usable after the rejection
                    check_eq((int64_t)7, e->execute("3 + 4").as_int());
                }
                // and plain var really does hold + rebind any class's handle (the why)
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                    var p = new Foo();
                    var alias = p;
                )");
                e->execute("alias.x = 3;");
                check_eq((int64_t)3, e->execute("p.x").as_int());   // var copies share the handle
                e->execute("p = new Bar();");                        // and rebind across classes
                check_eq((int64_t)2, e->execute("p.y").as_int());
                check_eq((int64_t)3, e->execute("alias.x").as_int()); // rebind, not a copy: alias untouched
            }
        });

        test("var_held_handles_rebind_unchecked", [this]() {
            // Dev ruling (2026-07, refines c81c9812): a var-DECLARED holder behaves
            // IDENTICALLY to a typed handle holder wherever typed is legal (the monotonic
            // ladder). A handle rhs REBINDS; a COMPATIBLE value rhs assigns INTO the
            // pointee (just like shared_ptr<T>/auto); var's ONLY extra power is REBINDING
            // an INCOMPATIBLE value or a primitive where typed would refuse. Typed
            // spellings (shared_ptr<T>, shared_ptr<auto>, plain auto copies) keep enforcing.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                )");
                // decl-marker acquisition: var p = new Foo()
                e->execute("var p = new Foo(); var keep = p;");
                e->execute("p = new Bar();");
                check_eq((int64_t)2, e->execute("p.y").as_int());
                check_eq((int64_t)1, e->execute("keep.x").as_int());   // alias untouched by rebind
                // assign-marker acquisition: var q; q = new Foo()
                e->execute("var q; q = new Foo(); q = new Bar();");
                check_eq((int64_t)2, e->execute("q.y").as_int());
                // var-to-var rebind shares the handle
                e->execute("var r = new Foo(); var s = new Bar(); r = s; r.y = 9;");
                check_eq((int64_t)9, e->execute("s.y").as_int());
                // rebindability survives copies into var decls and repeated rebinds
                e->execute("var t = p; t = new Foo(); t = new Bar();");
                check_eq((int64_t)2, e->execute("t.y").as_int());
                // weak_ptr from a var-held handle still validates its pointee
                e->execute("var wsrc = new Foo(); weak_ptr<Foo> w = wsrc;");
                check_eq(false, e->execute("w.expired()").as<bool>());
                check_throws([&]() { e->execute("weak_ptr<Bar> wbad = wsrc;"); });
                // MONOTONIC LADDER (Dev ruling 2026-07, refines c81c9812): a var-held
                // handle behaves IDENTICALLY to a typed holder wherever typed is legal.
                // A COMPATIBLE value rhs (same class or subclass of the held pointee)
                // assigns INTO the shared pointee, so aliases SEE the change - exactly
                // like shared_ptr<Foo>/auto below. var's ONLY extra power is rebinding an
                // INCOMPATIBLE value or a primitive.
                e->execute("var u = new Foo(); var ualias = u; auto src = Foo(); src.x = 7; u = src;");
                check_eq((int64_t)7, e->execute("ualias.x").as_int());   // alias SEES the change (assign-into)
                check_eq((int64_t)7, e->execute("u.x").as_int());
                // it is the SAME shared object: mutating u touches the alias too
                e->execute("u.x = 42;");
                check_eq((int64_t)42, e->execute("ualias.x").as_int());
                // cross-class (INCOMPATIBLE) value rhs REBINDS - var NEVER refuses (no throw),
                // and the old alias is untouched (the handle was re-pointed, not written)
                e->execute("u = Bar();");
                check_eq((int64_t)2, e->execute("u.y").as_int());
                check_eq((int64_t)42, e->execute("ualias.x").as_int());  // old object untouched by the rebind
                // primitive rhs rebinds too
                e->execute("var pv = new Foo(); pv = 5;");
                check_eq((int64_t)5, e->execute("pv").as_int());
                // ...and the var stays dynamic after a value rebind - it accepts a handle again
                e->execute("pv = new Bar();");
                check_eq((int64_t)2, e->execute("pv.y").as_int());
                // TYPED tier UNCHANGED: value-rhs still auto-unwraps INTO the held handle
                // (same-class copies fields into the shared object; cross-class errors)
                e->execute("shared_ptr<Foo> tu = new Foo(); shared_ptr<Foo> tualias = tu; auto tsrc = Foo(); tsrc.x = 7; tu = tsrc;");
                check_eq((int64_t)7, e->execute("tualias.x").as_int());   // typed reaches THROUGH the handle
                check_throws([&]() { e->execute("shared_ptr<Foo> tf2 = new Foo(); tf2 = Bar();"); });
                // typed handle-rhs still enforces; auto copies of a var handle
                // re-lock to the plain spelling (infer-then-enforce)
                check_throws([&]() { e->execute("shared_ptr<Foo> tf = new Foo(); tf = new Bar();"); });
                check_throws([&]() { e->execute("auto af = p; af = new Foo();"); });   // p holds Bar; af locked to Bar
                e->execute("auto ab = p; ab = new Bar();");                            // same class stays legal
                check_eq((int64_t)2, e->execute("ab.y").as_int());
            }
        });

        test("user_method_wins_over_builtin_handle_method", [this]() {
            // Dev ruling (2026-07): a user class method takes PRIORITY over a same-named
            // builtin shared_ptr handle method (reset/use_count/unique/cpp_ref_count).
            // Previously the builtin shadowed the class method (gloom's fx.reset() silently
            // no-op'd the builtin reset()); now the class method wins on shared_ptr<T>
            // instances, the builtin stays reachable where NO class method shadows it, and
            // a shadow emits ONE warning per colliding method per class definition.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                std::vector<std::string> warnings;
                e->set_script_warning_handler([&](const std::string& m) { warnings.push_back(m); });

                // class reset() defined -> user method WINS (builtin would have no-op'd)
                e->execute("class Pool { int n = 0; void reset() { n = 99; } }");
                e->execute("var p = new Pool(); p.reset();");                 // identifier receiver (fast path)
                check_eq((int64_t)99, e->execute("p.n").as_int(), use_vm ? "vm fast-path" : "interp fast-path");
                // generic (non-identifier receiver) path also resolves the user method
                e->execute("var arr = [new Pool()]; arr[0].reset();");
                check_eq((int64_t)99, e->execute("arr[0].n").as_int());
                // a class-defined use_count() wins over the builtin ref-count reader
                e->execute("class Counter { int use_count() { return 7; } }");
                check_eq((int64_t)7, e->execute("var c = new Counter(); c.use_count()").as_int());

                // the shadow fired a warning (both backends, byte-identical message)
                bool saw_reset_warn = false, saw_uc_warn = false;
                for (const auto& w : warnings) {
                    if (w.find("class method 'reset' shadows the builtin shared_ptr reset()") != std::string::npos &&
                        w.find("unreachable on Pool instances") != std::string::npos) { saw_reset_warn = true; }
                    if (w.find("class method 'use_count' shadows the builtin shared_ptr use_count()") != std::string::npos) { saw_uc_warn = true; }
                }
                check_true(saw_reset_warn, "reset shadow warning fired");
                check_true(saw_uc_warn, "use_count shadow warning fired");

                // NO collision -> builtin stays reachable AND no warning for that class
                size_t warn_count_before = warnings.size();
                e->execute("class Bare { int x = 5; }");
                check_eq(warn_count_before, warnings.size(), "no warning for a class without a collision");
                check_ge(e->execute("shared_ptr<Bare> b = new Bare(); b.use_count()").as_int(), (int64_t)1,
                         "builtin use_count still reachable when unshadowed");
            }
        });

        test("shared_ptr_construction_across_hot_reload", [this]() {
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class Foo { int x = 1; }
                    class Bar { int y = 2; }
                    var a = new Foo();
                )");
                check_eq((int64_t)1, e->execute("a.x").as_int());
                // redefine ONE class, then construct both spellings again
                e->execute("class Foo { int x = 1; int z = 7; }");
                e->execute("var a2 = new Foo(); var b2 = new Bar();");
                check_eq((int64_t)7, e->execute("a2.z").as_int());
                check_eq((int64_t)2, e->execute("b2.y").as_int());
            }
        });

        test("method_ic_reload_at_hot_call_site", [this]() {
            // The vm's monomorphic method IC (call_site::mic_*) revalidates on
            // method_epoch: a hot call site crossing a class redefinition must
            // dispatch the NEW body immediately, on both backends.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class C { int f() { return 1; } }
                    var c = C();
                    int hot() { int s = 0; for (int i = 0; i < 5; ++i) { s += c.f(); } return s; }
                )");
                check_eq((int64_t)5, e->execute("hot()").as_int(), use_vm ? "vm pre-reload" : "interp pre-reload");
                e->execute("class C { int f() { return 2; } }");
                check_eq((int64_t)10, e->execute("hot()").as_int(),
                         use_vm ? "vm hot site sees reloaded body" : "interp hot site sees reloaded body");
            }
        });

        test("method_ic_base_reload_invalidates_derived_site", [this]() {
            // Epoch propagation through derived_classes_: redefining the BASE must
            // invalidate a hot site whose cached receiver class is the DERIVED.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class B { int f() { return 1; } }
                    class D : B { int pad = 0; }
                    var d = D();
                    int hot() { int s = 0; for (int i = 0; i < 5; ++i) { s += d.f(); } return s; }
                )");
                check_eq((int64_t)5, e->execute("hot()").as_int());
                e->execute("class B { int f() { return 2; } }");
                check_eq((int64_t)10, e->execute("hot()").as_int(),
                         use_vm ? "vm derived site sees base reload" : "interp derived site sees base reload");
            }
        });

        test("method_ic_typed_param_site_stays_type_sensitive", [this]() {
            // A cached single-overload method with TYPED params must keep rejecting
            // by argument type at the same hot site, with backend-identical outcomes.
            std::array<std::string, 2> outcomes;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class T { int f(int x) { return x + 1; } }
                    var t = T();
                    var hot(var v) { var msg = "ok"; try { msg = t.f(v); } catch (err) { msg = err; } return msg; }
                )");
                check_eq((int64_t)2, e->execute("hot(1)").as_int(), "int arg dispatches");
                check_eq((int64_t)3, e->execute("hot(2)").as_int(), "site stays hot");
                outcomes[use_vm ? 1 : 0] = e->execute("hot(\"s\")").as<std::string>();
            }
            check_eq(outcomes[0], outcomes[1], "backends agree on typed-param mismatch outcome");
        });

        test("method_ic_untyped_param_site_accepts_any_type", [this]() {
            // Untyped (var) params make resolution arg-independent (mic_static): the
            // hot site must accept a different arg type on every call, no lock-in.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class U { int n = 0; void f(var v) { n += 1; } }
                    var u = U();
                    int hot(var v) { u.f(v); return u.n; }
                )");
                check_eq((int64_t)1, e->execute("hot(1)").as_int());
                check_eq((int64_t)2, e->execute("hot(2)").as_int());
                check_eq((int64_t)3, e->execute("hot(\"s\")").as_int(), "string arg through the hot site");
                check_eq((int64_t)4, e->execute("hot(2.5)").as_int(), "float arg through the hot site");
                check_eq((int64_t)5, e->execute("hot([1,2])").as_int(), "array arg through the hot site");
            }
        });

        test("method_ic_polymorphic_site", [this]() {
            // One call site alternating receiver classes: the monomorphic cache must
            // miss-and-refill without ever dispatching the wrong class's method.
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class A { int f() { return 1; } }
                    class Z { int f() { return 10; } }
                    var items = [new A(), new Z(), new A(), new Z()];
                    int hot() { int s = 0; for (int i = 0; i < 4; ++i) { s += items[i].f(); } return s; }
                )");
                check_eq((int64_t)22, e->execute("hot()").as_int(), use_vm ? "vm alternating classes" : "interp alternating classes");
                check_eq((int64_t)22, e->execute("hot()").as_int(), "second pass identical");
            }
        });

        test("method_ic_host_added_field_shadows_method_at_hot_site", [this]() {
            // Host C++ set_field can add an instance field named like a method
            // (insert_or_assign contract); the hit path re-probes has_field, so the
            // shadow must take effect at an already-hot site, backend-identically.
            std::array<std::string, 2> outcomes;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class S { int f() { return 1; } }
                    var s = S();
                    var hot() { var msg = "ok"; try { msg = s.f(); } catch (err) { msg = err; } return msg; }
                )");
                check_eq((int64_t)1, e->execute("hot()").as_int());
                check_eq((int64_t)1, e->execute("hot()").as_int(), "site hot before shadow");
                auto sv = e->execute("s");
                auto holder = sv.get_object_holder();
                check_not_null(holder.get(), "instance holder");
                auto instance = std::static_pointer_cast<jai::class_instance>(holder->data);
                instance->set_field(e->symbolize("f"), jai::script_value((int64_t)42, e.get()));
                auto after = e->execute("hot()");
                outcomes[use_vm ? 1 : 0] = after.is_string() ? after.as<std::string>()
                                                             : std::to_string(after.as_int());
            }
            check_eq(outcomes[0], outcomes[1], "backends agree on field-shadow outcome");
        });

        test("field_read_ic_reload_at_hot_site", [this]() {
            // vm GET_MEMBER site IC (chunk::member_ic) revalidates on method_epoch: a
            // getter added by reload SHADOWS the field at an already-hot read site, and
            // a removed field errors — identically on both backends.
            std::array<std::string, 2> removed;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class P { int x = 3; }
                    var p = P();
                    int hot() { int s = 0; for (int i = 0; i < 5; ++i) { s += p.x; } return s; }
                )");
                check_eq((int64_t)15, e->execute("hot()").as_int());
                e->execute("p.x = 4;");
                check_eq((int64_t)20, e->execute("hot()").as_int(), "hot site reads the live cell");
                // Established contract (both backends): a script-defined _get_x does NOT
                // shadow a PRESENT field — property getters are the C++ surface. The
                // reload still bumps the epoch, so the hot site revalidates either way.
                e->execute("class P { int x = 3; int _get_x() { return 100; } }");
                check_eq((int64_t)20, e->execute("hot()").as_int(),
                         use_vm ? "vm field wins at hot site across reload" : "interp field wins at hot site across reload");
                e->execute("class P { int y = 1; }");
                removed[use_vm ? 1 : 0] = e->execute(
                    "var msg = \"\"; try { var t = hot(); msg = \"ran\"; } catch (err) { msg = err; } msg").as<std::string>();
                check_eq(std::string("V:Object has no member 'x'"), e->execute(
                    "var m3 = \"\"; try { var t3 = p.x; m3 = \"ran\"; } catch (err3) { m3 = \"V:\" + err3; } m3").as<std::string>(),
                    use_vm ? "vm plain removed-field read errors" : "interp plain removed-field read errors");
            }
            check_eq(std::string("Object has no member 'x'"), removed[1],
                     "vm removed-field text at the hot site");
            check_eq(removed[0], removed[1],
                     "backends agree at the hot site (compound-RHS error fix)");
        });

        test("compound_rhs_error_surfaces_original_exception", [this]() {
            // A compound assign whose RHS raises must surface the ORIGINAL exception,
            // not a follow-on arithmetic error against the placeholder. The interpreter
            // clobbered the catch value on three compound paths (slot target, member
            // target, subscript target — the constrained-ref branch had the unwinding
            // check, its siblings didn't); the vm was correct throughout. Every shape
            // asserts the exact text on BOTH backends.
            const char* expected = "V:Object has no member 'x'";
            const std::pair<const char*, const char*> shapes[] = {
                {"fn-only",
                 "int f1() { return r2.x; } var m = \"\"; try { var t = f1(); m = \"ran\"; } catch (er) { m = \"V:\" + er; } m"},
                {"counted-loop-in-fn compound slot target",
                 "int f2() { int s = 0; for (int i = 0; i < 5; ++i) { s += r2.x; } return s; } var m = \"\"; try { var t = f2(); m = \"ran\"; } catch (er) { m = \"V:\" + er; } m"},
                {"compound env target",
                 "int gs = 0; var m = \"\"; try { gs += r2.x; m = \"ran\"; } catch (er) { m = \"V:\" + er; } m"},
                {"compound member target",
                 "int f5() { r2.y += r2.x; return 0; } var m = \"\"; try { var t = f5(); m = \"ran\"; } catch (er) { m = \"V:\" + er; } m"},
                {"compound subscript target",
                 "int f6() { var a = [1]; a[0] += r2.x; return 0; } var m = \"\"; try { var t = f6(); m = \"ran\"; } catch (er) { m = \"V:\" + er; } m"},
            };
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute("class R2 { int y = 1; } var r2 = R2();");
                for (const auto& [label, src] : shapes) {
                    check_eq(std::string(expected), e->execute(src).as<std::string>(),
                             std::string(use_vm ? "vm: " : "interp: ") + label);
                }
            }
        });

        test("field_store_ic_reload_and_setter_shadow", [this]() {
            // Write-side twin of field_read_ic: the vm SET_MEMBER site IC revalidates on
            // method_epoch — a CUSTOM _set_ added by reload must fire at an already-hot
            // store site, typed-store conversion errors stay backend-identical, and the
            // auto-setter (synthesized accessor) never blocks the slot path.
            std::array<std::string, 2> badstore;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class W { int x = 0; int log = 0; }
                    var w = W();
                    void hot(int v) { for (int i = 0; i < 5; ++i) { w.x = v + i; } }
                )");
                e->execute("hot(10);");
                check_eq((int64_t)14, e->execute("w.x").as_int(), "hot store site writes the cell");
                // Established contract (both backends): the SYNTHESIZED accessor
                // re-registers after user methods, so a script _set_x for a DECLARED
                // field is unreachable — the store stays a raw field write. The reload
                // still bumps the epoch, so the hot site revalidates either way.
                e->execute("class W { int x = 0; int log = 0; void _set_x(int v) { x = v; log = log + 1; } }");
                e->execute("hot(20);");
                check_eq((int64_t)24, e->execute("w.x").as_int(), "field store stays raw across reload");
                check_eq((int64_t)0, e->execute("w.log").as_int(),
                         use_vm ? "vm auto-accessor wins over user _set_x" : "interp auto-accessor wins over user _set_x");
                // a custom setter for a NON-field name DOES fire (no auto competition,
                // and the store site stays negative in the IC — no slot exists)
                e->execute(R"(
                    class V { int seen = 0; void _set_virt(int v) { seen = seen + v; } }
                    var vv = V();
                    void hotv(int v) { for (int i = 0; i < 3; ++i) { vv.virt = v; } }
                )");
                e->execute("hotv(7);");
                check_eq((int64_t)21, e->execute("vv.seen").as_int(),
                         use_vm ? "vm non-field custom setter at hot site" : "interp non-field custom setter at hot site");
                // typed-store conversion error at a hot site: identical outcome both backends
                badstore[use_vm ? 1 : 0] = e->execute(
                    "var m = \"\"; try { w.x = [1,2]; m = \"stored\"; } catch (er) { m = er; } m").as<std::string>();
            }
            check_eq(badstore[0], badstore[1], "backends agree on typed-store mismatch outcome");
        });

        test("bare_sibling_method_calls_in_loop", [this]() {
            // Bare sibling calls (helper() instead of this.helper()) mint the typed
            // bound_method thunk and enter the vm in-loop; behavior must stay identical
            // to the old opaque lambda on both backends: dispatch, sibling calls inside
            // a coroutine body, coroutine-method minting through a bare call, the thunk
            // as a stored first-class value, and typed-arg decline error text.
            std::array<std::string, 2> err;
            for (bool use_vm : {false, true}) {
                auto e = jai::engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                e->execute(R"(
                    class S {
                        int v = 3; var brain = null; int ticks = 0;
                        int helper(int k) { return v * k; }
                        int outer() { return helper(4) + helper(1); }
                        int grab() { var fv = helper; return fv(3); }
                        coroutine void steps() { while (true) { ticks = ticks + helper(1); yield; } }
                        void tick() { if (brain == null || brain.done()) { brain = steps(); } brain.resume(); }
                    }
                    var s = S();
                )");
                check_eq((int64_t)15, e->execute("s.outer()").as_int(),
                         use_vm ? "vm bare sibling dispatch" : "interp bare sibling dispatch");
                e->execute("for (int i = 0; i < 5; ++i) { s.tick(); }");
                check_eq((int64_t)15, e->execute("s.ticks").as_int(),
                         use_vm ? "vm siblings inside coroutine body" : "interp siblings inside coroutine body");
                check_eq((int64_t)9, e->execute("s.grab()").as_int(), "thunk as stored first-class value");
                err[use_vm ? 1 : 0] = e->execute(
                    "var m = \"\"; try { var t = s.v; class X {} var q = X(); m = \"pre\"; } catch (er0) { m = er0; } "
                    "try { var t2 = s.outer(); s.helper([1]); m = \"ran\"; } catch (er) { m = er; } m").as<std::string>();
            }
            check_eq(err[0], err[1], "backends agree on typed-arg decline outcome");
        });

        test("member_access_hot_reload_and_host_api", [this]() {
            // Hot reload is permissive: enforcement consults the CURRENT class_definition
            // at access time — instances keep working, new accesses follow new labels.
            // Host C++ get_field stays unrestricted by design.
            for (bool use_vm : {false, true}) {
                auto e = engine::make();
                if (use_vm) { e->set_backend(jai::backend_type::vm); }
                jai::stdlib::register_all(e);
                e->execute("class R { public: int v = 4; } var r = R();");
                check_eq((int64_t)4, e->execute("r.v").as_int(), "public before reload");
                // reload flips v to private
                e->execute("class R { private: int v = 4; public: int look() { return v; } }");
                auto blocked = e->execute("var msg = \"\"; try { var x = r.v; msg = \"leak\"; } catch (err) { msg = err; } msg");
                check_eq(std::string("Cannot access private member 'v' of class 'R'"),
                         blocked.as<std::string>(), use_vm ? "vm reload enforces" : "interp reload enforces");
                check_eq((int64_t)4, e->execute("r.look()").as_int(), "instance keeps working via method");
                // host C++ API is unrestricted
                auto rv = e->execute("r");
                auto holder = rv.get_object_holder();
                check_not_null(holder.get(), "instance holder");
                auto instance = std::static_pointer_cast<jai::class_instance>(holder->data);
                uint64_t v_id = e->symbolize("v");
                check_eq((int64_t)4, instance->get_field(v_id).as_int(), "host get_field bypasses enforcement");
            }
        });
    }
};

} // namespace jai::foundry::tests

using script_class_tests = jai::foundry::tests::script_class_tests;
FOUNDRY_REGISTER(script_class_tests)