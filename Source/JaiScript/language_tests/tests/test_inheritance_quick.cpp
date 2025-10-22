#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();

    // Register a C++ class
    struct Animal {
        std::string name;
        Animal(const std::string& n) : name(n) {}
        std::string speak() { return name + " makes a sound"; }
    };

    jai::class_builder<Animal>(*js, "Animal")
        .constructor<std::string>()
        .property("name", &Animal::name)
        .method("speak", &Animal::speak)
        .build();

    // Try to inherit from it in script
    const char* script = R"(
        class Dog : Animal {
            Dog(string n) : super(n) {
                print("Dog created");
            }
        }

        auto dog = Dog("Buddy");
        dog.name
    )";

    try {
        auto result = js->execute(script);
        std::cout << "Result: " << result.as<std::string>() << std::endl;
        std::cout << "SUCCESS!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
