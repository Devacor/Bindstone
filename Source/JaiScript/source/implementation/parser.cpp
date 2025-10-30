#include "../../include/jaiscript/detail/parser.hpp"
#include "../../include/jaiscript/detail/interpreter.hpp"  // For string_symbolizer
#include <sstream>
#include <iostream>
#include <optional>
#include <cctype>

namespace jai {

// One true constructor with dependency injection
parser::parser(const std::vector<token>& tokens, string_symbolizer* symbolizer, const std::unordered_set<std::string>& registeredTemplateTypes, const std::string& filename)
    : tokens_(tokens), filename_(filename), current_(0), registered_template_types_(registeredTemplateTypes), symbolizer_(symbolizer) {
    if (!symbolizer_) {
        throw std::invalid_argument("parser requires a valid string_symbolizer");
    }
}

std::vector<declaration_ptr> parser::parse() {
    std::vector<declaration_ptr> declarations;
    
    while (!is_at_end()) {
        try {
            auto decl = declaration();
            if (decl) {
                declarations.push_back(decl);
            }
        } catch (const parse_error&) {
            // Error already reported, synchronize and continue
            synchronize();
        }
    }
    
    // If there were parse errors, throw the first one
    if (!errors_.empty()) {
        throw parse_error(errors_[0], source_location{});
    }

    // Mark the last expression declaration as an implicit return
    if (!declarations.empty()) {
        if (auto* expr_decl = dynamic_cast<expression_decl*>(declarations.back().get())) {
            expr_decl->implicit_return = true;
        } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(declarations.back().get())) {
            // If the last declaration is a statement_decl containing a block,
            // mark the last expression in that block as implicit return
            if (auto* block = dynamic_cast<block_stmt*>(stmt_decl->statement.get())) {
                if (!block->declarations.empty()) {
                    if (auto* expr_decl = dynamic_cast<expression_decl*>(block->declarations.back().get())) {
                        expr_decl->implicit_return = true;
                    }
                }
            }
            // If the last declaration is a statement_decl containing an expression_stmt,
            // mark it as implicit return by setting a flag on the statement_decl
            else if (auto* expr_stmt = dynamic_cast<expression_stmt*>(stmt_decl->statement.get())) {
                // We need to mark this expression statement as an implicit return
                // Since expression_stmt doesn't have implicit_return flag, we need to convert
                // this statement_decl to an expression_decl
                auto expr_decl = std::make_shared<expression_decl>(
                    expr_stmt->expression->location,
                    expr_stmt->expression
                );
                expr_decl->implicit_return = true;
                declarations.back() = expr_decl;
            }
        }
    }

    return declarations;
}

// Error handling
void parser::error(const std::string& message, const token& token) {
    std::stringstream ss;
    ss << token.location.to_string() << ": " << message;
    errors_.push_back(ss.str());
    throw parse_error(message, token.location);
}

void parser::synchronize() {
    advance();
    
    while (!is_at_end()) {
        if (previous().type == token_type::semicolon) return;
        
        switch (peek().type) {
            case token_type::class_keyword:
            case token_type::auto_keyword:
            case token_type::var_keyword:
            case token_type::int_keyword:
            case token_type::float_keyword:
            case token_type::string_keyword:
            case token_type::bool_keyword:
            case token_type::char_keyword:
            case token_type::void_keyword:
            case token_type::if_keyword:
            case token_type::while_keyword:
            case token_type::for_keyword:
            case token_type::return_keyword:
            case token_type::try_keyword:
                return;
            default:
                advance();
        }
    }
}

// token management
token parser::peek() const {
    if (pushed_back_token_.has_value()) {
        return pushed_back_token_.value();
    }
    return tokens_[current_];
}

token parser::previous() const {
    return tokens_[current_ - 1];
}

token parser::advance() {
    if (pushed_back_token_.has_value()) {
        token token = pushed_back_token_.value();
        pushed_back_token_.reset();
        return token;
    }
    if (!is_at_end()) current_++;
    return previous();
}

bool parser::is_at_end() const {
    return peek().type == token_type::eof;
}

bool parser::check(token_type type) const {
    if (is_at_end()) return false;
    return peek().type == type;
}

