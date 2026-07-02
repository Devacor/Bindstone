#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/jaiscript.hpp>

namespace jai::foundry::tests {

class override_tests : public suite {
public:
    override_tests() : suite("Override and Virtual Tests") {}
    
    void forge_tests() override {
        test("override_promotes_to_virtual", [this]() {
            auto engine = make_engine();
            
            // Base class with non-virtual method
            engine->execute(R"(
                class Base {
                    auto greet() { return "Base"; }
                }
            )");
            
            // Derived class with override - should promote Base::greet to virtual
            engine->execute(R"(
                class Derived : Base {
                    auto greet() override { return "Derived"; }
                }
            )");
            
            // Test polymorphic behavior
            auto result = engine->execute(R"(
                Base b = Derived();
                b.greet();
            )");
            
            check_eq(result.as<std::string>(), "Derived");
        });
        
        test("override_keyword_required_for_virtual_methods", [this]() {
            auto engine = make_engine();
            
            // Base class with method that becomes virtual
            engine->execute(R"(
                class Animal {
                    auto speak() { return "..."; }
                }
                
                class Dog : Animal {
                    auto speak() override { return "Woof"; }
                }
            )");
            
            // Now Animal::speak is virtual, so Cat MUST use override
            try {
                engine->execute(R"(
                    class Cat : Animal {
                        auto speak() { return "Meow"; }  // Missing override!
                    }
                )");
                throw test_failure("Expected exception for missing override keyword");
            } catch (const runtime_error& e) {
                check_true(std::string(e.what()).find("Must use 'override' keyword") != std::string::npos,
                          "Exception message should contain 'Must use 'override' keyword'");
            }
        });
        
        test("override_without_base_method_throws", [this]() {
            auto engine = make_engine();
            
            engine->execute(R"(
                class Base {
                    auto foo() { return 42; }
                }
            )");
            
            try {
                engine->execute(R"(
                    class Derived : Base {
                        auto bar() override { return 99; }  // No bar() in Base!
                    }
                )");
                throw test_failure("Expected exception for override without base method");
            } catch (const runtime_error& e) {
                check_true(std::string(e.what()).find("no base class method found") != std::string::npos,
                          "Exception message should contain 'no base class method found'");
            }
        });
        
        test("shadowing_without_override_throws", [this]() {
            auto engine = make_engine();
            
            engine->execute(R"(
                class Base {
                    auto compute() { return 1; }
                }
            )");
            
            try {
                engine->execute(R"(
                    class Derived : Base {
                        auto compute() { return 2; }  // Shadows Base::compute without override
                    }
                )");
                throw test_failure("Expected exception for shadowing without override");
            } catch (const runtime_error& e) {
                check_true(std::string(e.what()).find("shadows base class method") != std::string::npos,
                          "Exception message should contain 'shadows base class method'");
            }
        });
        
        test("multi_level_virtual_promotion", [this]() {
            auto engine = make_engine();
            
            // Three levels of inheritance
            engine->execute(R"(
                class A {
                    auto value() { return 1; }
                }
                
                class B : A {
                    auto value() override { return 2; }
                }
                
                class C : B {
                    auto value() override { return 3; }
                }
            )");
            
            // Test polymorphism at each level
            auto result1 = engine->execute("A a = B(); a.value();");
            check_eq(result1.as<int>(), 2);
            
            auto result2 = engine->execute("A a = C(); a.value();");
            check_eq(result2.as<int>(), 3);
            
            auto result3 = engine->execute("B b = C(); b.value();");
            check_eq(result3.as<int>(), 3);
        });
        
        test("constructor_not_affected_by_override", [this]() {
            auto engine = make_engine();
            
            // Constructors shouldn't be virtual/override
            engine->execute(R"(
                class Base {
                    int x;
                    Base(int val) { x = val; }
                }
                
                class Derived : Base {
                    int y;
                    Derived(int a, int b) : super(a) { y = b; }
                }
            )");
            
            auto result = engine->execute(R"(
                auto d = Derived(10, 20);
                d.x + d.y;
            )");
            
            check_eq(result.as<int>(), 30);
        });
    }
};

}

FOUNDRY_REGISTER(jai::foundry::tests::override_tests)