#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/ast.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

class parser_constructor_syntax_tests : public suite {
public:
    parser_constructor_syntax_tests() : suite("Parser Constructor Syntax Tests") {}
    
    void forge_tests() override {
        test("parse_map_constructor", [this]() {
            std::string code = "auto m = map<string, int>{};";
            
            lexer lex(code);
            auto tokens = lex.tokenize();
            
            parser parse(tokens);
            auto declarations = parse.parse();
            check(declarations.size() == 1, "Should parse one declaration");
        });
        
        test("parse_array_constructor", [this]() {
            std::string code = "array<float> nums{};";
            
            lexer lex(code);
            auto tokens = lex.tokenize();
            
            parser parse(tokens);
            auto declarations = parse.parse();
            
            check(declarations.size() == 1, "Should parse one declaration");
        });
        
        test("parse_typed_map_declaration", [this]() {
            std::string code = "map<string, int> ages{};";
            
            lexer lex(code);
            auto tokens = lex.tokenize();
            
            parser parse(tokens);
            auto declarations = parse.parse();
            check(declarations.size() == 1, "Should parse one declaration");
        });
    }
};

// Enable individual test execution
CONDITIONAL_ISOLATED_TEST(parser_constructor_syntax_tests)