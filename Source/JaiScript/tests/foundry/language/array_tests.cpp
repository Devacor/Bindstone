#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class array_tests : public suite {
public:
    array_tests() : suite("Array Operations") {}
    
    void forge_tests() override {
        test("array_subscript_write", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [1, 2, 3];
                arr[1] = 99;
                arr[1];
            )");
            
            check_eq(result.as<int>(), 99);
        });
        
        test("array_swap_elements", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [10, 20];
                var temp = arr[0];
                arr[0] = arr[1];
                arr[1] = temp;
                arr[0];
            )");
            
            check_eq(result.as<int>(), 20);
        });
        
        test("array_reversal_algorithm", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                var n = 5;
                for (int i = 0; i < n / 2; ++i) {
                    var temp = arr[i];
                    arr[i] = arr[n - 1 - i];
                    arr[n - 1 - i] = temp;
                }
                arr;
            )");
            
            auto reversed = result.as_array();
            check_eq(reversed.size(), size_t(5));
            check_eq(reversed[0].as<int>(), 5);
            check_eq(reversed[1].as<int>(), 4);
            check_eq(reversed[2].as<int>(), 3);
            check_eq(reversed[3].as<int>(), 2);
            check_eq(reversed[4].as<int>(), 1);
        });
        
        test("nested_array_assignment", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var matrix = [[1, 2], [3, 4]];
                matrix[0][1] = 99;
                matrix[1][0] = 88;
                matrix;
            )");
            
            auto matrix = result.as_array();
            check_eq(matrix[0].as_array()[1].as<int>(), 99);
            check_eq(matrix[1].as_array()[0].as<int>(), 88);
        });
        
        test("array_arithmetic_modification", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [10, 20, 30];
                arr[0] += 5;
                arr[1] *= 2;
                arr[2] /= 3;
                arr;
            )");
            
            auto arr = result.as_array();
            check_eq(arr[0].as<int>(), 15);
            check_eq(arr[1].as<int>(), 40);
            check_eq(arr[2].as<int>(), 10);
        });
        
        test("array_mixed_type_assignment", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [1, "hello", 3.14];
                arr[0] = "world";
                arr[1] = 42;
                arr[2] = true;
                arr;
            )");
            
            auto arr = result.as_array();
            check_eq(arr[0].as_string(), "world");
            check_eq(arr[1].as<int>(), 42);
            check_eq(arr[2].as<bool>(), true);
        });
        
        // Array method tests
        test("array_size_method", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [1, 2, 3, 4, 5];
                arr.size();
            )");
            
            check_eq(result.as<int>(), 5);
        });
        
        test("array_push_pop_methods", [this]() {
            auto engine = engine::make();
            
            engine->execute(R"(
                var arr = [1, 2, 3];
                arr.push(4);
                arr.push(5);
            )");
            
            script_value size = engine->execute("arr.size();");
            check_eq(size.as<int>(), 5);
            
            script_value last = engine->execute("arr.pop();");
            check_eq(last.as<int>(), 5);
            
            size = engine->execute("arr.size();");
            check_eq(size.as<int>(), 4);
        });
        
        test("array_clear_empty_methods", [this]() {
            auto engine = engine::make();
            
            engine->execute("var arr = [1, 2, 3, 4, 5];");
            
            script_value empty = engine->execute("arr.empty();");
            check_eq(empty.as<bool>(), false);
            
            engine->execute("arr.clear();");
            
            script_value size = engine->execute("arr.size();");
            check_eq(size.as<int>(), 0);
            
            empty = engine->execute("arr.empty();");
            check_eq(empty.as<bool>(), true);
        });
        
        test("array_front_back_methods", [this]() {
            auto engine = engine::make();
            
            engine->execute("var arr = [10, 20, 30, 40];");
            
            script_value front = engine->execute("arr.front();");
            check_eq(front.as<int>(), 10);
            
            script_value back = engine->execute("arr.back();");
            check_eq(back.as<int>(), 40);
        });
        
        test("array_chained_operations", [this]() {
            auto engine = engine::make();
            
            script_value result = engine->execute(R"(
                var arr = [];
                arr.push(1).push(2).push(3);
                arr.size();
            )");
            
            check_eq(result.as<int>(), 3);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using array_tests = jai::foundry::tests::array_tests;
FOUNDRY_REGISTER(array_tests)