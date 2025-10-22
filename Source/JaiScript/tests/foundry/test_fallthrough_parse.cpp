#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();
    try {
        engine->execute(R"(
            fallthrough;
        )");
        std::cout << "ERROR: No exception thrown!\n";
        return 1;
    } catch (const jai::parse_error& e) {
        std::cout << "SUCCESS: parse_error caught: " << e.what() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "ERROR: Wrong exception type: " << e.what() << "\n";
        return 1;
    }
}
