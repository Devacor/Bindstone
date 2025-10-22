#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>
#include <chrono>
#include <vector>
#include <cmath>

using namespace jai;
using namespace std::chrono;

void measure(const std::string& name, std::function<void()> fn, int iterations = 1000) {
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        fn();
    }
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    std::cout << name << ": " << (duration / iterations) << " μs/iteration\n";
}

int main() {
    std::cout << "=== JaiScript Performance Test ===\n\n";
    
    // Test 1: Engine creation
    measure("Engine creation", []() {
        auto e = engine::make();
    }, 100);
    
    // Test 2: Simple arithmetic
    auto e = engine::make();
    measure("Simple arithmetic (1 + 2 * 3)", [&e]() {
        e->execute("1 + 2 * 3");
    });
    
    // Test 3: Variable assignment
    measure("Variable assignment", [&e]() {
        e->execute("auto x = 42; x = x + 1; x");
    });
    
    // Test 4: Function definition and call  
    try {
        e->execute("auto add = [](int a, int b) -> auto { return a + b; };");
        measure("Function call", [&e]() {
            e->execute("add(5, 3)");
        });
    } catch (const std::exception& ex) {
        std::cout << "Function call: Error - " << ex.what() << "\n";
    }
    
    // Test 5: Loop performance
    measure("Loop (100 iterations)", [&e]() {
        e->execute(R"(
            auto sum = 0;
            for (auto i = 0; i < 100; ++i) {
                sum = sum + i;
            }
            sum
        )");
    }, 100);
    
    // Test 6: Array operations
    e->execute("auto arr = [1, 2, 3, 4, 5];");
    measure("Array access", [&e]() {
        e->execute("arr[2]");
    });
    
    // Test 7: Class operations (if available)
    try {
        // Create a simple test class
        class TestClass {
        public:
            int value = 0;
            void increment() { value++; }
            int getValue() const { return value; }
        };
        
        class_builder<TestClass>(e, "TestClass")
            .constructor<>()
            .method("increment", &TestClass::increment)
            .method("getValue", &TestClass::getValue)
            .property("value", &TestClass::value)
            .build();
            
        e->execute("auto obj = TestClass();");
        measure("Class method call", [&e]() {
            e->execute("obj.increment(); obj.getValue()");
        });
    } catch (...) {
        std::cout << "Class operations: Not available\n";
    }
    
    // Test VM backend if available
    std::cout << "\n--- Testing VM Backend ---\n";
    e->set_backend(backend_type::jvm);
    
    measure("VM: Simple arithmetic", [&e]() {
        e->execute("1 + 2 * 3");
    });
    
    measure("VM: Variable assignment", [&e]() {
        e->execute("auto x = 42; x = x + 1; x");
    });
    
    measure("VM: Loop (100 iterations)", [&e]() {
        e->execute(R"(
            auto sum = 0;
            for (auto i = 0; i < 100; ++i) {
                sum = sum + i;
            }
            sum
        )");
    }, 100);
    
    return 0;
}