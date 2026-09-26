#pragma once

#ifndef __JAISCRIPT_VM_VM_COMPILER_HPP__
#define __JAISCRIPT_VM_VM_COMPILER_HPP__

#include "chunk.hpp"
#include <functional>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

namespace jai {
    struct builtin_method_registries;
}

namespace jai::detail { enum class math_fn : uint8_t; }

namespace jai::vm {

    class vm_compiler {
    public:
        explicit vm_compiler(string_symbolizer* symbolizer,
                             const builtin_method_registries* builtins = nullptr)
            : symbolizer_(symbolizer), builtins_(builtins) {}

        // Typed scalar lowering follows the checker's proofs: off when type proofs are off,
        // so proofs-off runs are also typed-ops-off runs (the fuzz gate's second pass)
        void typed_lowering(bool on) { typed_lowering_ = on; }
        // the listing tool and the tests: each closed region records its canonical operations (chunk::cell_loop::ir_text)
        void ir_listing(bool on) { ir_listing_ = on; }
        // the middle end's switch (engine::ir_passes): off, no proof-based elision and no pass; the regions run their checked forms
        void ir_passes(bool on) { ir_passes_ = on; }

        std::shared_ptr<chunk> compile_program(const std::vector<declaration_ptr>& declarations);
        std::shared_ptr<chunk> compile_callable(std::string_view name,
                                                const std::vector<parameter>& parameters,
                                                const std::shared_ptr<block_stmt>& body,
                                                size_t local_count);
        // Env-only lookups; safe to invoke mid-run (class decls, field initializers)
        std::shared_ptr<chunk> compile_standalone_expression(const expression_ptr& expr);
        std::shared_ptr<chunk> compile_standalone_declaration(const declaration_ptr& decl);

    private:
        enum class region_kind : uint8_t {
            loop,        // while / C-style for: break + continue target
            range_for,   // break + continue target; iteration state popped at the exit label
            switch_ctx,  // consumes break; continue passes through (pops the [value, ft] stack pair)
            try_block,   // pass-through: op_try_pop before jumps that exit it
            catch_block, // pass-through: op_catch_end before jumps that exit it
            graft,       // an inlined body: its returns jump to the graft's join (vm_inline.cpp)
        };

        struct loop_context {
            region_kind kind = region_kind::loop;
            std::vector<size_t> break_patches;
            std::vector<size_t> continue_patches;
            size_t continue_target = k_invalid_u32;   // known target (while/range-for); patches used otherwise
            bool continue_is_loop_back = false;       // while: continue re-checks budget via loop_back
            size_t scope_depth = 0;
        };

        struct callable_context {
            // Symbols this callable itself declares (params + locals): identifiers outside
            // this set compile to env lookups, never to baked (possibly outer-frame) slots.
            std::unordered_set<uint64_t> declared;
            // reference declarations bound to a value the unit declares (a value slot, a program-level value
            // declaration, an alias already plain), slot -> symbol: their dereference is a cell, never a getter
            std::unordered_map<uint32_t, uint64_t> plain_refs;
            // slots a reference or a capture may reach (declarations and parameters the parser marked escaping, reference
            // parameters): user code run by a hoisted read may rebind them, so they never stay in place ahead of such a read
            std::unordered_set<uint32_t> escaping;
            // value slots declared int, float or bool: they hold that kind at every point, so a unary or an intrinsic over
            // one runs no user operator or conversion
            std::unordered_set<uint32_t> primitive_slots;
            bool active = false;
        };

