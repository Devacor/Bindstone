#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
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
    
    std::cout << "=== Widget Conversion Debug ===" << std::endl;
    
    // Check conversion registry before registration
    auto conv_registry = engine->get_conversion_registry();
    std::cout << "Conversion registry available: " << (conv_registry ? "YES" : "NO") << std::endl;
    
    if (conv_registry) {
        std::cout << "Has TestWidget conversion before build(): " << conv_registry->has_conversion<TestWidget>() << std::endl;
    }
    
    // Register TestWidget class
    std::cout << "Building TestWidget class..." << std::endl;
    class_builder<TestWidget>(*engine, "TestWidget")
        .constructor<>()
        .constructor<const std::string&, int>()
        .property("name", &TestWidget::name)
        .property("value", &TestWidget::value)
        .build();
    
    std::cout << "TestWidget class built successfully." << std::endl;
    
    // Check conversion registry after registration
    if (conv_registry) {
        std::cout << "Has TestWidget conversion after build(): " << conv_registry->has_conversion<TestWidget>() << std::endl;
        std::cout << "Has std::vector<TestWidget> conversion: " << conv_registry->has_conversion<std::vector<TestWidget>>() << std::endl;
    }
    
    // Try to create a Widget in script
    std::cout << "Creating TestWidget in script..." << std::endl;
    try {
        auto result = engine->execute("TestWidget(\"test\", 42)");
        std::cout << "TestWidget creation successful: " << result.type_name() << std::endl;
    } catch (const std::exception& e) {
        std::cout << "TestWidget creation failed: " << e.what() << std::endl;
    }
    
    return 0;
}