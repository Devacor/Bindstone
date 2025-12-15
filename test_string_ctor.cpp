#include <jaiscript/jaiscript.hpp>
#include <iostream>

int main() {
    std::cerr << "=== ONLY Test 2: Check text field on converted Label ===" << std::endl;
    try {
        auto eng2 = jai::engine::make();
        eng2->execute(R"(
            class Label {
                string text = "";

                Label(string s) {
                    this.text = s;
                }
            }

            auto check_text(Label l) -> string {
                return l.text;
            }

            var result = check_text("Hello");
        )");
        auto result = eng2->execute("result");
        std::cerr << "result = '" << result.as<std::string>() << "'" << std::endl;
        std::cerr << "Test 2 passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test 2 failed: " << e.what() << std::endl;
    }

    return 0;
}
