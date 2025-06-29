#include <iostream>
#include <chrono>
#include "jaiscript/class_system.hpp"
#include "jaiscript/jvm/vm_class_system.hpp"
#include "jaiscript/jvm/vm_function_integration.hpp"

using namespace jaiscript;
using namespace jaiscript::jvm;

// Demonstration of VM bytecode functions with classes
int main() {
    std::cout << "JaiScript VM Function & Class System Demo\n";
    std::cout << "=========================================\n\n";
    
    // Create VM-aware engine with full class system
    vm_aware_engine engine;
    
    // Enable both class systems
    auto interpreter_classes = enable_class_system(engine);
    auto vm_classes = enable_vm_class_system(
        engine.get_vm_executor(),
        engine.get_vm_compiler()
    );
    
    // Enable cross-calling
    vm_classes->integrate_with_interpreter(interpreter_classes.get());
    
    // Enable profiling
    g_function_profiler.start_profiling();
    g_class_profiler.start_profiling();
    
    try {
        // Test 1: Define standalone function that compiles to bytecode
        std::cout << "Test 1: Standalone Function Compilation\n";
        engine.execute(R"(
            // This function is compiled to bytecode
            auto factorial(auto n) -> auto {
                if (n <= 1) return 1;
                return n * factorial(n - 1);
            }
            
            // Test the function
            print("Factorial(10) = " + factorial(10));
        )");
        
        // Test 2: Define class with methods compiled to bytecode
        std::cout << "\nTest 2: Class Methods as Bytecode\n";
        engine.execute(R"(
            class Calculator {
                float value = 0.0;
                
                Calculator(float initial) {
                    value = initial;
                }
                
                // These methods are compiled to bytecode
                float add(float x) {
                    value = value + x;
                    return value;
                }
                
                float multiply(float x) {
                    value = value * x;
                    return value;
                }
                
                float compute(float a, float b, float c) {
                    // Complex computation to show bytecode efficiency
                    auto temp = a * b + c;
                    temp = temp * temp;
                    return temp / (a + b + c);
                }
            }
            
            auto calc = make_shared<Calculator>(10.0);
            print("Add 5: " + calc->add(5.0));
            print("Multiply by 3: " + calc->multiply(3.0));
            print("Complex: " + calc->compute(2.0, 3.0, 4.0));
        )");
        
        // Test 3: Virtual method dispatch with bytecode
        std::cout << "\nTest 3: Virtual Methods in Bytecode\n";
        engine.execute(R"(
            class Shape {
                string name = "Shape";
                
                // Virtual method (becomes virtual when overridden)
                float area() {
                    return 0.0;
                }
                
                string describe() {
                    return name + " with area: " + area();
                }
            }
            
            class Circle : Shape {
                float radius = 1.0;
                
                Circle(float r) : super() {
                    radius = r;
                    name = "Circle";
                }
                
                // Override triggers virtualization
                float area() override {
                    return 3.14159 * radius * radius;
                }
            }
            
            class Square : Shape {
                float side = 1.0;
                
                Square(float s) : super() {
                    side = s;
                    name = "Square";
                }
                
                float area() override {
                    return side * side;
                }
            }
            
            // Test polymorphic calls
            auto shapes = [
                make_shared<Circle>(5.0),
                make_shared<Square>(4.0),
                make_shared<Circle>(3.0)
            ];
            
            for (auto shape : shapes) {
                print(shape->describe());
            }
        )");
        
        // Test 4: Function as first-class value
        std::cout << "\nTest 4: Functions as Values\n";
        engine.execute(R"(
            // Store function in variable
            auto operation = factorial;
            print("Using function variable: " + operation(6));
            
            // Pass function as parameter
            auto apply_twice(auto f, auto x) -> auto {
                return f(f(x));
            }
            
            auto double_it(auto x) -> auto {
                return x * 2;
            }
            
            print("Apply twice: " + apply_twice(double_it, 5));
        )");
        
        // Test 5: Closures with bytecode
        std::cout << "\nTest 5: Closures in Bytecode\n";
        engine.execute(R"(
            auto make_counter(auto start) -> auto {
                auto count = start;
                
                // Return closure that captures 'count'
                return auto() -> auto {
                    count = count + 1;
                    return count;
                };
            }
            
            auto counter1 = make_counter(0);
            auto counter2 = make_counter(100);
            
            print("Counter1: " + counter1());  // 1
            print("Counter1: " + counter1());  // 2
            print("Counter2: " + counter2());  // 101
            print("Counter1: " + counter1());  // 3
        )");
        
        // Test 6: Performance comparison
        std::cout << "\nTest 6: Performance Comparison\n";
        
        // Define computation-heavy function
        engine.execute(R"(
            auto fibonacci(auto n) -> auto {
                if (n <= 1) return n;
                return fibonacci(n - 1) + fibonacci(n - 2);
            }
        )");
        
        // Benchmark
        auto benchmark = vm_function_integration_utils::benchmark_function(
            "fibonacci",
            {script_value(25)},
            engine,
            10
        );
        
        std::cout << "Fibonacci(25) x 10 iterations:\n";
        std::cout << "  Interpreter: " << benchmark.interpreter_cycles / 1e6 << " ms\n";
        std::cout << "  Bytecode: " << benchmark.bytecode_cycles / 1e6 << " ms\n";
        std::cout << "  Speedup: " << benchmark.speedup_factor << "x\n";
        
        // Test 7: Cross-backend calling
        std::cout << "\nTest 7: Cross-Backend Function Calls\n";
        
        // Define function in interpreter
        engine.execute_with_interpreter(R"(
            auto interpreter_func(auto x) -> auto {
                print("Called from interpreter backend");
                return x * 2;
            }
        )");
        
        // Call from VM script
        engine.execute_with_vm(R"(
            auto vm_func(auto x) -> auto {
                print("Called from VM backend");
                // This calls the interpreter function from VM
                return interpreter_func(x) + 10;
            }
            
            print("Result: " + vm_func(5));  // Should print 20
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    
    // Stop profiling and show results
    g_function_profiler.stop_profiling();
    g_class_profiler.stop_profiling();
    
    std::cout << "\n" << g_function_profiler.generate_report();
    std::cout << "\n" << g_class_profiler.generate_optimization_report();
    
    // Show optimization candidates
    auto candidates = g_function_profiler.get_optimization_candidates();
    if (!candidates.empty()) {
        std::cout << "\nOptimization Candidates:\n";
        for (const auto& candidate : candidates) {
            std::cout << "  - " << candidate << "\n";
        }
    }
    
    return 0;
}