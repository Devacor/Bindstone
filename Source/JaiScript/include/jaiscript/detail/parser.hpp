#pragma once

#include "lexer.hpp"
#include "ast.hpp"
#include <jaiscript/core/types.hpp>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_set>

namespace jai {

    class parser {
    public:
        parser(const std::vector<token>& tokens, const std::string& filename = "<script>");
        parser(const std::vector<token>& tokens, const std::unordered_set<std::string>& registeredTemplateTypes, const std::string& filename = "<script>");
        
        // Parse the entire program
        std::vector<declaration_ptr> parse();
        
        // Check if parsing had errors
        bool has_errors() const { return !errors_.empty(); }
        const std::vector<std::string>& get_errors() const { return errors_; }
        
    private:
        const std::vector<token>& tokens_;
        std::string filename_;
        size_t current_ = 0;
        std::vector<std::string> errors_;
        std::unordered_set<std::string> registered_template_types_;
        
        // token buffer for handling >> splitting in generic contexts
        std::optional<token> pushed_back_token_;
        
        // Error handling
        void error(const std::string& message, const token& token);
        void synchronize();  // Error recovery
        
        // token management
        token peek() const;
        token previous() const;
        token advance();
        bool is_at_end() const;
        bool check(token_type type) const;
        bool match(token_type type);
        bool match(std::initializer_list<token_type> types);
        token consume(token_type type, const std::string& message);
        
        // declaration parsing
        declaration_ptr declaration();
        declaration_ptr class_declaration();
        declaration_ptr function_declaration();
        declaration_ptr variable_declaration();
        
        // statement parsing
        statement_ptr statement();
        statement_ptr expression_statement();
        statement_ptr block_statement();
        statement_ptr if_statement();
        statement_ptr while_statement();
        statement_ptr for_statement();
        statement_ptr return_statement();
        statement_ptr break_statement();
        statement_ptr continue_statement();
        statement_ptr try_statement();
        statement_ptr switch_statement();
        
        // expression parsing (precedence climbing)
        expression_ptr expression();
        expression_ptr assignment();
        expression_ptr ternary();
        expression_ptr logical_or();
        expression_ptr logical_and();
        expression_ptr bitwise_or();
        expression_ptr bitwise_xor();
        expression_ptr bitwise_and();
        expression_ptr equality();
        expression_ptr relational();
        expression_ptr shift();
        expression_ptr additive();
        expression_ptr multiplicative();
        expression_ptr unary();
        expression_ptr postfix();
        expression_ptr primary();
        
        // Helper parsers
        expression_ptr finish_call(expression_ptr callee);
        expression_ptr finish_member_access(expression_ptr object, bool is_arrow);
        expression_ptr parse_map_literal();
        type_info_ptr parse_type();
        std::vector<parameter> parse_parameter_list();
        
        // Lambda parsing
        expression_ptr lambda_expression();
        std::pair<std::vector<lambda_expr::capture>, lambda_expr::capture_default> parse_capture_list();
        
        // Helper for parsing function bodies
        declaration_ptr parse_function_body(const std::string& name, type_info_ptr return_type);
        
        // Helper for parsing > in generic contexts (handles >> token splitting)
        void consume_greater_in_generic(const std::string& message);
        
        // Context tracking for context-sensitive parsing
        bool in_switch_case_ = false;  // Track if we're inside a switch case
        
        // Helper to check if a type name is registered for template parsing
        bool is_registered_template_type(const std::string& type_name) const;
    };

} // namespace jai