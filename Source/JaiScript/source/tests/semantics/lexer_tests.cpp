#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/jaiscript_fwd.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <sstream>
#include <fstream>
#include <filesystem>

using namespace jai::foundry;

namespace jai::foundry::tests {

class lexer_tests : public suite {
public:
    lexer_tests() : suite("Lexer Tests") {}
    
    void forge_tests() override {
        test("empty_input", [this]() {
            string_symbolizer symbolizer;
            lexer lex("", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(1), tokens.size());
            check_eq(token_type::eof, tokens[0].type);
        });

        test("single_identifier", [this]() {
            string_symbolizer symbolizer;
            lexer lex("foo", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(2), tokens.size());
            check_eq(token_type::identifier, tokens[0].type);
            check_eq(std::string("foo"), std::string(tokens[0].lexeme));
            check_eq(token_type::eof, tokens[1].type);
        });

        test("keywords", [this]() {
            string_symbolizer symbolizer;
            lexer lex("int float bool class", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(5), tokens.size());
            check_eq(token_type::int_keyword, tokens[0].type);
            check_eq(token_type::float_keyword, tokens[1].type);
            check_eq(token_type::bool_keyword, tokens[2].type);
            check_eq(token_type::class_keyword, tokens[3].type);
        });

        test("integer_literals", [this]() {
            string_symbolizer symbolizer;
            lexer lex("42 0 999", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(4), tokens.size());
            check_eq(token_type::integer_literal, tokens[0].type);
            check_eq(42, tokens[0].int_value);
            check_eq(token_type::integer_literal, tokens[1].type);
            check_eq(0, tokens[1].int_value);
            check_eq(token_type::integer_literal, tokens[2].type);
            check_eq(999, tokens[2].int_value);
        });

        test("float_literals", [this]() {
            string_symbolizer symbolizer;
            lexer lex("3.14 0.0 42.0", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(4), tokens.size());
            check_eq(token_type::float_literal, tokens[0].type);
            check_near(3.14, tokens[0].float_value, 0.001);
            check_eq(token_type::float_literal, tokens[1].type);
            check_near(0.0, tokens[1].float_value, 0.001);
            check_eq(token_type::float_literal, tokens[2].type);
            check_near(42.0, tokens[2].float_value, 0.001);
        });

        test("string_literals", [this]() {
            string_symbolizer symbolizer;
            lexer lex(R"("hello" "world" "hello\nworld")", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(4), tokens.size());
            check_eq(token_type::string_literal, tokens[0].type);
            check_eq(std::string("hello"), tokens[0].string_value);
            check_eq(token_type::string_literal, tokens[1].type);
            check_eq(std::string("world"), tokens[1].string_value);
            check_eq(token_type::string_literal, tokens[2].type);
            check_eq(std::string("hello\nworld"), tokens[2].string_value);
        });

        test("character_literals", [this]() {
            string_symbolizer symbolizer;
            lexer lex("'a' '\\n' '0'", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(4), tokens.size());
            check_eq(token_type::char_literal, tokens[0].type);
            check_eq('a', tokens[0].char_value);
            check_eq(token_type::char_literal, tokens[1].type);
            check_eq('\n', tokens[1].char_value);
            check_eq(token_type::char_literal, tokens[2].type);
            check_eq('0', tokens[2].char_value);
        });

        test("boolean_keywords", [this]() {
            string_symbolizer symbolizer;
            lexer lex("true false", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(3), tokens.size());
            check_eq(token_type::true_keyword, tokens[0].type);
            check_eq(token_type::false_keyword, tokens[1].type);
        });

        test("operators", [this]() {
            string_symbolizer symbolizer;
            lexer lex("+ - * / % == != < > <= >= && || !", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            
            // Check that all operator tokens are present (excluding EOF)
            check(tokens.size() > 13);
            check_eq(token_type::plus, tokens[0].type);
            check_eq(token_type::minus, tokens[1].type);
            check_eq(token_type::star, tokens[2].type);
            check_eq(token_type::slash, tokens[3].type);
            check_eq(token_type::percent, tokens[4].type);
            check_eq(token_type::equal_equal, tokens[5].type);
            check_eq(token_type::bang_equal, tokens[6].type);
            check_eq(token_type::less, tokens[7].type);
            check_eq(token_type::greater, tokens[8].type);
            check_eq(token_type::less_equal, tokens[9].type);
            check_eq(token_type::greater_equal, tokens[10].type);
            check_eq(token_type::ampersand_ampersand, tokens[11].type);
            check_eq(token_type::pipe_pipe, tokens[12].type);
            check_eq(token_type::bang, tokens[13].type);
        });

        test("compound_assignments", [this]() {
            string_symbolizer symbolizer;
            lexer lex("+= -= *= /= %=", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(6), tokens.size());
            check_eq(token_type::plus_equal, tokens[0].type);
            check_eq(token_type::minus_equal, tokens[1].type);
            check_eq(token_type::star_equal, tokens[2].type);
            check_eq(token_type::slash_equal, tokens[3].type);
            check_eq(token_type::percent_equal, tokens[4].type);
        });

        test("delimiters", [this]() {
            string_symbolizer symbolizer;
            lexer lex("( ) { } [ ] ; ,", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(9), tokens.size());
            check_eq(token_type::left_paren, tokens[0].type);
            check_eq(token_type::right_paren, tokens[1].type);
            check_eq(token_type::left_brace, tokens[2].type);
            check_eq(token_type::right_brace, tokens[3].type);
            check_eq(token_type::left_bracket, tokens[4].type);
            check_eq(token_type::right_bracket, tokens[5].type);
            check_eq(token_type::semicolon, tokens[6].type);
            check_eq(token_type::comma, tokens[7].type);
        });

        // --- Encoding / line-ending handling ---------------------------------

        test("utf8_bom_stripped_at_position_zero", [this]() {
            // A leading UTF-8 BOM is invisible: the same tokens as the un-BOM'd source,
            // and the first token still reports line 1, column 1.
            string_symbolizer symbolizer;
            lexer lex("\xEF\xBB\xBF" "foo + 1", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(4), tokens.size());
            check_eq(token_type::identifier, tokens[0].type);
            check_eq(std::string("foo"), std::string(tokens[0].lexeme));
            check_eq(std::size_t(1), tokens[0].location.line);
            check_eq(std::size_t(1), tokens[0].location.column);
            check_eq(token_type::plus, tokens[1].type);
            check_eq(token_type::integer_literal, tokens[2].type);
        });

        test("utf8_bom_only_stripped_mid_file_is_not", [this]() {
            // The strip is position-0 only: a BOM byte sequence later in the source is a
            // real (exotic) character and must NOT be silently swallowed.
            string_symbolizer symbolizer;
            lexer lex("a\xEF\xBB\xBF", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(token_type::identifier, tokens[0].type);
            check_eq(std::string("a"), std::string(tokens[0].lexeme));
            check_eq(token_type::error, tokens[1].type);   // the mid-file 0xEF is unexpected
        });

        test("utf16_le_bom_is_a_clear_encoding_error", [this]() {
            string_symbolizer symbolizer;
            lexer lex("\xFF\xFE" "foo", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(token_type::error, tokens[0].type);
            check_eq(std::string("UTF-16 encoding is not supported - save the file as UTF-8"),
                     std::string(tokens[0].lexeme));
            check_eq(std::size_t(1), tokens[0].location.line);
            check_eq(std::size_t(1), tokens[0].location.column);
            // Rest of the file is consumed: the error is followed by eof, not more errors.
            check_eq(token_type::eof, tokens[1].type);
        });

        test("utf16_be_bom_is_a_clear_encoding_error", [this]() {
            string_symbolizer symbolizer;
            lexer lex("\xFE\xFF" "foo", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            check_eq(token_type::error, tokens[0].type);
            check_eq(std::string("UTF-16 encoding is not supported - save the file as UTF-8"),
                     std::string(tokens[0].lexeme));
            check_eq(std::size_t(1), tokens[0].location.line);
            check_eq(std::size_t(1), tokens[0].location.column);
        });

        test("cr_only_line_endings_report_correct_line", [this]() {
            // Classic-Mac CR-only source: the deliberate bad char sits on line 3, and the
            // lexer must report line 3 (pre-fix it reported everything at line 1).
            string_symbolizer symbolizer;
            lexer lex("a\rb\r@", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            const token* err = nullptr;
            for (auto& t : tokens) if (t.type == token_type::error) { err = &t; break; }
            check(err != nullptr);
            check_eq(std::size_t(3), err->location.line);
        });

        test("crlf_line_endings_are_not_double_counted", [this]() {
            // CRLF must count as ONE newline: the bad char on line 3 reports line 3, not 5.
            string_symbolizer symbolizer;
            lexer lex("a\r\nb\r\n@", &symbolizer, "test.jai");
            auto tokens = lex.tokenize();
            const token* err = nullptr;
            for (auto& t : tokens) if (t.type == token_type::error) { err = &t; break; }
            check(err != nullptr);
            check_eq(std::size_t(3), err->location.line);
        });

        test("utf8_bom_executes_identically", [this]() {
            // A BOM'd script must produce byte-for-byte identical output and result to the
            // un-BOM'd source. Runs on whichever backend the runner selected (--backend=).
            const std::string script = "print(\"hi\"); var s = 1 + 2; print(s); 40 + s;";
            auto run = [&](const std::string& src) {
                auto engine = make_engine();
                stdlib::register_all(*engine);
                auto capture = std::make_shared<std::ostringstream>();
                engine->set_output_stream(capture);
                jai::script_int result = engine->execute(src).as_int();
                return std::make_pair(capture->str(), result);
            };
            auto plain = run(script);
            auto bommed = run("\xEF\xBB\xBF" + script);
            check_eq(plain.first, bommed.first);     // byte-for-byte identical output
            check_eq(plain.second, bommed.second);   // identical result
        });

        test("utf8_bom_execute_file_matches", [this]() {
            // File-based path: a BOM'd file on disk lexes cleanly through execute_file and
            // matches the same script run without a BOM.
            namespace fs = std::filesystem;
            const std::string script = "print(\"file\"); 7 + 5;";
            fs::path path = fs::temp_directory_path() / "jai_bom_lexer_test.jai";
            {
                std::ofstream out(path, std::ios::binary);
                out << "\xEF\xBB\xBF" << script;
            }
            auto engine = make_engine();
            stdlib::register_all(*engine);
            auto capture = std::make_shared<std::ostringstream>();
            engine->set_output_stream(capture);
            jai::script_int result = engine->execute_file(path.string()).as_int();
            std::error_code ec; fs::remove(path, ec);
            check_eq(std::string("file\n"), capture->str());
            check_eq((jai::script_int)12, result);
        });

        test("template_string_crlf_output_matches_lf", [this]() {
            // A template string spanning source lines: CRLF- and LF-saved forms must yield
            // byte-identical string values (line endings normalized to '\n').
            const std::string lf   = "var n = \"X\"; print(`line1\nline2 ${n}`);";
            const std::string crlf = "var n = \"X\"; print(`line1\r\nline2 ${n}`);";
            auto run = [&](const std::string& src) {
                auto engine = make_engine();
                stdlib::register_all(*engine);
                auto capture = std::make_shared<std::ostringstream>();
                engine->set_output_stream(capture);
                engine->execute(src);
                return capture->str();
            };
            std::string lf_out = run(lf);
            std::string crlf_out = run(crlf);
            check_eq(lf_out, crlf_out);
            check(crlf_out.find('\r') == std::string::npos);   // no stray CR survived
            check(crlf_out.find("line1\nline2 X") != std::string::npos);
        });
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::lexer_tests)