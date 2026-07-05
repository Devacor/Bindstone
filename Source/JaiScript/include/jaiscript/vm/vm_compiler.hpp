#pragma once

#ifndef __JAISCRIPT_VM_VM_COMPILER_HPP__
#define __JAISCRIPT_VM_VM_COMPILER_HPP__

#include "chunk.hpp"
#include <jaiscript/detail/string_symbolizer.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

namespace jai::vm {

    class vm_compiler {
    public:
        explicit vm_compiler(string_symbolizer* symbolizer) : symbolizer_(symbolizer) {}

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
            bool active = false;
        };

        string_symbolizer* symbolizer_;
        chunk* chunk_ = nullptr;
        std::vector<loop_context> loops_;
        callable_context callable_;
        size_t scope_depth_ = 0;
        const ast_node* current_stmt_ = nullptr;

        size_t emit(opcode op, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0);
        // Fuses a trailing fused-comparison condition with its jump_if_false
        size_t emit_cond_jump_false(const expression* cond_expr);
        // Fuses a trailing fused-binary RHS with its identifier compound store
        void emit_compound_store(uint32_t symbol_index, uint32_t slot, uint32_t kind_flags);
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

        void compile_identifier_load(identifier_expr* ident);
        void compile_binary(const std::shared_ptr<binary_expr>& expr);
        void compile_unary(const std::shared_ptr<unary_expr>& expr);
        void compile_assignment(const std::shared_ptr<assignment_expr>& expr, bool as_statement);
        void compile_call(const std::shared_ptr<call_expr>& expr);
        void compile_member(const std::shared_ptr<member_expr>& expr);
        void compile_new(const std::shared_ptr<new_expr>& expr);
        void compile_lambda(const std::shared_ptr<lambda_expr>& expr);
        void compile_variable_decl(const std::shared_ptr<variable_decl>& decl);
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
        fused_operand make_fused_operand(const expression* e);

        static uint32_t binary_shape(binary_expr* expr);
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_VM_COMPILER_HPP__
