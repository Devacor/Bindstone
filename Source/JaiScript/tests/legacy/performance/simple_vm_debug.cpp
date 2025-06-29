#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/jvm/vm_backend.hpp>

using namespace jai;

int main() {
    try {
        // Create engine with VM backend
        engine vm_engine;
        
        // Create and configure VM backend with debug mode
        auto backend = jvm::create_vm_backend();
        if (auto* vm_backend = dynamic_cast<jvm::vm_backend*>(backend.get())) {
            vm_backend->set_debug_mode(true);
        }
        vm_engine.set_backend(std::move(backend));
        
        // Test the failing case
        std::cout << "=== Testing for loop with final expression ===\n";
        script_value result = vm_engine.execute(R"(
            var sum = 0;
            for (var i = 0; i < 3; i = i + 1) {
                sum = sum + i;
            }
            sum;
        )");
        
        std::cout << "\n=== Execution complete ===\n";
        std::cout << "Result type: " << static_cast<int>(result.type());
        if (result.is_int()) {
            std::cout << ", value: " << result.as_int();
        } else if (result.is_null()) {
            std::cout << " (NULL!)";
        }
        std::cout << "\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}