#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"
#include <string>
#include <memory>

using namespace JaiScript;
using namespace JaiScript::Testing;

// Example C++ class
class Button {
public:
    Button() : text_("Default") {}
    Button(const std::string& text) : text_(text) {}
    
    std::string getText() const { return text_; }
    void setText(const std::string& text) { text_ = text; }
    
private:
    std::string text_;
};

// No need for separate factory functions - we'll use lambdas

JAI_TEST_SUITE(CustomTypeReturn)

JAI_TEST(return_custom_type_from_function) {
    Engine engine;
    
    std::cout << "Test: Registering Button class..." << std::endl;
    
    // Register Button class
    makeClassBuilder<Button>(engine, "Button")
        .constructor<>()
        .constructor<const std::string&>()
        .method("getText", &Button::getText)
        .method("setText", &Button::setText)
        .build();
    
    std::cout << "Test: Button class registered." << std::endl;
    
    std::cout << "Test: Registering createButton function..." << std::endl;
    
    // Register factory function using lambda
    engine.addFunction("createButton", [](const std::string& text) -> Button {
        std::cout << "Factory: Creating button with text: " << text << std::endl;
        Button btn(text);
        std::cout << "Factory: Button created, text is: " << btn.getText() << std::endl;
        return btn;
    });
    
    std::cout << "Test: createButton function registered." << std::endl;
    
    // This should work but currently fails because typeid(Button).name() != "Button"
    std::string script = R"(
        auto btn = createButton("Click Me");
        return btn.getText();
    )";
    
    std::cout << "Test: Executing script..." << std::endl;
    
    try {
        Value result = engine.execute(script);
        std::cout << "Test: Script executed successfully." << std::endl;
        expect_eq(result.as<std::string>(), std::string("Click Me"));
    } catch (const std::exception& e) {
        // Currently this throws because the type name doesn't match
        std::cerr << "Error: " << e.what() << std::endl;
        expect_true(false); // Force test failure
    }
}

JAI_TEST(return_shared_ptr_custom_type) {
    Engine engine;
    
    // Register Button class
    makeClassBuilder<Button>(engine, "Button")
        .constructor<>()
        .constructor<const std::string&>()
        .method("getText", &Button::getText)
        .method("setText", &Button::setText)
        .build();
    
    // Register factory function that returns shared_ptr
    engine.addFunction("createSharedButton", [](const std::string& text) -> std::shared_ptr<Button> {
        return std::make_shared<Button>(text);
    });
    
    std::string script = R"(
        auto btn = createSharedButton("Shared Button");
        return btn.getText();
    )";
    
    try {
        Value result = engine.execute(script);
        expect_eq(result.as<std::string>(), std::string("Shared Button"));
    } catch (const std::exception& e) {
        // This also fails for the same reason
        std::cerr << "Error: " << e.what() << std::endl;
        expect_true(false); // Force test failure
    }
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()