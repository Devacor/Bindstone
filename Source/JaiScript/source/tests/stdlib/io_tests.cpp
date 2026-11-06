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
        // The bare-engine print contract (the docs' two-line hello):
        // engine::make() ships a basic print (concatenation, no {} formatting,
        // host-controlled stream); register_all upgrades it IN PLACE — same
        // name, one registration, never a stacked double-print.
        // ============================================================

        test("bare_engine_ships_basic_print", [this]() {
            auto engine = jai::foundry::make_engine();   // NO register_all
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute("print(\"hello\");");
            check(capture->str().find("hello") != std::string::npos);
        });

        test("bare_print_concatenates_without_formatting", [this]() {
            auto engine = jai::foundry::make_engine();   // NO register_all
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute("print(\"Value: {}\", 42);");
            std::string output = capture->str();
            check(output.find("Value: {}42") != std::string::npos,
                "bare print concatenates; {} stays literal until stdlib upgrades it");
        });

        test("stdlib_upgrade_replaces_print_exactly_once", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute("print(\"once\");");
            std::string output = capture->str();
            check(output.find("once") != std::string::npos);
            check_eq(output.find("once"), output.rfind("once"),
                "one registration fires: the upgrade replaced the basic print, not stacked on it");
        });

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

        // Single-arg print is VERBATIM passthrough — {{ }} escape
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

        test("format_spec_outside_the_subset_raises", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            // a {n:...} placeholder whose spec the subset does not have raises, naming it; a {...} that is no placeholder stays literal
            auto caught = [&](const char* call) {
                return engine->execute(std::string("var msg = \"none\"; try { ") + call + "; } catch (e) { msg = e; } msg;").as<std::string>();
            };
            check_eq(std::string("Unsupported format spec ':zz' in format string"), caught("format(\"{} {:zz}\", 1, 2)"));
            check_eq(std::string("Unsupported format spec ':,x' in format string"), caught("format(\"{:,x}\", 255)"));
            check_eq(std::string("Unsupported format spec ':#x' in format string"), caught("print(\"{0:#x}\", 255)"));
            check_eq(std::string("{\"a\": 1} 2"), engine->execute(R"(format("{\"a\": 1} {}", 2);)").as<std::string>());
        });

        test("format_spec_groups_thousands", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            check_eq(std::string("1,234,567 1,234,567.89 1_234_567 dead_beef 00,001,234 -0,001,234    1,234,567| -1,234,567.5 12 -999"),
                     engine->execute(R"(format("{:,} {:,.2f} {:_} {:_x} {:010,} {:010,} {:>12,}| {:,} {:,} {:,}", 1234567, 1234567.891, 1234567, 3735928559, 1234, -1234, 1234567, -1234567.5, 12, -999);)").as<std::string>());
            check_eq(std::string("1,234,567 0.500 9_876_543_210 +1,000"), engine->execute(R"(`${1234567:,} ${0.5:,.3f} ${9876543210:_d} ${1000:+,}`;)").as<std::string>());
            auto result = engine->execute(R"(var msg = ""; try { format("{:,}", "abc"); } catch (e) { msg = e; } msg;)");
            check_eq(std::string("format spec ':,' does not apply to string value"), result.as<std::string>());
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

        // C++20's constant-format rule: only a string literal written as the first argument of print or format is a format
        // string; any other text, user input included, prints as it is
        test("only_a_string_literal_is_a_format_string", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            engine->execute(R"(
                string user = "a{}b";
                print(user, 5);
                print("x{}y", 5);
                var held = "{} {}";
                print(held, 1, 2);
                array<string> texts = ["{0}"];
                print(texts[0], 3);
                print(`t${1}{}`, 4);
                auto p = print;
                p("{}", 6);
                print(vformat(user, 7));
            )");
            check_eq(std::string("a{}b5\nx5y\n{} {}12\n{0}3\nt1{}4\n{}6\na7b\n"), capture->str());
            check_eq(std::string("{}1"), engine->execute(R"(var s = "{}"; format(s, 1);)").as<std::string>());
            check_eq(std::string("1-{2}"), engine->execute(R"(string s = "{}-{{{}}}"; vformat(s, 1, 2);)").as<std::string>());
            check_eq(std::string("mine"), engine->execute(R"(function format(a, b) { return "mine"; } format("{}", 1);)").as<std::string>());
        });

        test("format_of_a_runtime_text_is_a_load_error", [this]() {
            auto engine = make_engine();
            stdlib::register_all(*engine);
            std::string message;
            try {
                engine->execute(R"(string s = "{}"; format(s, 1);)");
            } catch (const static_check_error& e) {
                message = e.what();
            }
            check(message.find("the format text of 'format' must be a string literal: vformat(fmt, ...) formats a run-time text, and + or a template string joins text") != std::string::npos, message);
            check_eq(std::string("5 units"), engine->execute(R"(int n = 5; format(n, " units");)").as<std::string>());
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::io_stdlib_tests)
