#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();

    engine->execute(R"(
        var m = {};
        m["float"] = 3.14;
    )");

    try {
        jai::script_value float_val = engine->execute("m[\"float\"];");
        std::cout << "Retrieved value\n";
        std::cout << "Type: " << static_cast<int>(float_val.get_type()) << "\n";
        std::cout << "Trying to convert to double...\n";
        double d = float_val.as<double>();
        std::cout << "Success: " << d << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
