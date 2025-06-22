// Example demonstrating how to migrate from ChaiScript to JaiScript class bindings
// This shows how Bindstone's current ChaiScript bindings would look in JaiScript

#include "../include/jaiscript/jaiscript.hpp"
#include "../include/jaiscript/core/class_builder.hpp"

// Mock Bindstone classes for demonstration
namespace MV {
    class Task {
    public:
        Task() = default;
        Task(bool infinite) : infinite_(infinite) {}
        Task(const std::string& name, bool infinite = false) 
            : name_(name), infinite_(infinite) {}
        
        Task& now(const std::string& name) {
            // Implementation
            return *this;
        }
        
        Task& then(const std::string& name) {
            // Implementation
            return *this;
        }
        
        void update(double dt) {
            elapsed_ += dt;
        }
        
        double elapsed() const { return elapsed_; }
        const std::string& name() const { return name_; }
        bool finished() const { return finished_; }
        void cancel() { finished_ = true; }
        
    private:
        std::string name_;
        bool infinite_ = false;
        bool finished_ = false;
        double elapsed_ = 0.0;
    };
    
    namespace Scene {
        class Component {
        public:
            virtual ~Component() = default;
            std::string id() const { return id_; }
            void id(const std::string& newId) { id_ = newId; }
            
        private:
            std::string id_;
        };
        
        class Drawable : public Component {
        public:
            bool visible() const { return visible_; }
            void hide() { visible_ = false; }
            void show() { visible_ = true; }
            
        private:
            bool visible_ = true;
        };
        
        class Sprite : public Drawable {
        public:
            void setTexture(const std::string& path) {
                texturePath_ = path;
            }
            
            std::string getTexture() const {
                return texturePath_;
            }
            
        private:
            std::string texturePath_;
        };
        
        class Clickable : public Sprite {
        public:
            void disable() { enabled_ = false; }
            bool enabled() const { return enabled_; }
            
            // Signals would be bound here
            
        private:
            bool enabled_ = true;
        };
        
        class Button : public Clickable {
        public:
            void text(const std::string& t) { text_ = t; }
            std::string text() const { return text_; }
            
        private:
            std::string text_;
        };
    }
}

namespace JaiScript {
    
    // Registration helper matching ChaiScript's Registrar pattern
    template<typename T>
    class Registrar {
    public:
        using RegistrationFunc = std::function<void(Engine&, const MV::Services&)>;
        
        Registrar(RegistrationFunc func) : registrationFunc_(func) {
            // In real implementation, this would register with a global registry
        }
        
        void registerType(Engine& engine, const MV::Services& services) {
            registrationFunc_(engine, services);
        }
        
    private:
        RegistrationFunc registrationFunc_;
    };
}

// Example registrations showing the migration from ChaiScript to JaiScript

// OLD ChaiScript style (from utilityHooks.cxx):
/*
namespace MV {
    Script::Registrar<Task> _hookTask([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
        a_script.add(chaiscript::user_type<Task>(), "Task");
        a_script.add(chaiscript::constructor<Task()>(), "Task");
        a_script.add(chaiscript::constructor<Task(ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::constructor<Task(const std::string&, ExactType<bool>)>(), "Task");
        a_script.add(chaiscript::fun(&Task::update), "update");
        a_script.add(chaiscript::fun(&Task::elapsed), "elapsed");
        a_script.add(chaiscript::fun(&Task::name), "name");
        a_script.add(chaiscript::fun(&Task::finished), "finished");
        a_script.add(chaiscript::fun([](Task& a_self) {a_self.cancel(); }), "cancel");
        // ... many more verbose registrations
    });
}
*/

// NEW JaiScript style - much cleaner!
namespace MV {
    JaiScript::Registrar<Task> _hookTask([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Task>(engine, "Task")
            .constructor<>()
            .constructor<bool>()
            .constructor<const std::string&, bool>()
            .method("now", static_cast<Task&(Task::*)(const std::string&)>(&Task::now))
            .method("then", static_cast<Task&(Task::*)(const std::string&)>(&Task::then))
            .method("update", &Task::update)
            .method("elapsed", &Task::elapsed)
            .method("name", &Task::name)
            .method("finished", &Task::finished)
            .method("cancel", &Task::cancel)
            .build();
    });
}