        string_symbolizer* symbolizer_;
        const builtin_method_registries* builtins_ = nullptr;   // compile-time bi_* fill
        bool typed_lowering_ = true;
        bool ir_listing_ = false;
        bool ir_passes_ = true;
        chunk::typed_site& typed_site_at(size_t ip);
        void lower_typed_ops(chunk& c);
        bool try_lower_decl_move(const std::shared_ptr<variable_decl>& decl, const expression* init);
        bool fusable_index_expr(const expression* e);
        bool index_read_partner_shape(const binary_expr* rb);
        bool try_emit_fused_index(binary_expr* expr, uint32_t flags);
        bool compile_subscript_into_temp(const expression_ptr& e, uint8_t expect, fused_operand& out);
        uint32_t member_base_slot(const expression* e) const;   // a member read or store whose base is a frame-slot identifier: the base's slot, else invalid
        bool member_temp_operand(const expression* e) const;
        void compile_member_into_temp(const expression_ptr& e, fused_operand& out);
        bool call_temp_operand(const expression* e) const;   // a call an expression consumes as an operand: a plain-name callee, its result into a compiler temp
        bool call_partner_ok(const expression* e, const expression* call);           // a partner a hoisted call may run ahead of: a literal or a private local
        void compile_call_into_temp(const expression_ptr& e, fused_operand& out);
        bool quiet_partner(const expression* e);
        bool private_local(const expression* e);       // a frame slot no user code can rebind: a value declaration, not escaping
        bool primitive_local(const expression* e);     // a private local declared int, float or bool
        bool quiet_read(const expression* e);          // a hoisted read that cannot run user code (a private container of proven element class over a quiet index; a unary or an intrinsic over a quiet operand)
        bool quiet_index(const expression* e);         // a literal, a private local or a quiet read (never a binary: a custom numeric operator is user code)
        bool observing_hoist_in(const expression* e);  // a subtree holding a hoisted read that may run user code
        bool env_reference_candidate(const expression* e);   // an environment name this unit does not declare as a value: its read may run a getter (a reference to a property) or raise
        bool hoist_in(const expression* e);            // a subtree holding a hoisted read, intrinsic or unary, quiet or not
        // a call to a one-argument math:: intrinsic whose result a compiler temp can carry (none = not one)
        detail::math_fn math_temp_operand(const expression* e) const;
        void compile_math_into_temp(const expression_ptr& e, fused_operand& out);
        bool unary_temp_operand(const expression* e);
        void compile_unary_into_temp(const expression_ptr& e, fused_operand& out);
        bool env_identifier(const expression* e);
        bool value_temp_operand(const expression* e);
        bool compile_value_into_temp(const expression_ptr& e, uint8_t expect, fused_operand& out);
        // the register-file lift's classification (compile_binary), shared with the fused index emitter
        bool lift_operand_shape(const expression* e);
        bool lift_temp_only(const expression* e);
        bool lift_partners_ok(const binary_expr* b);
        int lift_fusible_nodes(const expression* e);
        bool binary_lands_fused(binary_expr* b);
        bool reference_slot_operand(const expression* e);
        void materialize_operand_temp(const expression_ptr& e, fused_operand& out);
        void guard_container_temp(const expression_ptr& e, fused_operand& container, uint32_t& guard, bool always);
        static uint8_t operand_expect(const binary_expr* node);
        bool try_lower_store_move(identifier_expr* ident, const expression_ptr& value, uint32_t flags);
        chunk* chunk_ = nullptr;
        std::vector<loop_context> loops_;
        callable_context callable_;
        size_t scope_depth_ = 0;
        const ast_node* current_stmt_ = nullptr;
        // Register-file temps: expression temporaries are frame slots above the parser's
        // local_count (LIFO within one expression tree); compile_callable raises the
        // chunk's local_count to the watermark so the window covers them. Only the main
        // function-body chunk allocates (default-arg/standalone chunks share or lack the
        // callee window — chunk_->is_function_body is the gate).
        size_t temp_next_ = 0;
        size_t temp_high_ = 0;
        bool temps_active_ = false;
        uint32_t temp_symbol_index_ = k_invalid_u32;   // shared "\x01tmp" symbols entry
        uint32_t temp_symbol_index();
        // Tail-position hint for `return f(args)` with clean region exits: consumed by
        // compile_call so nested arg calls never inherit it; stamped as c=1 on
        // op_call_from_scratch (a pure hint — every correctness gate lives at runtime)
        bool pending_tail_call_ = false;
        // inlined leaves (vm_inline.cpp): the callable's own body (no recursion), the graft's own compile (no inlining inside a graft or its
        // fallback) and the result slot's name
        const block_stmt* callable_body_ = nullptr;
        bool inlining_off_ = false;
        uint64_t inline_symbol_ = 0;
        struct hoist_plan { std::shared_ptr<call_expr> call; const function_decl* fn = nullptr; std::shared_ptr<identifier_expr> receiver; std::vector<std::pair<expression_ptr, script_value_type>> spills; bool in_loop = false; const char* why = nullptr; };   // why: the rule that kept the call when the plan failed
        const char* inline_eligible(const call_expr* call, const function_decl* fn) const;   // nullptr: the graft takes the call; else the rule that keeps it
        bool plan_hoist(const expression_ptr& root, bool statement_position, hoist_plan& out);
        type_info_ptr inline_type(script_value_type kind);
        std::shared_ptr<identifier_expr> inline_temp(const source_location& loc);
        expression_ptr compile_hoist(const hoist_plan& plan, const expression_ptr& root);
        expression_ptr hoist_leaf_calls(const expression_ptr& e, bool statement_position, bool& changed);
        void compile_graft_exit();
        bool try_inline_expression(const expression_ptr& e);
        bool try_inline_variable_decl(const std::shared_ptr<variable_decl>& decl);
        bool try_inline_return(const statement_ptr& stmt);
        expression_ptr hoisted_condition(const expression_ptr& cond);
        void compile_hoisted_update(const expression_ptr& update);

