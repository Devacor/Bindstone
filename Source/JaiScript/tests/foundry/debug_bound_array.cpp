#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/core/bound_array.hpp>
#include <iostream>

using namespace jai;

struct TestWidget {
    std::string name;
    int value;
    
    TestWidget() : name("default"), value(0) {}
    TestWidget(const std::string& n, int v) : name(n), value(v) {}
};

int main() {
    auto engine = engine::make();
    std::cout << "Engine created at: " << engine.get() << std::endl;
    
    std::cout << "=== Bound Array Debug ===" << std::endl;
    
    // Register TestWidget class
    std::cout << "Registering TestWidget class..." << std::endl;
    class_builder<TestWidget>(*engine, "TestWidget")
        .constructor<>()
        .constructor<const std::string&, int>()
        .property("name", &TestWidget::name)
        .property("value", &TestWidget::value)
        .build();
    std::cout << "TestWidget class registered." << std::endl;
    
    // Check if conversion is registered
    auto conv_registry = engine->get_conversion_registry();
    std::cout << "Has TestWidget conversion: " << conv_registry->has_conversion<TestWidget>() << std::endl;
    
    // Create a function that uses bound_array<TestWidget>  
    engine->add_function("process_widgets", [engine_ptr = engine](const bound_array<TestWidget>& widgets) -> int {
        std::cout << "Inside process_widgets, array size: " << widgets.size() << std::endl;
        
        int count = 0;
        try {
            std::cout << "Attempting to iterate over widgets..." << std::endl;
            for (const auto& w : widgets) {
                std::cout << "Widget " << count << ": " << w.name << " = " << w.value << std::endl;
                count++;
            }
            std::cout << "Successfully iterated " << count << " widgets" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Error iterating widgets: " << e.what() << std::endl;
            
            // Try to use the engine's conversion registry directly
            auto registry = engine_ptr->get_conversion_registry();
            if (registry && registry->has_conversion<TestWidget>()) {
                std::cout << "Registry HAS TestWidget conversion" << std::endl;
            } else {
                std::cout << "Registry does NOT have TestWidget conversion" << std::endl;
            }
            
            throw;
        }
        return count;
    });
    
    // Test script
    try {
        // First test: can we extract a directly created widget?
        std::cout << "\n=== Direct Widget Test ===" << std::endl;
        auto direct_result = engine->execute(R"(
            auto w = TestWidget("direct", 42);
            w
        )");
        
        std::cout << "Direct result type: " << static_cast<int>(direct_result.type()) << std::endl;
        std::cout << "Is object: " << direct_result.is_object() << std::endl;
        
        // Try direct extraction first
        try {
            auto direct_widget = direct_result.as<TestWidget>();
            std::cout << "Direct widget extraction SUCCESS: " << direct_widget.name << " = " << direct_widget.value << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Direct widget extraction FAILED: " << e.what() << std::endl;
            
            // Try using the conversion registry directly
            try {
                auto registry = engine->get_conversion_registry();
                if (registry && registry->has_conversion<TestWidget>()) {
                    std::cout << "Registry has TestWidget conversion, trying direct conversion..." << std::endl;
                    auto w = registry->convert_from_script<TestWidget>(direct_result);
                    std::cout << "Registry conversion SUCCESS: " << w.name << " = " << w.value << std::endl;
                    
                    // Debug: Let's check if the problem is in how the object is created
                    std::cout << "\n=== Debug: Creating Widget via C++ ===" << std::endl;
                    TestWidget cpp_widget("cpp_test", 99);
                    auto cpp_result = registry->convert_to_script<TestWidget>(cpp_widget);
                    std::cout << "Converted C++ widget to script_value" << std::endl;
                    
                    // Now try to extract it back
                    try {
                        auto extracted = cpp_result.as<TestWidget>();
                        std::cout << "Extraction SUCCESS: " << extracted.name << " = " << extracted.value << std::endl;
                    } catch (const std::exception& e3) {
                        std::cout << "Extraction FAILED: " << e3.what() << std::endl;
                    }
                }
            } catch (const std::exception& e2) {
                std::cout << "Registry conversion also FAILED: " << e2.what() << std::endl;
            }
        }
        
        // Now test with array
        std::cout << "\n=== Array Test ===" << std::endl;
        auto result = engine->execute(R"(
            auto widgets = [];
            auto w1 = TestWidget("first", 10);
            widgets.push(w1);
            
            auto w2 = TestWidget("second", 20);
            widgets.push(w2);
            
            process_widgets(widgets)
        )");
        
        std::cout << "Result: " << result.as<int>() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Script execution failed: " << e.what() << std::endl;
    }
    
    return 0;
}