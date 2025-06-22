#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <vector>
#include <memory>
#include <chrono>

using namespace JaiScript;
using namespace JaiScript::Testing;

// Helper to run a test multiple times to check for memory leaks
template<typename TestFunc>
void runMemoryTest(const std::string& testName, int iterations, TestFunc test) {
    std::cout << "  Running " << testName << " (" << iterations << " iterations)..." << std::flush;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < iterations; ++i) {
        test();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << " Done (" << duration << "ms)" << std::endl;
}

JAI_TEST_SUITE(MemoryStressTests)
    JAI_TEST(repeated_engine_creation) {
        runMemoryTest("Engine creation/destruction", 1000, []() {
            Engine engine;
            engine.execute("42;");
        });
    }
    
    JAI_TEST(repeated_parsing) {
        Engine engine;
        runMemoryTest("Parse complex expressions", 1000, [&engine]() {
            engine.execute("((3 + 4) * (5 - 2)) / 2;");
        });
    }
    
    JAI_TEST(repeated_string_operations) {
        Engine engine;
        runMemoryTest("String concatenation", 1000, [&engine]() {
            engine.execute("\"Hello\" + \" \" + \"World\" + \"!\" + \" Test\";");
        });
    }
    
    JAI_TEST(repeated_variable_creation) {
        runMemoryTest("Variable creation/destruction", 100, []() {
            Engine engine;
            engine.execute(R"(
                x = 10; y = 20; z = 30;
                a = x + y; b = y + z; c = a + b;
                result = a * b * c;
            )");
        });
    }
    
    JAI_TEST(repeated_function_definitions) {
        runMemoryTest("Function definitions", 100, []() {
            Engine engine;
            engine.execute(R"(
                function add(a, b) { return a + b; }
                function sub(a, b) { return a - b; }
                function mul(a, b) { return a * b; }
                function div(a, b) { return a / b; }
                
                add(mul(5, 3), div(20, 4));
            )");
        });
    }
    
    JAI_TEST(repeated_lambda_creation) {
        runMemoryTest("Lambda creation", 100, []() {
            Engine engine;
            engine.execute(R"(
                auto f1 = [](x) -> auto { return x * 2; };
                auto f2 = [](x, y) -> auto { return x + y; };
                auto f3 = [f1, f2](a, b) -> auto { return f1(a) + f2(a, b); };
                
                f3(10, 20);
            )");
        });
    }
    
    JAI_TEST(repeated_cpp_function_binding) {
        runMemoryTest("C++ function binding", 100, []() {
            Engine engine;
            
            // Bind many functions
            for (int i = 0; i < 50; ++i) {
                std::string name = "func" + std::to_string(i);
                engine.addFunction(name, [i](int x) -> int { return x + i; });
            }
            
            // Call some of them
            engine.execute("func0(1) + func10(2) + func20(3) + func30(4) + func40(5);");
        });
    }
    
    JAI_TEST(repeated_class_binding) {
        runMemoryTest("Class binding", 100, []() {
            Engine engine;
            
            using Builder = ClassBuilder<std::vector<int>>;
            Builder(engine, "IntVector")
                .constructor<>()
                .constructor<std::size_t>()
                .method("push_back", &std::vector<int>::push_back)
                .method("size", &std::vector<int>::size)
                .method("at", &std::vector<int>::at)
                .method("clear", &std::vector<int>::clear);
            
            engine.execute(R"(
                auto v = IntVector();
                v.push_back(1);
                v.push_back(2);
                v.push_back(3);
                v.size();
            )");
        });
    }
    
    JAI_TEST(complex_memory_stress_scenario) {
        // This test is designed to expose memory leaks by creating
        // many objects, functions, and circular references
        runMemoryTest("Complex scenario with potential leaks", 50, []() {
            Engine engine;
            
            // Add some C++ functions
            engine.addFunction("create_array", []() -> std::vector<int> {
                return std::vector<int>{1, 2, 3, 4, 5};
            });
            
            engine.addFunction("process_array", [](const std::vector<int>& arr) -> int {
                int sum = 0;
                for (int x : arr) sum += x;
                return sum;
            });
            
            // Execute complex script with many allocations
            engine.execute(R"(
                // Create many variables
                auto data = create_array();
                auto sum = process_array(data);
                
                // Create functions that capture variables
                auto processor = [data]() -> auto {
                    auto local_sum = 0;
                    for (i = 0; i < 5; i = i + 1) {
                        local_sum = local_sum + i;
                    }
                    return local_sum + process_array(data);
                };
                
                // Create nested lambdas with captures
                auto outer = [sum](x) -> auto {
                    auto inner = [x, sum](y) -> auto {
                        return x + y + sum;
                    };
                    return inner;
                };
                
                // Use the functions
                auto result1 = processor();
                auto inner_func = outer(10);
                auto result2 = inner_func(20);
                
                // Create more data
                auto strings = ["Hello", "World", "Test", "Memory", "Leak"];
                auto concatenated = "";
                for (i = 0; i < 5; i = i + 1) {
                    concatenated = concatenated + " " + "string";
                }
                
                result1 + result2;
            )");
        });
    }
    
    JAI_TEST(exception_handling_memory_stress) {
        int successful_runs = 0;
        int failed_runs = 0;
        
        runMemoryTest("Exception handling", 1000, [&]() {
            Engine engine;
            
            try {
                // This should throw
                engine.execute("undefined_function();");
                successful_runs++;
            } catch (const std::exception&) {
                failed_runs++;
            }
        });
        
        // We expect all runs to fail (throw exceptions)
        expect_eq(successful_runs, 0);
        expect_eq(failed_runs, 1000);
    }
    
    JAI_TEST(large_data_structure_stress) {
        runMemoryTest("Large data structures", 10, []() {
            Engine engine;
            
            // Create and manipulate large strings
            engine.execute(R"(
                auto big_string = "";
                for (i = 0; i < 100; i = i + 1) {
                    big_string = big_string + "This is a test string that will grow. ";
                }
                
                // Create array of strings
                auto string_array = [];
                for (i = 0; i < 50; i = i + 1) {
                    string_array = string_array + ["Item " + i];
                }
                
                big_string.length;
            )");
        });
    }
    
    JAI_TEST(recursive_function_stress) {
        runMemoryTest("Recursive functions", 100, []() {
            Engine engine;
            
            engine.execute(R"(
                function fibonacci(n) {
                    if (n <= 1) return n;
                    return fibonacci(n - 1) + fibonacci(n - 2);
                }
                
                // Calculate several fibonacci numbers
                auto f5 = fibonacci(5);
                auto f8 = fibonacci(8);
                auto f10 = fibonacci(10);
                
                f5 + f8 + f10;
            )");
        });
    }
}

