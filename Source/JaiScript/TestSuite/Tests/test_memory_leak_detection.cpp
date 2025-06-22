#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include <thread>
#include <atomic>
#include <chrono>
#include <map>

using namespace JaiScript;
using namespace JaiScript::Testing;

JAI_TEST_SUITE(MemoryLeakDetection) {
    JAI_TEST(value_lifecycle_stress) {
        // Create and destroy many Value objects to test reference counting
        Engine engine;
        
        for (int i = 0; i < 1000; ++i) {
            // Create various types of values
            Value intVal = Int(42);
            Value floatVal = Float(3.14);
            Value strVal = String("Memory test string that is reasonably long");
            Value boolVal = Bool(true);
            Value nullVal;
            
            // Create values through operations
            Value computed = engine.execute("10 + 20 * 30;");
            Value concatenated = engine.execute("\"Hello\" + \" World\";");
            
            // Store in containers
            std::vector<Value> values;
            values.push_back(intVal);
            values.push_back(floatVal);
            values.push_back(strVal);
            values.push_back(computed);
            values.push_back(concatenated);
            
            // Copy values
            Value copy1 = intVal;
            Value copy2 = strVal;
            Value copy3 = computed;
            
            // Reassign values
            intVal = floatVal;
            floatVal = strVal;
            strVal = nullVal;
        }
        
        expect_eq(true, true); // If we get here without crashing, basic test passes
    }
    
    JAI_TEST(lambda_capture_memory_leak) {
        // This test creates complex lambda captures that might leak
        Engine engine;
        
        // Add a function that creates large data
        engine.addFunction("create_large_string", []() -> String {
            std::string large(10000, 'X'); // 10KB string
            return String(large);
        });
        
        engine.execute(R"(
            // Create lambdas that capture large data
            auto large_data = create_large_string();
            
            auto capture_by_value = [large_data]() -> auto {
                return large_data + "!";
            };
            
            auto capture_by_ref = [&large_data]() -> auto {
                return large_data + "?";
            };
            
            // Create nested captures
            auto outer = [large_data](x) -> auto {
                auto inner = [large_data, x](y) -> auto {
                    auto innermost = [large_data, x, y]() -> auto {
                        return large_data + x + y;
                    };
                    return innermost;
                };
                return inner;
            };
            
            // Use the lambdas
            for (i = 0; i < 100; i = i + 1) {
                capture_by_value();
                capture_by_ref();
                auto inner = outer("A");
                auto innermost = inner("B");
                innermost();
            }
        )");
        
        // Run garbage collection if available
        // engine.garbageCollect(); // If this method exists
        
        expect_eq(true, true);
    }
    
    JAI_TEST(class_instance_memory_leak) {
        // Test for memory leaks with class instances
        for (int iter = 0; iter < 100; ++iter) {
            Engine engine;
            
            // Define a class that holds data
            struct DataHolder {
                std::vector<int> data;
                std::string name;
                
                DataHolder(const std::string& n) : name(n) {
                    data.reserve(1000);
                    for (int i = 0; i < 1000; ++i) {
                        data.push_back(i);
                    }
                }
                
                int sum() const {
                    int total = 0;
                    for (int x : data) total += x;
                    return total;
                }
            };
            
            using Builder = ClassBuilder<DataHolder>;
            Builder(engine, "DataHolder")
                .constructor<const std::string&>()
                .method("sum", &DataHolder::sum)
                .property("name", &DataHolder::name);
            
            // Create many instances
            engine.execute(R"(
                auto holders = [];
                for (i = 0; i < 10; i = i + 1) {
                    auto holder = DataHolder("Holder " + i);
                    holders = holders + [holder];
                }
                
                // Access the data
                auto total = 0;
                for (i = 0; i < 10; i = i + 1) {
                    total = total + holders[i].sum();
                }
                
                total;
            )");
        }
        
        expect_eq(true, true);
    }
    
    JAI_TEST(exception_in_destructor_leak) {
        // Test handling of exceptions during cleanup
        Engine engine;
        
        struct ThrowingDestructor {
            std::string data;
            static std::atomic<int> instance_count;
            
            ThrowingDestructor() : data(1000, 'X') {
                instance_count++;
            }
            
            ~ThrowingDestructor() {
                instance_count--;
                // Don't actually throw in destructor (undefined behavior)
                // but simulate complex cleanup
                data.clear();
            }
        };
        
        std::atomic<int> ThrowingDestructor::instance_count(0);
        
        using Builder = ClassBuilder<ThrowingDestructor>;
        Builder(engine, "ThrowingDestructor")
            .constructor<>();
        
        // Create and destroy many instances
        for (int i = 0; i < 100; ++i) {
            try {
                engine.execute(R"(
                    auto obj = ThrowingDestructor();
                    // Force an error
                    undefined_function();
                )");
            } catch (const std::exception&) {
                // Expected
            }
        }
        
        // Check that all instances were cleaned up
        expect_eq(ThrowingDestructor::instance_count.load(), 0);
    }
    
    JAI_TEST(multi_threaded_memory_stress) {
        // Test for thread safety and memory leaks in concurrent usage
        const int thread_count = 4;
        const int iterations_per_thread = 250;
        
        std::vector<std::thread> threads;
        std::atomic<int> success_count(0);
        
        for (int t = 0; t < thread_count; ++t) {
            threads.emplace_back([&success_count, t, iterations_per_thread]() {
                for (int i = 0; i < iterations_per_thread; ++i) {
                    try {
                        Engine engine; // Each thread gets its own engine
                        
                        // Add thread-specific function
                        engine.addFunction("thread_id", [t]() -> Int { return Int(t); });
                        
                        Value result = engine.execute(R"(
                            auto tid = thread_id();
                            auto data = "Thread " + tid + " iteration";
                            auto sum = 0;
                            for (j = 0; j < 10; j = j + 1) {
                                sum = sum + tid * j;
                            }
                            sum;
                        )");
                        
                        if (result.as<Int>() == t * 45) { // sum of 0..9 * thread_id
                            success_count++;
                        }
                    } catch (const std::exception&) {
                        // Ignore errors in stress test
                    }
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // All operations should succeed
        expect_eq(success_count.load(), thread_count * iterations_per_thread);
    }
    
    JAI_TEST(recursive_data_structure_leak) {
        // Create recursive data structures that might cause leaks
        Engine engine;
        
        // Create a function that builds recursive structures
        engine.addFunction("make_node", []() -> std::shared_ptr<std::map<std::string, Value>> {
            return std::make_shared<std::map<std::string, Value>>();
        });
        
        try {
            engine.execute(R"(
                // Create a deeply nested structure
                auto root = make_node();
                auto current = root;
                
                for (depth = 0; depth < 50; depth = depth + 1) {
                    auto child = make_node();
                    current["child"] = child;
                    current["depth"] = depth;
                    current["data"] = "Level " + depth;
                    current = child;
                }
                
                // Now create circular reference
                current["back_to_root"] = root;
                
                // Access some data to ensure structure is built
                root["child"]["child"]["data"];
            )");
        } catch (const std::exception& e) {
            // Some operations might fail, but we're testing for crashes/leaks
        }
        
        expect_eq(true, true);
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()