// Example demonstrating lambda-based method binding vs traditional binding
// This shows the flexibility that ChaiScript's lambda approach provides

#include "../include/jaiscript/jaiscript.hpp"
#include "../include/jaiscript/core/class_builder.hpp"
#include <iostream>
#include <string>

// Example classes to demonstrate different binding approaches
class Button {
public:
    Button(const std::string& text = "") : text_(text), enabled_(true) {}
    
    // Overloaded methods - traditionally requires ugly static_cast
    void text(const std::string& newText) { text_ = newText; }
    const std::string& text() const { return text_; }
    
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool isEnabled() const { return enabled_; }
    
    // Method that might need validation/logging
    void click() {
        if (enabled_) {
            std::cout << "Button '" << text_ << "' clicked!\n";
            clickCount_++;
        } else {
            std::cout << "Button '" << text_ << "' is disabled!\n";
        }
    }
    
    int getClickCount() const { return clickCount_; }
    
private:
    std::string text_;
    bool enabled_;
    int clickCount_ = 0;
};

class TextInput {
public:
    void setValue(const std::string& value) {
        if (value.length() > maxLength_) {
            throw std::runtime_error("Text too long!");
        }
        value_ = value;
    }
    
    const std::string& getValue() const { return value_; }
    
    void setMaxLength(int length) { maxLength_ = length; }
    int getMaxLength() const { return maxLength_; }
    
private:
    std::string value_;
    int maxLength_ = 100;
};

int main() {
    std::cout << "Lambda Method Binding Examples\n";
    std::cout << "==============================\n\n";
    
    JaiScript::Engine engine;
    
    // Example 1: Traditional vs Lambda binding for overloaded methods
    std::cout << "1. Overloaded Methods - Traditional vs Lambda\n\n";
    
    // TRADITIONAL WAY - requires ugly static_cast
    /*
    JaiScript::makeClassBuilder<Button>(engine, "Button")
        .constructor<const std::string&>()
        .method("setText", static_cast<void(Button::*)(const std::string&)>(&Button::text))
        .method("getText", static_cast<const std::string&(Button::*)() const>(&Button::text))
        .build();
    */
    
    // LAMBDA WAY - much cleaner!
    JaiScript::makeClassBuilder<Button>(engine, "Button")
        .constructor<const std::string&>()
        .method("setText", [](Button* self, const std::string& text) {
            self->text(text);  // Clean! No static_cast needed
        })
        .method("getText", [](Button* self) -> std::string {
            return self->text();  // Clean! No static_cast needed
        })
        .method("click", &Button::click)  // Direct binding still works too
        .method("getClickCount", &Button::getClickCount)
        .build();
    
    // Example 2: Adding validation/logging with lambdas
    std::cout << "2. Adding Validation and Logging\n\n";
    
    JaiScript::makeClassBuilder<TextInput>(engine, "TextInput")
        .constructor<>()
        .method("setValue", [](TextInput* self, const std::string& value) {
            // Add logging before the call
            std::cout << "Setting text input to: '" << value << "'\n";
            
            try {
                self->setValue(value);
                std::cout << "Text set successfully\n";
            } catch (const std::exception& e) {
                std::cout << "Error setting text: " << e.what() << "\n";
                throw;  // Re-throw for script to handle
            }
        })
        .method("getValue", [](TextInput* self) -> std::string {
            std::string value = self->getValue();
            std::cout << "Getting text input value: '" << value << "'\n";
            return value;
        })
        .method("setMaxLength", [](TextInput* self, int length) {
            if (length < 1) {
                throw std::runtime_error("Max length must be positive!");
            }
            std::cout << "Setting max length to: " << length << "\n";
            self->setMaxLength(length);
        })
        .method("getMaxLength", &TextInput::getMaxLength)  // Direct binding when no wrapper needed
        .build();
    
    // Example 3: ChaiScript-style property access with lambdas
    std::cout << "3. Property-style Access (like ChaiScript)\n\n";
    
    // This creates both getter and setter using the same name, like ChaiScript does
    JaiScript::makeClassBuilder<Button>(engine, "ButtonWithProps")
        .constructor<>()
        .method("enabled", [](Button* self) -> bool {
            return self->isEnabled();
        })
        .method("enabled", [](Button* self, bool enabled) {
            std::cout << "Setting button enabled: " << (enabled ? "true" : "false") << "\n";
            self->setEnabled(enabled);
        })
        .build();
    
    std::cout << "Benefits of Lambda Binding:\n";
    std::cout << "- No ugly static_cast<> for overloaded methods\n";
    std::cout << "- Easy to add validation, logging, or processing\n";
    std::cout << "- Can adapt C++ signatures to script-friendly ones\n";
    std::cout << "- Same flexibility as ChaiScript but with better syntax\n";
    std::cout << "- Can mix with direct method pointer binding\n\n";
    
    // Test script that uses the lambda-bound methods
    std::string testScript = R"(
        // Test button with lambda-bound methods
        auto btn = Button("Click Me");
        string text = btn.getText();
        print("Initial text: " + text);
        
        btn.click();
        btn.click();
        int clicks = btn.getClickCount();
        print("Click count: " + to_string(clicks));
        
        // Test text input with validation
        auto input = TextInput();
        input.setMaxLength(10);
        
        try {
            input.setValue("Short");
            string value = input.getValue();
            print("Input value: " + value);
            
            input.setValue("This is way too long for the limit");
        } catch (error) {
            print("Caught expected error: " + error.message);
        }
        
        // Test property-style access
        auto propBtn = ButtonWithProps();
        bool enabled = propBtn.enabled();
        print("Button enabled: " + to_string(enabled));
        
        propBtn.enabled(false);
        enabled = propBtn.enabled();
        print("Button now enabled: " + to_string(enabled));
    )";
    
    // Note: This script won't actually run until the interpreter supports classes,
    // but it shows the intended usage
    std::cout << "Example script usage:\n";
    std::cout << testScript << "\n";
    
    return 0;
}