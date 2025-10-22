#include <iostream>
#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/types.hpp>

int main() {
    try {
        auto engine = jai::engine::make();
        
        std::cout << "Testing fallthrough at top level..." << std::endl;
        engine->execute("fallthrough;");
        std::cout << "ERROR: Should have thrown exception" << std::endl;
        
    } catch (...) {
        std::cout << "SUCCESS: Caught an exception" << std::endl;
        try {
            throw;
        } catch (const jai::parse_error& e) {
            std::cout << "  It's a parse_error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "  It's a different exception: " << typeid(e).name() << " - " << e.what() << std::endl;
        } catch (...) {
            std::cout << "  It's an unknown exception type" << std::endl;
        }
    }
    
    return 0;
}
