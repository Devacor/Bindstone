#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    auto engine = jai::engine::make();
    
    std::cout << "Testing global variable persistence..." << std::endl;
    
    try {
        // Define a variable at top level
        auto result1 = engine->execute("auto x = 42;");
        std::cout << "Step 1: Defined x = 42, is_int: " << result1.is_int() 
                  << ", is_null: " << result1.is_null() << std::endl;
        if (result1.is_int()) {
            std::cout << "  Value: " << result1.as<int>() << std::endl;
        }
        
        // Try to access it in next execute
        auto result2 = engine->execute("x");
        std::cout << "Step 2: Accessed x, is_int: " << result2.is_int() 
                  << ", is_null: " << result2.is_null() << std::endl;
        if (result2.is_int()) {
            std::cout << "  Value: " << result2.as<int>() << std::endl;
        }
        
        // Try to modify it
        auto result_modify = engine->execute("x = 100;");
        std::cout << "Step 3: Modified x to 100, is_int: " << result_modify.is_int() << std::endl;
        
        auto result3 = engine->execute("x");
        std::cout << "Step 4: Accessed x again, is_int: " << result3.is_int() 
                  << ", is_null: " << result3.is_null() << std::endl;
        if (result3.is_int()) {
            std::cout << "  Value: " << result3.as<int>() << std::endl;
            std::cout << "SUCCESS: Global variable persistence works!" << std::endl;
        } else {
            std::cout << "FAILURE: Variable x is not accessible or wrong type!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
        std::cout << "FAILURE: Exception occurred!" << std::endl;
    }
    
    return 0;
}