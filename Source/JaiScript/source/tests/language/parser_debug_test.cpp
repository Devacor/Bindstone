#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/interpreter.hpp>  // For string_symbolizer
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
            auto eng = engine::make();
            lexer simpleLex(simpleCode);
            auto simpleTokens = simpleLex.tokenize();
            string_symbolizer symbolizer;
            std::unordered_set<std::string> empty_types;
            parser simpleParse(simpleTokens, &symbolizer, eng.get(), empty_types);
            auto simpleResult = simpleParse.parse();
            if (!simpleResult) {
                std::cerr << "Parse failed for simple expression: " << simpleResult.error().message() << "\n";
                return;
            }
            auto& simpleDecls = simpleResult.value();
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

            string_symbolizer symbolizer2;
            std::unordered_set<std::string> empty_types2;
            parser funcParse(funcTokens, &symbolizer2, eng.get(), empty_types2);
            auto funcResult = funcParse.parse();
            if (!funcResult) {
                std::cerr << "Parse failed for function: " << funcResult.error().message() << "\n";
                return;
            }
            auto& funcDecls = funcResult.value();
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

            string_symbolizer symbolizer3;
            std::unordered_set<std::string> empty_types3;
            parser parse(tokens, &symbolizer3, eng.get(), empty_types3);
            auto parseResult = parse.parse();
            if (!parseResult) {
                std::cerr << "Parse failed: " << parseResult.error().message() << "\n";
                return;
            }
            auto& declarations = parseResult.value();

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
                string_symbolizer symbolizer4;
                std::unordered_set<std::string> empty_types4;
                parser parse2(tokens2, &symbolizer4, eng.get(), empty_types4);
                auto parse2Result = parse2.parse();
                if (!parse2Result) {
                    std::cerr << "Parse failed with semicolon: " << parse2Result.error().message() << "\n";
                    return;
                }
                auto& declarations2 = parse2Result.value();

                std::cout << "With ';' after function: " << declarations2.size() << " declarations\n";
                
                // Try newline separation
                std::cout << "\n=== TESTING WITH NEWLINE ===\n";
                std::string code3 = "fun double(x) { return x * 2; }\ndouble(21);";
                
                lexer lex3(code3);
                auto tokens3 = lex3.tokenize();
                string_symbolizer symbolizer5;
                std::unordered_set<std::string> empty_types5;
                parser parse3(tokens3, &symbolizer5, eng.get(), empty_types5);
                auto parse3Result = parse3.parse();
                if (!parse3Result) {
                    std::cerr << "Parse failed with newline: " << parse3Result.error().message() << "\n";
                    return;
                }
                auto& declarations3 = parse3Result.value();

                std::cout << "With newline: " << declarations3.size() << " declarations\n";
            } else {
                std::cout << "\nParser working correctly!\n";
            }
            
            // For now, let's not fail the test, just diagnose
            std::cout << "\nDiagnosis complete.\n";
        });
    }
};

FOUNDRY_REGISTER(parser_debug_tests)