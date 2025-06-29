#include <iostream>
#include <map>
#include <jaiscript/core/value.hpp>

int main() {
    // Test that script_value can be used as a map key
    std::map<jai::script_value, jai::script_value> test_map;
    
    jai::script_value key1("key");
    jai::script_value key2("key");
    jai::script_value value1(42);
    
    std::cout << "key1 == key2: " << (key1 == key2) << "\n";
    std::cout << "key1 < key2: " << ((key1 <=> key2) < 0) << "\n";
    std::cout << "key1 > key2: " << ((key1 <=> key2) > 0) << "\n";
    
    // Test map insertion
    test_map[key1] = value1;
    std::cout << "Inserted key1 -> 42\n";
    
    // Test map lookup
    auto it = test_map.find(key2);
    if (it != test_map.end()) {
        std::cout << "Found with key2: " << it->second.as<int>() << "\n";
    } else {
        std::cout << "NOT FOUND with key2!\n";
    }
    
    // Direct access
    std::cout << "Direct access test_map[key2]: ";
    try {
        auto& val = test_map[key2];
        if (val.is_null()) {
            std::cout << "null\n";
        } else {
            std::cout << val.as<int>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "Error: " << e.what() << "\n";
    }
    
    return 0;
}