// OLD ChaiScript style (from sceneHooks.cxx):
/*
namespace MV {
    Script::Registrar<Button> _hookButton([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
        a_script.add(chaiscript::user_type<Button>(), "Button");
        a_script.add(chaiscript::base_class<Clickable, Button>());
        a_script.add(chaiscript::base_class<Sprite, Button>());
        a_script.add(chaiscript::base_class<Drawable, Button>());
        a_script.add(chaiscript::base_class<Component, Button>());
        
        a_script.add(chaiscript::fun([](Button& a_self, const std::string& a_newValue) { 
            return a_self.text(a_newValue); 
        }), "text");
        a_script.add(chaiscript::fun([](Button& a_self) { 
            return a_self.text(); 
        }), "text");
        
        // Type conversions - very verbose!
        a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>(
            [](const SafeComponent<Button>& a_item) { return a_item.self(); }));
        // ... many more type conversions
    });
}
*/

// NEW JaiScript style with inheritance:
namespace MV {
    using namespace Scene;
    
    JaiScript::Registrar<Component> _hookComponent([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Component>(engine, "Component")
            .method("id", static_cast<std::string(Component::*)() const>(&Component::id))
            .method("id", static_cast<void(Component::*)(const std::string&)>(&Component::id))
            .build();
    });
    
    JaiScript::Registrar<Drawable> _hookDrawable([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Drawable>(engine, "Drawable")
            .inherits<Component>()
            .method("visible", &Drawable::visible)
            .method("hide", &Drawable::hide)
            .method("show", &Drawable::show)
            .build();
    });
    
    JaiScript::Registrar<Sprite> _hookSprite([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Sprite>(engine, "Sprite")
            .inherits<Drawable>()
            .method("setTexture", &Sprite::setTexture)
            .method("getTexture", &Sprite::getTexture)
            .build();
    });
    
    JaiScript::Registrar<Clickable> _hookClickable([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Clickable>(engine, "Clickable")
            .inherits<Sprite>()
            .method("disable", &Clickable::disable)
            .method("enabled", &Clickable::enabled)
            .build();
    });
    
    JaiScript::Registrar<Button> _hookButton([](JaiScript::Engine& engine, const MV::Services& services) {
        JaiScript::makeClassBuilder<Button>(engine, "Button")
            .inherits<Clickable>()
            .method("text", static_cast<void(Button::*)(const std::string&)>(&Button::text))
            .method("text", static_cast<std::string(Button::*)() const>(&Button::text))
            .build();
            
        // Type conversions are handled automatically by the class system!
        // No need for manual type_conversion registrations
    });
}

// Example of how signals would be registered:
/*
template<typename Signature>
class ScriptSignalRegistrar {
    // Implementation that automatically registers signal types
};

// Usage:
ScriptSignalRegistrar<Clickable::ButtonSignalSignature> _clickableButtonSignal{};
*/

int main() {
    // Demonstration of the cleaner syntax
    std::cout << "JaiScript Class Builder Pattern Demo\n";
    std::cout << "=====================================\n\n";
    
    std::cout << "Benefits over ChaiScript:\n";
    std::cout << "1. Cleaner, more readable syntax\n";
    std::cout << "2. Type-safe inheritance with .inherits<T>()\n";
    std::cout << "3. Automatic type conversions\n";
    std::cout << "4. Less verbose constructor registration\n";
    std::cout << "5. Better IDE support with method chaining\n";
    std::cout << "6. No need for ExactType<T> wrappers\n\n";
    
    std::cout << "Migration effort:\n";
    std::cout << "- Replace Script::Registrar with JaiScript::Registrar\n";
    std::cout << "- Use makeClassBuilder<T>() instead of multiple add() calls\n";
    std::cout << "- Remove manual type conversions\n";
    std::cout << "- Simplify lambda wrappers\n";
    
    return 0;
}