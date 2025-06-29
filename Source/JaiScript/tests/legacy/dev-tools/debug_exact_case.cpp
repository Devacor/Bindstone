#include <iostream>
#include <jaiscript/core/engine.hpp>

using namespace jai;

int main() {
    engine eng;
    
    std::cout << "Test: Exact failing case from test suite\n";
    script_value result = eng.execute(R"(
        var map = {{"one", 1}, {"two", 2}};
        map["one"]
    )");
    
    std::cout << "Result is_null: " << result.is_null() << "\n";
    if (!result.is_null()) {
        std::cout << "Result value: " << result.as<int>() << "\n";
    }
    
    return 0;
}