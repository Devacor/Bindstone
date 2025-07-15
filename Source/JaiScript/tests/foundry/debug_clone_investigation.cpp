#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

using namespace jai;

class SimpleObj {
public:
    static int copy_count;
    int value;
    
    SimpleObj(int v) : value(v) {
        std::cout << "SimpleObj(" << v << ")" << std::endl;
    }
    
    SimpleObj(const SimpleObj& other) : value(other.value) {
        copy_count++;
        std::cout << "COPY SimpleObj: " << other.value << " (count=" << copy_count << ")" << std::endl;
    }
    
    int getValue() const { return value; }
    
    static void reset() { copy_count = 0; }
};

int SimpleObj::copy_count = 0;

int main() {
    auto eng = engine::make();
    
    class_builder<SimpleObj>(*eng, "SimpleObj")
        .constructor<int>()
        .method("getValue", &SimpleObj::getValue)
        .property("value", &SimpleObj::value)
        .build();
    
    SimpleObj::copy_count = 0;
    
    try {
        std::cout << "=== Creating object ===\n";
        eng->execute("auto obj = SimpleObj(789);");
        std::cout << "Copy count after creation: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Getting object ===\n";
        auto obj_value = eng->get_variable("obj");
        std::cout << "Object type: " << static_cast<int>(obj_value.type()) << "\n";
        std::cout << "Is object: " << obj_value.is_object() << "\n";
        std::cout << "Copy count after getting: " << SimpleObj::copy_count << "\n\n";
        
        std::cout << "=== Manual clone test ===\n";
        auto cloned = obj_value.clone();
        std::cout << "Copy count after manual clone: " << SimpleObj::copy_count << "\n";
        std::cout << "Cloned type: " << static_cast<int>(cloned.type()) << "\n";
        std::cout << "Cloned is object: " << cloned.is_object() << "\n\n";
        
        std::cout << "=== Accessing cloned object ===\n";
        try {
            if (cloned.is_object()) {
                // Try to access the value through the cloned object
                eng->add_global("cloned_obj", cloned);
                auto cloned_value = eng->execute("cloned_obj.value");
                std::cout << "Cloned object value: " << cloned_value.as<int>() << "\n";
            }
        } catch (const std::exception& e) {
            std::cout << "Error accessing cloned object: " << e.what() << "\n";
        }
        
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }
    
    return 0;
}