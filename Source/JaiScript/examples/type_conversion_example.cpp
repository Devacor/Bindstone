// Example showing how to handle type conversions with the general-purpose ClassBuilder
// This demonstrates how Bindstone could use the generic type conversion system

#include "../include/jaiscript/jaiscript.hpp"
#include "../include/jaiscript/core/class_builder.hpp"

// Mock Bindstone types for demonstration
template<typename T>
class SafeComponent {
public:
    SafeComponent(std::shared_ptr<T> ptr) : ptr_(ptr) {}
    std::shared_ptr<T> self() const { return ptr_; }
private:
    std::shared_ptr<T> ptr_;
};

namespace MV::Scene {
    class Component {
    public:
        virtual ~Component() = default;
        std::string id() const { return "component"; }
    };
    
    class Drawable : public Component {
    public:
        bool visible() const { return true; }
    };
    
    class Sprite : public Drawable {
    public:
        void setTexture(const std::string& path) {}
    };
    
    class Button : public Sprite {
    public:
        void setText(const std::string& text) { text_ = text; }
        std::string getText() const { return text_; }
    private:
        std::string text_;
    };
}

// Example of how Bindstone would register Button with all the necessary conversions
void registerButtonWithConversions(JaiScript::Engine& engine) {
    using namespace MV::Scene;
    
    // Register the Button class with lambda methods (no static_cast needed!)
    JaiScript::makeClassBuilder<Button>(engine, "Button")
        .inherits<Sprite>()  // This would handle basic inheritance
        .constructor<>()
        
        // Clean lambda-based method binding
        .method("setText", [](Button* self, const std::string& text) {
            self->setText(text);
        })
        .method("getText", [](Button* self) -> std::string {
            return self->getText();
        })
        
        // General-purpose type conversions - Bindstone can add these as needed
        .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Button>>(
            [](const SafeComponent<Button>& item) { 
                return item.self(); 
            }
        )
        .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Sprite>>(
            [](const SafeComponent<Button>& item) { 
                return std::static_pointer_cast<Sprite>(item.self()); 
            }
        )
        .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Drawable>>(
            [](const SafeComponent<Button>& item) { 
                return std::static_pointer_cast<Drawable>(item.self()); 
            }
        )
        .addTypeConversion<SafeComponent<Button>, std::shared_ptr<Component>>(
            [](const SafeComponent<Button>& item) { 
                return std::static_pointer_cast<Component>(item.self()); 
            }
        )
        
        .build();
}

// Bindstone could even create a helper to reduce boilerplate
template<typename T>
class BindstoneClassBuilder {
public:
    BindstoneClassBuilder(JaiScript::Engine& engine, const std::string& name)
        : builder_(JaiScript::makeClassBuilder<T>(engine, name)) {}
    
    // Automatically add SafeComponent conversions
    template<typename Base>
    BindstoneClassBuilder& inherits() {
        builder_.inherits<Base>();
        
        // Add the SafeComponent conversions automatically
        builder_.addTypeConversion<SafeComponent<T>, std::shared_ptr<Base>>(
            [](const SafeComponent<T>& item) { 
                return std::static_pointer_cast<Base>(item.self()); 
            }
        );
        
        return *this;
    }
    
    // Forward other methods to the underlying builder
    template<typename... Args>
    BindstoneClassBuilder& constructor() {
        builder_.constructor<Args...>();
        return *this;
    }
    
    template<typename Callable>
    BindstoneClassBuilder& method(const std::string& name, Callable&& callable) {
        builder_.method(name, std::forward<Callable>(callable));
        return *this;
    }
    
    void build() {
        // Always add the main SafeComponent<T> -> std::shared_ptr<T> conversion
        builder_.addTypeConversion<SafeComponent<T>, std::shared_ptr<T>>(
            [](const SafeComponent<T>& item) { return item.self(); }
        );
        
        builder_.build();
    }
    
private:
    JaiScript::ClassBuilder<T> builder_;
};

// Now Bindstone registrations become even cleaner:
void registerButtonBindstoneStyle(JaiScript::Engine& engine) {
    using namespace MV::Scene;
    
    BindstoneClassBuilder<Button>(engine, "Button")
        .inherits<Sprite>()  // Automatically adds SafeComponent conversions!
        .constructor<>()
        .method("setText", [](Button* self, const std::string& text) {
            self->setText(text);
        })
        .method("getText", [](Button* self) -> std::string {
            return self->getText();
        })
        .build();  // Automatically adds SafeComponent<Button> -> std::shared_ptr<Button>
}

int main() {
    std::cout << "Type Conversion Examples\n";
    std::cout << "========================\n\n";
    
    std::cout << "1. General-purpose ClassBuilder with explicit conversions:\n";
    std::cout << "   - Uses .addTypeConversion<From, To>(converter)\n";
    std::cout << "   - Framework-agnostic\n";
    std::cout << "   - User has full control\n\n";
    
    std::cout << "2. Bindstone-specific wrapper (BindstoneClassBuilder):\n";
    std::cout << "   - Automatically adds SafeComponent conversions\n";
    std::cout << "   - Reduces boilerplate for Bindstone classes\n";
    std::cout << "   - Built on top of the general ClassBuilder\n\n";
    
    std::cout << "Benefits:\n";
    std::cout << "- ClassBuilder stays general-purpose\n";
    std::cout << "- Bindstone can add domain-specific helpers\n";
    std::cout << "- Other projects can use ClassBuilder without Bindstone-specific code\n";
    std::cout << "- Type conversions are explicit and customizable\n";
    
    return 0;
}