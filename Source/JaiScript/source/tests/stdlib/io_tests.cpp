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

        // RULED (2026-07): single-arg print is VERBATIM passthrough — {{ }} escape
        // processing only runs when format substitution runs (format path with args).
        // print is one shared registry entry, so both backends share this behavior.
        test("print_single_arg_verbatim_braces", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute(R"(print("{{x}}");)");
            check_eq(std::string("{{x}}\n"), capture->str(), "single-arg print prints braces raw");
        });

        test("print_format_path_processes_escapes", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute(R"(print("{{}} {0}", 7);)");
            check_eq(std::string("{} 7\n"), capture->str(), "format path processes {{ }} escapes");
        });

        // ============================================================
        // Spec placeholders: {:spec} / {n:spec} — same mini-language as
        // template-string ${x:spec}, through the one shared formatter
        // ============================================================

        test("format_spec_placeholders", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(std::string("3.14"), engine->execute(R"(format("{:.2f}", 3.14159);)").as<std::string>());
            check_eq(std::string("    42"), engine->execute(R"(format("{0:>6}", 42);)").as<std::string>());
            check_eq(std::string("ff FF"), engine->execute(R"(format("{:x} {0:X}", 255);)").as<std::string>());
            check_eq(std::string("x=1 y=2.5"), engine->execute(R"(format("x={} y={1:.1f}", 1, 2.5);)").as<std::string>());
        });

        test("print_spec_placeholder", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute(R"(print("[{:>5}]", 42);)");
            check_eq(std::string("[   42]\n"), capture->str());
        });

        test("format_spec_invalid_stays_literal", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // Lenient like invalid positional indexes: a non-parsing spec is not a placeholder
            check_eq(std::string("1 {:zz}"), engine->execute(R"(format("{} {:zz}", 1);)").as<std::string>());
        });

        test("format_spec_type_mismatch_throws", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto result = engine->execute(R"(var msg = ""; try { format("{:.1f}", "abc"); } catch (e) { msg = e; } msg;)");
            check_eq(std::string("format spec ':.1f' does not apply to string value"), result.as<std::string>());
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
