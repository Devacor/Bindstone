#include <iostream>
#include <chrono>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;
using namespace std::chrono;

class TestClass {
public:
    int value;
    TestClass() : value(0) {}
    TestClass(int v) : value(v) {}
    int getValue() const { return value; }
    void setValue(int v) { value = v; }
    int add(int a, int b) { return a + b + value; }
};

template<typename Engine>
void register_bindings(Engine& engine) {
    // Register 10 classes with 5 methods each
    // This simulates a realistic game binding scenario
}

int main() {
    std::cout << "=== Compilation Time Benchmark ===" << std::endl;
    std::cout << "Measuring time to compile a file with scripting engine bindings...\n" << std::endl;
    
    // JaiScript binding
    {
        auto start = high_resolution_clock::now();
        
        engine jai_engine;
        
        // Register 10 test classes
        for (int i = 0; i < 10; i++) {
            std::string class_name = "TestClass" + std::to_string(i);
            class_builder<TestClass>(jai_engine, class_name)
                .constructor<>()
                .constructor<int>()
                .method("getValue", &TestClass::getValue)
                .method("setValue", &TestClass::setValue)
                .method("add", &TestClass::add)
                .property("value", &TestClass::value)
                .build();
        }
        
        // Register 50 functions
        for (int i = 0; i < 50; i++) {
            std::string func_name = "test_func_" + std::to_string(i);
            jai_engine.add_function(func_name, [i](int x) { return x + i; });
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        std::cout << "JaiScript setup time: " << duration.count() << "ms" << std::endl;
    }
    
    std::cout << "\nNote: For compilation time comparison, run:" << std::endl;
    std::cout << "time g++ -std=c++20 -I../include benchmark_compile_time.cpp -c -o benchmark_jai.o" << std::endl;
    std::cout << "time g++ -std=c++17 -I/path/to/chaiscript benchmark_chai.cpp -c -o benchmark_chai.o" << std::endl;
    std::cout << "time g++ -std=c++17 -I/path/to/squirrel benchmark_squirrel.cpp -c -o benchmark_squirrel.o" << std::endl;
    
    return 0;
}