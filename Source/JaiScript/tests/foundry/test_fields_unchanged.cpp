#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <iostream>
#include <vector>
#include <memory>

using namespace jai;

int main() {
    std::cout << "=== Fields Unchanged Optimization Test ===" << std::endl;

    auto engine = engine::make();

    auto migration_log = std::make_shared<std::vector<std::string>>();
    engine->add_function("log_migration", [migration_log, engine](const std::string& msg) {
        migration_log->push_back(msg);
        std::cout << "LOG: " << msg << std::endl;
        return script_value(std::monostate{}, engine->weak_from_this());
    });

    auto test_results = std::make_shared<std::vector<std::string>>();
    engine->add_function("check_value", [test_results, engine](const std::string& desc, bool result) {
        test_results->push_back(desc + ": " + (result ? "PASS" : "FAIL"));
        std::cout << "CHECK: " << desc << ": " << (result ? "PASS" : "FAIL") << std::endl;
        return script_value(std::monostate{}, engine->weak_from_this());
    });

    // Create class with migration method
    std::cout << "=== First definition ===" << std::endl;
    engine->execute(R"(
        class Widget {
            auto width = 100;
            auto height = 50;
            auto color = "blue";

            void hot_reload_migrate() {
                log_migration("migrate called - this should NOT happen when fields unchanged");
            }
        }

        auto w1 = Widget();
        w1.width = 200;
        w1.height = 100;
        w1.color = "red";

        auto w2 = Widget();
        w2.width = 150;
    )");

    migration_log->clear();

    // Redefine with SAME fields (optimization should skip migration)
    std::cout << "=== Second definition (same fields) ===" << std::endl;
    engine->execute(R"(
        class Widget {
            auto width = 100;    // Same field names
            auto height = 50;    // Same field names
            auto color = "blue"; // Same field names

            void process() {
                log_migration("new process called");
            }

            void render() {      // New method
                log_migration("render called");
            }

            void hot_reload_migrate() {
                log_migration("migrate called - this should NOT happen when fields unchanged");
            }
        }

        // Verify values preserved without migration
        check_value("w1.width preserved", w1.width == 200);
        check_value("w1.height preserved", w1.height == 100);
        check_value("w1.color preserved", w1.color == "red");
        check_value("w2.width preserved", w2.width == 150);

        // New methods should work
        w1.process();
        w1.render();
    )");

    std::cout << "=== After same-field reload: Migration log has " << migration_log->size() << " entries ===" << std::endl;
    for (size_t i = 0; i < migration_log->size(); ++i) {
        std::cout << "  [" << i << "] " << (*migration_log)[i] << std::endl;
    }

    if (migration_log->size() != 2) {
        std::cout << "FAIL: Expected 2 log entries, got " << migration_log->size() << std::endl;
        return 1;
    }
    if ((*migration_log)[0] != "new process called") {
        std::cout << "FAIL: Expected first entry 'new process called', got '" << (*migration_log)[0] << "'" << std::endl;
        return 1;
    }
    if ((*migration_log)[1] != "render called") {
        std::cout << "FAIL: Expected second entry 'render called', got '" << (*migration_log)[1] << "'" << std::endl;
        return 1;
    }

    // Check no migration was called
    for (const auto& log : *migration_log) {
        if (log.find("migrate called") != std::string::npos) {
            std::cout << "FAIL: hot_reload_migrate should not be called when fields unchanged" << std::endl;
            return 1;
        }
    }

    std::cout << "SUCCESS: Part 1 passed!" << std::endl;
    migration_log->clear();

    // Now change fields - migration SHOULD happen
    std::cout << "=== Third definition (fields changed) ===" << std::endl;
    engine->execute(R"(
        class Widget {
            auto width = 100;
            auto height = 50;
            auto depth = 10;     // New field (replaces color)

            void hot_reload_migrate() {
                log_migration("migrate called - fields changed");
            }
        }
    )");

    std::cout << "=== After field-change reload: Migration log has " << migration_log->size() << " entries ===" << std::endl;
    for (size_t i = 0; i < migration_log->size(); ++i) {
        std::cout << "  [" << i << "] " << (*migration_log)[i] << std::endl;
    }

    if (migration_log->size() != 1) {
        std::cout << "FAIL: Expected 1 log entry after field change, got " << migration_log->size() << std::endl;
        return 1;
    }
    if ((*migration_log)[0] != "migrate called - fields changed") {
        std::cout << "FAIL: Expected 'migrate called - fields changed', got '" << (*migration_log)[0] << "'" << std::endl;
        return 1;
    }

    // Verify all check_value results passed
    for (const auto& result : *test_results) {
        if (result.find("FAIL") != std::string::npos) {
            std::cout << "FAIL: " << result << std::endl;
            return 1;
        }
    }

    std::cout << "SUCCESS: All checks passed!" << std::endl;
    return 0;
}
