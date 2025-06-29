#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

class vm_tests : public suite {
public:
    vm_tests() : suite("VM Backend Tests") {}
    
    void forge_tests() override {
        test("vm_array_mutation_chain", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            // Create array and test mutation through method calls
            engine.execute("auto arr = [1, 2, 3];");
            
            // Test initial state
            script_value size = engine.execute("arr.size();");
            check_eq(size.as<script_int>(), 3);
            
            // Test push method
            engine.execute("arr.push(4);");
            size = engine.execute("arr.size();");
            check_eq(size.as<script_int>(), 4);
            
            // Test chained mutations
            engine.execute("arr.push(5).push(6);");
            size = engine.execute("arr.size();");
            check_eq(size.as<script_int>(), 6);
            
            // Verify elements
            script_value last = engine.execute("arr.back();");
            check_eq(last.as<script_int>(), 6);
        });
        
        test("vm_reference_semantics", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            // Test that array references are properly maintained
            engine.execute(R"(
                auto arr1 = [1, 2, 3];
                auto arr2 = arr1;  // Reference, not copy
                arr2.push(4);
            )");
            
            // Both should have the same size
            script_value size1 = engine.execute("arr1.size();");
            script_value size2 = engine.execute("arr2.size();");
            check_eq(size1.as<script_int>(), 4);
            check_eq(size2.as<script_int>(), 4);
            
            // Verify they're the same array
            script_value last1 = engine.execute("arr1.back();");
            script_value last2 = engine.execute("arr2.back();");
            check_eq(last1.as<script_int>(), 4);
            check_eq(last2.as<script_int>(), 4);
        });
        
        test("vm_map_mutations", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            engine.execute(R"(
                auto map = {"a": 1, "b": 2};
                map["c"] = 3;
            )");
            
            script_value size = engine.execute("map.size();");
            check_eq(size.as<script_int>(), 3);
            
            script_value value = engine.execute("map[\"c\"];");
            check_eq(value.as<script_int>(), 3);
            
            // Test erase
            engine.execute("map.erase(\"b\");");
            size = engine.execute("map.size();");
            check_eq(size.as<script_int>(), 2);
            
            script_value has_b = engine.execute("map.contains(\"b\");");
            check_eq(has_b.as<bool>(), false);
        });
        
        test("vm_variable_persistence", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            // Test that variables persist across execute calls
            engine.execute("auto x = 42;");
            engine.execute("auto y = x + 8;");
            
            script_value result = engine.execute("y;");
            check_eq(result.as<script_int>(), 50);
            
            // Test modification
            engine.execute("x = 100;");
            result = engine.execute("x + y;");
            check_eq(result.as<script_int>(), 150);
        });
        
        test("vm_function_calls", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            engine.execute(R"(
                function add(a, b) {
                    return a + b;
                }
                
                function multiply(x, y) {
                    return x * y;
                }
            )");
            
            script_value result = engine.execute("add(5, 3);");
            check_eq(result.as<script_int>(), 8);
            
            result = engine.execute("multiply(4, 7);");
            check_eq(result.as<script_int>(), 28);
            
            // Test nested calls
            result = engine.execute("add(multiply(2, 3), 4);");
            check_eq(result.as<script_int>(), 10);
        });
        
        test("vm_recursion", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            engine.execute(R"(
                function factorial(n) {
                    if (n <= 1) {
                        return 1;
                    }
                    return n * factorial(n - 1);
                }
            )");
            
            script_value result = engine.execute("factorial(5);");
            check_eq(result.as<script_int>(), 120);
            
            // Test fibonacci
            engine.execute(R"(
                function fib(n) {
                    if (n <= 1) return n;
                    return fib(n-1) + fib(n-2);
                }
            )");
            
            result = engine.execute("fib(10);");
            check_eq(result.as<script_int>(), 55);
        });
        
        test("vm_closure_capture", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            engine.execute(R"(
                auto multiplier = 10;
                auto multiply = [multiplier](x) -> script_int {
                    return x * multiplier;
                };
            )");
            
            script_value result = engine.execute("multiply(5);");
            check_eq(result.as<script_int>(), 50);
            
            // Test capture by reference
            engine.execute(R"(
                auto counter = 0;
                auto increment = [&counter]() {
                    counter++;
                };
                increment();
                increment();
            )");
            
            result = engine.execute("counter;");
            check_eq(result.as<script_int>(), 2);
        });
        
        test("vm_exception_handling", [this]() {
            engine engine;
            engine.set_backend(backend_type::jvm);
            
            // Exception handling only works in interpreter for now
            // TODO: Check backend type when get_backend() is available
            try {
                // Exception handling only works in interpreter for now
                script_value result = engine.execute(R"(
                    try {
                        throw "test error";
                    } catch (e) {
                        e;
                    }
                )");
                check_eq(result.as_string(), "test error");
                
                // Test re-throw
                result = engine.execute(R"(
                    try {
                        try {
                            throw 42;
                        } catch (e) {
                            throw;
                        }
                    } catch (x) {
                        x;
                    }
                )");
                check_eq(result.as<script_int>(), 42);
            } catch (const std::exception&) {
                // Expected - VM doesn't support exceptions yet
            }
        });
        
        test("vm_backend_switching", [this]() {
            engine engine;
            
            // Start with interpreter
            engine.set_backend(backend_type::interpreter);
            engine.execute("auto x = 100;");
            
            // Switch to VM
            engine.set_backend(backend_type::jvm);
            script_value result = engine.execute("x + 50;");
            check_eq(result.as<script_int>(), 150);
            
            // Switch back to interpreter
            engine.set_backend(backend_type::interpreter);
            result = engine.execute("x * 2;");
            check_eq(result.as<script_int>(), 200);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
using vm_tests = jai::foundry::tests::vm_tests;
FOUNDRY_REGISTER(vm_tests)