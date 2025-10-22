#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <iostream>

class Simple {
public:
    int value = 42;
};

int main() {
    std::cout << "Creating engine..." << std::endl;
    auto js = jai::engine::make();

    std::cout << "Registering class..." << std::endl;
    jai::class_builder<Simple>(*js, "Simple")
        .constructor<>()
        .property("value", &Simple::value)
        .build();

    // Test 1: Create in script
    std::cout << "Test 1: Object created in script..." << std::endl;
    try {
        auto result = js->execute("auto script_obj = Simple(); script_obj.value = 77; script_obj.value");
        std::cout << "✓ Script-created object works: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "✗ Script-created object failed: " << e.what() << std::endl;
    }

    // Test 2: Create in C++ and add via make_object
    std::cout << "\nTest 2: Object created in C++ via make_object..." << std::endl;
    auto obj = std::make_shared<Simple>();
    obj->value = 99;

    std::cout << "Adding to globals..." << std::endl;
    js->add_global("obj", js->make_object(obj));

    std::cout << "Executing simple expression..." << std::endl;
    try {
        auto result = js->execute("1 + 1");
        std::cout << "Simple expression works: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR in simple expression: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Accessing obj variable..." << std::endl;
    try {
        auto result = js->execute("obj");
        std::cout << "obj variable accessed, type: " << static_cast<int>(result.type()) << std::endl;

        // Check the flag
        auto holder = result.get_object_holder();
        std::cout << "is_cpp_class_instance: " << (holder ? holder->is_cpp_class_instance : -1) << std::endl;
        std::cout << "type_name: " << (holder ? holder->type_name : "null") << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR accessing obj: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Accessing obj.value..." << std::endl;
    try {
        auto result = js->execute("obj.value");
        std::cout << "SUCCESS: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "ERROR accessing obj.value: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
