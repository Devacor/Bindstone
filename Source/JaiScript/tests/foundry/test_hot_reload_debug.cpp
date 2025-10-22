#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <iostream>

using namespace jai;

int main() {
    std::cout << "=== Hot Reload Migrate Debug ===" << std::endl;

    auto engine = engine::make();

    // Add print function
    engine->add_variadic_function("print", [engine](const std::vector<script_value>& args) {
        for (const auto& arg : args) {
            std::cout << arg.to_string() << " ";
        }
        std::cout << std::endl;
        return script_value(std::monostate{}, engine->weak_from_this());
    });

    // First definition - no migration
    std::cout << "=== First definition ===" << std::endl;
    engine->execute(R"(
        class Config {
            auto version = 1;
            auto flags = 42;
        }

        auto cfg = Config();
    )");

    // Second definition - add hot_reload_migrate
    std::cout << "=== Second definition with hot_reload_migrate ===" << std::endl;
    try {
        engine->execute(R"(
            class Config {
                auto version = 2;
                auto settings = 0;

                void hot_reload_migrate() {
                    print("Migration called!");
                    print("About to set field");
                    this.settings = 123;
                    print("Migration done!");
                }
            }
        )");
        std::cout << "Hot reload succeeded!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Hot reload failed: " << e.what() << std::endl;
        return 1;
    }

    // Check if migration ran
    try {
        auto result = engine->execute("cfg.settings");
        std::cout << "Settings value: " << result.as_int() << std::endl;
        if (result.as_int() == 123) {
            std::cout << "SUCCESS: Migration ran and set the field!" << std::endl;
        } else {
            std::cout << "FAIL: Migration did not run correctly, got " << result.as_int() << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cout << "Check failed: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
