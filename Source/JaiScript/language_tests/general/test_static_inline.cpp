#include <iostream>
#include <unordered_map>
#include <string>

// Simulate what should happen
class MockClassDef {
    std::unordered_map<std::string, int> static_fields;
public:
    bool has_static_field(const std::string& name) const {
        return static_fields.find(name) != static_fields.end();
    }
    
    const int& get_static_field(const std::string& name) const {
        static int default_val = 0;
        auto it = static_fields.find(name);
        return it != static_fields.end() ? it->second : default_val;
    }
    
    int& get_static_field(const std::string& name) {
        auto it = static_fields.find(name);
        if (it != static_fields.end()) {
            return it->second;
        }
        static int default_val = 0;
        return default_val;
    }
    
    void set_static_field(const std::string& name, int value) {
        static_fields[name] = value;
    }
};

int main() {
    MockClassDef class_def;
    
    // Test setting and getting static fields
    class_def.set_static_field("count", 0);
    std::cout << "Initial count: " << class_def.get_static_field("count") << std::endl;
    
    // Test non-const access
    class_def.get_static_field("count") = 5;
    std::cout << "After assignment: " << class_def.get_static_field("count") << std::endl;
    
    // Test const access
    const MockClassDef& const_def = class_def;
    std::cout << "Const access: " << const_def.get_static_field("count") << std::endl;
    
    return 0;
}