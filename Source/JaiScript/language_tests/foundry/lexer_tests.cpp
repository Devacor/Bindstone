#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/jaiscript_fwd.hpp>

using namespace jai::foundry;

namespace jai::foundry::tests {

class lexer_tests : public suite {
public:
    lexer_tests() : suite("Lexer Tests") {}
    
    void forge_tests() override {
        test("empty_input", [this]() {
            lexer lex("", "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(1), tokens.size());
            check_eq(token_type::eof, tokens[0].type);
        });

        test("single_identifier", [this]() {
            lexer lex("foo", "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(2), tokens.size());
            check_eq(token_type::identifier, tokens[0].type);
            check_eq(std::string("foo"), tokens[0].lexeme);
            check_eq(token_type::eof, tokens[1].type);
        });

        test("keywords", [this]() {
            lexer lex("int float bool class", "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(5), tokens.size());
            check_eq(token_type::int_keyword, tokens[0].type);
            check_eq(token_type::float_keyword, tokens[1].type);
            check_eq(token_type::bool_keyword, tokens[2].type);
            check_eq(token_type::class_keyword, tokens[3].type);
        });

        test("integer_literals", [this]() {
            lexer lex("42 0 999", "test.jai");
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
            lexer lex("3.14 0.0 42.0", "test.jai");
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
            lexer lex(R"("hello" "world" "hello\nworld")", "test.jai");
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
            lexer lex("'a' '\\n' '0'", "test.jai");
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
            lexer lex("true false", "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(3), tokens.size());
            check_eq(token_type::true_keyword, tokens[0].type);
            check_eq(token_type::false_keyword, tokens[1].type);
        });

        test("operators", [this]() {
            lexer lex("+ - * / % == != < > <= >= && || !", "test.jai");
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
            lexer lex("+= -= *= /= %=", "test.jai");
            auto tokens = lex.tokenize();
            check_eq(std::size_t(6), tokens.size());
            check_eq(token_type::plus_equal, tokens[0].type);
            check_eq(token_type::minus_equal, tokens[1].type);
            check_eq(token_type::star_equal, tokens[2].type);
            check_eq(token_type::slash_equal, tokens[3].type);
            check_eq(token_type::percent_equal, tokens[4].type);
        });

        test("delimiters", [this]() {
            lexer lex("( ) { } [ ] ; ,", "test.jai");
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
    }
};

} // namespace jai::foundry::tests

// Auto-register with the test framework
FOUNDRY_REGISTER(jai::foundry::tests::lexer_tests)