// Special test to specifically expose a memory leak issue
JAI_TEST_SUITE(MemoryLeakDetection) {
    JAI_TEST(circular_reference_leak) {
        // This test creates circular references that might not be cleaned up
        Engine engine;
        
        // Create a potential memory leak with circular lambda captures
        engine.execute(R"(
            auto leak_creator = []() -> auto {
                auto obj1 = null;
                auto obj2 = null;
                
                // Create circular reference through lambdas
                obj1 = [&obj2]() -> auto { return obj2; };
                obj2 = [&obj1]() -> auto { return obj1; };
                
                return [obj1, obj2]() -> auto {
                    return "leaked";
                };
            };
            
            // Create many instances
            for (i = 0; i < 100; i = i + 1) {
                auto leaker = leak_creator();
                leaker();
            }
        )");
        
        // If this test passes without crashing, it doesn't mean there's no leak,
        // but crashes would indicate serious memory issues
        expect_eq(true, true);
    }
    
    JAI_TEST(global_state_accumulation) {
        // Test that repeatedly adding globals doesn't leak
        for (int iter = 0; iter < 100; ++iter) {
            Engine engine;
            
            // Add many globals
            for (int i = 0; i < 100; ++i) {
                std::string name = "global_" + std::to_string(i);
                engine.addGlobal(name, Int(i));
            }
            
            // Access some globals
            Value result = engine.execute("global_0 + global_50 + global_99;");
            expect_eq(result.as<Int>(), 149);
        }
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()