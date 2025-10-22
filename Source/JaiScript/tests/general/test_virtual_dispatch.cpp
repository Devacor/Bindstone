#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/detail/interpreter.hpp>

int main() {
    try {
        // Create a simple test to verify virtual dispatch works
        auto symbolizer = jai::string_symbolizer();
        
        // Create base environment
        auto base_env = std::make_shared<jai::environment>(&symbolizer);
        
        // Create method environment
        auto eng = jai::engine::make();
        auto dummy_this = jai::script_value::make_null(eng);
        auto method_env = std::make_shared<jai::method_environment>(base_env, &symbolizer, dummy_this);
        
        // Test virtual dispatch
        std::cout << "Testing virtual dispatch:\n";
        
        // Get a pointer to base class
        jai::environment* env_ptr = method_env.get();
        
        std::cout << "Type of env_ptr: " << typeid(*env_ptr).name() << "\n";
        std::cout << "Is method_environment: " << (dynamic_cast<jai::method_environment*>(env_ptr) != nullptr) << "\n";
        
        // Try to call get_ref through base pointer
        uint64_t test_id = symbolizer.intern("test_var");
        
        try {
            std::cout << "Calling get_ref through base pointer...\n";
            const jai::script_value& val = env_ptr->get_ref(test_id);
            std::cout << "Unexpected success!\n";
        } catch (const std::exception& e) {
            std::cout << "Expected exception: " << e.what() << "\n";
        }
        
        std::cout << "\nDone!\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}