#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();

    // Register C++ base
    struct Base {
        std::string name;
        Base(const std::string& n) : name(n) {}
        std::string greet() { return "Hello from " + name; }
    };

    jai::class_builder<Base>(*js, "Base")
        .constructor<std::string>()
        .property("name", &Base::name)
        .method("greet", &Base::greet)
        .build();

    // Script inherits
    const char* script = R"(
        class Derived : Base {
            Derived(string n) : super(n) {}
            string custom() {
                return super::greet() + "!";
            }
        }

        auto d = Derived("World");
        d.custom()
    )";

    try {
        auto result = js->execute(script);
        std::string msg = result.as<std::string>();
        std::cout << "Result: " << msg << std::endl;
        if (msg == "Hello from World!") {
            std::cout << "SUCCESS!" << std::endl;
            return 0;
        } else {
            std::cout << "FAIL: Expected 'Hello from World!' got '" << msg << "'" << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
