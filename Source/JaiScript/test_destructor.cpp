#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    using namespace jai;
    
    auto js_engine = engine::make();
    stdlib::register_all(js_engine);
    
    // Counter to track destructor calls
    int destructor_count = 0;
    js_engine->add_global_ref("destructor_count", destructor_count);
    
    try {
        // Test 1: Basic destructor
        std::cout << "Test 1: Basic destructor\n";
        js_engine->execute(R"(
            class TestObj {
                int id = 0;
                
                TestObj(int i) {
                    id = i;
                    print("TestObj " + to_string(id) + " created");
                }
                
                ~TestObj() {
                    print("TestObj " + to_string(id) + " destroyed");
                    destructor_count = destructor_count + 1;
                }
            }
            
            // Create and destroy in a scope
            {
                auto obj = TestObj(1);
            }
            print("After scope, destructor_count = " + to_string(destructor_count));
        )");
        
        // Test 2: Array of objects
        std::cout << "\nTest 2: Array of objects\n";
        std::cout << "destructor_count before test 2: " << destructor_count << std::endl;
        destructor_count = 0;
        js_engine->execute(R"(
            auto arr = [TestObj(10), TestObj(20), TestObj(30)];
            print("Created array with 3 objects");
            arr = [];  // Clear array
            print("After clearing array, destructor_count = " + to_string(destructor_count));
        )");
        
        // Test 3: Explicit null assignment
        std::cout << "\nTest 3: Explicit null assignment\n";
        std::cout << "destructor_count before test 3: " << destructor_count << std::endl;
        destructor_count = 0;
        js_engine->execute(R"(
            auto obj = TestObj(100);
            obj = null;
            print("After null assignment, destructor_count = " + to_string(destructor_count));
        )");
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}