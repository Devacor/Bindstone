#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    auto eng = engine::make();
    
    std::cout << "=== Testing simple assignment ===\n";
    eng->execute("auto x = 5;");
    
    std::cout << "=== Testing variable assignment ===\n";
    eng->execute("auto y = 10; y = 20;");
    
    std::cout << "=== Testing map creation ===\n";
    eng->execute("auto map = {};");
    
    std::cout << "=== Testing map assignment ===\n";
    eng->execute("map[\"key\"] = 42;");
    
    std::cout << "=== Done ===\n";
    return 0;
}