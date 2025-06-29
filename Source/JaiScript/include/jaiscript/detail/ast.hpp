#pragma once

#include "../core/types.hpp"
#include "../core/type_info.hpp"
#include "../core/value.hpp"
#include "lexer.hpp"
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

namespace jai {

    // Forward declarations
    class ast_node;
    class expression;
    class statement;
    class declaration;
    
    using ast_node_ptr = std::shared_ptr<ast_node>;
    using expression_ptr = std::shared_ptr<expression>;
    using statement_ptr = std::shared_ptr<statement>;
    using declaration_ptr = std::shared_ptr<declaration>;
    
    // parameter information for functions and lambdas
    struct parameter {
        type_info_ptr type;
        std::string name;
        bool is_reference = false;
        bool is_const = false;
        mutable uint64_t symbol_id = UINT64_MAX;  // Cached symbol ID for optimization
        
        parameter(type_info_ptr t, const std::string& n, bool ref = false, bool c = false)
            : type(t), name(n), is_reference(ref), is_const(c), symbol_id(UINT64_MAX) {}
    };
    
    // Visitor pattern for AST traversal
    class ast_visitor {
    public:
        virtual ~ast_visitor() = default;
        // expression visitors
        virtual void visit_literal_expr(class literal_expr* expr) = 0;
        virtual void visit_identifier_expr(class identifier_expr* expr) = 0;
        virtual void visit_binary_expr(class binary_expr* expr) = 0;
        virtual void visit_unary_expr(class unary_expr* expr) = 0;
        virtual void visit_assignment_expr(class assignment_expr* expr) = 0;
        virtual void visit_call_expr(class call_expr* expr) = 0;
        virtual void visit_member_expr(class member_expr* expr) = 0;
        virtual void visit_lambda_expr(class lambda_expr* expr) = 0;
        virtual void visit_new_expr(class new_expr* expr) = 0;
        virtual void visit_ternary_expr(class ternary_expr* expr) = 0;
        virtual void visit_array_literal_expr(class array_literal_expr* expr) = 0;
        virtual void visit_map_literal_expr(class map_literal_expr* expr) = 0;
        virtual void visit_this_expr(class this_expr* expr) = 0;
        virtual void visit_super_expr(class super_expr* expr) = 0;
        virtual void visit_throw_expr(class throw_expr* expr) = 0;
        
        // statement visitors
        virtual void visit_expression_stmt(class expression_stmt* stmt) = 0;
        virtual void visit_block_stmt(class block_stmt* stmt) = 0;
        virtual void visit_if_stmt(class if_stmt* stmt) = 0;
        virtual void visit_while_stmt(class while_stmt* stmt) = 0;
        virtual void visit_for_stmt(class for_stmt* stmt) = 0;
        virtual void visit_range_for_stmt(class range_for_stmt* stmt) = 0;
        virtual void visit_return_stmt(class return_stmt* stmt) = 0;
        virtual void visit_break_stmt(class break_stmt* stmt) = 0;
        virtual void visit_continue_stmt(class continue_stmt* stmt) = 0;
        virtual void visit_try_stmt(class try_stmt* stmt) = 0;
        
        // declaration visitors
        virtual void visit_variable_decl(class variable_decl* decl) = 0;
        virtual void visit_function_decl(class function_decl* decl) = 0;
        virtual void visit_class_decl(class class_decl* decl) = 0;
        virtual void visit_expression_decl(class expression_decl* decl) = 0;
    };
    
    // Base AST node
    class ast_node {
    public:
        source_location location;
        
        ast_node(const source_location& loc) : location(loc) {}
        virtual ~ast_node() = default;
        virtual void accept(ast_visitor* visitor) = 0;
    };
    
    // Base expression node
    class expression : public ast_node {
    public:
        type_info_ptr result_type;  // Type of the expression result
        
        expression(const source_location& loc) : ast_node(loc) {}
    };
    
    // Literal expression
    class literal_expr : public expression {
    public:
        script_value value;
        
