#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

namespace jai::foundry::tests {

class global_persistence_tests : public suite {
public:
    global_persistence_tests() : suite("Global Variable Persistence Tests") {}
    
    void forge_tests() override {
        test("top_level_variable_persistence", [this]() {
            auto eng = engine::make();
            
            // Define a variable at top level
            eng->execute("auto x = 42;");
            
            // It should be accessible in the next execute call
            auto result = eng->execute("x");
            check_eq(result.as<int>(), 42);
            
            // Should be able to modify it
            eng->execute("x = 100;");
            result = eng->execute("x");
            check_eq(result.as<int>(), 100);
        });
        
        test("top_level_multiple_variables", [this]() {
            auto eng = engine::make();
            
            // Define multiple variables
            eng->execute(R"(
                auto a = 10;
                auto b = 20;
                auto c = a + b;
            )");
            
            // All should be accessible
            check_eq(eng->execute("a").as<int>(), 10);
            check_eq(eng->execute("b").as<int>(), 20);
            check_eq(eng->execute("c").as<int>(), 30);
            
            // Modify one and use others
            eng->execute("a = 5;");
            auto result = eng->execute("a + b + c");
            check_eq(result.as<int>(), 55); // 5 + 20 + 30
        });
        
        test("top_level_object_persistence", [this]() {
            auto eng = engine::make();

            // Define a map at top level
            eng->execute(R"(
                auto obj = {
                    "x": 10,
                    "y": 20
                };
            )");

            // Should be accessible
            check_eq(eng->execute("obj[\"x\"]").as<int>(), 10);
            check_eq(eng->execute("obj[\"y\"]").as<int>(), 20);

            // Modify it
            eng->execute("obj[\"x\"] = 30;");
            check_eq(eng->execute("obj[\"x\"]").as<int>(), 30);
        });
        
        test("top_level_function_persistence", [this]() {
            auto eng = engine::make();
            
            // Define a function at top level
            eng->execute(R"(
                function add(a, b) {
                    return a + b;
                }
            )");
            
            // Should be callable in next execute
            auto result = eng->execute("add(5, 3)");
            check_eq(result.as<int>(), 8);
            
            // Define a variable using the function
            eng->execute("auto sum = add(10, 20);");
            check_eq(eng->execute("sum").as<int>(), 30);
        });
        
        test("top_level_class_persistence", [this]() {
            auto eng = engine::make();
            
            // Define a class at top level
            eng->execute(R"(
                class Point {
                    int x = 0;
                    int y = 0;

                    Point(x_, y_) {
                        x = x_;
                        y = y_;
                    }

                    int sum() {
                        return x + y;
                    }
                }
            )");
            
            // Should be able to create instances in next execute
            eng->execute("auto p = Point(3, 4);");
            
            // Instance should persist
            auto result = eng->execute("p.sum()");
            check_eq(result.as<int>(), 7);
            
            // Modify instance
            eng->execute("p.x = 10;");
            result = eng->execute("p.sum()");
            check_eq(result.as<int>(), 14);
        });
        
        test("explicit_global_vs_top_level", [this]() {
            auto eng = engine::make();
            
            // Add an explicit global
            eng->add_global("explicit_var", 100);
            
            // Define a top-level variable
            eng->execute("auto script_var = 200;");
            
            // Both should be accessible
            check_eq(eng->execute("explicit_var").as<int>(), 100);
            check_eq(eng->execute("script_var").as<int>(), 200);
            
            // Both should be modifiable
            eng->execute("explicit_var = 150;");
            eng->execute("script_var = 250;");
            
            check_eq(eng->execute("explicit_var").as<int>(), 150);
            check_eq(eng->execute("script_var").as<int>(), 250);
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::global_persistence_tests)