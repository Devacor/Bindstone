#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    auto js = jai::engine::make();

    const char* script = R"(
        class Resource {
            string name = "";

            Resource(string n) {
                name = n;
            }

            ~Resource() {
                // Destructor
            }
        }

        auto r = Resource("test");
        r.name
    )";

    try {
        auto result = js->execute(script);
        std::cout << "Result: " << result.as<std::string>() << std::endl;
        std::cout << "✓ Destructor parsed successfully!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cout << "✗ ERROR: " << e.what() << std::endl;
        return 1;
    }
}
