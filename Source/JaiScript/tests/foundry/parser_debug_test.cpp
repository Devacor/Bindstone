#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/ast.hpp>
#include <iostream>

using namespace jai;
using namespace jai::foundry;

class parser_debug_tests : public suite {
public:
    parser_debug_tests() : suite("Parser Debug Tests") {}
    
    void forge_tests() override {
        test("parse_function_and_call", [this]() {
            // Test parsing a function declaration followed by a call
            std::string code = "function double(:x) { return x * 2; } double(21);";
            
            // Also test with a simple expression to understand the issue
            std::cout << "\n=== TESTING SIMPLE EXPRESSION ===\n";
            std::string simpleCode = "42;";
            lexer simpleLex(simpleCode);
            auto simpleTokens = simpleLex.tokenize();
            parser simpleParse(simpleTokens);
            auto simpleDecls = simpleParse.parse();
            std::cout << "Simple '42;' produces " << simpleDecls.size() << " declarations\n";
            
            // Test just the function declaration alone
            std::cout << "\n=== TESTING JUST FUNCTION ===\n";
            std::string funcOnly = "function double(:x) { return x * 2; }";
            lexer funcLex(funcOnly);
            auto funcTokens = funcLex.tokenize();
            
            std::cout << "Function tokens:\n";
            for (size_t i = 0; i < funcTokens.size() && i < 15; ++i) {
                std::cout << "[" << i << "] " << static_cast<int>(funcTokens[i].type) 
                          << " '" << funcTokens[i].lexeme << "'\n";
            }
            
            parser funcParse(funcTokens);
            auto funcDecls = funcParse.parse();
            std::cout << "Function alone produces " << funcDecls.size() << " declarations\n";
            if (funcDecls.size() > 0) {
                if (auto* func_decl = dynamic_cast<function_decl*>(funcDecls[0].get())) {
                    std::cout << "  Type: function_decl\n";
                    std::cout << "  Name: " << func_decl->name << "\n";
                } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(funcDecls[0].get())) {
                    std::cout << "  Type: statement_decl\n";
                    // Check what's inside
                    if (auto* block = dynamic_cast<block_stmt*>(stmt_decl->statement.get())) {
                        std::cout << "  Contains: block_stmt with " << block->declarations.size() << " declarations\n";
                    }
                } else {
                    std::cout << "  Type: other\n";
                }
            }
            
            lexer lex(code);
            auto tokens = lex.tokenize();
            
            std::cout << "\n=== TOKENS ===\n";
            for (size_t i = 0; i < tokens.size() && i < 20; ++i) {
                std::cout << "[" << i << "] " << static_cast<int>(tokens[i].type) 
                          << " '" << tokens[i].lexeme << "'\n";
            }
            
            parser parse(tokens);
            auto declarations = parse.parse();
            
            std::cout << "\n=== DECLARATIONS ===\n";
            std::cout << "Total: " << declarations.size() << "\n";
            
            for (size_t i = 0; i < declarations.size(); ++i) {
                std::cout << "\nDeclaration " << i << ":\n";
                
                if (auto* func_decl = dynamic_cast<function_decl*>(declarations[i].get())) {
                    std::cout << "  Type: function_decl\n";
                    std::cout << "  Name: " << func_decl->name << "\n";
                    std::cout << "  Parameters: " << func_decl->parameters.size() << "\n";
                    
                } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(declarations[i].get())) {
                    std::cout << "  Type: statement_decl\n";
                    if (auto* block = dynamic_cast<block_stmt*>(stmt_decl->statement.get())) {
                        std::cout << "  Contains: block_stmt with " << block->declarations.size() << " declarations\n";
                        
                        // Check what's inside the block
                        for (size_t j = 0; j < block->declarations.size(); ++j) {
                            if (dynamic_cast<function_decl*>(block->declarations[j].get())) {
                                std::cout << "    [" << j << "] function_decl\n";
                            } else if (dynamic_cast<expression_decl*>(block->declarations[j].get())) {
                                std::cout << "    [" << j << "] expression_decl\n";
                            }
                        }
                    } else if (auto* expr_stmt = dynamic_cast<expression_stmt*>(stmt_decl->statement.get())) {
                        std::cout << "  Contains: expression_stmt\n";
                    }
                    
                } else if (auto* expr_decl = dynamic_cast<expression_decl*>(declarations[i].get())) {
                    std::cout << "  Type: expression_decl\n";
                }
            }
            
            // Test what we expect vs what we got
            if (declarations.size() == 1) {
                std::cout << "\nPARSER BUG CONFIRMED: Expected 2 declarations, got 1\n";
                
                // Check if the declaration contains the function call
                if (auto* stmt_decl = dynamic_cast<statement_decl*>(declarations[0].get())) {
                    if (auto* block = dynamic_cast<block_stmt*>(stmt_decl->statement.get())) {
                        std::cout << "\nWARNING: The single declaration contains a block with " << block->declarations.size() << " sub-declarations\n";
                        for (size_t i = 0; i < block->declarations.size(); ++i) {
                            if (auto* func_decl = dynamic_cast<function_decl*>(block->declarations[i].get())) {
                                std::cout << "  [" << i << "] function_decl: " << func_decl->name << "\n";
                            } else if (auto* expr_decl = dynamic_cast<expression_decl*>(block->declarations[i].get())) {
                                std::cout << "  [" << i << "] expression_decl\n";
                            } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(block->declarations[i].get())) {
                                std::cout << "  [" << i << "] statement_decl\n";
                            } else {
                                std::cout << "  [" << i << "] unknown declaration type\n";
                            }
                        }
                    }
                }
                
                // Let's also try with explicit semicolons
                std::cout << "\n=== TESTING WITH EXPLICIT SEMICOLON ===\n";
                std::string code2 = "fun double(x) { return x * 2; }; double(21);";
                
                lexer lex2(code2);
                auto tokens2 = lex2.tokenize();
                parser parse2(tokens2);
                auto declarations2 = parse2.parse();
                
                std::cout << "With ';' after function: " << declarations2.size() << " declarations\n";
                
                // Try newline separation
                std::cout << "\n=== TESTING WITH NEWLINE ===\n";
                std::string code3 = "fun double(x) { return x * 2; }\ndouble(21);";
                
                lexer lex3(code3);
                auto tokens3 = lex3.tokenize();
                parser parse3(tokens3);
                auto declarations3 = parse3.parse();
                
                std::cout << "With newline: " << declarations3.size() << " declarations\n";
            } else {
                std::cout << "\nParser working correctly!\n";
            }
            
            // For now, let's not fail the test, just diagnose
            std::cout << "\nDiagnosis complete.\n";
        });
    }
};

int main() {
    parser_debug_tests tests;
    return tests.quench();
}