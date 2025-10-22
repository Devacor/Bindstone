#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    try {
        auto eng = jai::engine::make();
        
        std::cout << "Before registering stdlib:\n";
        std::cout << "Has print: " << eng->has_variable("print") << "\n";
        std::cout << "Has type_of: " << eng->has_variable("type_of") << "\n";
        std::cout << "Has to_string: " << eng->has_variable("to_string") << "\n";
        
        jai::stdlib::register_all(*eng);
        
        std::cout << "\nAfter registering stdlib:\n";
        std::cout << "Has print: " << eng->has_variable("print") << "\n";
        std::cout << "Has type_of: " << eng->has_variable("type_of") << "\n";
        std::cout << "Has to_string: " << eng->has_variable("to_string") << "\n";
        
        // Try to get the functions
        try {
            auto print_func = eng->get_variable("print");
            std::cout << "\nprint is a function: " << print_func.is_function() << "\n";
        } catch (const std::exception& e) {
            std::cout << "\nFailed to get print: " << e.what() << "\n";
        }
        
        try {
            auto type_of_func = eng->get_variable("type_of");
            std::cout << "type_of is a function: " << type_of_func.is_function() << "\n";
        } catch (const std::exception& e) {
            std::cout << "Failed to get type_of: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}