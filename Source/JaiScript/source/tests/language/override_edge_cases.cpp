#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

namespace jai::foundry::tests {

class override_edge_cases : public suite {
public:
    override_edge_cases() : suite("Override Edge Cases") {}
    
    void forge_tests() override {
        test("override_on_multiple_inheritance_levels", [this]() {
            auto engine = make_engine();
            
            // Deep inheritance hierarchy
            engine->execute(R"(
                class A {
                    auto value() { return "A"; }
                }
                
                class B : A {
                    auto value() override { return "B"; }
                }
                
                class C : B {
                    auto value() override { return "C"; }
                }
                
                class D : C {
                    auto value() override { return "D"; }
                }
            )");
            
            // Test polymorphism at each level
            auto result1 = engine->execute("A a = D(); a.value();");
            check_eq(result1.as<std::string>(), "D");
            
            auto result2 = engine->execute("B b = D(); b.value();");
            check_eq(result2.as<std::string>(), "D");
            
            auto result3 = engine->execute("C c = D(); c.value();");
            check_eq(result3.as<std::string>(), "D");
        });
        
        test("override_with_different_return_types", [this]() {
            auto engine = make_engine();
            
            // Override should work with compatible return types
            engine->execute(R"(
                class Shape {
                    auto area() { return 0.0; }
                }
                
                class Circle : Shape {
                    float radius = 5.0;
                    auto area() override { return 3.14159 * radius * radius; }
                }
            )");
            
            auto result = engine->execute("Shape s = Circle(); s.area();");
            check_near(result.as<double>(), 78.53975, 0.001);
        });
        
        test("override_with_multiple_base_methods", [this]() {
            auto engine = make_engine();
            
            // Class with multiple methods, only some overridden
            engine->execute(R"(
                class Animal {
                    auto speak() { return "..."; }
                    auto move() { return "moves"; }
                    auto eat() { return "eats"; }
                }
                
                class Dog : Animal {
                    auto speak() override { return "Woof!"; }
                    auto move() override { return "runs"; }
                    // eat() is not overridden
                }
            )");
            
            auto dog = engine->execute("Animal a = Dog(); a");
            auto speak = engine->execute("Animal a = Dog(); a.speak();");
            auto move = engine->execute("Animal a = Dog(); a.move();");
            auto eat = engine->execute("Animal a = Dog(); a.eat();");
            
            check_eq(speak.as<std::string>(), "Woof!");
            check_eq(move.as<std::string>(), "runs");
            check_eq(eat.as<std::string>(), "eats");  // Uses base implementation
        });
        
        test("override_in_diamond_inheritance", [this]() {
            auto engine = make_engine();
            
            // Diamond inheritance pattern (single inheritance in JaiScript)
            engine->execute(R"(
                class Device {
                    auto power() { return "Device power"; }
                }
                
                class Phone : Device {
                    auto power() override { return "Phone power"; }
                }
                
                class Computer : Device {
                    auto power() override { return "Computer power"; }
                }
                
                // In JaiScript, we can only inherit from one class
                // So we test that override works correctly in parallel hierarchies
            )");
            
            auto phone = engine->execute("Device d = Phone(); d.power();");
            auto computer = engine->execute("Device d = Computer(); d.power();");
            
            check_eq(phone.as<std::string>(), "Phone power");
            check_eq(computer.as<std::string>(), "Computer power");
        });
        
        test("override_method_calling_super", [this]() {
            auto engine = make_engine();
            
            // Override that calls parent implementation
            engine->execute(R"(
                class Logger {
                    auto log(string msg) { return "[LOG] " + msg; }
                }
                
                class TimestampLogger : Logger {
                    auto log(string msg) override { 
                        return "[TIME] " + super::log(msg); 
                    }
                }
            )");
            
            auto result = engine->execute(R"(
                Logger l = TimestampLogger();
                l.log("Hello");
            )");
            
            check_eq(result.as<std::string>(), "[TIME] [LOG] Hello");
        });
        
        test("override_with_private_base_method", [this]() {
            auto engine = make_engine();
            
            // Private methods shouldn't be overrideable (when we add private support)
            // For now, test that methods work as expected
            engine->execute(R"(
                class Base {
                    auto internal() { return 42; }  // Would be private
                    auto public_method() { return internal() * 2; }
                }
                
                class Derived : Base {
                    // When private is supported, this should NOT override the private method
                    // For now, we use 'override' keyword to explicitly shadow the base method
                    auto internal() override { return 99; }
                    auto public_method() override { return internal() * 3; }
                }
            )");
            
            auto result = engine->execute("Base b = Derived(); b.public_method();");
            check_eq(result.as<int>(), 297);  // 99 * 3
        });

        test("shadowing_without_override_fails", [this]() {
            auto engine = make_engine();

            // Attempting to shadow a base class method without 'override' keyword should fail
            check_throws([&]() {
                engine->execute(R"(
                    class Base {
                        auto method() { return 42; }
                    }

                    class Derived : Base {
                        // Missing 'override' keyword - should fail
                        auto method() { return 99; }
                    }
                )");
            });

            // Also test that using 'override' makes it work
            engine->execute(R"(
                class BaseOk {
                    auto method() { return 42; }
                }

                class DerivedOk : BaseOk {
                    auto method() override { return 99; }
                }
            )");

            auto result = engine->execute("BaseOk b = DerivedOk(); b.method();");
            check_eq(result.as<int>(), 99);
        });

        test("constructor_with_override_in_same_class", [this]() {
            auto engine = make_engine();
            jai::stdlib::register_all(*engine);  // Register stdlib including to_string

            // Class with both constructor and override methods
            engine->execute(R"(
                class Vehicle {
                    string type = "generic";
                    auto describe() { return "Vehicle: " + type; }
                }
                
                class Car : Vehicle {
                    int wheels;
                    Car(int w) : super() { 
                        wheels = w; 
                        type = "car";
                    }
                    auto describe() override { 
                        return "Car with " + to_string(wheels) + " wheels"; 
                    }
                }
            )");
            
            auto result = engine->execute(R"(
                Vehicle v = Car(4);
                v.describe();
            )");
            
            check_eq(result.as<std::string>(), "Car with 4 wheels");
        });
        
        test("override_chain_with_skip", [this]() {
            auto engine = make_engine();
            
            // A->B->C where B doesn't override but C does
            engine->execute(R"(
                class A {
                    auto method() { return 1; }
                }
                
                class B : A {
                    // B does NOT override method()
                    auto other() { return 2; }
                }
                
                class C : B {
                    // C overrides A's method, skipping B
                    auto method() override { return 3; }
                }
            )");
            
            auto result1 = engine->execute("A a = C(); a.method();");
            check_eq(result1.as<int>(), 3);
            
            auto result2 = engine->execute("B b = C(); b.method();");
            check_eq(result2.as<int>(), 3);
        });
        
        test("override_with_field_access", [this]() {
            auto engine = make_engine();
            
            // Override methods that access fields
            engine->execute(R"(
                class Counter {
                    int count = 0;
                    auto increment() { count = count + 1; return count; }
                    auto get_count() { return count; }
                }
                
                class DoubleCounter : Counter {
                    auto increment() override { 
                        count = count + 2; 
                        return count; 
                    }
                }
            )");
            
            auto result = engine->execute(R"(
                Counter c = DoubleCounter();
                c.increment();
                c.increment();
                c.get_count();
            )");
            
            check_eq(result.as<int>(), 4);  // Two increments of 2 each
        });
        
        test("multiple_override_errors_in_one_class", [this]() {
            auto engine = make_engine();
            
            engine->execute(R"(
                class Base {
                    auto foo() { return 1; }
                    auto bar() { return 2; }
                }
            )");
            
            // Try to define a class with multiple override errors
            try {
                engine->execute(R"(
                    class Bad : Base {
                        auto foo() { return 10; }  // Missing override
                        auto baz() override { return 20; }  // No such method in base
                    }
                )");
                throw test_failure("Expected exception for override errors");
            } catch (const runtime_error& e) {
                // Should catch the first error encountered
                check_true(
                    std::string(e.what()).find("shadows base class method") != std::string::npos ||
                    std::string(e.what()).find("no base class method found") != std::string::npos,
                    "Should report override error"
                );
            }
        });
        
        test("override_static_vs_instance_methods", [this]() {
            auto engine = make_engine();
            
            // When we add static method support, test that static methods
            // cannot override instance methods and vice versa
            // For now, test instance methods only
            engine->execute(R"(
                class Math {
                    auto calculate(int x) { return x * 2; }
                }
                
                class AdvancedMath : Math {
                    auto calculate(int x) override { return x * x; }
                }
            )");
            
            auto result = engine->execute("Math m = AdvancedMath(); m.calculate(5);");
            check_eq(result.as<int>(), 25);
        });

        test("static_method_cannot_use_override", [this]() {
            auto engine = make_engine();

            // Static methods cannot use 'override' keyword - they're not virtual
            check_throws([&]() {
                engine->execute(R"(
                    class Base {
                        auto method() { return 42; }
                    }

                    class Derived : Base {
                        static auto method() override { return 99; }
                    }
                )");
            });
        });

        test("instance_method_cannot_override_static", [this]() {
            auto engine = make_engine();

            // Attempting to override static method with instance should fail
            check_throws([&]() {
                engine->execute(R"(
                    class Base {
                        static auto method() { return 42; }
                    }

                    class Derived : Base {
                        auto method() override { return 99; }
                    }
                )");
            });
        });

        test("static_in_base_static_in_derived_same_name", [this]() {
            auto engine = make_engine();

            // Both base and derived have static method with same name
            // Static methods don't need override - they're fully qualified
            engine->execute(R"(
                class Base {
                    static auto method() { return 1; }
                }

                class Derived : Base {
                    static auto method() { return 2; }
                }
            )");

            // Static methods: ONLY accessible via Class::method()
            // 1. Base::method()
            auto result1 = engine->execute("Base::method();");
            check_eq(result1.as<int>(), 1);

            // 2. Derived::method()
            auto result2 = engine->execute("Derived::method();");
            check_eq(result2.as<int>(), 2);

            // 3. instance.method() should NOT work for static methods
            check_throws([&]() {
                engine->execute("Base b = Base(); b.method();");
            });

            check_throws([&]() {
                engine->execute("Derived d = Derived(); d.method();");
            });
        });

        test("instance_in_base_static_in_derived_same_name", [this]() {
            auto engine = make_engine();

            // Base has instance method, derived adds static with same name
            // Different namespaces - both should work
            engine->execute(R"(
                class Base {
                    auto method() { return 10; }
                }

                class Derived : Base {
                    static auto method() { return 20; }
                }
            )");

            // Test all 4 access patterns:
            // 1. Base::method() - no static method in Base, should fail
            check_throws([&]() {
                engine->execute("Base::method();");
            });

            // 2. Base instance.method() - instance method
            auto result2 = engine->execute("Base b = Base(); b.method();");
            check_eq(result2.as<int>(), 10);

            // 3. Derived::method() - static method
            auto result3 = engine->execute("Derived::method();");
            check_eq(result3.as<int>(), 20);

            // 4. Derived instance.method() - inherited instance method
            auto result4 = engine->execute("Derived d = Derived(); d.method();");
            check_eq(result4.as<int>(), 10);  // Inherits Base's instance method
        });

        test("static_in_base_instance_in_derived_same_name", [this]() {
            auto engine = make_engine();

            // Base has static method, derived adds instance with same name
            // Different namespaces - both should work
            engine->execute(R"(
                class Base {
                    Base() {}
                    static auto method() { return 100; }
                }

                class Derived : Base {
                    Derived() : super() {}
                    auto method() { return 200; }
                }
            )");

            // Base::method() - static method works
            auto result1 = engine->execute("Base::method();");
            check_eq(result1.as<int>(), 100);

            // Base instance.method() - no instance method in Base, should fail
            check_throws([&]() {
                engine->execute("Base b = Base(); b.method();");
            });

            // Derived::method() - static methods are NOT inherited (C++ semantics)
            // Derived doesn't have its own static method, so this should fail
            check_throws([&]() {
                engine->execute("Derived::method();");
            });

            // Derived instance.method() - instance method works
            auto result2 = engine->execute("Derived d = Derived(); d.method();");
            check_eq(result2.as<int>(), 200);
        });

        test("instance_in_base_instance_in_derived_same_name", [this]() {
            auto engine = make_engine();

            // Both have instance methods - must use override
            engine->execute(R"(
                class Base {
                    auto method() { return 5; }
                }

                class Derived : Base {
                    auto method() override { return 50; }
                }
            )");

            // Test all 4 access patterns:
            // 1. Base::method() - no static method, should fail
            check_throws([&]() {
                engine->execute("Base::method();");
            });

            // 2. Base instance.method() - instance method
            auto result2 = engine->execute("Base b = Base(); b.method();");
            check_eq(result2.as<int>(), 5);

            // 3. Derived::method() - no static method, should fail
            check_throws([&]() {
                engine->execute("Derived::method();");
            });

            // 4. Derived instance.method() - overridden instance method
            auto result4 = engine->execute("Derived d = Derived(); d.method();");
            check_eq(result4.as<int>(), 50);

            // Bonus: polymorphic call
            auto result5 = engine->execute("Base b2 = Derived(); b2.method();");
            check_eq(result5.as<int>(), 50);  // Virtual dispatch
        });

        test("static_methods_no_virtual_dispatch", [this]() {
            auto engine = make_engine();

            // Static methods are resolved at compile-time via Class::
            engine->execute(R"(
                class Base {
                    static auto getValue() { return 1; }
                    auto getInstance() { return Base::getValue(); }
                }

                class Derived : Base {
                    static auto getValue() { return 2; }
                }
            )");

            // When called through base reference, uses base's static method
            auto result1 = engine->execute("Base b = Derived(); b.getInstance();");
            check_eq(result1.as<int>(), 1);  // Uses Base::getValue(), not Derived::getValue()

            // Direct static calls use the class specified
            auto result2 = engine->execute("Base::getValue();");
            check_eq(result2.as<int>(), 1);

            auto result3 = engine->execute("Derived::getValue();");
            check_eq(result3.as<int>(), 2);
        });

        test("mixed_static_instance_same_name", [this]() {
            auto engine = make_engine();

            // A class can have both static and instance methods with same name
            // They're in different namespaces (Class::method vs obj.method)
            engine->execute(R"(
                class Counter {
                    int value = 0;
                    static int total = 0;

                    Counter(int v) {
                        value = v;
                        total = total + v;
                    }

                    auto getValue() { return value; }
                    static auto getValue() { return total; }
                }
            )");

            // Instance method
            auto result1 = engine->execute("Counter c = Counter(10); c.getValue();");
            check_eq(result1.as<int>(), 10);

            // Static method
            auto result2 = engine->execute("Counter::getValue();");
            check_eq(result2.as<int>(), 10);  // total accumulated

            // Create another instance
            engine->execute("Counter c2 = Counter(5);");

            // Static method sees accumulated total
            auto result3 = engine->execute("Counter::getValue();");
            check_eq(result3.as<int>(), 15);
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::override_edge_cases)