bool parser::match(token_type type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool parser::match(std::initializer_list<token_type> types) {
    for (token_type type : types) {
        if (check(type)) {
            advance();
            return true;
        }
    }
    return false;
}

token parser::consume(token_type type, const std::string& message) {
    if (check(type)) return advance();
    error(message, peek());
    return token(token_type::error, "", peek().location); // Never reached
}

// Primary expressions
expression_ptr parser::primary() {
    // Literals
    if (match(token_type::true_keyword)) {
        script_value val(script_value::ast_literal_tag{}, true);
        return std::make_shared<literal_expr>(previous().location, val);
    }
    
    if (match(token_type::false_keyword)) {
        script_value val(script_value::ast_literal_tag{}, false);
        return std::make_shared<literal_expr>(previous().location, val);
    }
    
    if (match(token_type::null_keyword)) {
        script_value val(script_value::ast_literal_tag{}, std::monostate{});
        return std::make_shared<literal_expr>(previous().location, val);
    }
    
    if (match(token_type::integer_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.int_value);
        return std::make_shared<literal_expr>(token.location, val);
    }
    
    if (match(token_type::float_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.float_value);
        return std::make_shared<literal_expr>(token.location, val);
    }
    
    if (match(token_type::string_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.string_value);
        return std::make_shared<literal_expr>(token.location, val);
    }
    
    if (match(token_type::char_literal)) {
        token token = previous();
        script_value val(script_value::ast_literal_tag{}, token.char_value);
        return std::make_shared<literal_expr>(token.location, val);
    }
    
    // Keywords
    if (match(token_type::this_keyword)) {
        return std::make_shared<this_expr>(previous().location);
    }
    
    if (match(token_type::super_keyword)) {
        // super:: must be followed by a method name
        token superToken = previous();
        token methodName = consume(token_type::identifier, "Expected method name after 'super::'");

        // Create a member access on super
        auto superExpr = std::make_shared<super_expr>(superToken.location);
        return std::make_shared<member_expr>(methodName.location, superExpr, methodName.lexeme, false);
    }

    // Check if a keyword is being used as a namespace identifier (e.g., string::length)
    // This must come before identifier handling
    if (peek().type != token_type::identifier && peek().type != token_type::eof) {
        // Check if this token is followed by ::
        size_t lookAhead = current_ + 1;
        if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::colon_colon) {
            // This is a keyword being used as a namespace name
            token nameToken = advance();
            return std::make_shared<identifier_expr>(nameToken.location, nameToken.lexeme);
        }
    }

    // Identifiers (including potential template constructors)
    if (match(token_type::identifier)) {
        token identToken = previous();
        
        // Check if this might be a templated type like Point<int>
        if (check(token_type::less)) {
            size_t savedPos = current_ - 1; // Save position including the identifier
            current_--; // Back up to re-parse as type
            
            try {
                type_info_ptr type = parse_type();
                if (type && check(token_type::left_brace)) {
                    // This is a brace-initialized constructor like array<int>{}
                    advance(); // consume '{'
                    
                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_brace)) {
                        arguments.reserve(4);
                        do {
                            arguments.push_back(expression());
                        } while (match(token_type::comma));
                    }
                    
                    consume(token_type::right_brace, "Expected '}' after constructor arguments");
                    return std::make_shared<new_expr>(identToken.location, type, std::move(arguments));
                } else if (type && check(token_type::left_paren)) {
                    // Parentheses constructor for user-defined template types
                    advance(); // consume '('

                    std::vector<expression_ptr> arguments;
                    if (!check(token_type::right_paren)) {
                        do {
                            arguments.push_back(expression());
                        } while (match(token_type::comma));
                    }

                    consume(token_type::right_paren, "Expected ')' after constructor arguments");
                    return std::make_shared<new_expr>(identToken.location, type, std::move(arguments));
                }
            } catch (const parse_error&) {
                // Not a valid template type, continue as regular identifier
            }
            
            // Not a constructor, restore position and continue
            current_ = savedPos + 1;
        }
        
        return std::make_shared<identifier_expr>(identToken.location, identToken.lexeme);
    }
    
    // Grouped expression
    if (match(token_type::left_paren)) {
        expression_ptr expr = expression();
        consume(token_type::right_paren, "Expected ')' after expression");
        return expr;
    }

    // Array literal with [] or Lambda expression
    if (match(token_type::left_bracket)) {
        auto startLoc = previous().location;
        
        // Check for empty array OR empty capture list for lambda
        if (check(token_type::right_bracket)) {
            consume(token_type::right_bracket, "Expected ']'");
            
            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Put back the brackets so lambda_expression can parse them
                current_ -= 2; // Back up to before '['
                return lambda_expression();
            }
            
            return std::make_shared<array_literal_expr>(startLoc, std::vector<expression_ptr>());
        }
        
        // Save position for potential backtrack to lambda
        size_t savedPos = current_;
        
        // Try to parse as array literal first
        std::vector<expression_ptr> elements;
        bool is_array = true;
        
        // Check if this might be a lambda with default capture [= or [&
        // or explicit capture [this, [x, [&x
        if (check(token_type::equal) || check(token_type::ampersand)) {
            // This is definitely a lambda, not an array
            current_ = savedPos - 1;
            return lambda_expression();
        }

        // Check for [this]() or [identifier]() pattern which is definitely a lambda
        // We need to look ahead further to distinguish from single-element arrays like [x]
        if (check(token_type::this_keyword) || check(token_type::identifier)) {
            // Look ahead to see if this is followed by ] and then (
            size_t lookAhead = current_ + 1;
            if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::right_bracket) {
                // Check if there's a ( after the ]
                size_t lookAhead2 = lookAhead + 1;
                if (lookAhead2 < tokens_.size() && tokens_[lookAhead2].type == token_type::left_paren) {
                    // Pattern is [this]() or [x]() - definitely a lambda
                    current_ = savedPos - 1;
                    return lambda_expression();
                }
            }
            // NOTE: We intentionally do NOT check for [identifier, here anymore
            // because [x, y, z] could be either an array literal OR a lambda capture list.
            // We'll distinguish them later by checking if there's a ( after the closing ].
        }
        
        // Parse first element/expression
        elements.push_back(expression());
        
        // If we see a comma, it's definitely an array
        if (match(token_type::comma)) {
            // Continue parsing array elements
            do {
                elements.push_back(expression());
            } while (match(token_type::comma));
            
            consume(token_type::right_bracket, "Expected ']' after array elements");
            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Restore position and parse as lambda
                current_ = savedPos - 1;
                return lambda_expression();
            }
            return std::make_shared<array_literal_expr>(startLoc, std::move(elements));
        }
        // If we see ], it might be a single-element array or lambda capture
        else if (match(token_type::right_bracket)) {
            // Check if this is followed by '(' which would make it a lambda
            if (check(token_type::left_paren)) {
                // Restore position and parse as lambda
                current_ = savedPos - 1;
                return lambda_expression();
            }
            return std::make_shared<array_literal_expr>(startLoc, std::move(elements));
        }
        // Otherwise, it might be a lambda capture list
        else {
            // Restore position and parse as lambda
            current_ = savedPos - 1;
            return lambda_expression();
        }
    }
    
    
    // Check for type constructors (array<int>(), map<string, int>(), etc.)
    if (check(token_type::array_keyword) || check(token_type::map_keyword) || 
        check(token_type::weak_ptr_keyword) || check(token_type::shared_ptr_keyword) ||
        check(token_type::int_keyword) || check(token_type::float_keyword) || 
        check(token_type::string_keyword) || check(token_type::bool_keyword) || check(token_type::char_keyword)) {
        size_t savedPos = current_;
        type_info_ptr type = parse_type();
        if (type) {
            // This is a type constructor - convert it to an identifier expression
            // that will be handled by postfix() for () or {} initialization
            current_ = savedPos;
            token typeToken = advance();
            std::string type_name = typeToken.lexeme;
            
            // For template types, we need to parse the full type
            if (check(token_type::less)) {
                // This is a template type, parse the full type
                current_ = savedPos;
                type = parse_type();
                
                // Check if this is followed by {} or () for constructor syntax
                if (check(token_type::left_brace) || check(token_type::left_paren)) {
                    bool is_brace = check(token_type::left_brace);
                    advance(); // consume '{' or '('
                    
                    std::vector<expression_ptr> arguments;
                    if (!check(is_brace ? token_type::right_brace : token_type::right_paren)) {
                        arguments.reserve(4);
                        do {
                            arguments.push_back(expression());
                        } while (match(token_type::comma));
                    }
                    
                    if (is_brace) {
                        consume(token_type::right_brace, "Expected '}' after constructor arguments");
                    } else {
                        consume(token_type::right_paren, "Expected ')' after constructor arguments");
                    }
                    return std::make_shared<new_expr>(typeToken.location, type, std::move(arguments));
                }
                
                // Otherwise return identifier with type name
                type_name = type->type_name;
            }
            
            return std::make_shared<identifier_expr>(typeToken.location, type_name);
        }
        current_ = savedPos;
    }
    
    // Check for custom type constructors (Point<int>(), SafeComponent<Button>(), etc.)
    if (check(token_type::identifier) || check(token_type::user_template_type)) {
        size_t savedPos = current_;
        advance(); // consume identifier or template type

        // Check if this might be a templated constructor
        if (check(token_type::less) || previous().type == token_type::user_template_type) {
            // Backtrack and parse as type
            current_ = savedPos;
            type_info_ptr type = parse_type();
            if (type && check(token_type::left_brace)) {
                // This is a brace-initialized constructor expression like map<string, int>{}
                // Parse the arguments and create new_expr directly
                advance(); // consume '{'

                std::vector<expression_ptr> arguments;
                if (!check(token_type::right_brace)) {
                    arguments.reserve(4);
                    do {
                        arguments.push_back(expression());
                    } while (match(token_type::comma));
                }

                consume(token_type::right_brace, "Expected '}' after constructor arguments");

                // Create new_expr with the full type info preserved
                return std::make_shared<new_expr>(tokens_[savedPos].location, type, std::move(arguments));
            } else if (type && check(token_type::left_paren)) {
                // Parentheses constructor for template types
                advance(); // consume '('

                std::vector<expression_ptr> arguments;
                if (!check(token_type::right_paren)) {
                    do {
                        arguments.push_back(expression());
                    } while (match(token_type::comma));
                }

                consume(token_type::right_paren, "Expected ')' after constructor arguments");
                return std::make_shared<new_expr>(tokens_[savedPos].location, type, std::move(arguments));
            }
            // Otherwise backtrack and continue
            current_ = savedPos;
        } else {
            // Backtrack - not a template
            current_ = savedPos;
        }
    }
    
    // Super keyword
    if (match(token_type::super_keyword)) {
        return std::make_shared<super_expr>(previous().location);
    }

    // Regular identifier (variable name)
    if (match(token_type::identifier)) {
        token name = previous();
        return std::make_shared<identifier_expr>(name.location, name.lexeme);
    }

    error("Expected expression", peek());
    return nullptr; // Never reached
}

// Type parsing
type_info_ptr parser::parse_type() {
    // Handle auto/var/function
    if (match({token_type::auto_keyword, token_type::var_keyword, token_type::function_keyword})) {
        return nullptr; // Type inference or function keyword
    }
    
    // Primitive types
    if (match(token_type::int_keyword)) {
        return type_info::make_int();
    }
    if (match(token_type::float_keyword)) {
        return type_info::make_float();
    }
    if (match(token_type::string_keyword)) {
        return type_info::make_string();
    }
    if (match(token_type::bool_keyword)) {
        return type_info::make_bool();
    }
    if (match(token_type::char_keyword)) {
        return type_info::make_char();
    }
    if (match(token_type::void_keyword)) {
        auto info = std::make_shared<type_info>(script_value_type::jai_null_type);
        info->type_name = "void";
        return info;
    }
    
    // Generic types
    if (match(token_type::array_keyword)) {
        consume(token_type::less, "Expected '<' after 'array'");
        type_info_ptr element_type = parse_type();
        consume_greater_in_generic("Expected '>' after array element type");
        return type_info::make_array(element_type);
    }
    
    if (match(token_type::map_keyword)) {
        consume(token_type::less, "Expected '<' after 'map'");
        type_info_ptr keyType = parse_type();
        consume(token_type::comma, "Expected ',' after map key type");
        type_info_ptr valueType = parse_type();
        consume_greater_in_generic("Expected '>' after map value type");
        return type_info::make_map(keyType, valueType);
    }
    
    if (match(token_type::weak_ptr_keyword)) {
        consume(token_type::less, "Expected '<' after 'weak_ptr'");
        type_info_ptr pointee_type = parse_type();
        consume_greater_in_generic("Expected '>' after weak_ptr type");
        return type_info::make_weak_ptr(pointee_type);
    }
    
    if (match(token_type::shared_ptr_keyword)) {
        consume(token_type::less, "Expected '<' after 'shared_ptr'");
        type_info_ptr pointee_type = parse_type();
        consume_greater_in_generic("Expected '>' after shared_ptr type");
        // Create a shared_ptr type that wraps the pointee type
        // This ensures reference semantics (no clone on assign)
        auto info = std::make_shared<type_info>(script_value_type::jai_shared_ptr_type);
        info->type_params.push_back(pointee_type);
        // For shared_ptr<T>, the type_name should be T since it's just reference semantics
        if (pointee_type) {
            info->type_name = pointee_type->type_name;
        } else {
            info->type_name = "shared_ptr";
        }
        return info;
    }
    
    // User-defined type (potentially templated)
    if (match({token_type::identifier, token_type::user_template_type})) {
        std::string type_name = previous().lexeme;
        token_type typeToken = previous().type;
        
        // If it's a user_template_type token, we know it's safe to parse template syntax
        if (typeToken == token_type::user_template_type && match(token_type::less)) {
            // Try to parse as templated type like Point<int> or SafeComponent<Button>
            size_t savedPos = current_ - 1; // Save position after '<'
            
            try {
                std::vector<type_info_ptr> templateParams;
                
                // Parse template arguments
                // Use a while loop instead of do-while to ensure we have at least one argument
                type_info_ptr firstParam = parse_type();
                if (!firstParam) {
                    throw parse_error("Expected template parameter", peek().location);
                }
                templateParams.push_back(firstParam);
                
                while (match(token_type::comma)) {
                    type_info_ptr param = parse_type();
                    if (!param) {
                        throw parse_error("Expected template parameter after ','", peek().location);
                    }
                    templateParams.push_back(param);
                }
                
                consume_greater_in_generic("Expected '>' after template parameters");
                
                // Build the full template type name
                type_name += "<";
                for (size_t i = 0; i < templateParams.size(); ++i) {
                    if (i > 0) type_name += ", ";
                    if (templateParams[i]) {
                        type_name += templateParams[i]->type_name;
                    } else {
                        // Handle null type (shouldn't happen but let's be safe)
                        type_name += "unknown";
                    }
                }
                type_name += ">";
                
                // Create a templated object type
                auto type_info = type_info::make_object(type_name);
                type_info->type_params = std::move(templateParams);
                return type_info;
            } catch (const parse_error&) {
                // Failed to parse as template, restore position and treat as simple type
                current_ = savedPos;
            }
        }
        
        return type_info::make_object(type_name);
    }
    
    error("Expected type", peek());
    return nullptr;
}

