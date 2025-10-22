#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <cassert>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class cpp_bound_tests : public suite {
public:
    cpp_bound_tests() : suite("C++ Bound Values") {}
    
    void forge_tests() override {
        test("primitive_types_read_write", [this]() {
            auto eng = jai::engine::make();
            
            int counter = 100;
            double temperature = 20.5;
            std::string name = "Initial";
            bool flag = false;
            
            // Add global references
            eng->add_global_ref("counter", counter);
            eng->add_global_ref("temperature", temperature);
            eng->add_global_ref("name", name);
            eng->add_global_ref("flag", flag);
            
            // Test reading from script
            auto result = eng->execute("counter");
            check_eq(result.template as<int>(), 100, "Read C++ int from script");
            
            result = eng->execute("temperature");
            check_eq(result.template as<double>(), 20.5, "Read C++ double from script");
            
            result = eng->execute("name");
            check_eq(result.template as<std::string>(), "Initial", "Read C++ string from script");
            
            result = eng->execute("flag");
            check_eq(result.template as<bool>(), false, "Read C++ bool from script");
            
            // Test writing from script
            eng->execute("counter = 200;");
            check_eq(counter, 200, "Write to C++ int from script");
            
            eng->execute("temperature = 30.5;");
            check_eq(temperature, 30.5, "Write to C++ double from script");
            
            eng->execute("name = \"Updated\";");
            check_eq(name, "Updated", "Write to C++ string from script");
            
            eng->execute("flag = true;");
            check_eq(flag, true, "Write to C++ bool from script");
            
            // Test arithmetic operations
            eng->execute("counter = counter + 50;");
            check_eq(counter, 250, "Arithmetic on C++ bound int");
            
            // Test string concatenation
            eng->execute("name = name + \" Again\";");
            check_eq(name, "Updated Again", "String concatenation on C++ bound string");
        });
        
        test("cpp_changes_visible_in_script", [this]() {
            auto eng = jai::engine::make();
            
            int health = 100;
            eng->add_global_ref("health", health);
            
            // Change in C++
            health = 75;
            
            // Read from script
            auto result = eng->execute("health");
            check_eq(result.template as<int>(), 75, "C++ changes visible in script");
        });
        
        test("integration_with_functions", [this]() {
            auto eng = jai::engine::make();
            
            int score = 0;
            eng->add_global_ref("score", score);
            
            eng->add_function("add_score", [&score](int points) {
                score += points;
            });
            
            eng->execute("add_score(10);");
            check_eq(score, 10, "Function modifies C++ bound variable");
            
            eng->execute("score = score * 2;");
            check_eq(score, 20, "Script modifies same variable");
        });
        
        test("mixed_operations", [this]() {
            auto eng = jai::engine::make();
            
            int x = 10;
            int y = 20;
            eng->add_global_ref("x", x);
            eng->add_global_ref("y", y);
            eng->add_global("z", 30);  // Regular global for comparison
            
            // Mixed arithmetic
            auto result = eng->execute("x + y + z");
            check_eq(result.template as<int>(), 60, "Mixed arithmetic with bound and regular values");
            
            // Modify bound values
            eng->execute("x = x * 2; y = y + 5;");
            check_eq(x, 20, "x modified correctly");
            check_eq(y, 25, "y modified correctly");
            
            // Check they can be used in expressions
            result = eng->execute("x > y");
            check_eq(result.template as<bool>(), false, "Comparison with bound values");
        });
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::cpp_bound_tests)