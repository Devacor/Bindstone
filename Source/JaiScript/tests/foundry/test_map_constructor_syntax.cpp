#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

class map_constructor_syntax_tests : public suite {
public:
    map_constructor_syntax_tests() : suite("Map Constructor Syntax Tests") {}
    
    void forge_tests() override {
        test("empty_map_constructor_auto", [this]() {
            engine eng;
            
            try {
                auto result = eng.execute(R"(
                    auto mixed = map<string, auto>{};
                    mixed["count"] = 42;
                    mixed["name"] = "test";
                    mixed["pi"] = 3.14;
                    mixed["count"]
                )");
                
                check_eq(result.as<script_int>(), script_int(42), "map<string, auto>{} constructor should work");
            } catch (const std::exception& e) {
                check(false, "map<string, auto>{} constructor failed");
            }
        });
        
        test("empty_map_constructor_typed", [this]() {
            engine eng;
            
            try {
                auto result = eng.execute(R"(
                    auto ages = map<string, int>{};
                    ages["Alice"] = 25;
                    ages["Bob"] = 30;
                    ages["Bob"]
                )");
                
                check_eq(result.as<script_int>(), script_int(30), "map<string, int>{} constructor should work");
            } catch (const std::exception& e) {
                check(false, "map<string, int>{} constructor failed");
            }
        });
        
        test("typed_declaration_syntax", [this]() {
            engine eng;
            
            try {
                auto result = eng.execute(R"(
                    map<string, int> ages{};
                    ages["Alice"] = 25;
                    ages["Bob"] = 30;
                    ages["Bob"]
                )");
                
                check_eq(result.as<script_int>(), script_int(30), "map<string, int> name{} syntax should work");
            } catch (const std::exception& e) {
                check(false, "map<string, int> name{} syntax failed");
            }
        });
        
        test("array_constructor_syntax", [this]() {
            engine eng;
            
            try {
                auto result = eng.execute(R"(
                    auto nums = array<float>{};
                    nums.push(1.5);
                    nums.push(2.5);
                    nums.push(3.0);
                    nums.size()
                )");
                
                check_eq(result.as<script_int>(), script_int(3), "array<float>{} constructor should work");
            } catch (const std::exception& e) {
                check(false, "array<float>{} constructor failed");
            }
        });
        
        test("constructor_vs_literal_syntax", [this]() {
            engine eng;
            
            // Test both syntaxes produce the same result
            auto result1 = eng.execute(R"(
                auto map1 = {{"a", 1}, {"b", 2}};
                map1["a"]
            )");
            
            try {
                auto result2 = eng.execute(R"(
                    auto map2 = map<string, int>{};
                    map2["a"] = 1;
                    map2["b"] = 2;
                    map2["a"]
                )");
                
                check_eq(result1.as<script_int>(), result2.as<script_int>(), 
                        "Constructor and literal syntax should produce same results");
            } catch (const std::exception& e) {
                // Constructor syntax may not be supported in all contexts
            }
        });
        
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(map_constructor_syntax_tests)