        literal_expr(const source_location& loc, const script_value& val) 
            : expression(loc), value(val) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_literal_expr(this);
        }
    };
    
    // identifier expression
    class identifier_expr : public expression {
    public:
        std::string name;
        mutable uint64_t symbol_id = UINT64_MAX;  // Cached symbol ID, computed lazily
        
        identifier_expr(const source_location& loc, const std::string& n)
            : expression(loc), name(n) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_identifier_expr(this);
        }
    };
    
    // Binary expression
    class binary_expr : public expression {
    public:
        expression_ptr left;
        token op;
        expression_ptr right;
        
        binary_expr(const source_location& loc, expression_ptr l, const token& o, expression_ptr r)
            : expression(loc), left(l), op(o), right(r) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_binary_expr(this);
        }
    };
    
    // Unary expression
    class unary_expr : public expression {
    public:
        token op;
        expression_ptr operand;
        bool is_postfix;
        
        unary_expr(const source_location& loc, const token& o, expression_ptr expr, bool postfix = false)
            : expression(loc), op(o), operand(expr), is_postfix(postfix) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_unary_expr(this);
        }
    };
    
    // Assignment expression
    class assignment_expr : public expression {
    public:
        expression_ptr target;
        token op;
        expression_ptr value;
        
        assignment_expr(const source_location& loc, expression_ptr t, const token& o, expression_ptr v)
            : expression(loc), target(t), op(o), value(v) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_assignment_expr(this);
        }
    };
    
    // Function call expression
    class call_expr : public expression {
    public:
        expression_ptr callee;
        std::vector<expression_ptr> arguments;
        
        call_expr(const source_location& loc, expression_ptr c, std::vector<expression_ptr> args)
            : expression(loc), callee(c), arguments(std::move(args)) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_call_expr(this);
        }
    };
    
    // member access expression
    class member_expr : public expression {
    public:
        expression_ptr object;
        std::string member;
        bool is_arrow;  // true for ->, false for .
        
        member_expr(const source_location& loc, expression_ptr obj, const std::string& mem, bool arrow)
            : expression(loc), object(obj), member(mem), is_arrow(arrow) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_member_expr(this);
        }
    };
    
    // Lambda expression
    class lambda_expr : public expression {
    public:
        enum class capture_default {
            none,           // No default capture (explicit captures only)
            by_value,       // [=] - capture all by value
            by_reference    // [&] - capture all by reference
        };
        
        struct capture {
            std::string name;           // Variable name
            bool by_reference;          // True if this specific variable is captured by reference
            
            // Constructor for explicit variable captures
            capture(const std::string& n, bool by_ref) 
                : name(n), by_reference(by_ref) {}
        };
        
        capture_default default_capture = capture_default::none;
        
        std::vector<capture> captures;
        std::vector<parameter> parameters;
        type_info_ptr return_type;
        statement_ptr body;
        
        lambda_expr(const source_location& loc) : expression(loc) {}
        
        void accept(ast_visitor* visitor) override {
            visitor->visit_lambda_expr(this);
        }
    };
    
    // New expression
    class new_expr : public expression {
    public:
        type_info_ptr type;
        std::vector<expression_ptr> arguments;
        
        new_expr(const source_location& loc, type_info_ptr t, std::vector<expression_ptr> args)
            : expression(loc), type(t), arguments(std::move(args)) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_new_expr(this);
        }
    };
    
    // Ternary expression
    class ternary_expr : public expression {
    public:
        expression_ptr condition;
        expression_ptr then_expression;
        expression_ptr else_expression;
        
        ternary_expr(const source_location& loc, expression_ptr c, expression_ptr t, expression_ptr e)
            : expression(loc), condition(c), then_expression(t), else_expression(e) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_ternary_expr(this);
        }
    };
    
    // Array literal expression
    class array_literal_expr : public expression {
    public:
        std::vector<expression_ptr> elements;
        
        array_literal_expr(const source_location& loc, std::vector<expression_ptr> elems)
            : expression(loc), elements(std::move(elems)) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_array_literal_expr(this);
        }
    };
    
    // Map literal expression
    class map_literal_expr : public expression {
    public:
        std::vector<std::pair<expression_ptr, expression_ptr>> entries;
        
        map_literal_expr(const source_location& loc, std::vector<std::pair<expression_ptr, expression_ptr>> e)
            : expression(loc), entries(std::move(e)) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_map_literal_expr(this);
        }
    };
    
    // This expression
    class this_expr : public expression {
    public:
        this_expr(const source_location& loc) : expression(loc) {}
        
        void accept(ast_visitor* visitor) override {
            visitor->visit_this_expr(this);
        }
    };
    
    // Super expression
    class super_expr : public expression {
    public:
        super_expr(const source_location& loc) : expression(loc) {}
        
        void accept(ast_visitor* visitor) override {
            visitor->visit_super_expr(this);
        }
    };
    
    // Throw expression
    class throw_expr : public expression {
    public:
        expression_ptr value;  // Optional - null for re-throw
        
        throw_expr(const source_location& loc, expression_ptr val = nullptr)
            : expression(loc), value(val) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_throw_expr(this);
        }
    };
    
    // Base statement node
    class statement : public ast_node {
    public:
        statement(const source_location& loc) : ast_node(loc) {}
    };
    
    // expression statement
    class expression_stmt : public statement {
    public:
        expression_ptr expression;
        
        expression_stmt(const source_location& loc, expression_ptr expr)
            : statement(loc), expression(expr) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_expression_stmt(this);
        }
    };
    
    // Block statement
    class block_stmt : public statement {
    public:
        std::vector<declaration_ptr> declarations;
        
        block_stmt(const source_location& loc, std::vector<declaration_ptr> decls)
            : statement(loc), declarations(std::move(decls)) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_block_stmt(this);
        }
    };
    
    // If statement
    class if_stmt : public statement {
    public:
        expression_ptr condition;
        statement_ptr then_statement;
        statement_ptr else_statement;  // Can be null
        
        if_stmt(const source_location& loc, expression_ptr c, statement_ptr t, statement_ptr e = nullptr)
            : statement(loc), condition(c), then_statement(t), else_statement(e) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_if_stmt(this);
        }
    };
    
    // While statement
    class while_stmt : public statement {
    public:
        expression_ptr condition;
        statement_ptr body;
        
        while_stmt(const source_location& loc, expression_ptr c, statement_ptr b)
            : statement(loc), condition(c), body(b) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_while_stmt(this);
        }
    };
    
    // For statement
    class for_stmt : public statement {
    public:
        declaration_ptr initializer;     // Can be null
        expression_ptr condition;  // Can be null
        expression_ptr update;     // Can be null
        statement_ptr body;
        
        for_stmt(const source_location& loc, declaration_ptr i, expression_ptr c, expression_ptr u, statement_ptr b)
            : statement(loc), initializer(i), condition(c), update(u), body(b) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_for_stmt(this);
        }
    };
    
    // Range-based for statement (C++11 style)
    class range_for_stmt : public statement {
    public:
        type_info_ptr element_type;      // Type of loop variable (auto, int, etc.)
        std::string variable_name;     // Name of loop variable
        bool is_reference;             // true for auto&, false for auto
        bool is_const;                 // true for const auto&
        expression_ptr container;      // The container to iterate over
        statement_ptr body;            // Loop body
        
        range_for_stmt(const source_location& loc, type_info_ptr type, const std::string& varName, 
                     bool ref, bool constRef, expression_ptr cont, statement_ptr b)
            : statement(loc), element_type(type), variable_name(varName), 
              is_reference(ref), is_const(constRef), container(cont), body(b) {}
              
        void accept(ast_visitor* visitor) override {
            visitor->visit_range_for_stmt(this);
        }
    };
    
    // Return statement
    class return_stmt : public statement {
    public:
        expression_ptr value;  // Can be null
        
        return_stmt(const source_location& loc, expression_ptr v = nullptr)
            : statement(loc), value(v) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_return_stmt(this);
        }
    };
    
    // Break statement
    class break_stmt : public statement {
    public:
        break_stmt(const source_location& loc) : statement(loc) {}
        
        void accept(ast_visitor* visitor) override {
            visitor->visit_break_stmt(this);
        }
    };
    
    // Continue statement
    class continue_stmt : public statement {
    public:
        continue_stmt(const source_location& loc) : statement(loc) {}
        
        void accept(ast_visitor* visitor) override {
            visitor->visit_continue_stmt(this);
        }
    };
    
    // Try-catch statement
    class try_stmt : public statement {
    public:
        statement_ptr try_block;
        std::string catch_var;  // Optional - variable name for exception message
        statement_ptr catch_block;
        
        try_stmt(const source_location& loc, statement_ptr try_blk, statement_ptr catch_blk, const std::string& var = "")
            : statement(loc), try_block(try_blk), catch_var(var), catch_block(catch_blk) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_try_stmt(this);
        }
    };
    
    // Base declaration node
    class declaration : public statement {
    public:
        declaration(const source_location& loc) : statement(loc) {}
    };
    
    // Variable declaration
    class variable_decl : public declaration {
    public:
        type_info_ptr type;
        std::string name;
        expression_ptr initializer;  // Can be null
        
        variable_decl(const source_location& loc, type_info_ptr t, const std::string& n, expression_ptr init = nullptr)
            : declaration(loc), type(t), name(n), initializer(init) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_variable_decl(this);
        }
    };
    
    // Function declaration
    class function_decl : public declaration {
    public:
        std::string name;
        std::vector<parameter> parameters;
        type_info_ptr return_type;
        std::shared_ptr<block_stmt> body;
        
        function_decl(const source_location& loc, const std::string& n)
            : declaration(loc), name(n) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_function_decl(this);
        }
    };
    
    // Class declaration
    class class_decl : public declaration {
    public:
        enum member_visibility {
            Public,
            Private
        };
        
        struct member {
            member_visibility visibility;
            declaration_ptr declaration;
        };
        
        std::string name;
        std::vector<std::string> base_classes;
        std::vector<member> members;
        
        class_decl(const source_location& loc, const std::string& n)
            : declaration(loc), name(n) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_class_decl(this);
        }
    };
    
    // expression as declaration (for top-level expressions)
    class expression_decl : public declaration {
    public:
        expression_ptr expression;
        bool implicit_return;  // True if this expression should implicitly return its value
        
        expression_decl(const source_location& loc, expression_ptr expr, bool implicit_ret = false)
            : declaration(loc), expression(expr), implicit_return(implicit_ret) {}
            
        void accept(ast_visitor* visitor) override {
            visitor->visit_expression_decl(this);
        }
    };
    
    // statement as declaration (for top-level statements in scripting context)
    class statement_decl : public declaration {
    public:
        statement_ptr statement;
        
        statement_decl(const source_location& loc, statement_ptr stmt)
            : declaration(loc), statement(stmt) {}
            
        void accept(ast_visitor* visitor) override {
            // Just visit the wrapped statement
            statement->accept(visitor);
        }
    };
    
} // namespace jai