// Simple expression for now
expression_ptr parser::expression() {
    return assignment();
}

expression_ptr parser::assignment() {
    // Check for map literals first (both C++ and JSON style)
    // Only try to parse as map if it looks like one, to avoid exception-based control flow
    if (check(token_type::left_brace) && looks_like_map_literal()) {
        return parse_map_literal();
    }

    expression_ptr expr = ternary();

    if (match({token_type::equal, token_type::plus_equal, token_type::minus_equal,
               token_type::star_equal, token_type::slash_equal, token_type::percent_equal})) {
        token op = previous();
        expression_ptr right = assignment();
        return std::make_shared<assignment_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

// Helper function to check if { starts a map literal (as opposed to a block statement)
// This avoids using exceptions for control flow
bool parser::looks_like_map_literal() {
    // Save position for lookahead
    size_t savedPos = current_;

    // Consume the {
    advance();

    // Empty braces {} is an empty map
    if (check(token_type::right_brace)) {
        current_ = savedPos;
        return true;
    }

    // Check for keywords that indicate a block statement, not a map
    if (check(token_type::auto_keyword) || check(token_type::var_keyword) ||
        check(token_type::if_keyword) || check(token_type::while_keyword) ||
        check(token_type::for_keyword) || check(token_type::return_keyword) ||
        check(token_type::class_keyword) || check(token_type::function_keyword)) {
        current_ = savedPos;
        return false;
    }

    // Check for function calls: identifier followed by (
    // This indicates a block with statements, not a map
    if (check(token_type::identifier)) {
        advance(); // consume identifier
        if (check(token_type::left_paren)) {
            current_ = savedPos;
            return false;
        }
        current_ = savedPos + 1; // back to after {
    }

    // Check for JSON-style map: identifier/string followed by :
    if (check(token_type::identifier) || check(token_type::string_literal)) {
        advance();
        bool isMap = check(token_type::colon);
        current_ = savedPos;
        return isMap;
    }

    // Check for C++ style map: {{ ...
    if (check(token_type::left_brace)) {
        current_ = savedPos;
        return true;
    }

    // Default: not a map (could be block or other construct)
    current_ = savedPos;
    return false;
}

expression_ptr parser::parse_map_literal() {
    consume(token_type::left_brace, "Expected '{'");
    auto startLoc = previous().location;

    // Empty map
    if (check(token_type::right_brace)) {
        consume(token_type::right_brace, "Expected '}'");
        return std::make_shared<map_literal_expr>(startLoc, std::vector<std::pair<expression_ptr, expression_ptr>>());
    }

    // Save position to check for JSON style
    size_t savedPos = current_;
    
    // Check if it's JSON style by looking for "key": value pattern
    bool isJsonStyle = false;
    
    // First, check if the first token could be a key (string or identifier)
    if (check(token_type::string_literal) || check(token_type::identifier)) {
        advance(); // consume the potential key
        if (check(token_type::colon)) {
            isJsonStyle = true;
        }
        // Restore position
        current_ = savedPos;
    }
    
    std::vector<std::pair<expression_ptr, expression_ptr>> entries;
    entries.reserve(8);
    
    if (isJsonStyle) {
        // Parse JSON style: {key: value, key2: value2} or {"key": value}
        do {
            expression_ptr key;

            // Check if key is a bare identifier (convert to string literal)
            if (check(token_type::identifier)) {
                token keyToken = advance();
                // Convert bare identifier to string literal for JSON-style syntax
                // e.g., {x: 10} becomes {"x": 10}
                const script_string& keyStr = keyToken.lexeme;
                script_value keyValue(script_value::ast_literal_tag{}, keyStr);
                key = std::make_shared<literal_expr>(keyToken.location, keyValue);
            } else {
                // Parse as expression (for quoted strings or computed keys)
                key = expression();
            }

            consume(token_type::colon, "Expected ':' after key in JSON-style map");
            expression_ptr value = expression();
            entries.emplace_back(std::move(key), std::move(value));
        } while (match(token_type::comma));
    } else {
        // Parse C++ style: {{"key", value}, {"key2", value2}}
        do {
            consume(token_type::left_brace, "Expected '{' for map entry");
            expression_ptr key = expression();
            consume(token_type::comma, "Expected ',' between key and value in map entry");
            expression_ptr value = expression();
            consume(token_type::right_brace, "Expected '}' after map entry");
            
            entries.emplace_back(std::move(key), std::move(value));
        } while (match(token_type::comma));
    }
    
    consume(token_type::right_brace, "Expected '}' after map entries");
    return std::make_shared<map_literal_expr>(startLoc, std::move(entries));
}

expression_ptr parser::ternary() {
    expression_ptr expr = logical_or();
    
    if (match(token_type::question)) {
        token questionLoc = previous();
        expression_ptr then_expression = expression();
        consume(token_type::colon, "Expected ':' after then expression in ternary");
        expression_ptr else_expression = ternary();
        return std::make_shared<ternary_expr>(questionLoc.location, expr, then_expression, else_expression);
    }
    
    return expr;
}

// expression precedence chain implementations
expression_ptr parser::logical_or() {
    expression_ptr expr = logical_and();
    
    while (match(token_type::pipe_pipe)) {
        token op = previous();
        expression_ptr right = logical_and();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::logical_and() {
    expression_ptr expr = bitwise_or();
    
    while (match(token_type::ampersand_ampersand)) {
        token op = previous();
        expression_ptr right = bitwise_or();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::bitwise_or() {
    expression_ptr expr = bitwise_xor();
    
    while (match(token_type::pipe)) {
        token op = previous();
        expression_ptr right = bitwise_xor();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::bitwise_xor() {
    expression_ptr expr = bitwise_and();
    
    while (match(token_type::caret)) {
        token op = previous();
        expression_ptr right = bitwise_and();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::bitwise_and() {
    expression_ptr expr = equality();
    
    while (match(token_type::ampersand)) {
        token op = previous();
        expression_ptr right = equality();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::equality() {
    expression_ptr expr = relational();
    
    while (match({token_type::equal_equal, token_type::bang_equal})) {
        token op = previous();
        expression_ptr right = relational();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::relational() {
    expression_ptr expr = shift();
    
    while (match({token_type::less, token_type::less_equal, 
                   token_type::greater, token_type::greater_equal, token_type::spaceship})) {
        token op = previous();
        expression_ptr right = shift();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::shift() {
    expression_ptr expr = additive();
    
    while (match({token_type::left_shift, token_type::right_shift})) {
        token op = previous();
        expression_ptr right = additive();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::additive() {
    expression_ptr expr = multiplicative();
    
    while (match({token_type::plus, token_type::minus})) {
        token op = previous();
        expression_ptr right = multiplicative();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::multiplicative() {
    expression_ptr expr = unary();
    
    while (match({token_type::star, token_type::slash, token_type::percent})) {
        token op = previous();
        expression_ptr right = unary();
        expr = std::make_shared<binary_expr>(op.location, expr, op, right);
    }
    
    return expr;
}

expression_ptr parser::unary() {
    if (match({token_type::bang, token_type::minus, token_type::plus_plus, 
               token_type::minus_minus, token_type::ampersand, token_type::tilde})) {
        token op = previous();
        expression_ptr right = unary();
        return std::make_shared<unary_expr>(op.location, op, right);
    }
    
    if (match(token_type::throw_keyword)) {
        token throw_token = previous();
        expression_ptr value = nullptr;
        
        // Check if there's an expression after throw (not a semicolon or end of statement)
        if (!check(token_type::semicolon) && !is_at_end()) {
            value = expression();
        }
        
        return std::make_shared<throw_expr>(throw_token.location, value);
    }
    
    return postfix();
}

expression_ptr parser::postfix() {
    expression_ptr expr = primary();
    
    while (true) {
        if (match(token_type::left_paren)) {
            expr = finish_call(expr);
        } else if (match(token_type::left_brace)) {
            // Brace initialization: Type{args...}
            // This should only be valid if expr is an identifier (type name)
            if (auto* identExpr = dynamic_cast<identifier_expr*>(expr.get())) {
                std::vector<expression_ptr> arguments;
                
                if (!check(token_type::right_brace)) {
                    // Reserve capacity for constructor arguments
                    arguments.reserve(4);
                    do {
                        arguments.push_back(expression());
                    } while (match(token_type::comma));
                }
                
                consume(token_type::right_brace, "Expected '}' after constructor arguments");
                
                // Create a new_expr for object construction
                // The type name is in the identifier
                auto type_info_ptr = std::make_shared<type_info>(script_value_type::jai_object_type, identExpr->name);
                expr = std::make_shared<new_expr>(identExpr->location, type_info_ptr, std::move(arguments));
            } else {
                error("Brace initialization can only be used with type names", previous());
            }
        } else if (match(token_type::dot)) {
            expr = finish_member_access(expr, false);
        } else if (match(token_type::arrow)) {
            expr = finish_member_access(expr, true);
        } else if (match(token_type::colon_colon)) {
            // Static member access or namespace member access
            // Allow keywords as member names (for accessing namespace members)
            token name = peek();
            if (name.type == token_type::identifier ||
                (name.type != token_type::eof && name.type != token_type::left_paren &&
                 name.type != token_type::right_paren && name.type != token_type::semicolon)) {
                advance();
                expr = std::make_shared<member_expr>(name.location, expr, name.lexeme, false, true);
            } else {
                error("Expected member name after '::'", name);
            }
        } else if (match(token_type::left_bracket)) {
            // Array subscript
            expression_ptr index = expression();
            consume(token_type::right_bracket, "Expected ']' after array index");
            token op(token_type::left_bracket, "[", previous().location);
            expr = std::make_shared<binary_expr>(op.location, expr, op, index);
        } else if (match({token_type::plus_plus, token_type::minus_minus})) {
            // Postfix increment/decrement
            token op = previous();
            expr = std::make_shared<unary_expr>(op.location, op, expr, true);  // true = postfix
        } else {
            break;
        }
    }
    
    return expr;
}

// Stub for declaration
declaration_ptr parser::declaration() {
    if (match(token_type::class_keyword)) return class_declaration();
    if (match(token_type::namespace_keyword)) return namespace_declaration();

    // Check for include/import directives
    if (match(token_type::include_keyword)) {
        auto result = include_declaration();
        match(token_type::semicolon);  // Consume optional trailing semicolon
        return result;
    }
    if (match(token_type::import_keyword)) {
        auto result = import_declaration();
        match(token_type::semicolon);  // Consume optional trailing semicolon
        return result;
    }
    
    // Check specifically for function keyword to handle function declarations
    if (check(token_type::function_keyword)) {
        // This is definitely a function declaration
        auto result = function_declaration();
        // After parsing a function, consume optional semicolon but don't require it
        match(token_type::semicolon);
        return result;
    }
    
    // Check for explicit type keywords that start declarations
    if (match({token_type::auto_keyword, token_type::var_keyword, token_type::int_keyword, token_type::float_keyword,
               token_type::string_keyword, token_type::bool_keyword, token_type::char_keyword, token_type::void_keyword,
               token_type::array_keyword, token_type::map_keyword, token_type::weak_ptr_keyword, token_type::shared_ptr_keyword})) {

        // Check if this keyword is followed by :: - if so, it's a namespace access, not a type
        if (check(token_type::colon_colon)) {
            // This is a namespace access expression like string::length(), not a variable declaration
            // Back up and let it fall through to expression parsing
            current_--;
            // Fall through to expression parsing below
        } else {
            // We already consumed the type keyword, so we need to backtrack
            current_--;

            // Look ahead to determine if this is a function or variable declaration
            size_t savedPos = current_;
            parse_type(); // consume the type
        
        // Skip optional & for reference types
        if (check(token_type::ampersand)) {
            advance();
        }
        
        // Check if this is followed by an identifier (variable/function name)
        if (check(token_type::identifier)) {
            advance(); // consume identifier
            if (check(token_type::left_paren)) {
                // This is a function declaration
                current_ = savedPos;
                return function_declaration();
            }
            // Otherwise it's a variable declaration
            current_ = savedPos;
            return variable_declaration();
        } else if (check(token_type::left_paren) || check(token_type::left_brace)) {
            // This is a constructor expression like array<int>() or array<int>{}
            current_ = savedPos;
            // Fall through to parse as expression
            } else {
                // No identifier after type, so it's a variable declaration
                current_ = savedPos;
                return variable_declaration();
            }
        }
    }
    
    // Check if this might be a type name followed by an identifier (for custom types)
    // We need to look ahead to see if this is a declaration
    if (check(token_type::identifier) || check(token_type::user_template_type)) {
        // If it's a user_template_type, we know it's a type declaration
        if (peek().type == token_type::user_template_type) {
            return variable_declaration();
        }
        
        // Save current position
        size_t savedPos = current_;
        std::string firstIdentifier = peek().lexeme;
        
        // Look ahead to see if this is "identifier identifier" pattern
        advance(); // consume first identifier
        
        // Check for template syntax after the identifier
        if (check(token_type::less)) {
            // Might be a templated type like Point<int>
            // Try to parse the full type
            current_ = savedPos;
            size_t typeParsePos = current_;
            try {
                type_info_ptr type = parse_type();
                if (type && check(token_type::identifier)) {
                    // We have a type followed by an identifier - it's a declaration
                    current_ = savedPos;
                    return variable_declaration();
                }
            } catch (...) {
                // Failed to parse as type
            }
            current_ = savedPos;
        } else if (check(token_type::identifier)) {
            // Pattern: identifier identifier - this is likely a declaration
            // Examples: Point p, MyClass obj, etc.
            current_ = savedPos;
            return variable_declaration();
        } else {
            // Not a simple declaration pattern, restore and parse as expression
            current_ = savedPos;
            // Fall through to parse as expression
        }
    }
    
    // Try parsing as expression first (includes map literals, array literals, etc.)
    // If it fails, we'll try parsing as statement
    if (check(token_type::left_brace)) {
        // Use lookahead to determine if this is a map literal or block statement
        // This avoids exception-based control flow
        if (looks_like_map_literal()) {
            // Looks like a map literal - parse as expression
            auto expr = expression();

            // Allow semicolon to be optional at end of file for single expressions
            if (!is_at_end()) {
                consume(token_type::semicolon, "Expected ';' after expression");
            }

            // Create an expression_decl for top-level expressions
            return std::make_shared<expression_decl>(expr->location, expr);
        } else {
            // Looks like a block statement - parse as statement
            auto stmt = statement();
            return std::make_shared<statement_decl>(stmt->location, stmt);
        }
    }
    
    // Check for other statements that can appear at top level
    if (check(token_type::if_keyword) || check(token_type::while_keyword) || check(token_type::for_keyword) ||
        check(token_type::return_keyword) || check(token_type::break_keyword) || check(token_type::continue_keyword) ||
        check(token_type::try_keyword) || check(token_type::switch_keyword) || check(token_type::fallthrough_keyword)) {
        // We need to wrap the statement in a declaration since parse() returns declarations
        auto stmt = statement();
        // Create a statement_decl to wrap statements at the top level
        return std::make_shared<statement_decl>(stmt->location, stmt);
    }
    
    // Otherwise it's a top-level expression statement
    auto expr = expression();
    
    // Allow semicolon to be optional at end of file for single expressions
    if (!is_at_end()) {
        consume(token_type::semicolon, "Expected ';' after expression");
    }
    
    // Create an expression_decl for top-level expressions
    return std::make_shared<expression_decl>(expr->location, expr);
}

// Helper method implementations
expression_ptr parser::finish_call(expression_ptr callee) {
    std::vector<expression_ptr> arguments;
    
    if (!check(token_type::right_paren)) {
        // Reserve capacity for common case of 2-4 arguments
        arguments.reserve(4);
        do {
            arguments.push_back(expression());
        } while (match(token_type::comma));
    }
    
    token paren = consume(token_type::right_paren, "Expected ')' after arguments");
    
    return std::make_shared<call_expr>(paren.location, callee, std::move(arguments));
}

expression_ptr parser::finish_member_access(expression_ptr object, bool is_arrow) {
    token name = consume(token_type::identifier, "Expected member name");
    return std::make_shared<member_expr>(name.location, object, name.lexeme, is_arrow);
}

std::vector<parameter> parser::parse_parameter_list() {
    std::vector<parameter> params;
    
    if (!check(token_type::right_paren)) {
        do {
            // Parse parameter without const support (const is only for C++ binding)
            bool is_const = false;
            
            type_info_ptr type = nullptr;
            std::string name;
            bool is_reference = false;
            
            // Check for :name syntax (shorthand for auto: name)
            if (match(token_type::colon)) {
                // :name means auto type
                type = nullptr; // nullptr means auto
                name = consume(token_type::identifier, "Expected parameter name after ':'").lexeme;
            }
            // Check for type: name syntax
            else if (check(token_type::identifier) || check(token_type::auto_keyword) || check(token_type::var_keyword) ||
                     check(token_type::function_keyword) || check(token_type::int_keyword) || check(token_type::float_keyword) || 
                     check(token_type::string_keyword) || check(token_type::bool_keyword) || check(token_type::char_keyword) || 
                     check(token_type::void_keyword) || check(token_type::array_keyword) || check(token_type::map_keyword) ||
                     check(token_type::weak_ptr_keyword) || check(token_type::shared_ptr_keyword)) {
                
                type = parse_type();

                if (match(token_type::colon)) {
                    // type: name syntax
                    name = consume(token_type::identifier, "Expected parameter name after ':'").lexeme;
                } else if (check(token_type::ampersand) || check(token_type::identifier)) {
                    // Traditional type name syntax
                    // Check for reference
                    is_reference = match(token_type::ampersand);
                    name = consume(token_type::identifier, "Expected parameter name").lexeme;
                } else if (check(token_type::comma) || check(token_type::right_paren)) {
                    // No identifier after type - treat the type as the parameter name with auto type
                    // This handles shorthand like: void foo(x) where x is untyped
                    // The type parsed is actually just a simple identifier, so use it as the name
                    if (type && type->base_type == script_value_type::jai_object_type && !type->type_name.empty()) {
                        name = type->type_name;
                        type = nullptr; // Auto type
                    } else {
                        error("Expected parameter name", peek());
                    }
                } else {
                    error("Expected parameter name", peek());
                }
            }
            // No type specified, error
            else {
                error("Expected parameter type or ':' for auto parameter", peek());
            }
            
            params.push_back(parameter(type, name, is_reference, is_const));
        } while (match(token_type::comma));
    }
    
    return params;
}

// Lambda expression parsing
expression_ptr parser::lambda_expression() {
    auto lambda = std::make_shared<lambda_expr>(peek().location);
    
    // Parse capture list
    consume(token_type::left_bracket, "Expected '[' for lambda");
    auto [captures, default_mode] = parse_capture_list();
    lambda->captures = captures;
    lambda->default_capture = default_mode;
    consume(token_type::right_bracket, "Expected ']' after capture list");
    
    // Parse parameters
    consume(token_type::left_paren, "Expected '(' for lambda parameters");
    lambda->parameters = parse_parameter_list();
    consume(token_type::right_paren, "Expected ')' after parameters");
    
    // Parse return type if specified
    if (match(token_type::arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(token_type::left_brace)) {
            // -> { means auto return type
            lambda->return_type = nullptr; // nullptr means auto
        } else {
            lambda->return_type = parse_type();
        }
    }
    
    // Parse body
    consume(token_type::left_brace, "Expected '{' for lambda body");
    lambda->body = std::dynamic_pointer_cast<block_stmt>(block_statement());
    
    return lambda;
}

std::pair<std::vector<lambda_expr::capture>, lambda_expr::capture_default> parser::parse_capture_list() {
    std::vector<lambda_expr::capture> captures;
    lambda_expr::capture_default default_mode = lambda_expr::capture_default::none;

    // Helper lambda to parse a capture variable name (identifier or 'this')
    auto parse_capture_name = [this]() -> std::string {
        if (check(token_type::this_keyword)) {
            advance();  // consume 'this'
            return "this";
        } else {
            return consume(token_type::identifier, "Expected capture variable name or 'this'").lexeme;
        }
    };

    if (!check(token_type::right_bracket)) {
        // Check for default capture first
        if (check(token_type::equal)) {
            // [=...] - capture all by value (with possible exceptions)
            advance(); // consume '='
            default_mode = lambda_expr::capture_default::by_value;
            
            // Check for exceptions after comma
            if (match(token_type::comma)) {
                do {
                    bool byRef = match(token_type::ampersand);
                    std::string name = parse_capture_name();
                    captures.emplace_back(name, byRef);
                } while (match(token_type::comma));
            }
        } else if (check(token_type::ampersand)) {
            // Could be [&...] or [&variable]
            advance(); // consume '&'
            
            if (check(token_type::comma) || check(token_type::right_bracket)) {
                // [&...] - capture all by reference (with possible exceptions)
                default_mode = lambda_expr::capture_default::by_reference;
                
                // Check for exceptions after comma
                if (match(token_type::comma)) {
                    do {
                        bool byRef = false; // Explicit variables after [&,] are by value unless prefixed with &
                        if (match(token_type::ampersand)) {
                            byRef = true;
                        }
                        std::string name = parse_capture_name();
                        captures.emplace_back(name, byRef);
                    } while (match(token_type::comma));
                }
            } else {
                // [&variable] - specific variable by reference
                std::string name = parse_capture_name();
                captures.emplace_back(name, true);

                // Continue parsing other captures
                while (match(token_type::comma)) {
                    bool byRef = match(token_type::ampersand);
                    std::string varName = parse_capture_name();
                    captures.emplace_back(varName, byRef);
                }
            }
        } else {
            // Regular explicit captures: [x, y, &z, this]
            do {
                bool byRef = match(token_type::ampersand);
                std::string name = parse_capture_name();
                captures.emplace_back(name, byRef);
            } while (match(token_type::comma));
        }
    }
    
    return {captures, default_mode};
}

// statement parsing implementations
statement_ptr parser::statement() {
    if (match(token_type::left_brace)) return block_statement();
    if (match(token_type::if_keyword)) return if_statement();
    if (match(token_type::while_keyword)) return while_statement();
    if (match(token_type::for_keyword)) return for_statement();
    if (match(token_type::return_keyword)) return return_statement();
    if (match(token_type::break_keyword)) return break_statement();
    if (match(token_type::continue_keyword)) return continue_statement();
    if (match(token_type::try_keyword)) return try_statement();
    if (match(token_type::switch_keyword)) return switch_statement();
    
    // Check for fallthrough keyword 
    if (match(token_type::fallthrough_keyword)) {
        if (!in_switch_case_) {
            error("'fallthrough' can only be used inside a switch case", previous());
        }
        auto fallthrough = std::make_shared<fallthrough_stmt>(previous().location);
        consume(token_type::semicolon, "Expected ';' after 'fallthrough'");
        return fallthrough;
    }
    
    return expression_statement();
}

statement_ptr parser::block_statement() {
    token leftBrace = previous();
    std::vector<declaration_ptr> declarations;

    while (!check(token_type::right_brace) && !is_at_end()) {
        declarations.push_back(declaration());
    }

    consume(token_type::right_brace, "Expected '}' after block");

    return std::make_shared<block_stmt>(leftBrace.location, std::move(declarations));
}

statement_ptr parser::expression_statement() {
    expression_ptr expr = expression();
    consume(token_type::semicolon, "Expected ';' after expression");
    return std::make_shared<expression_stmt>(expr->location, expr);
}

statement_ptr parser::if_statement() {
    token ifToken = previous();
    
    consume(token_type::left_paren, "Expected '(' after 'if'");
    expression_ptr condition = expression();
    consume(token_type::right_paren, "Expected ')' after if condition");
    
    statement_ptr then_statement = statement();
    statement_ptr else_statement = nullptr;
    
    if (match(token_type::else_keyword)) {
        else_statement = statement();
    }
    
    return std::make_shared<if_stmt>(ifToken.location, condition, then_statement, else_statement);
}

statement_ptr parser::while_statement() {
    token whileToken = previous();
    
    consume(token_type::left_paren, "Expected '(' after 'while'");
    expression_ptr condition = expression();
    consume(token_type::right_paren, "Expected ')' after while condition");
    
    statement_ptr body = statement();
    
    return std::make_shared<while_stmt>(whileToken.location, condition, body);
}

statement_ptr parser::for_statement() {
    token forToken = previous();
    
    consume(token_type::left_paren, "Expected '(' after 'for'");
    
    // Save position for potential backtracking
    size_t savedPosition = current_;
    
    // Try to parse as range-based for loop first
    // This will be: [const] type [&] identifier : expression
    bool is_const = match(token_type::const_keyword);
    bool is_reference = false;
    type_info_ptr element_type = nullptr;
    
    // Check if we have a type followed by optional & and identifier : pattern
    if (check(token_type::auto_keyword) || check(token_type::var_keyword) || check(token_type::int_keyword) || 
        check(token_type::float_keyword) || check(token_type::string_keyword) || check(token_type::bool_keyword) || 
        check(token_type::char_keyword) || check(token_type::identifier)) {
        
        // Parse the type
        element_type = parse_type();
        
        // Check for reference after type
        if (match(token_type::ampersand)) {
            is_reference = true;
        }
        
        // Must have an identifier
        if (check(token_type::identifier)) {
            token varName = advance();
            
            // Check for colon - this indicates range-based for
            if (match(token_type::colon)) {
                // This is a range-based for loop!
                expression_ptr container = expression();
                consume(token_type::right_paren, "Expected ')' after range expression");
                statement_ptr body = statement();
                
                return std::make_shared<range_for_stmt>(
                    forToken.location, element_type, varName.lexeme, 
                    is_reference, is_const, container, body
                );
            }
        }
    }
    
    // Not a range-based for loop, restore position and parse as traditional for
    current_ = savedPosition;
    
    // Init
    declaration_ptr init = nullptr;
    if (match(token_type::semicolon)) {
        // No init
    } else if (check(token_type::auto_keyword) || check(token_type::var_keyword) || 
               check(token_type::int_keyword) || check(token_type::float_keyword) || 
               check(token_type::string_keyword) || check(token_type::bool_keyword) || 
               check(token_type::char_keyword) || check(token_type::array_keyword) ||
               check(token_type::map_keyword) || check(token_type::identifier)) {
        // Parse variable declaration without consuming the semicolon
        // (the for loop will handle it)
        type_info_ptr type = parse_type();
        
        // Check for reference after type
        if (match(token_type::ampersand)) {
            auto refType = std::make_shared<type_info>(script_value_type::jai_reference_type);
            refType->type_name = type ? (type->type_name + "&") : "auto&";
            refType->type_params.push_back(type);
            type = refType;
        }
        
        token name = consume(token_type::identifier, "Expected variable name");
        
        expression_ptr initializer = nullptr;
        if (match(token_type::equal)) {
            initializer = expression();
        }
        
        init = std::make_shared<variable_decl>(name.location, type, name.lexeme, symbolizer_->intern(name.lexeme), initializer);
        // Note: NOT consuming semicolon here - the for loop will handle it
    } else {
        // expression init - wrap in a variable declaration without a type
        expression_ptr expr = expression();
        consume(token_type::semicolon, "Expected ';' after for loop initializer");
        // For now, we'll skip expression-only init since it needs to be a declaration
        // This is a limitation we can address later with a more flexible AST
    }
    
    // Consume semicolon after init
    if (init) {
        consume(token_type::semicolon, "Expected ';' after for loop initializer");
    }
    
    // Condition
    expression_ptr condition = nullptr;
    if (!check(token_type::semicolon)) {
        condition = expression();
    }
    consume(token_type::semicolon, "Expected ';' after for loop condition");
    
    // Update
    expression_ptr update = nullptr;
    if (!check(token_type::right_paren)) {
        update = expression();
    }
    consume(token_type::right_paren, "Expected ')' after for loop clauses");
    
    statement_ptr body = statement();
    
    return std::make_shared<for_stmt>(forToken.location, init, condition, update, body);
}

statement_ptr parser::return_statement() {
    token returnToken = previous();
    
    expression_ptr value = nullptr;
    if (!check(token_type::semicolon)) {
        value = expression();
    }
    
    consume(token_type::semicolon, "Expected ';' after return value");
    
    return std::make_shared<return_stmt>(returnToken.location, value);
}

statement_ptr parser::break_statement() {
    token breakToken = previous();
    consume(token_type::semicolon, "Expected ';' after 'break'");
    return std::make_shared<break_stmt>(breakToken.location);
}

statement_ptr parser::continue_statement() {
    token continueToken = previous();
    consume(token_type::semicolon, "Expected ';' after 'continue'");
    return std::make_shared<continue_stmt>(continueToken.location);
}

statement_ptr parser::try_statement() {
    token tryToken = previous();
    
    // Parse try block
    consume(token_type::left_brace, "Expected '{' after 'try'");
    statement_ptr try_block = block_statement();
    
    // Must have catch block
    consume(token_type::catch_keyword, "Expected 'catch' after try block");
    
    // Optional catch variable
    std::string catch_var;
    if (match(token_type::left_paren)) {
        token var_name = consume(token_type::identifier, "Expected variable name in catch");
        catch_var = var_name.lexeme;
        consume(token_type::right_paren, "Expected ')' after catch variable");
    }
    
    // Parse catch block
    consume(token_type::left_brace, "Expected '{' after 'catch'");
    statement_ptr catch_block = block_statement();
    
    return std::make_shared<try_stmt>(tryToken.location, try_block, catch_block, catch_var);
}

// declaration parsing implementations
declaration_ptr parser::class_declaration() {
    token className = consume(token_type::identifier, "Expected class name");
    
    std::vector<std::string> base_classes;
    if (match(token_type::colon)) {
        do {
            base_classes.push_back(consume(token_type::identifier, "Expected base class name").lexeme);
        } while (match(token_type::comma));
    }
    
    consume(token_type::left_brace, "Expected '{' before class body");

    // Intern the class name at parse time for fast comparisons later
    auto classDecl = std::make_shared<class_decl>(className.location, className.lexeme, symbolizer_->intern(className.lexeme));
    classDecl->base_classes = std::move(base_classes);
    
    // Parse class members
    class_decl::member_visibility visibility = class_decl::Public;
    
    while (!check(token_type::right_brace) && !is_at_end()) {
        // Check for visibility specifiers
        if (match(token_type::public_keyword)) {
            consume(token_type::colon, "Expected ':' after 'public'");
            visibility = class_decl::Public;
            continue;
        }
        if (match(token_type::private_keyword)) {
            consume(token_type::colon, "Expected ':' after 'private'");
            visibility = class_decl::Private;
            continue;
        }
        
        // Parse member declaration
        declaration_ptr member;

        // Check for constructor: ClassName(params)
        // But NOT a field like: ClassName fieldName = ...
        // We need to look ahead to distinguish
        if (check(token_type::identifier) && peek().lexeme == className.lexeme) {
            // Look ahead to see if this is followed by '(' (constructor) or identifier (field)
            size_t lookAhead = current_ + 1;
            if (lookAhead < tokens_.size() && tokens_[lookAhead].type == token_type::left_paren) {
                // This is a constructor
                advance(); // consume class name
                member = parse_function_body(className.lexeme, nullptr);
                // TODO: Parse constructor delegation syntax (: base(args), : this(args))
                // Currently no support for constructor chaining
            } else {
                // This is a field with the class type, fall through to regular member parsing
                bool is_static = match(token_type::static_keyword);
                bool is_override = match(token_type::override_keyword);
                type_info_ptr type = parse_type();
                // Continue with regular member parsing...
                if (check(token_type::identifier)) {
                    token name = advance();
                    if (match(token_type::left_paren)) {
                        // Function
                        current_--;
                        auto func = std::make_shared<function_decl>(previous().location, name.lexeme);
                        func->is_static = is_static;
                        func->is_override = is_override;
                        consume(token_type::left_paren, "Expected '(' after function name");
                        func->parameters = parse_parameter_list();
                        consume(token_type::right_paren, "Expected ')' after parameters");
                        if (match(token_type::arrow)) {
                            if (check(token_type::left_brace)) {
                                func->return_type = nullptr;
                            } else {
                                func->return_type = parse_type();
                            }
                        } else {
                            func->return_type = type;
                        }
                        if (match(token_type::override_keyword)) {
                            func->is_override = true;
                        }
                        if (match(token_type::colon)) {
                            do {
                                if (match(token_type::super_keyword)) {
                                    consume(token_type::left_paren, "Expected '(' after 'super'");
                                    std::vector<expression_ptr> args;
                                    if (!check(token_type::right_paren)) {
                                        do {
                                            args.push_back(expression());
                                        } while (match(token_type::comma));
                                    }
                                    consume(token_type::right_paren, "Expected ')' after super arguments");
                                    func->initializers.emplace_back("super", std::move(args));
                                } else if (match(token_type::this_keyword)) {
                                    consume(token_type::left_paren, "Expected '(' after 'this'");
                                    std::vector<expression_ptr> args;
                                    if (!check(token_type::right_paren)) {
                                        do {
                                            args.push_back(expression());
                                        } while (match(token_type::comma));
                                    }
                                    consume(token_type::right_paren, "Expected ')' after this arguments");
                                    func->initializers.emplace_back("this", std::move(args));
                                } else {
                                    error("Expected 'super' or 'this' in constructor initializer list", peek());
                                }
                            } while (match(token_type::comma));
                        }
                        if (!match(token_type::left_brace)) {
                            error("Expected '{' before function body", peek());
                        }
                        func->body = std::dynamic_pointer_cast<block_stmt>(block_statement());
                        member = func;
                    } else {
                        // Variable
                        expression_ptr init = nullptr;
                        if (match(token_type::equal)) {
                            init = expression();
                        }
                        consume(token_type::semicolon, "Expected ';' after field declaration");
                        auto var_decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, symbolizer_->intern(name.lexeme), init);
                        var_decl->is_static = is_static;
                        member = var_decl;
                    }
                } else {
                    error("Expected member name", peek());
                }
            }
        } else if (match(token_type::tilde)) {
            consume(token_type::identifier, "Expected class name after '~'");
            member = parse_function_body("~" + className.lexeme, nullptr);
            // TODO: Mark destructor as virtual if class has any virtual methods
            // Currently no virtual destructor support
        } else {
            // Regular member (variable or function)
            bool is_static = match(token_type::static_keyword);
            bool is_override = match(token_type::override_keyword);

            type_info_ptr type = parse_type();

            if (check(token_type::identifier)) {
                token name = advance();

                if (match(token_type::left_paren)) {
                    // Function - we need to parse parameters and check for override before body
                    current_--; // Back up to before '('
                    
                    // Create function declaration
                    auto func = std::make_shared<function_decl>(previous().location, name.lexeme);
                    func->is_static = is_static;
                    func->is_override = is_override; // Set from earlier check

                    // Parse parameters
                    consume(token_type::left_paren, "Expected '(' after function name");
                    func->parameters = parse_parameter_list();
                    consume(token_type::right_paren, "Expected ')' after parameters");

                    // Handle trailing return type
                    if (match(token_type::arrow)) {
                        if (check(token_type::left_brace)) {
                            func->return_type = nullptr; // auto return
                        } else {
                            func->return_type = parse_type();
                        }
                    } else {
                        func->return_type = type; // Use declared type
                    }

                    // Check for override keyword after parameters (alternative position)
                    if (match(token_type::override_keyword)) {
                        func->is_override = true;
                    }
                    
                    // Parse constructor initialization list if present
                    if (match(token_type::colon)) {
                        do {
                            if (match(token_type::super_keyword)) {
                                consume(token_type::left_paren, "Expected '(' after 'super'");
                                std::vector<expression_ptr> args;
                                if (!check(token_type::right_paren)) {
                                    do {
                                        args.push_back(expression());
                                    } while (match(token_type::comma));
                                }
                                consume(token_type::right_paren, "Expected ')' after super arguments");
                                func->initializers.emplace_back("super", std::move(args));
                            } else if (match(token_type::this_keyword)) {
                                consume(token_type::left_paren, "Expected '(' after 'this'");
                                std::vector<expression_ptr> args;
                                if (!check(token_type::right_paren)) {
                                    do {
                                        args.push_back(expression());
                                    } while (match(token_type::comma));
                                }
                                consume(token_type::right_paren, "Expected ')' after this arguments");
                                func->initializers.emplace_back("this", std::move(args));
                            } else {
                                error("Expected 'super' or 'this' in constructor initializer list", peek());
                            }
                        } while (match(token_type::comma));
                    }
                    
                    // Now parse the body
                    if (!match(token_type::left_brace)) {
                        error("Expected '{' before function body", peek());
                    }
                    func->body = std::dynamic_pointer_cast<block_stmt>(block_statement());
                    
                    member = func;
                } else {
                    // Variable
                    expression_ptr init = nullptr;
                    if (match(token_type::equal)) {
                        init = expression();
                    }
                    consume(token_type::semicolon, "Expected ';' after field declaration");
                    auto var_decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, symbolizer_->intern(name.lexeme), init);
                    var_decl->is_static = is_static;
                    member = var_decl;
                }
            } else {
                error("Expected member name", peek());
            }
        }
        
        classDecl->members.push_back({visibility, member});
    }
    
    consume(token_type::right_brace, "Expected '}' after class body");

    return classDecl;
}

declaration_ptr parser::namespace_declaration() {
    // Accept identifier or any keyword as namespace name
    // Supports: namespace string {}, namespace mylib {}, namespace my::nested::nspace {}
    std::vector<std::string> namespace_path;
    source_location start_loc = peek().location;

    // Parse first namespace name
    token first_name = advance();
    if (first_name.type == token_type::eof ||
        first_name.type == token_type::left_brace ||
        first_name.type == token_type::right_brace ||
        first_name.type == token_type::left_paren ||
        first_name.type == token_type::right_paren ||
        first_name.type == token_type::semicolon ||
        first_name.type == token_type::colon_colon) {
        error("Expected namespace name", first_name);
        return nullptr;
    }
    namespace_path.push_back(first_name.lexeme);

    // Check for :: (nested namespace syntax like C++17)
    while (match(token_type::colon_colon)) {
        token next_name = advance();
        if (next_name.type == token_type::eof ||
            next_name.type == token_type::left_brace ||
            next_name.type == token_type::right_brace ||
            next_name.type == token_type::left_paren ||
            next_name.type == token_type::right_paren ||
            next_name.type == token_type::semicolon ||
            next_name.type == token_type::colon_colon) {
            error("Expected namespace name after '::'", next_name);
            return nullptr;
        }
        namespace_path.push_back(next_name.lexeme);
    }

    consume(token_type::left_brace, "Expected '{' before namespace body");

    // Join the namespace path with :: to create the full namespace name
    // namespace my::nested::nspace {} becomes namespace "my::nested::nspace"
    // This is simpler than creating actual nested structures
    std::string full_namespace_name = namespace_path[0];
    for (size_t i = 1; i < namespace_path.size(); ++i) {
        full_namespace_name += "::" + namespace_path[i];
    }

    // Intern the namespace name at parse time for fast comparisons later
    auto namespace_decl_node = std::make_shared<namespace_decl>(start_loc, full_namespace_name, symbolizer_->intern(full_namespace_name));

    // Parse namespace members (functions, classes, variables)
    while (!check(token_type::right_brace) && !is_at_end()) {
        // Parse any declaration (class, function, variable)
        declaration_ptr member_decl = nullptr;

        if (match(token_type::class_keyword)) {
            member_decl = class_declaration();
            match(token_type::semicolon);  // Consume optional semicolon after class
        } else if (match(token_type::namespace_keyword)) {
            // Nested namespaces
            member_decl = namespace_declaration();
        } else {
            // Function or variable declaration
            type_info_ptr type = parse_type();

            if (!check(token_type::identifier)) {
                error("Expected member name in namespace", peek());
                synchronize();
                continue;
            }

            token member_name = advance();

            if (match(token_type::left_paren)) {
                // Function declaration
                auto func = std::make_shared<function_decl>(member_name.location, member_name.lexeme);
                func->return_type = type;

                // Parse parameters
                func->parameters = parse_parameter_list();
                consume(token_type::right_paren, "Expected ')' after parameters");

                // Check for override keyword after parameters
                bool is_override = match(token_type::override_keyword);
                func->is_override = is_override;

                // Handle trailing return type
                if (match(token_type::arrow)) {
                    if (!check(token_type::left_brace)) {
                        func->return_type = parse_type();
                    }
                }

                // Parse function body
                consume(token_type::left_brace, "Expected '{' before function body");
                func->body = std::dynamic_pointer_cast<block_stmt>(block_statement());

                member_decl = func;
            } else {
                // Variable declaration
                expression_ptr init = nullptr;
                if (match(token_type::equal)) {
                    init = expression();
                }
                consume(token_type::semicolon, "Expected ';' after variable declaration");

                auto var_decl = std::make_shared<variable_decl>(member_name.location, type, member_name.lexeme, symbolizer_->intern(member_name.lexeme), init);
                member_decl = var_decl;
            }
        }

        if (member_decl) {
            namespace_decl_node->declarations.push_back(member_decl);
        }
    }

    consume(token_type::right_brace, "Expected '}' after namespace body");

    return namespace_decl_node;
}

declaration_ptr parser::include_declaration() {
    // include "path" or include <path> or include(expr)
    
    // Check for function-style syntax: include(expr)
    if (match(token_type::left_paren)) {
        // Parse the expression inside parentheses
        expression_ptr path_expr = expression();
        consume(token_type::right_paren, "Expected ')' after include expression");
        return std::make_shared<include_decl>(previous().location, path_expr);
    }
    
    // Original literal syntax
    std::string path;
    
    if (match(token_type::string_literal)) {
        // include "path"
        path = previous().string_value;
    } else if (match(token_type::less)) {
        // include <path>
        // Read until we find >
        std::string path_buffer;
        while (!check(token_type::greater) && !is_at_end()) {
            path_buffer += peek().lexeme;
            advance();
        }
        consume(token_type::greater, "Expected '>' after include path");
        path = path_buffer;
    } else {
        error("Expected string literal, '<', or '(' after include", peek());
    }
    
    return std::make_shared<include_decl>(previous().location, path);
}

declaration_ptr parser::import_declaration() {
    // import "path" or import <path> or import(expr)
    
    // Check for function-style syntax: import(expr)
    if (match(token_type::left_paren)) {
        // Parse the expression inside parentheses
        expression_ptr path_expr = expression();
        consume(token_type::right_paren, "Expected ')' after import expression");
        return std::make_shared<import_decl>(previous().location, path_expr);
    }
    
    // Original literal syntax
    std::string path;
    
    if (match(token_type::string_literal)) {
        // import "path"
        path = previous().string_value;
    } else if (match(token_type::less)) {
        // import <path>
        // Read until we find >
        std::string path_buffer;
        while (!check(token_type::greater) && !is_at_end()) {
            path_buffer += peek().lexeme;
            advance();
        }
        consume(token_type::greater, "Expected '>' after import path");
        path = path_buffer;
    } else {
        error("Expected string literal, '<', or '(' after import", peek());
    }
    
    return std::make_shared<import_decl>(previous().location, path);
}

declaration_ptr parser::function_declaration() {
    type_info_ptr return_type = parse_type();
    
    // Check for reference return type
    if (match(token_type::ampersand)) {
        // Create a reference type
        auto refType = std::make_shared<type_info>(script_value_type::jai_reference_type);
        refType->type_name = return_type ? (return_type->type_name + "&") : "auto&";
        refType->type_params.push_back(return_type);
        return_type = refType;
    }
    
    token name = consume(token_type::identifier, "Expected function name");
    return parse_function_body(name.lexeme, return_type);
}

declaration_ptr parser::parse_function_body(const std::string& name, type_info_ptr return_type) {
    auto func = std::make_shared<function_decl>(previous().location, name);
    
    consume(token_type::left_paren, "Expected '(' after function name");
    func->parameters = parse_parameter_list();
    consume(token_type::right_paren, "Expected ')' after parameters");
    
    // Handle trailing return type
    if (match(token_type::arrow)) {
        // Check if return type is specified or if we go directly to {
        if (check(token_type::left_brace)) {
            // -> { means auto return type
            func->return_type = nullptr; // nullptr means auto
        } else {
            func->return_type = parse_type();
        }
    } else {
        // No arrow - if we have a return type from before 'function', use it
        // Otherwise default to auto (nullptr) for inference
        func->return_type = return_type;  // nullptr means auto inference
    }
    
    // Parse constructor initialization list (: super(args), : this(args))
    if (match(token_type::colon)) {
        do {
            // Parse initializer target (super or this)
            if (match(token_type::super_keyword)) {
                // Parse super(args)
                consume(token_type::left_paren, "Expected '(' after 'super'");
                std::vector<expression_ptr> args;
                if (!check(token_type::right_paren)) {
                    do {
                        args.push_back(expression());
                    } while (match(token_type::comma));
                }
                consume(token_type::right_paren, "Expected ')' after super arguments");
                func->initializers.emplace_back("super", std::move(args));
            } else if (match(token_type::this_keyword)) {
                // Parse this(args)
                consume(token_type::left_paren, "Expected '(' after 'this'");
                std::vector<expression_ptr> args;
                if (!check(token_type::right_paren)) {
                    do {
                        args.push_back(expression());
                    } while (match(token_type::comma));
                }
                consume(token_type::right_paren, "Expected ')' after this arguments");
                func->initializers.emplace_back("this", std::move(args));
            } else {
                error("Expected 'super' or 'this' in constructor initializer list", peek());
            }
        } while (match(token_type::comma));
    }
    
    if (!match(token_type::left_brace)) {
        error("Expected '{' before function body", peek());
    }
    func->body = std::dynamic_pointer_cast<block_stmt>(block_statement());
    
    return func;
}

declaration_ptr parser::variable_declaration() {
    type_info_ptr type = parse_type();
    
    // Check for reference after type (e.g., int& x or auto& x)
    bool is_reference = false;
    if (match(token_type::ampersand)) {
        is_reference = true;
        // Create a reference type
        auto refType = std::make_shared<type_info>(script_value_type::jai_reference_type);
        refType->type_name = type ? (type->type_name + "&") : "auto&";
        refType->type_params.push_back(type);
        type = refType;
    }
    
    token name = consume(token_type::identifier, "Expected variable name");
    
    expression_ptr initializer = nullptr;
    if (match(token_type::equal)) {
        initializer = expression();
    } else if (check(token_type::left_brace)) {
        // Brace initialization: Type name{args...}
        // Create a new_expr for construction
        if (type) {
            advance(); // consume '{'
            
            std::vector<expression_ptr> arguments;
            if (!check(token_type::right_brace)) {
                arguments.reserve(4);
                do {
                    arguments.push_back(expression());
                } while (match(token_type::comma));
            }
            
            consume(token_type::right_brace, "Expected '}' after constructor arguments");
            
            // Create new_expr with the variable's type
            initializer = std::make_shared<new_expr>(name.location, type, std::move(arguments));
        } else {
            // Auto type with brace initializer - parse as expression
            initializer = expression();
        }
    }
    
    consume(token_type::semicolon, "Expected ';' after variable declaration");
    
    auto decl = std::make_shared<variable_decl>(name.location, type, name.lexeme, symbolizer_->intern(name.lexeme), initializer);
    return decl;
}

void parser::consume_greater_in_generic(const std::string& message) {
    // Handle the case where >> is tokenized as RightShift when we need >
    if (check(token_type::right_shift)) {
        // We have >>, but we only want to consume one >
        token rightShift = advance();
        
        // Push back a synthetic > token for the second >
        // This allows the parser to continue as if there were two separate > tokens
        token syntheticGreater(token_type::greater, ">", rightShift.location);
        pushed_back_token_ = syntheticGreater;
        
        // We've consumed one > (implicitly), the other is pushed back
        return;
    }
    
    // Normal case: just consume a > token
    consume(token_type::greater, message);
}

bool parser::is_registered_template_type(const std::string& type_name) const {
    return registered_template_types_.find(type_name) != registered_template_types_.end();
}

statement_ptr parser::switch_statement() {
    token switchToken = previous();
    
    consume(token_type::left_paren, "Expected '(' after 'switch'");
    expression_ptr condition = expression();
    consume(token_type::right_paren, "Expected ')' after switch condition");
    
    consume(token_type::left_brace, "Expected '{' after switch condition");
    
    auto switchStmt = std::make_shared<switch_stmt>(switchToken.location, condition);
    
    // Parse cases and default
    while (!check(token_type::right_brace) && !is_at_end()) {
        if (match(token_type::case_keyword)) {
            // Parse case
            expression_ptr caseValue = expression();
            consume(token_type::colon, "Expected ':' after case value");
            
            auto caseStmt = std::make_shared<case_stmt>(previous().location, caseValue);
            
            // Set context for parsing case body
            bool oldInSwitchCase = in_switch_case_;
            in_switch_case_ = true;
            
            // Parse case body until we hit another case, default, or end of switch
            while (!check(token_type::case_keyword) && !check(token_type::default_keyword) && 
                   !check(token_type::right_brace) && !is_at_end()) {
                
                // Check for fallthrough as the last statement
                if (check(token_type::fallthrough_keyword)) {
                    auto fallthroughStmt = std::make_shared<fallthrough_stmt>(peek().location);
                    advance(); // consume 'fallthrough'
                    consume(token_type::semicolon, "Expected ';' after 'fallthrough'");
                    caseStmt->body.push_back(fallthroughStmt);
                    caseStmt->has_fallthrough = true;
                    break; // fallthrough must be the last statement in a case
                }
                
                // Check for break statement (it's allowed but redundant)
                if (check(token_type::break_keyword)) {
                    advance(); // consume 'break'
                    consume(token_type::semicolon, "Expected ';' after 'break'");
                    // Don't add break to the body since it's implicit
                    break;
                }
                
                // Parse regular statement
                caseStmt->body.push_back(statement());
            }
            
            // Restore context
            in_switch_case_ = oldInSwitchCase;
            
            switchStmt->cases.push_back(caseStmt);
            
        } else if (match(token_type::default_keyword)) {
            // Parse default
            if (switchStmt->default_case) {
                error("Multiple default labels in switch statement", previous());
            }
            
            consume(token_type::colon, "Expected ':' after 'default'");
            
            auto defaultStmt = std::make_shared<default_stmt>(previous().location);
            
            // Set context for parsing default body
            bool oldInSwitchCase = in_switch_case_;
            in_switch_case_ = true;
            
            // Parse default body
            while (!check(token_type::case_keyword) && !check(token_type::default_keyword) && 
                   !check(token_type::right_brace) && !is_at_end()) {
                
                // Check for break statement (allowed but redundant)
                if (check(token_type::break_keyword)) {
                    advance(); // consume 'break'
                    consume(token_type::semicolon, "Expected ';' after 'break'");
                    break;
                }
                
                defaultStmt->body.push_back(statement());
            }
            
            // Restore context
            in_switch_case_ = oldInSwitchCase;
            
            switchStmt->default_case = defaultStmt;
            
        } else {
            error("Expected 'case' or 'default' in switch statement", peek());
            advance(); // Try to recover
        }
    }
    
    consume(token_type::right_brace, "Expected '}' after switch body");
    
    return switchStmt;
}

} // namespace jai