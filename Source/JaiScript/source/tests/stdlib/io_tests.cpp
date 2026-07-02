#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <sstream>

using namespace jai::foundry;

namespace jai::foundry::tests {

class io_stdlib_tests : public suite {
public:
    io_stdlib_tests() : suite("IO Stdlib Tests") {}

    void forge_tests() override {
        // ============================================================
        // Output stream redirection
        // ============================================================

        test("output_capture_basic", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            // Create capture buffer
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            // Run script that prints
            engine->execute("print(\"Hello, World!\");");

            // Get captured output
            std::string output = capture->str();
            check(output.find("Hello, World!") != std::string::npos);
        });

        test("output_capture_format_string", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            engine->execute("print(\"Value: {}\", 42);");

            std::string output = capture->str();
            check(output.find("Value: 42") != std::string::npos);
        });

        test("output_capture_multiple_args", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            engine->execute("print(\"a\", \"b\", \"c\");");

            std::string output = capture->str();
            check(output.find("abc") != std::string::npos);
        });

        test("output_capture_multiple_prints", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            engine->execute("print(\"Line 1\");");
            engine->execute("print(\"Line 2\");");
            engine->execute("print(\"Line 3\");");

            std::string output = capture->str();
            check(output.find("Line 1") != std::string::npos);
            check(output.find("Line 2") != std::string::npos);
            check(output.find("Line 3") != std::string::npos);
        });

        test("output_capture_reset_to_stdout", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            engine->execute("print(\"Captured\");");

            // Reset to stdout (nullptr)
            engine->set_output_stream(nullptr);

            // Further prints should go to stdout, not capture buffer
            std::string captured = capture->str();
            check(captured.find("Captured") != std::string::npos);
        });

        test("output_capture_skip_newline", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);

            engine->execute("print(\"NoNewline\", skip_newline);");
            engine->execute("print(\"Next\");");

            std::string output = capture->str();
            // Should have "NoNewline" immediately followed by "Next" on same line
            check(output.find("NoNewlineNext") != std::string::npos);
        });

        // ============================================================
        // Format function tests
        // ============================================================

        test("format_basic", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto result = engine->execute("format(\"Hello, {}!\", \"World\");");
            check_eq(result.as<std::string>(), "Hello, World!");
        });

        test("format_multiple_placeholders", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto result = engine->execute("format(\"{} + {} = {}\", 2, 3, 5);");
            check_eq(result.as<std::string>(), "2 + 3 = 5");
        });

        test("format_concatenation", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto result = engine->execute("format(\"a\", \"b\", \"c\");");
            check_eq(result.as<std::string>(), "abc");
        });

        test("format_single_value", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);

            auto result = engine->execute("format(42);");
            check_eq(result.as<std::string>(), "42");
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::io_stdlib_tests)
