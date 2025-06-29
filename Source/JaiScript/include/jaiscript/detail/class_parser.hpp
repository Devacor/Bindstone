#pragma once

#include "jaiscript/detail/parser.hpp"
#include "jaiscript/detail/ast.hpp"
#include "jaiscript/core/script_class.hpp"
#include "jaiscript/core/class_registry.hpp"

namespace jai {

// Forward declarations
struct field_decl;
struct method_decl;
struct constructor_decl;
struct destructor_decl;
struct script_super_expr;

// AST nodes for class declarations (script-specific)
struct script_class_decl : public statement {
    std::string name;
    std::string base_class_name;  // Empty if no inheritance
    std::vector<std::unique_ptr<field_decl>> fields;
    std::vector<std::unique_ptr<method_decl>> methods;
    std::vector<std::unique_ptr<constructor_decl>> constructors;
    std::unique_ptr<destructor_decl> destructor;
    access_level default_access = access_level::public_access;
    
    void accept(ast_visitor* visitor) override;
};

struct field_decl : public statement {
    std::string name;
    std::string type_name;
    std::unique_ptr<expression> default_value;
    access_level access = access_level::public_access;
    
    void accept(ast_visitor* visitor) override;
};

struct method_decl : public statement {
    std::string name;
    std::vector<std::string> parameters;
    std::string return_type;
    std::unique_ptr<block_stmt> body;
    access_level access = access_level::public_access;
    bool is_override = false;
    bool is_virtual = true;  // All script methods are virtual by default
    
    void accept(ast_visitor* visitor) override;
};

struct constructor_decl : public statement {
    std::string class_name;
    std::vector<std::string> parameters;
    std::unique_ptr<block_stmt> body;
    
    // Delegation information
    bool is_delegating = false;
    delegation_type delegation_type_val = delegation_type::none;
    std::vector<std::unique_ptr<expression>> delegation_args;
    
    void accept(ast_visitor* visitor) override;
};

struct destructor_decl : public statement {
    std::string class_name;
    std::unique_ptr<block_stmt> body;
    
    void accept(ast_visitor* visitor) override;
};

// Super expression for base class method calls
struct script_super_expr : public expression {
    std::string method_name;
    std::vector<std::unique_ptr<expression>> arguments;
    
    void accept(ast_visitor* visitor) override;
};

// Class parser extensions
class class_parser {
public:
    explicit class_parser(parser& parent_parser);
    
    // Parse class declaration
    std::unique_ptr<script_class_decl> parse_class_declaration();
    
    // Parse class members
    std::unique_ptr<field_decl> parse_field_declaration();
    std::unique_ptr<method_decl> parse_method_declaration();
    std::unique_ptr<constructor_decl> parse_constructor_declaration();
    std::unique_ptr<destructor_decl> parse_destructor_declaration();
    
    // Parse access modifiers
    access_level parse_access_level();
    
    // Parse constructor delegation
    void parse_constructor_delegation(constructor_decl* ctor);
    
    // Parse super expressions
    std::unique_ptr<script_super_expr> parse_super_expression();
    
private:
    parser& parser_;
    std::string current_class_name_;
    
    // Helper methods
    bool is_access_modifier(token_type type);
    bool is_constructor_name(const std::string& name);
    bool is_destructor_name(const std::string& name);
    void consume_access_modifier();
    std::vector<std::string> parse_parameter_list();
    std::vector<std::unique_ptr<expression>> parse_argument_list();
};

// Visitor extensions for class AST nodes
class class_visitor : public ast_visitor {
public:
    virtual void visit_script_class_decl(script_class_decl* stmt) = 0;
    virtual void visit_field_decl(field_decl* stmt) = 0;
    virtual void visit_method_decl(method_decl* stmt) = 0;
    virtual void visit_constructor_decl(constructor_decl* stmt) = 0;
    virtual void visit_destructor_decl(destructor_decl* stmt) = 0;
    virtual void visit_script_super_expr(script_super_expr* expr) = 0;
};

} // namespace jai