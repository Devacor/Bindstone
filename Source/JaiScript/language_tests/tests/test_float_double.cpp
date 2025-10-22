#include <jaiscript/core/engine.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();

    std::cout << "=== Test 1: Float literal type ===\n";
    try {
        jai::script_value result = engine->execute("3.14;");
        std::cout << "Type enum: " << static_cast<int>(result.get_type()) << "\n";
        std::cout << "Is int? " << (result.is_int() ? "YES" : "NO") << "\n";
        std::cout << "Is float? " << (result.is_float() ? "YES" : "NO") << "\n";
        std::cout << "Is double? " << (result.is_double() ? "YES" : "NO") << "\n";

        // Try conversions
        try {
            double d = result.as<double>();
            std::cout << "as<double>() SUCCESS: " << d << "\n";
        } catch (const std::exception& e) {
            std::cout << "as<double>() FAILED: " << e.what() << "\n";
        }

        try {
            float f = result.as<float>();
            std::cout << "as<float>() SUCCESS: " << f << "\n";
        } catch (const std::exception& e) {
            std::cout << "as<float>() FAILED: " << e.what() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }

    std::cout << "\n=== Test 2: Map with float ===\n";
    try {
        engine->execute(R"(
            var m = {};
            m["float"] = 3.14;
        )");

        jai::script_value float_val = engine->execute("m[\"float\"];");
        std::cout << "Retrieved type enum: " << static_cast<int>(float_val.get_type()) << "\n";

        try {
            double d = float_val.as<double>();
            std::cout << "SUCCESS: as<double>() = " << d << "\n";
        } catch (const std::exception& e) {
            std::cout << "FAILED: as<double>() - " << e.what() << "\n";
        }
    } catch (const std::exception& e) {
        std::cout << "ERROR: " << e.what() << "\n";
    }

    return 0;
}
