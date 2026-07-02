#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <iostream>

namespace jai::foundry::tests {

class cat_class_syntax_test : public suite {
public:
    cat_class_syntax_test() : suite("Cat Class Syntax Test") {}
    
    void forge_tests() override {
        test("exact_user_syntax", [this]() {
            auto js_engine = make_engine();
            
            const char* script = R"(
                class Cat { 
                    int a = 0; 
                    Cat(int val) { 
                        a = val; 
                    } 
                }
                
                auto cat = Cat(42);
                cat.a
            )";
            
            try {
                auto result = js_engine->execute(script);
                std::cout << "Script executed successfully!" << std::endl;
                
                if (result.is_int()) {
                    std::cout << "Result is int: " << result.as_int() << std::endl;
                    check_eq(result.as_int(), 42);
                } else {
                    std::cout << "Result type: " << result.to_string() << std::endl;
                    check(false); // "Result is not an int");
                }
            } catch (const std::exception& e) {
                std::cout << "Exception caught: " << e.what() << std::endl;
                check(false); // std::string("Script execution failed: ") + e.what());
            }
        });
        
        test("simpler_no_constructor", [this]() {
            auto js_engine = make_engine();
            
            const char* script = R"(
                class Cat { 
                    int a = 42; 
                }
                
                auto cat = Cat();
                cat.a
            )";
            
            try {
                auto result = js_engine->execute(script);
                std::cout << "Simple class test executed successfully!" << std::endl;
                
                if (result.is_int()) {
                    std::cout << "Result is int: " << result.as_int() << std::endl;
                    check_eq(result.as_int(), 42);
                } else {
                    std::cout << "Result type: " << result.to_string() << std::endl;
                    check(false); // "Result is not an int");
                }
            } catch (const std::exception& e) {
                std::cout << "Exception caught: " << e.what() << std::endl;
                check(false); // std::string("Simple class test failed: ") + e.what());
            }
        });
        
        test("field_assignment_without_this", [this]() {
            auto js_engine = make_engine();
            
            const char* script = R"(
                class Cat { 
                    int a = 0; 
                    Cat(int val) { 
                        a = val; 
                    } 
                }
                
                auto cat = Cat(99);
                cat.a
            )";
            
            try {
                auto result = js_engine->execute(script);
                std::cout << "Field assignment test executed successfully!" << std::endl;
                
                if (result.is_int()) {
                    std::cout << "Result is int: " << result.as_int() << std::endl;
                    check_eq(result.as_int(), 99);
                } else {
                    std::cout << "Result type: " << result.to_string() << std::endl;
                    check(false); // "Result is not an int");
                }
            } catch (const std::exception& e) {
                std::cout << "Exception caught: " << e.what() << std::endl;
                check(false); // std::string("Field assignment test failed: ") + e.what());
            }
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using cat_class_syntax_test = jai::foundry::tests::cat_class_syntax_test;
FOUNDRY_REGISTER(cat_class_syntax_test)