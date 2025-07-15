#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    // Force interpreter backend
    eng->set_backend(backend_type::interpreter);
    
    std::cout << "=== Testing with myMap instead of map ===\n";
    eng->execute("auto myMap = {}; myMap[\"key\"] = 42; myMap[\"key\"]");
    
    return 0;
}