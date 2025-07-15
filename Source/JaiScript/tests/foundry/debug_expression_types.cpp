#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    // Force interpreter backend
    eng->set_backend(backend_type::interpreter);
    
    std::cout << "=== Testing variable assignment ===\n";
    eng->execute("auto x = 5; x = 10;");
    
    std::cout << "\n=== Testing map assignment ===\n";
    eng->execute("auto map = {}; map[\"key\"] = 42;");
    
    return 0;
}