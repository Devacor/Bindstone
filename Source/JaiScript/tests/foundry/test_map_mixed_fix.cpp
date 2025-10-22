#include <iostream>
#include <jaiscript/jaiscript.hpp>

int main() {
    try {
        auto engine = jai::engine::make();

        std::cout << "Testing map mixed types fix...\n";

        engine->execute(R"(
            var m = {};
            m["int"] = 42;
            m["float"] = 3.14;
            m["string"] = "hello";
        )");

        std::cout << "Map created successfully\n";

        // This was failing before - execute() was returning a reference, not the actual value
        jai::script_value float_val = engine->execute("m[\"float\"];");
        std::cout << "Got float value, type: " << static_cast<int>(float_val.type()) << "\n";

        double d = float_val.as<double>();
        std::cout << "Float value: " << d << "\n";

        if (d == 3.14) {
            std::cout << "SUCCESS: Map mixed types fix working!\n";
            return 0;
        } else {
            std::cout << "FAIL: Expected 3.14, got " << d << "\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cout << "EXCEPTION: " << e.what() << "\n";
        return 1;
    }
}
