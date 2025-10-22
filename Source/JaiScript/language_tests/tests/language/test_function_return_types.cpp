#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/types.hpp>

using namespace jai;
using namespace jai::foundry;

class function_return_type_tests : public suite {
public:
    function_return_type_tests() : suite("Function Return Type Tests") {}
    
    void forge_tests() override {
        test("auto_return_by_default", [this]() {
            auto eng = engine::make();
            
            // Function without return type should infer return type
            auto result = eng->execute(R"(
                function double(:x) { return x * 2; }
                double(21)
            )");
            
            check_eq(result.as<script_int>(), script_int(42));
        });
        
        test("auto_return_with_no_return_statement", [this]() {
            auto eng = engine::make();
            
            // Function without return statement should return null
            auto result = eng->execute(R"(
                function doNothing(:x) { 
                    x + 1;  // Expression but no return
                }
                doNothing(5)
            )");
            
            check(result.is_null(), "Function without return should return null");
        });
        
        test("explicit_void_return", [this]() {
            auto eng = engine::make();
            
            // Explicit void function
            auto result = eng->execute(R"(
                void printOnly(:x) { 
                    // In real code this might print or have side effects
                    x + 1;
                }
                printOnly(5)
            )");
            
            check(result.is_null(), "Void function should return null");
        });
        
        test("explicit_int_return", [this]() {
            auto eng = engine::make();
            
            // Explicit int return type
            auto result = eng->execute(R"(
                int add(:a, :b) { 
                    return a + b;
                }
                add(10, 32)
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Explicit int return should work");
        });
        
        test("arrow_auto_syntax", [this]() {
            auto eng = engine::make();
            
            // Arrow with auto (no type)
            auto result = eng->execute(R"(
                function multiply(:x, :y) -> { 
                    return x * y;
                }
                multiply(6, 7)
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Arrow auto syntax should work");
        });
        
        test("arrow_explicit_type_syntax", [this]() {
            auto eng = engine::make();
            
            // Arrow with explicit type
            auto result = eng->execute(R"(
                function divide(:x, :y) -> float { 
                    return x / y;
                }
                divide(84.0, 2.0)
            )");
            
            check_eq(result.as<script_float>(), script_float(42.0), "Arrow with type should work");
        });
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(function_return_type_tests)