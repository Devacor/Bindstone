#pragma once

#ifndef __JAISCRIPT_DETAIL_AST_HPP__
#define __JAISCRIPT_DETAIL_AST_HPP__

#include <jaiscript/core/types.hpp>
#include <jaiscript/core/type_info.hpp>
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
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
    
    // Forward declarations for switch statement components
    class switch_stmt;
    class case_stmt;
    class default_stmt;
    class fallthrough_stmt;
    
    using switch_stmt_ptr = std::shared_ptr<switch_stmt>;
    using case_stmt_ptr = std::shared_ptr<case_stmt>;
    using default_stmt_ptr = std::shared_ptr<default_stmt>;
    using fallthrough_stmt_ptr = std::shared_ptr<fallthrough_stmt>;
    
    // Node type enum for switch-based dispatch (faster than virtual calls)
    enum class node_type : uint8_t {
        // Expressions (0-19)
        literal_expr = 0,
        identifier_expr,
        binary_expr,
        unary_expr,
        assignment_expr,
        call_expr,
        member_expr,
        lambda_expr,
        new_expr,
        ternary_expr,
        array_literal_expr,
        map_literal_expr,
        this_expr,
        super_expr,
        throw_expr,
        yield_expr,

        // Statements (20-39)
        expression_stmt = 20,
        block_stmt,
        if_stmt,
        while_stmt,
        for_stmt,
        range_for_stmt,
        return_stmt,
        break_stmt,
        continue_stmt,
        try_stmt,
        switch_stmt,
        case_stmt,
        default_stmt,
        fallthrough_stmt,

        // Declarations (40-59)
        variable_decl = 40,
        function_decl,
        class_decl,
        namespace_decl,
        expression_decl,
        statement_decl,
        include_decl,
        import_decl,
        enum_decl,
        destructuring_decl,
    };

    // parameter information for functions and lambdas
    struct parameter {
        type_info_ptr type;
        std::string name;
        bool is_reference = false;
        bool is_const = false;
        // Parser escape analysis: the parameter may be bound by reference downstream
        // (bare-identifier ref argument / ref-decl source), so value binding boxes its
        // storage into a cell (see reference_holder cell mode)
        bool ref_escaping = false;
        mutable uint64_t symbol_id = UINT64_MAX;  // Cached symbol ID for optimization
        mutable size_t slot_index = SIZE_MAX;     // Slot index for fast local access (SIZE_MAX = not assigned)
        expression_ptr default_value;

        parameter(type_info_ptr t, const std::string& n, bool ref = false, bool c = false)
            : type(t), name(n), is_reference(ref), is_const(c), symbol_id(UINT64_MAX), slot_index(SIZE_MAX) {}
    };
    
    // Visitor pattern for AST traversal
    class ast_visitor {
    public:
        virtual ~ast_visitor() = default;
        // expression visitors
        virtual checked_result<void> visit_literal_expr(class literal_expr* expr) = 0;
        virtual checked_result<void> visit_identifier_expr(class identifier_expr* expr) = 0;
        virtual checked_result<void> visit_binary_expr(class binary_expr* expr) = 0;
        virtual checked_result<void> visit_unary_expr(class unary_expr* expr) = 0;
        virtual checked_result<void> visit_assignment_expr(class assignment_expr* expr) = 0;
        virtual checked_result<void> visit_call_expr(class call_expr* expr) = 0;
        virtual checked_result<void> visit_member_expr(class member_expr* expr) = 0;
        virtual checked_result<void> visit_lambda_expr(class lambda_expr* expr) = 0;
        virtual checked_result<void> visit_new_expr(class new_expr* expr) = 0;
        virtual checked_result<void> visit_ternary_expr(class ternary_expr* expr) = 0;
        virtual checked_result<void> visit_array_literal_expr(class array_literal_expr* expr) = 0;
        virtual checked_result<void> visit_map_literal_expr(class map_literal_expr* expr) = 0;
        virtual checked_result<void> visit_this_expr(class this_expr* expr) = 0;
        virtual checked_result<void> visit_super_expr(class super_expr* expr) = 0;
        virtual checked_result<void> visit_throw_expr(class throw_expr* expr) = 0;

        // statement visitors
        virtual checked_result<void> visit_expression_stmt(class expression_stmt* stmt) = 0;
        virtual checked_result<void> visit_block_stmt(class block_stmt* stmt) = 0;
        virtual checked_result<void> visit_if_stmt(class if_stmt* stmt) = 0;
        virtual checked_result<void> visit_while_stmt(class while_stmt* stmt) = 0;
        virtual checked_result<void> visit_for_stmt(class for_stmt* stmt) = 0;
        virtual checked_result<void> visit_range_for_stmt(class range_for_stmt* stmt) = 0;
        virtual checked_result<void> visit_return_stmt(class return_stmt* stmt) = 0;
        virtual checked_result<void> visit_break_stmt(class break_stmt* stmt) = 0;
        virtual checked_result<void> visit_continue_stmt(class continue_stmt* stmt) = 0;
        virtual checked_result<void> visit_try_stmt(class try_stmt* stmt) = 0;
        virtual checked_result<void> visit_switch_stmt(class switch_stmt* stmt) = 0;
        virtual checked_result<void> visit_case_stmt(class case_stmt* stmt) = 0;
        virtual checked_result<void> visit_default_stmt(class default_stmt* stmt) = 0;
        virtual checked_result<void> visit_fallthrough_stmt(class fallthrough_stmt* stmt) = 0;

        // declaration visitors
        virtual checked_result<void> visit_variable_decl(class variable_decl* decl) = 0;
        virtual checked_result<void> visit_function_decl(class function_decl* decl) = 0;
        virtual checked_result<void> visit_class_decl(class class_decl* decl) = 0;
        virtual checked_result<void> visit_namespace_decl(class namespace_decl* decl) = 0;
        virtual checked_result<void> visit_expression_decl(class expression_decl* decl) = 0;
        virtual checked_result<void> visit_include_decl(class include_decl* decl) = 0;
        virtual checked_result<void> visit_import_decl(class import_decl* decl) = 0;
        virtual checked_result<void> visit_enum_decl(class enum_decl* decl) = 0;
        virtual checked_result<void> visit_destructuring_decl(class destructuring_decl* decl) = 0;
    };
    
    // Base AST node
    class ast_node {
    public:
        source_location location;
        node_type type_;  // For switch-based dispatch

        ast_node(const source_location& loc, node_type t) : location(loc), type_(t) {}
        virtual ~ast_node() = default;

        // Fast type check for switch dispatch
        [[nodiscard]] node_type get_type() const noexcept { return type_; }
    };
    
    // Base expression node
    class expression : public ast_node {
    public:
        type_info_ptr result_type;  // Type of the expression result

        expression(const source_location& loc, node_type t) : ast_node(loc, t) {}
    };
    
    // Literal expression
    class literal_expr : public expression {
    public:
        script_value value;

        literal_expr(const source_location& loc, const script_value& val)
            : expression(loc, node_type::literal_expr), value(val) {}    };
    
    // identifier expression
    class identifier_expr : public expression {
    public:
        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t symbol_id;     // Interned symbol ID - must be set by parser
        size_t slot_index = SIZE_MAX; // Slot index for fast local access (SIZE_MAX = use environment)
        // Parser resolution: false when this name statically resolves to a REFERENCE
        // declaration (ref param / auto& decl / auto& loop var). Simple stores consult it
        // when the storage holds a cell: value-decl names write the cell like the variable
        // itself (typed enforcement); ref-alias names store through unconstrained.
        bool names_value_decl = true;

        identifier_expr(const source_location& loc, std::string_view n, uint64_t sym_id)
            : expression(loc, node_type::identifier_expr), name(n), symbol_id(sym_id), slot_index(SIZE_MAX) {}
    };
    
    // Binary expression
    class binary_expr : public expression {
    public:
        expression_ptr left;
        token op;
        expression_ptr right;

        binary_expr(const source_location& loc, expression_ptr l, const token& o, expression_ptr r)
            : expression(loc, node_type::binary_expr), left(l), op(o), right(r) {}
        // Returns true if this binary expression ALWAYS produces a boolean result
        // (comparison or logical operators)
        [[nodiscard]] inline bool returns_bool() const noexcept {
            switch (op.type) {
                // Comparison operators - always return bool
                case token_type::less:
                case token_type::greater:
                case token_type::less_equal:
                case token_type::greater_equal:
                case token_type::equal_equal:
                case token_type::bang_equal:
                // Logical operators - always return bool
                case token_type::ampersand_ampersand:
                case token_type::pipe_pipe:
                    return true;
                default:
                    return false;
            }
        }
    };
    
    // Unary expression
    class unary_expr : public expression {
    public:
        token op;
        expression_ptr operand;
        bool is_postfix;

        unary_expr(const source_location& loc, const token& o, expression_ptr expr, bool postfix = false)
            : expression(loc, node_type::unary_expr), op(o), operand(expr), is_postfix(postfix) {}
        // Returns true if this unary expression ALWAYS produces a boolean result
        [[nodiscard]] inline bool returns_bool() const noexcept {
            return op.type == token_type::bang;  // Logical NOT always returns bool
        }
    };

    // Assignment expression
    class assignment_expr : public expression {
    public:
        expression_ptr target;
        token op;
        expression_ptr value;

        assignment_expr(const source_location& loc, expression_ptr t, const token& o, expression_ptr v)
            : expression(loc, node_type::assignment_expr), target(t), op(o), value(v) {}    };

    // Function call expression
    class call_expr : public expression {
    public:
        expression_ptr callee;
        std::vector<expression_ptr> arguments;

        call_expr(const source_location& loc, expression_ptr c, std::vector<expression_ptr> args)
            : expression(loc, node_type::call_expr), callee(c), arguments(std::move(args)) {}    };

    // member access expression
    class member_expr : public expression {
    public:
        expression_ptr object;
        std::string_view member;  // Points to symbolizer storage (permanent)
        uint64_t member_id = UINT64_MAX;  // Cached interned ID for the member name
        mutable uint64_t getter_id = UINT64_MAX;  // Cached interned ID for "_get_" + member (computed on first use)
        bool is_arrow;  // true for ->, false for .
        bool is_static; // true for ::, false for . or ->
        bool null_safe = false;  // true for ?. null-safe member access

        member_expr(const source_location& loc, expression_ptr obj, std::string_view mem, bool arrow, bool static_access = false)
            : expression(loc, node_type::member_expr), object(obj), member(mem), is_arrow(arrow), is_static(static_access) {}

        member_expr(const source_location& loc, expression_ptr obj, std::string_view mem, uint64_t mem_id, bool arrow, bool static_access = false)
            : expression(loc, node_type::member_expr), object(obj), member(mem), member_id(mem_id), is_arrow(arrow), is_static(static_access) {}    };
    
    // Lambda expression
    class lambda_expr : public expression {
    public:
        enum class capture_default {
            none,           // No default capture (explicit captures only)
            by_value,       // [=] - capture all by value
            by_reference    // [&] - capture all by reference
        };
        
        struct capture {
            std::string_view name;      // Variable name - points to symbolizer storage (permanent)
            bool by_reference;          // True if this specific variable is captured by reference
            mutable uint64_t symbol_id = UINT64_MAX;  // Cached symbol ID for optimization

            // Constructor for explicit variable captures
            capture(std::string_view n, bool by_ref)
                : name(n), by_reference(by_ref), symbol_id(UINT64_MAX) {}
        };
        
        capture_default default_capture = capture_default::none;

        std::vector<capture> captures;
        std::vector<parameter> parameters;
        type_info_ptr return_type;
        statement_ptr body;
        size_t local_count = 0;  // Total slots needed (params + locals) for stack allocation

        // Outer-frame locals this lambda captures, as (symbol_id, outer slot) pairs.
        // Built on the FIRST closure creation, which also patches the body identifiers'
        // slot_index to SIZE_MAX — so later creations from this same AST must replay
        // this plan rather than re-scan (a re-scan would find nothing).
        mutable std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
        mutable bool outer_slot_plan_built = false;

        lambda_expr(const source_location& loc) : expression(loc, node_type::lambda_expr), local_count(0) {}
    };

    // New expression
    class new_expr : public expression {
    public:
        type_info_ptr type;
        std::vector<expression_ptr> arguments;

        new_expr(const source_location& loc, type_info_ptr t, std::vector<expression_ptr> args)
            : expression(loc, node_type::new_expr), type(t), arguments(std::move(args)) {}    };

    // Ternary expression
    class ternary_expr : public expression {
    public:
        expression_ptr condition;
        expression_ptr then_expression;
        expression_ptr else_expression;

        ternary_expr(const source_location& loc, expression_ptr c, expression_ptr t, expression_ptr e)
            : expression(loc, node_type::ternary_expr), condition(c), then_expression(t), else_expression(e) {}    };

    // Array literal expression
    class array_literal_expr : public expression {
    public:
        std::vector<expression_ptr> elements;

        array_literal_expr(const source_location& loc, std::vector<expression_ptr> elems)
            : expression(loc, node_type::array_literal_expr), elements(std::move(elems)) {}    };

    // Map literal expression
    class map_literal_expr : public expression {
    public:
        std::vector<std::pair<expression_ptr, expression_ptr>> entries;

        map_literal_expr(const source_location& loc, std::vector<std::pair<expression_ptr, expression_ptr>> e)
            : expression(loc, node_type::map_literal_expr), entries(std::move(e)) {}    };

    // This expression
    class this_expr : public expression {
    public:
        this_expr(const source_location& loc) : expression(loc, node_type::this_expr) {}    };

    // Super expression
    class super_expr : public expression {
    public:
        super_expr(const source_location& loc) : expression(loc, node_type::super_expr) {}    };

    // Throw expression
    class throw_expr : public expression {
    public:
        expression_ptr value;  // Optional - null for re-throw

        throw_expr(const source_location& loc, expression_ptr val = nullptr)
            : expression(loc, node_type::throw_expr), value(val) {}    };

    class yield_expr : public expression {
    public:
        expression_ptr value;  // Optional - null for void yield

        yield_expr(const source_location& loc, expression_ptr val = nullptr)
            : expression(loc, node_type::yield_expr), value(std::move(val)) {}    };

    // Base statement node
    class statement : public ast_node {
    public:
        statement(const source_location& loc, node_type t) : ast_node(loc, t) {}
    };
    
    // expression statement
    class expression_stmt : public statement {
    public:
        expression_ptr expression;

        expression_stmt(const source_location& loc, expression_ptr expr)
            : statement(loc, node_type::expression_stmt), expression(expr) {}    };

    // Block statement
    class block_stmt : public statement {
    public:
        std::vector<declaration_ptr> declarations;

        block_stmt(const source_location& loc, std::vector<declaration_ptr> decls)
            : statement(loc, node_type::block_stmt), declarations(std::move(decls)) {}    };

    // If statement
    class if_stmt : public statement {
    public:
        expression_ptr condition;
        statement_ptr then_statement;
        statement_ptr else_statement;  // Can be null

        if_stmt(const source_location& loc, expression_ptr c, statement_ptr t, statement_ptr e = nullptr)
            : statement(loc, node_type::if_stmt), condition(c), then_statement(t), else_statement(e) {}    };

    // While statement
    class while_stmt : public statement {
    public:
        expression_ptr condition;
        statement_ptr body;

        while_stmt(const source_location& loc, expression_ptr c, statement_ptr b)
            : statement(loc, node_type::while_stmt), condition(c), body(b) {}    };

    // For statement
    class for_stmt : public statement {
    public:
        declaration_ptr initializer;     // Can be null
        expression_ptr condition;  // Can be null
        expression_ptr update;     // Can be null
        statement_ptr body;

        for_stmt(const source_location& loc, declaration_ptr i, expression_ptr c, expression_ptr u, statement_ptr b)
            : statement(loc, node_type::for_stmt), initializer(i), condition(c), update(u), body(b) {}    };

    // Range-based for statement (C++11 style)
    class range_for_stmt : public statement {
    public:
        type_info_ptr element_type;      // Type of loop variable (auto, int, etc.)
        std::string_view variable_name;  // Name of loop variable - points to symbolizer storage (permanent)
        uint64_t variable_name_id = UINT64_MAX;  // Interned ID for fast lookup (UINT64_MAX = not interned)
        size_t variable_slot_index = SIZE_MAX;   // Slot index for fast local access (SIZE_MAX = use environment)
        bool is_reference;             // true for auto&, false for auto
        bool is_const;                 // true for const auto&
        // Parser escape analysis (value-mode loop vars): per-iteration values are stored
        // through a cell so the variable can be bound by reference
        bool var_ref_escaping = false;
        expression_ptr container;      // The container to iterate over
        statement_ptr body;            // Loop body

        range_for_stmt(const source_location& loc, type_info_ptr type, std::string_view varName,
                     bool ref, bool constRef, expression_ptr cont, statement_ptr b)
            : statement(loc, node_type::range_for_stmt), element_type(type), variable_name(varName),
              is_reference(ref), is_const(constRef), container(cont), body(b) {}

        range_for_stmt(const source_location& loc, type_info_ptr type, std::string_view varName, uint64_t varNameId,
                     bool ref, bool constRef, expression_ptr cont, statement_ptr b)
            : statement(loc, node_type::range_for_stmt), element_type(type), variable_name(varName), variable_name_id(varNameId),
              is_reference(ref), is_const(constRef), container(cont), body(b) {}    };

    // Return statement
    class return_stmt : public statement {
    public:
        expression_ptr value;  // Can be null

        return_stmt(const source_location& loc, expression_ptr v = nullptr)
            : statement(loc, node_type::return_stmt), value(v) {}    };

    // Break statement
    class break_stmt : public statement {
    public:
        break_stmt(const source_location& loc) : statement(loc, node_type::break_stmt) {}    };

    // Continue statement
    class continue_stmt : public statement {
    public:
        continue_stmt(const source_location& loc) : statement(loc, node_type::continue_stmt) {}    };

    // Try-catch statement
    class try_stmt : public statement {
    public:
        statement_ptr try_block;
        std::string catch_var;  // Optional - variable name for exception message
        statement_ptr catch_block;

        try_stmt(const source_location& loc, statement_ptr try_blk, statement_ptr catch_blk, const std::string& var = "")
            : statement(loc, node_type::try_stmt), try_block(try_blk), catch_var(var), catch_block(catch_blk) {}    };

    // Switch statement
    class switch_stmt : public statement {
    public:
        expression_ptr condition;
        std::vector<case_stmt_ptr> cases;
        default_stmt_ptr default_case;  // Can be null

        switch_stmt(const source_location& loc, expression_ptr cond)
            : statement(loc, node_type::switch_stmt), condition(cond), default_case(nullptr) {}    };

    // Case statement (part of switch)
    class case_stmt : public statement {
    public:
        expression_ptr value;  // The case value to match
        std::vector<statement_ptr> body;
        bool has_fallthrough = false;  // Set if last statement is fallthrough

        case_stmt(const source_location& loc, expression_ptr val)
            : statement(loc, node_type::case_stmt), value(val) {}    };

    // Default statement (part of switch)
    class default_stmt : public statement {
    public:
        std::vector<statement_ptr> body;

        default_stmt(const source_location& loc)
            : statement(loc, node_type::default_stmt) {}    };

    // Fallthrough statement (only valid inside switch cases)
    class fallthrough_stmt : public statement {
    public:
        fallthrough_stmt(const source_location& loc)
            : statement(loc, node_type::fallthrough_stmt) {}    };
    
    // Base declaration node
    class declaration : public statement {
    public:
        declaration(const source_location& loc, node_type t) : statement(loc, t) {}
    };
    
    // Variable declaration
    class variable_decl : public declaration {
    public:
        type_info_ptr type;
        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t name_id = UINT64_MAX;  // Interned name for fast lookup (UINT64_MAX = not interned)
        expression_ptr initializer;  // Can be null
        bool is_static = false;      // For static class members
        // Parser escape analysis: the variable may be bound by reference, so its
        // declaration boxes the value into a cell (storage IS the cell from birth)
        bool ref_escaping = false;
        size_t slot_index = SIZE_MAX; // Slot index for fast local access (SIZE_MAX = global/class member)

        variable_decl(const source_location& loc, type_info_ptr t, std::string_view n, expression_ptr init = nullptr)
            : declaration(loc, node_type::variable_decl), type(t), name(n), initializer(init), is_static(false), slot_index(SIZE_MAX) {}

        variable_decl(const source_location& loc, type_info_ptr t, std::string_view n, uint64_t nid, expression_ptr init = nullptr)
            : declaration(loc, node_type::variable_decl), type(t), name(n), name_id(nid), initializer(init), is_static(false), slot_index(SIZE_MAX) {}
    };
    
    // Constructor initialization entry (for : super(args), : this(args))
    struct constructor_initializer {
        std::string target; // "super" or "this"
        std::vector<expression_ptr> arguments;
        
        constructor_initializer(const std::string& t, std::vector<expression_ptr> args)
            : target(t), arguments(std::move(args)) {}
    };

    // Function declaration
    class function_decl : public declaration {
    public:
        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t name_id = UINT64_MAX;  // Interned ID for the function name
        std::vector<parameter> parameters;
        type_info_ptr return_type;
        std::shared_ptr<block_stmt> body;
        std::vector<constructor_initializer> initializers; // For constructor initialization lists
        bool is_override = false; // For override keyword in derived classes
        bool is_static = false;   // For static methods
        bool is_coroutine = false;
        size_t local_count = 0;   // Total slots needed (params + locals) for stack allocation

        // Enclosing-frame slot locals a nested COROUTINE body references, as
        // (symbol_id, outer slot). Interpreter-owned: built on the first declaration
        // execution, which also patches those body identifiers' slot_index to SIZE_MAX
        // (mirrors lambda_expr::outer_slot_plan); the vm derives its own compile-time
        // plan and never reads these.
        mutable std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
        mutable bool outer_slot_plan_built = false;

        function_decl(const source_location& loc, std::string_view n, uint64_t id = UINT64_MAX)
            : declaration(loc, node_type::function_decl), name(n), name_id(id), is_override(false), is_static(false), is_coroutine(false), local_count(0) {}
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

        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t name_id = UINT64_MAX;  // Interned name for fast comparison (UINT64_MAX = not interned)
        std::vector<std::string_view> base_classes;  // Points to symbolizer storage (permanent)
        std::vector<member> members;

        class_decl(const source_location& loc, std::string_view n, uint64_t nid = UINT64_MAX)
            : declaration(loc, node_type::class_decl), name(n), name_id(nid) {}    };

    // Namespace declaration
    class namespace_decl : public declaration {
    public:
        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t name_id = UINT64_MAX;  // Interned name for fast comparison (UINT64_MAX = not interned)
        std::vector<declaration_ptr> declarations;  // Can hold functions, classes, variables

        namespace_decl(const source_location& loc, std::string_view n, uint64_t nid = UINT64_MAX)
            : declaration(loc, node_type::namespace_decl), name(n), name_id(nid) {}    };

    // expression as declaration (for top-level expressions)
    class expression_decl : public declaration {
    public:
        expression_ptr expression;
        bool implicit_return;  // True if this expression should implicitly return its value

        expression_decl(const source_location& loc, expression_ptr expr, bool implicit_ret = false)
            : declaration(loc, node_type::expression_decl), expression(expr), implicit_return(implicit_ret) {}    };

    // statement as declaration (for top-level statements in scripting context)
    class statement_decl : public declaration {
    public:
        statement_ptr statement;

        statement_decl(const source_location& loc, statement_ptr stmt)
            : declaration(loc, node_type::statement_decl), statement(stmt) {}    };

    // include declaration - always parses the file
    class include_decl : public declaration {
    public:
        std::string path;  // The file path to include (for literal syntax)
        expression_ptr path_expr;  // The expression that evaluates to path (for function syntax)

        // Constructor for literal syntax: include "file.jai"
        include_decl(const source_location& loc, const std::string& p)
            : declaration(loc, node_type::include_decl), path(p), path_expr(nullptr) {}

        // Constructor for expression syntax: include(expr)
        include_decl(const source_location& loc, expression_ptr expr)
            : declaration(loc, node_type::include_decl), path(), path_expr(expr) {}    };

    // import declaration - intelligently manages file parsing
    class import_decl : public declaration {
    public:
        std::string path;  // The file path to import (for literal syntax)
        expression_ptr path_expr;  // The expression that evaluates to path (for function syntax)

        // Constructor for literal syntax: import "file.jai"
        import_decl(const source_location& loc, const std::string& p)
            : declaration(loc, node_type::import_decl), path(p), path_expr(nullptr) {}

        // Constructor for expression syntax: import(expr)
        import_decl(const source_location& loc, expression_ptr expr)
            : declaration(loc, node_type::import_decl), path(), path_expr(expr) {}    };

    // Enum declaration
    class enum_decl : public declaration {
    public:
        std::string_view name;
        uint64_t name_id;
        std::vector<std::pair<std::string_view, uint64_t>> values;  // name, symbol_id pairs

        enum_decl(const source_location& loc, std::string_view n, uint64_t nid)
            : declaration(loc, node_type::enum_decl), name(n), name_id(nid) {}
    };

    // Destructuring declaration: auto [x, y, z] = [1, 2, 3];
    class destructuring_decl : public declaration {
    public:
        std::vector<std::pair<std::string_view, uint64_t>> names;
        std::vector<size_t> slot_indices;
        // Parser escape analysis, per name (empty = no name escapes)
        std::vector<bool> ref_escaping;
        expression_ptr initializer;

        destructuring_decl(const source_location& loc, expression_ptr init)
            : declaration(loc, node_type::destructuring_decl), initializer(std::move(init)) {}
    };

    // Helper function to check if an expression is GUARANTEED to return a boolean
    // This allows skipping is_truthy() type dispatch and using unchecked_as_bool() directly
    [[nodiscard]] inline bool expression_returns_bool(const expression* expr) noexcept {
        switch (expr->get_type()) {
            case node_type::binary_expr:
                return static_cast<const binary_expr*>(expr)->returns_bool();
            case node_type::unary_expr:
                return static_cast<const unary_expr*>(expr)->returns_bool();
            case node_type::literal_expr:
                return static_cast<const literal_expr*>(expr)->value.is_bool();
            default:
                return false;
        }
    }

} // namespace jai

#endif // __JAISCRIPT_DETAIL_AST_HPP__