        size_t emit(opcode op, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
        // Fuses a trailing fused-comparison condition with its jump_if_false
        size_t emit_cond_jump_false(const expression* cond_expr);
        // Fuses a whole-RHS trailing fused-binary/load/const with its identifier compound store
        void emit_compound_store(uint32_t symbol_index, uint32_t slot, uint32_t kind_flags, const expression* rhs, uint8_t typed_class = operand_class_unknown);
        void patch_jump(size_t at, size_t target);
        uint32_t add_constant(const script_value& v);
        uint32_t add_symbol(uint64_t id);
        uint32_t add_message(std::string msg);
        uint32_t add_node(std::shared_ptr<ast_node> node);
        void emit_unsupported(const char* construct, const char* stage);

        void compile_declaration(const declaration_ptr& decl, bool top_level);
        void compile_statement(const statement_ptr& stmt);
        void compile_block(block_stmt* block);
        void compile_expression(const expression_ptr& expr, bool as_statement = false);
        // Statement-position stores/incdecs whose result is discarded compile to a
        // no-result form (no push, no op_pop). Returns false if the shape doesn't apply.
        bool compile_no_result_expression(const expression_ptr& expr);
        // Fused subscript store shared by the expression and statement (no_result) forms;
        // false = the shape doesn't fuse and the caller compiles the unfused sequence
        bool try_emit_fused_index_store(binary_expr* sub, const expression_ptr& value, bool no_result);
        bool try_emit_fused_push(const std::shared_ptr<call_expr>& call);
        // Branch-position condition: &&/|| thread straight to conditional jumps (no bool
        // materialization; each comparison pairs into fused_cmp_jump). Falsy control lands
        // on the collected jumps — the caller patches them to the else/exit target.
        void compile_branch_condition(const expression_ptr& cond, std::vector<size_t>& false_jumps);
        void compile_branch_condition_true(const expression_ptr& cond, std::vector<size_t>& true_jumps);

        void compile_identifier_load(const expression_ptr& expr);
        void compile_binary(const std::shared_ptr<binary_expr>& expr);
        void compile_unary(const std::shared_ptr<unary_expr>& expr);
        void compile_assignment(const std::shared_ptr<assignment_expr>& expr, bool as_statement);
        void compile_call(const std::shared_ptr<call_expr>& expr);
        void compile_member(const std::shared_ptr<member_expr>& expr);
        void member_tail_into_temp(const expression* e);
        bool size_temp_operand(const expression* e) const;
        void compile_new(const std::shared_ptr<new_expr>& expr);
        void compile_cast(const std::shared_ptr<cast_expr>& expr);
        void compile_lambda(const std::shared_ptr<lambda_expr>& expr);
        void compile_variable_decl(const std::shared_ptr<variable_decl>& decl);
        void compile_decl_value(const std::shared_ptr<variable_decl>& decl, const expression_ptr& init);
        void compile_ternary_store(identifier_expr* ident, assignment_expr* assign, const std::shared_ptr<ternary_expr>& t);
        void compile_store_arm(identifier_expr* ident, assignment_expr* assign, const expression_ptr& value);
        void compile_function_decl(const std::shared_ptr<function_decl>& decl);
        void compile_if(if_stmt* stmt);
        void compile_while(while_stmt* stmt);
        void compile_for(for_stmt* stmt);
        // Emits the op_cfor_prep/back counted-loop form when the for matches the
        // interpreter's counting-loop pattern; false = use the generic codegen
        bool compile_counted_for(for_stmt* stmt);
        void compile_range_for(range_for_stmt* stmt);
        void compile_try(try_stmt* stmt);
        void compile_switch(switch_stmt* stmt);
        void compile_break();
        void compile_continue();
        // Emits op_try_pop/op_catch_end/switch stack pops for every region between the
        // current point and target_region (SIZE_MAX = all regions, for return)
        void emit_region_exits(size_t target_region);
        void collect_declared_symbols(const statement_ptr& stmt, std::unordered_set<uint64_t>& out) const;
        void collect_used_identifiers(const statement_ptr& stmt, const lambda_expr* lambda,
                                      std::vector<uint64_t>& out, std::unordered_set<uint64_t>& seen) const;
        void collect_outer_slot_candidates(const statement_ptr& stmt,
                                           const std::unordered_set<uint64_t>& declared,
                                           std::vector<std::pair<uint64_t, size_t>>& out,
                                           std::unordered_set<uint64_t>& seen) const;

        uint32_t identifier_slot_operand(identifier_expr* ident) const;
        bool symbol_is_env_only(uint64_t symbol_id) const;
        bool call_args_need_probe(const call_expr& call) const;
        fused_operand make_fused_operand(const expression* e);
        // Node-owning spelling: an env-resolved plain identifier also pins its node
        // (fused_operand::ident_node) so field proofs stay live across re-stamps
        fused_operand make_fused_operand(const expression_ptr& e);
        // chunk::ident_nodes[ip] = the identifier's node when it resolves through the env
        // (fields are never slotted); slotted identifiers leave no entry
        void note_ident_node(size_t ip, const expression_ptr& ident);

        static uint32_t binary_shape(binary_expr* expr);
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_VM_COMPILER_HPP__
