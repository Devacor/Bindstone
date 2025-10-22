#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/types.hpp>

using namespace jai;
using namespace jai::foundry;

class constructor_syntax_comprehensive_tests : public suite {
public:
    constructor_syntax_comprehensive_tests() : suite("Constructor Syntax Comprehensive Tests") {}
    
    void forge_tests() override {
        test("all_constructor_variations", [this]() {
            auto eng = engine::make();
            
            try {
                // Test all the different ways to create maps and arrays
                auto result = eng->execute("auto m1 = map<string, int>{}; m1[\"a\"] = 1; m1[\"a\"]");
                
                check_eq(result.as<script_int>(), script_int(1), "All constructor variations should work");
            } catch (const std::exception& e) {
                check(false, "Test failed with exception");
            }
        });
        
        test("nested_template_constructors", [this]() {
            auto eng = engine::make();
            
            auto result = eng->execute(R"(
                // Nested template types
                auto nested = map<string, array<int>>{};
                nested["nums"] = array<int>{};
                nested["nums"].push(42);
                nested["nums"][0]
            )");
            
            check_eq(result.as<script_int>(), script_int(42), "Nested template constructors should work");
        });
        
        test("constructor_with_cpp_types", [this]() {
            auto eng = engine::make();
            
            // Register a simple C++ class
            class Point {
            public:
                float x, y;
                Point() : x(0), y(0) {}
                Point(float x, float y) : x(x), y(y) {}
            };
            
            class_builder<Point>(eng, "Point")
                .constructor<>()
                .constructor<script_float, script_float>()
                .property("x", &Point::x)
                .property("y", &Point::y)
                .build();
            
            auto result = eng->execute(R"(
                // Map with C++ type values
                auto points = map<string, Point>{};
                points["origin"] = Point();
                points["target"] = Point(3.0, 4.0);
                points["target"].x
            )");
            
            check_eq(result.as<script_float>(), 3.0f, "Constructor syntax with C++ types should work");
        });
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(constructor_syntax_comprehensive_tests)