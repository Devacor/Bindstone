#include <iostream>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/engine.hpp>

int main() {
    try {
        // Create a float script_value
        auto eng = jai::engine::make();
        jai::script_value float_val(7.8, eng);
        
        std::cout << "Created float value: " << float_val.as<double>() << std::endl;
        std::cout << "Type: " << static_cast<int>(float_val.type()) << std::endl;
        std::cout << "Is float: " << float_val.is_float() << std::endl;
        std::cout << "Is int: " << float_val.is_int() << std::endl;
        
        // Try to convert to int
        try {
            int int_val = float_val.as<int>();
            std::cout << "Successfully converted to int: " << int_val << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Failed to convert float to int: " << e.what() << std::endl;
            
            // Alternative: get as double first, then cast
            double d = float_val.as<double>();
            int truncated = static_cast<int>(d);
            std::cout << "Alternative: get as double (" << d << ") then cast to int: " << truncated << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}