#include <jaiscript/jaiscript.hpp>
#include <iostream>
#include <string>

int main() {
    auto js = jai::engine::make();

    // Register C++ base class
    struct Animal {
        std::string name;
        Animal(const std::string& n) : name(n) {}
        virtual std::string speak() { return name + " makes a sound"; }
    };

    jai::class_builder<Animal>(*js, "Animal")
        .constructor<std::string>()
        .property("name", &Animal::name)
        .method("speak", &Animal::speak)
        .build();

    // Test: Script class overrides speak(), but super::speak() calls parent
    const char* script = R"(
        class Dog : Animal {
            Dog(string n) : super(n) {
                // Constructor delegation works!
            }

            string speak() override {
                // Call parent's speak(), then add to it
                return super::speak() + " - Woof!";
            }
        }

        auto dog = Dog("Buddy");
        dog.speak()
    )";

    try {
        auto result = js->execute(script);
        std::string msg = result.as<std::string>();
        std::cout << "Result: " << msg << std::endl;

        // Expected: "Buddy makes a sound - Woof!"
        // NOT: infinite recursion or derived-only behavior
        if (msg == "Buddy makes a sound - Woof!") {
            std::cout << "✓ SUCCESS: super::speak() correctly called parent method!" << std::endl;
            return 0;
        } else {
            std::cout << "✗ FAIL: Expected 'Buddy makes a sound - Woof!' got '" << msg << "'" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
