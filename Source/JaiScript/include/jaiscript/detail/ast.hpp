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
    struct script_defined_function;   // environment.hpp; node mint caches hold it opaquely
    class class_definition;           // class_definition.hpp; proof stamps hold it by raw pointer
    
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
        include_expr,   // include in expression position: evaluates to the file's result
        cast_expr,      // cast<T>(e) / cast<T&>(e) / cast<shared_ptr<T>>(e)

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
        parallel_for_stmt,   // parallel_for (auto& x : arr) { ... } - in-place fork-join

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
        extern_decl,      // extern [type | function] name; - a name defined outside the program
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
        // A reference parameter the body never writes through takes a temporary
        // (detail/ref_param_writes.hpp): the verdict and the definition epoch it was derived at
        mutable uint64_t temporary_epoch = 0;
        mutable bool takes_temporary = false;
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
        virtual checked_result<void> visit_cast_expr(class cast_expr* expr) = 0;

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
        virtual checked_result<void> visit_parallel_for_stmt(class parallel_for_stmt* stmt) = 0;

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
        virtual checked_result<void> visit_extern_decl(class extern_decl* decl) = 0;
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
            : expression(loc, node_type::literal_expr), value(val) {
#ifdef JAISCRIPT_DIAG_XTHREAD_RC
            value.diag_tag_single_thread();
#endif
        }    };
    
    // Parse-time type proofs (docs/type_proofs_design.md). Stamped by the static checker's proof
    // pass, never serialized (jaibite re-derives them at load), consumed behind total runtime
    // guards (detail/type_proofs.hpp). Default = no proof.
    inline constexpr uint8_t proof_none = 0;
    inline constexpr uint8_t proof_same_type = 1;
    inline constexpr uint8_t operand_class_unknown = 0, operand_class_int_int = 1, operand_class_float_float = 2,
        operand_class_int_float = 3, operand_class_float_int = 4, operand_class_string_string = 5, operand_class_bool_bool = 6,
        operand_class_object_element = 7;   // a subscript whose container the checker proved an array of class instances

    // identifier expression
    class identifier_expr : public expression {
    public:
        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t symbol_id;     // Interned symbol ID - must be set by parser
        size_t slot_index = SIZE_MAX; // Slot index for fast local access (SIZE_MAX = use environment)
        // C++ ::name spelling: resolution is GLOBAL-ENV-ONLY - locals, captures, and
        // the member scope are all skipped (the escape hatch for deliberately shadowed
        // globals). Parser never assigns a slot when set.
        bool global_qualified = false;
        // Parser resolution: false when this name statically resolves to a REFERENCE
        // declaration (ref param / auto& decl / auto& loop var). Simple stores consult it
        // when the storage holds a cell: value-decl names write the cell like the variable
        // itself (typed enforcement); ref-alias names store through unconstrained.
        bool names_value_decl = true;
        // Parser resolution: true when this name statically resolves to a VALUE declaration the unit can see (a
        // local, a parameter, a program-level declaration of this unit); false for reference declarations and
        // for names the unit does not declare. A reference bound to such a name dereferences a cell, never a getter.
        bool names_known_value = false;
        // Field proof (bare this-field read/write inside a method): the receiver's certain class
        // object, its flat field slot, the field_shape_epoch at stamp time, the declared type and
        // whether a write may bypass the accessor ladder. Raw pointers like mcache_*: script
        // class_definitions are engine-lifetime and hot reload reuses the object.
        mutable class_definition* proven_class = nullptr;
        mutable uint32_t proven_slot = UINT32_MAX;
        mutable uint64_t proven_epoch = 0;
        mutable type_info* proven_field_type = nullptr;
        mutable bool proven_field_write = false;
        mutable bool this_field = false;   // a bare name in a method the checker resolved to a field of the class chain (declared, no user accessor): the method graft's `recv.name`

        identifier_expr(const source_location& loc, std::string_view n, uint64_t sym_id)
            : expression(loc, node_type::identifier_expr), name(n), symbol_id(sym_id), slot_index(SIZE_MAX) {}
    };
    
    // Binary expression
    class binary_expr : public expression {
    public:
        expression_ptr left;
        token op;
        expression_ptr right;
        // Subscript reads (op == left_bracket) whose result is provably consumed as a
        // transient value: both backends elide the element-reference mint and push a
        // shallow copy instead (runtime-gated on operator overrides). Stamped by the
        // parser's transient_read_marker pass (detail/transient_read.hpp).
        bool transient_element_read = false;
        // Static operand kinds (operand_class_*): annotation only in v1, no consumer.
        mutable uint8_t static_operand_class = operand_class_unknown;

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
        // Simple `=` whose identifier target resolves to a slot-resident int/float value
        // decl AND whose RHS's static type provably matches it (typed-store proof pass in
        // parser.cpp): both backends may skip store-time type enforcement. Consumers
        // re-guard on the runtime type tags, so a stale/wrong stamp only costs the skip.
        bool typed_store_provable = false;
        // proof_same_type: the RHS static type IS the target's declared type (identity conversion)
        mutable uint8_t static_store_proof = proof_none;
        // a compound op on a local: the class of the target's declared kind against the value's (operand_class_*), the
        // typed row the compiler picks for the fused compound (unknown: the int row speculated; mixed: no typed row)
        mutable uint8_t static_operand_class = operand_class_unknown;
        // Postfix ++/-- on a non-identifier target desugars to this node with `+= 1`;
        // the expression result is then the OLD value (the parser sets this)
        bool postfix_old_result = false;
        // Set on every ++/-- desugar (prefix and postfix): the target must be numeric, exactly
        // like the identifier path's gate (a string element must not concatenate "1")
        bool incdec_desugar = false;

        assignment_expr(const source_location& loc, expression_ptr t, const token& o, expression_ptr v)
            : expression(loc, node_type::assignment_expr), target(t), op(o), value(v) {}    };

    // Function call expression
    class call_expr : public expression {
    public:
        expression_ptr callee;
        std::vector<expression_ptr> arguments;
        // Per-argument proof_same_type against the ONE statically resolved same-script callee
        // (identified by body); empty = unproven call
        mutable std::vector<uint8_t> static_arg_proofs;
        mutable const block_stmt* proven_callee_body = nullptr;
        // weak: the resolved declaration may belong to a program that dies before this site compiles (a dropped jaibite, an evicted parse-cache entry, a redefinition)
        mutable std::weak_ptr<const class function_decl> proven_callee_decl;
        mutable std::weak_ptr<const class function_decl> proven_method_decl;   // `recv.m(...)` on an object receiver in a frame slot: the one overload of the receiver's class the checker resolved (the inliner's method graft)
        mutable uint8_t builtin_receiver = 0;   // the checker proved the method receiver: 1 string, 2 array, 3 map (0 unproven)   // the one free script function a plain identifier callee resolves to (exact arity, a body, not a coroutine; reference parameters allowed): the inliner's identity
        std::vector<uint8_t> arg_captures;   // per argument, the capture its value needs before a later argument runs (detail::arg_capture_kind); empty = none

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

        // Monomorphic method-dispatch inline cache (interpreter obj.method() call sites).
        // Valid iff mcache_cls matches the receiver's class identity AND mcache_epoch
        // matches the class's method_epoch() (bumped on any method/access/parent change,
        // incl. hot reload, propagated to derived). mcache_dispatch/mcache_decl point
        // into storage the class pins (methods_ payload / method_overloads_ entry); every
        // replacement of either bumps the epoch first, and script class_definitions are
        // engine-lifetime (dispatcher cycle), so a matching identity is never a recycled
        // address. Raw pointers on purpose: a shared_ptr<function_decl> here would cycle
        // (method body AST -> this node -> method decl).
        mutable const void* mcache_cls = nullptr;
        mutable uint64_t mcache_epoch = 0;
        mutable const void* mcache_dispatch = nullptr;   // script_method_dispatch* into methods_ payload
        mutable void* mcache_decl = nullptr;             // function_decl* (the lone overload)
        mutable uint8_t mcache_sig[8] = {};              // expected raw storage index per arg; 0xFF = any
        mutable uint8_t mcache_argc = 0xFF;
        mutable uint8_t mcache_flags = 0;                // 1 = access check needed, 2 = member is an sp-builtin name
        // Field proof (see identifier_expr::proven_class)
        mutable class_definition* proven_class = nullptr;
        mutable uint32_t proven_slot = UINT32_MAX;
        mutable uint64_t proven_epoch = 0;
        mutable type_info* proven_field_type = nullptr;
        mutable bool proven_field_write = false;

        // math:: intrinsic resolution cache (static access `math::<fn>`): 255 = not yet
        // resolved, 254 = resolved-not-an-intrinsic, else detail::math_fn. Name-based,
        // engine-independent - safe to share across engines like the other AST caches.
        mutable uint8_t math_intrinsic_cache = 255;

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
            size_t outer_slot = SIZE_MAX;  // Enclosing-frame slot the parser resolved the name to
            expression_ptr initializer;    // an init-capture `[x = e]`: e, evaluated at creation into the lambda's own x

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

        // the lambda as a declaration for a call of the local it initializes (the checker's proofs, the inliner's leaf graft)
        mutable std::shared_ptr<class function_decl> graft_decl;
        mutable std::vector<const class function_decl*> graft_overloads;
        mutable uint8_t inferred_return_kind = 0;   // no declared return type, every return the same certain kind: 1 int, 2 float, 3 bool (the checker's)

        // Chain captures resolved by the parser for NESTED lambdas: enclosing-frame
        // locals that only a nested lambda uses (this lambda captures the slot), and
        // names a nested lambda needs this lambda's default capture to carry
        std::vector<std::pair<uint64_t, size_t>> chained_outer_slots;
        std::vector<uint64_t> chained_outer_names;

        // Shared immutable parameter storage, minted once per node (see function_decl):
        // every closure created from this lambda shares it instead of deep-copying.
        // Capture-free lambdas (closure_env = nullptr) additionally reuse the ONE
        // script_defined_function across creations under one unit (a file two units include
        // mints one per includer: docs/global_shadowing.md).
        mutable std::shared_ptr<const std::vector<parameter>> shared_params_cache;
        mutable std::shared_ptr<script_defined_function> mint_cache;

        lambda_expr(const source_location& loc) : expression(loc, node_type::lambda_expr), local_count(0) {}
    };

    // Node-cached shared parameter storage (function_decl / lambda_expr): built on the
    // first mint, shared by every later function value from the same node. Lazy fill is
    // single-threaded by construction (parallel bodies reject nested declarations).
    template<typename NodeT>
    inline const std::shared_ptr<const std::vector<parameter>>& shared_parameters_for(const NodeT* node) {
        if (!node->shared_params_cache) {
            node->shared_params_cache = std::make_shared<const std::vector<parameter>>(node->parameters);
        }
        return node->shared_params_cache;
    }

    // New expression
    class new_expr : public expression {
    public:
        type_info_ptr type;
        std::vector<expression_ptr> arguments;
        std::vector<uint8_t> arg_captures;

        new_expr(const source_location& loc, type_info_ptr t, std::vector<expression_ptr> args)
            : expression(loc, node_type::new_expr), type(t), arguments(std::move(args)) {}    };

    // include in expression position — always runs the file, evaluates to its result
    // (exactly what engine::execute returns for that source). Statement-position include
    // stays include_decl (value discarded).
    class include_expr : public expression {
    public:
        std::string path;          // literal syntax: include "file.jai" / include <file>
        expression_ptr path_expr;  // computed syntax: include(expr)

        include_expr(const source_location& loc, const std::string& p)
            : expression(loc, node_type::include_expr), path(p), path_expr(nullptr) {}

        include_expr(const source_location& loc, expression_ptr expr)
            : expression(loc, node_type::include_expr), path(), path_expr(expr) {}    };

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

    // cast<T>(e) (value copy tagged T), cast<T&>(e) (reference, by_reference), and
    // cast<shared_ptr<T>>(e) (target IS the shared_ptr<T> spelling) - detail/cast_kernel.hpp
    class cast_expr : public expression {
    public:
        type_info_ptr target;
        bool by_reference;
        expression_ptr operand;

        cast_expr(const source_location& loc, type_info_ptr t, bool by_ref, expression_ptr e)
            : expression(loc, node_type::cast_expr), target(std::move(t)), by_reference(by_ref), operand(std::move(e)) {}    };

    // Base statement node
    // [[warn_mode]] / [[strict_mode]] led the statement (docs/static_checking.md "Escape hatches").
    // Checker-only: on a block's first statement it covers the block, elsewhere the statement's
    // own subtree; both backends ignore it.
    inline constexpr uint8_t check_mode_attr_none = 0, check_mode_attr_warn = 1, check_mode_attr_strict = 2;

    class statement : public ast_node {
    public:
        uint8_t check_mode_attr = check_mode_attr_none;

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

    // parallel_for (auto& x : arr) { ... } — the in-place fork-join loop (parallel_for
    // v1; docs/parallel_design.md §1, §11 Q4 RULED). The body is parsed as a
    // ONE-PARAMETER FUNCTION BODY (own slot numbering: the loop variable is slot 0,
    // own local_count) so both backends execute it through the parallel region
    // machinery: workers bind the loop variable by reference to each owned element of
    // the evaluated container and mutate it in place. No output array, no merge.
    class parallel_for_stmt : public statement {
    public:
        parameter loop_param;            // declarator: name/type; is_reference must be true (admission errors otherwise)
        expression_ptr container;
        // The zip form `parallel_for (auto& x, auto& o : xs, outs)`: further loop variables (body
        // slots 1..m-1, declared in this order) walking further arrays of the SAME length in
        // lockstep - the emitter pattern's pre-allocated output written in place. Empty = plain.
        std::vector<parameter> extra_params;
        std::vector<expression_ptr> extra_containers;
        size_t arity() const noexcept { return 1 + extra_params.size(); }
        statement_ptr body;              // block_stmt, parsed inside the body's own function scope
        size_t local_count = 0;          // body frame size (loop variable = slot 0)
        // Locals of the frame the statement runs in that the body names (symbol, slot): the
        // barrier provisions them from that frame's slots, which no environment chain can see
        std::vector<std::pair<uint64_t, size_t>> frame_captures;
        // The lazily-synthesized body function both backends share. Its body pointer
        // keys admission + the region slot pools; hot reload reparses the file and
        // mints a fresh node, so the cache can never go stale.
        mutable std::shared_ptr<script_defined_function> body_fn_cache;
        // The worker's chunk loop for this statement (parallel_transform.cpp build_chunk_wrapper):
        // the body grafted into a counter loop (mode 1), or a call per element when the body
        // returns from inside a nested loop (mode 2). 0 = not built yet.
        mutable std::shared_ptr<class function_decl> chunk_decl_cache;
        mutable uint8_t chunk_mode = 0;

        parallel_for_stmt(const source_location& loc, parameter p, expression_ptr cont, statement_ptr b)
            : statement(loc, node_type::parallel_for_stmt), loop_param(std::move(p)),
              container(std::move(cont)), body(std::move(b)) {}
    };

    // Return statement
    class return_stmt : public statement {
    public:
        expression_ptr value;  // Can be null
        // Enclosing function declares a reference return (int& f() / -> auto&): the
        // value binds as a reference instead of evaluating to a copy. Stamped by the
        // parser's ref_escape_marker pass (innermost function, coroutines excluded).
        bool binds_reference = false;
        // the VM's graft: a return that jumps to the inlined body's join (synthesized by the compiler, never parsed or serialized)
        bool inline_exit = false;
        mutable uint8_t static_return_proof = proof_none;
        // Interpreter memo of detail::classify_return_operand (by-value return
        // materialization); derived at first visit, never serialized
        mutable uint8_t return_operand_class = 0;

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
        uint64_t catch_var_id = UINT64_MAX;    // Interned catch variable (UINT64_MAX = unnamed catch)
        size_t catch_slot_index = SIZE_MAX;    // Frame slot of the catch variable (SIZE_MAX = scope env)
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
        // Declared with the `function` keyword (`function f = expr;`). The slot itself stays
        // auto (parse_type maps `function` to auto, the same as a function-typed parameter);
        // the checker reads this to hold the slot to callables and null
        bool function_spelling = false;
        // Declared without an initializer (`T x;`, `var x;`): the parser supplies the type's default, and a same-unit re-run
        // of a program-scope declaration keeps the existing value (detail::redeclaration_keeps_value)
        bool default_initialized = false;
        // Declared `auto` (Dev ruling 2026-09-27): the unit's first proof pass types the slot by its initializer's
        // STATIC type, once - a known type keeps auto (the runtime locks to the value, which has that type), a var
        // or unknown static type makes `type` the var marker; the spelling stays auto for re-checks and .jaibite
        bool auto_spelled = false;
        bool auto_typed = false;
        // Parser escape analysis: the variable may be bound by reference, so its
        // declaration boxes the value into a cell (storage IS the cell from birth)
        bool ref_escaping = false;
        size_t slot_index = SIZE_MAX; // Slot index for fast local access (SIZE_MAX = global/class member)

        // Construction-time classification for both backends' decl fast path
        // (exec_decl_var / visit_variable_decl): a decl carrying BOTH bits stores a
        // scalar or string payload whose storage index already matches the declared type
        // straight into its slot - conversion/clone/homogeneity are provably identity there
        // (a string copies as its shared node, exactly what clone_for_assignment does).
        // 0 = always the full path (weak_ptr/shared_ptr/reference declared types).
        static constexpr uint8_t decl_fast_plain = 1 << 0;  // no weak_ptr/shared_ptr/reference init rules apply
        static constexpr uint8_t decl_fast_scalar = 1 << 1; // auto/var/int/float/bool/char/string: matching payloads need no conversion
        static constexpr uint8_t decl_fast_slot_store = decl_fast_plain | decl_fast_scalar;
        uint8_t decl_fast_flags = 0;
        mutable uint8_t static_init_proof = proof_none;
        // the VM's synthesized declarations: the typed-slot rejection spells the site the declaration stands for (a graft's
        // result converts with the callee's return text); the parser leaves the declaration site
        static constexpr uint8_t site_declaration = 0, site_parameter = 1, site_return = 2;
        uint8_t site_kind = site_declaration;
        std::string_view site_owner;

        static uint8_t classify_decl_fast(const type_info_ptr& t) {
            if (!t) {
                return decl_fast_slot_store;   // auto: infer-then-enforce, matching payloads pass untouched
            }
            switch (t->base_type) {
                case script_value_type::jai_weak_ptr_type:
                case script_value_type::jai_shared_ptr_type:
                case script_value_type::jai_reference_type:
                    return 0;
                case script_value_type::jai_any_type:
                case script_value_type::jai_int_type:
                case script_value_type::jai_float_type:
                case script_value_type::jai_bool_type:
                case script_value_type::jai_char_type:
                case script_value_type::jai_string_type:
                    return decl_fast_slot_store;
                default:
                    return decl_fast_plain;   // container/object: payload needs full enforcement
            }
        }

        // The fast path's admission, shared by every decl landing on both backends: a raw
        // int/float/bool/char/string payload (never a bound or reference storage index) lands
        // as-is when the declared type is var/auto or names that payload's type
        static bool identity_payload(size_t storage_index, const type_info* declared) noexcept {
            const script_value_type bt = declared ? declared->base_type : script_value_type::jai_any_type;
            switch (storage_index) {
                case script_value::TYPEID_INT: return bt == script_value_type::jai_any_type || bt == script_value_type::jai_int_type;
                case script_value::TYPEID_FLOAT: return bt == script_value_type::jai_any_type || bt == script_value_type::jai_float_type;
                case script_value::TYPEID_BOOL: return bt == script_value_type::jai_any_type || bt == script_value_type::jai_bool_type;
                case script_value::TYPEID_CHAR: return bt == script_value_type::jai_any_type || bt == script_value_type::jai_char_type;
                case script_value::TYPEID_STRING: return bt == script_value_type::jai_any_type || bt == script_value_type::jai_string_type;
                default: return false;
            }
        }
        // The one implicit scalar conversion a declaration performs with no user code and no
        // failure mode: a raw int into a float slot widens (the conversion table's int -> float
        // row); both fast paths land it as the widened value stamped with the declared type
        static bool widening_payload(size_t storage_index, const type_info* declared) noexcept {
            return storage_index == script_value::TYPEID_INT && declared && declared->base_type == script_value_type::jai_float_type;
        }

        variable_decl(const source_location& loc, type_info_ptr t, std::string_view n, expression_ptr init = nullptr)
            : declaration(loc, node_type::variable_decl), type(t), name(n), initializer(init), is_static(false), slot_index(SIZE_MAX), decl_fast_flags(classify_decl_fast(t)) {}

        variable_decl(const source_location& loc, type_info_ptr t, std::string_view n, uint64_t nid, expression_ptr init = nullptr)
            : declaration(loc, node_type::variable_decl), type(t), name(n), name_id(nid), initializer(init), is_static(false), slot_index(SIZE_MAX), decl_fast_flags(classify_decl_fast(t)) {}
    };
    
    // Constructor initialization entry (for : super(args), : this(args))
    struct constructor_initializer {
        std::string target; // "super" or "this"
        std::vector<expression_ptr> arguments;
        std::vector<uint8_t> arg_captures;

        constructor_initializer(const std::string& t, std::vector<expression_ptr> args)
            : target(t), arguments(std::move(args)) {}
    };

    // Function declaration
    class function_decl : public declaration, public std::enable_shared_from_this<function_decl> {
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
        // (mirrors lambda_expr::outer_slot_plan); the vm copies a completed plan or
        // derives its own at compile time when the interpreter has not built one.
        mutable std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
        mutable bool outer_slot_plan_built = false;

        // Shared immutable parameter storage, minted once per node: every function value
        // created from this declaration shares it instead of deep-copying the vector
        // (built lazily on the first mint - single-threaded; parallel bodies reject
        // nested declarations). Coroutine declarations additionally cache their owning
        // function_decl copy (the factory needs shared ownership of an AST the visitor
        // only sees by raw pointer).
        mutable std::shared_ptr<const std::vector<parameter>> shared_params_cache;
        mutable std::shared_ptr<function_decl> coroutine_mint_cache;
        // Non-coroutine declarations always mint with closure_env = nullptr, so the ONE
        // script_defined_function can be reused for every execution of this node under one
        // unit (the cached mint's unit id is the reuse condition; the coroutine copy's too)
        mutable std::shared_ptr<script_defined_function> mint_cache;
        // The unit whose code ran this declaration (methods, constructors, coroutines and namespace
        // functions run under it: docs/global_shadowing.md)
        mutable unit_stamp unit{};
        // Return-epilogue classification memo (vm method frames; classify_return_conv
        // derives it from return_type alone, engine-independent). 0 = uncomputed. Same
        // benign-idempotent write pattern as script_defined_function::backend_return_conv.
        mutable uint8_t backend_return_conv = 0;
        // a lambda's graft declaration without a declared return type: the kind its returns certainly yield (1 int, 2 float, 3 bool)
        mutable uint8_t graft_return_kind = 0;

        function_decl(const source_location& loc, std::string_view n, uint64_t id = UINT64_MAX)
            : declaration(loc, node_type::function_decl), name(n), name_id(id), is_override(false), is_static(false), is_coroutine(false), local_count(0) {}
    };

    // Class declaration
    class class_decl : public declaration {
    public:
        enum member_visibility {
            Public,
            Private,
            Protected
        };

        struct member {
            member_visibility visibility;
            declaration_ptr declaration;
        };

        std::string_view name;  // Points to symbolizer storage (permanent)
        uint64_t name_id = UINT64_MAX;  // Interned name for fast comparison (UINT64_MAX = not interned)
        std::vector<std::string_view> base_classes;  // Points to symbolizer storage (permanent)
        std::vector<member> members;
        bool nocopy = false;  // 'nocopy class': by-value copies of instances are errors (a deleted C++ copy constructor)

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

    // extern declaration: a name defined outside the program (host-registered, another engine,
    // a later dynamic definition). The checker treats it as a declared global; both backends
    // run it as a no-op and the name resolves dynamically as before.
    class extern_decl : public declaration {
    public:
        type_info_ptr type;   // null for `extern name;` (var) and the function form
        bool is_function;     // extern function name; - a callable of unknown signature
        std::string_view name;
        uint64_t name_id;

        extern_decl(const source_location& loc, type_info_ptr t, bool fn, std::string_view n, uint64_t nid)
            : declaration(loc, node_type::extern_decl), type(std::move(t)), is_function(fn), name(n), name_id(nid) {}
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
