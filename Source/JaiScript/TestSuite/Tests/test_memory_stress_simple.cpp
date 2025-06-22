#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
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
    // Create and destroy many engines
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

JAI_TEST(value_lifecycle_stress) {
    // Test Value object lifecycle
    runMemoryTest("Value object lifecycle", 10000, []() {
        Value intVal = Int(42);
        Value floatVal = Float(3.14);
        Value strVal = String("Test string");
        Value boolVal = Bool(true);
        Value nullVal;
        
        // Copy values
        Value copy1 = intVal;
        Value copy2 = strVal;
        
        // Reassign values
        intVal = floatVal;
        floatVal = strVal;
        strVal = nullVal;
    });
}

JAI_TEST(exception_handling_stress) {
    int failed_runs = 0;
    
    runMemoryTest("Exception handling", 1000, [&failed_runs]() {
        Engine engine;
        
        try {
            // This should throw
            engine.execute("undefined_function();");
        } catch (const std::exception&) {
            failed_runs++;
        }
    });
    
    // We expect all runs to fail (throw exceptions)
    expect_eq(failed_runs, 1000);
}

JAI_TEST(lambda_execution_stress) {
    Engine engine;
    
    runMemoryTest("Lambda creation and execution", 100, [&engine]() {
        engine.execute(R"(
            auto f1 = [](x) -> auto { return x * 2; };
            auto f2 = [](x, y) -> auto { return x + y; };
            
            auto result = f1(10) + f2(5, 3);
            result;
        )");
    });
}

JAI_TEST(control_flow_stress) {
    Engine engine;
    
    runMemoryTest("Control flow statements", 100, [&engine]() {
        engine.execute(R"(
            auto sum = 0;
            for (i = 0; i < 100; i = i + 1) {
                if (i % 2 == 0) {
                    sum = sum + i;
                } else {
                    sum = sum - 1;
                }
            }
            
            auto j = 0;
            while (j < 50) {
                sum = sum + 1;
                j = j + 1;
            }
            
            sum;
        )");
    });
}

JAI_TEST(arithmetic_stress) {
    Engine engine;
    
    runMemoryTest("Arithmetic operations", 1000, [&engine]() {
        engine.execute(R"(
            auto a = 1.5;
            auto b = 2.5;
            auto c = 3;
            auto d = 4;
            
            auto result = ((a + b) * (c - d)) / 2.0 + 
                         (a * b * c * d) - 
                         (a / b + c / d) +
                         (c % d);
            
            result;
        )");
    });
}

JAI_TEST(memory_leak_scenario) {
    // This test creates a scenario that might expose memory leaks
    Engine engine;
    
    // Add functions that allocate memory
    engine.addFunction("make_string", [](int size) -> String {
        return String(std::string(size, 'X'));
    });
    
    engine.addFunction("concat_strings", [](const String& a, const String& b) -> String {
        return String(a + b);
    });
    
    runMemoryTest("Potential memory leak scenario", 100, [&engine]() {
        engine.execute(R"(
            // Create strings of various sizes
            auto s1 = make_string(100);
            auto s2 = make_string(200);
            auto s3 = make_string(300);
            
            // Concatenate them multiple times
            auto result = s1;
            for (i = 0; i < 10; i = i + 1) {
                result = concat_strings(result, s2);
                result = concat_strings(result, s3);
            }
            
            // Create temporary strings
            for (j = 0; j < 20; j = j + 1) {
                auto temp = make_string(50);
                temp = concat_strings(temp, " suffix");
            }
            
            result;
        )");
    });
}

JAI_TEST(global_variable_stress) {
    // Test that repeatedly adding and using globals doesn't leak
    for (int iter = 0; iter < 100; ++iter) {
        Engine engine;
        
        // Add many globals
        for (int i = 0; i < 50; ++i) {
            std::string name = "global_" + std::to_string(i);
            engine.addGlobal(name, Int(i * 2));
        }
        
        // Use the globals
        Value result = engine.execute(R"(
            auto sum = 0;
            sum = sum + global_0 + global_10 + global_20 + global_30 + global_40;
            sum;
        )");
        
        expect_eq(result.as<Int>(), 0 + 20 + 40 + 60 + 80); // 200
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()