#pragma once

#ifndef __JAISCRIPT_VM_CHUNK_HPP__
#define __JAISCRIPT_VM_CHUNK_HPP__

#include "opcode.hpp"
#include <jaiscript/core/value.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace jai::vm {

    struct chunk;

    // One side of a fused binary/compound operand: identifier (slot + symbol index)
    // or literal (constant index). Mirrors what op_load/op_const would resolve.
    // One level of a fused subscript chain: an identifier index or an int literal.
    struct fused_subscript_index {
        static constexpr uint8_t index_ident = 1;
        static constexpr uint8_t index_literal = 2;
        uint8_t kind = index_ident;
        uint32_t slot = k_invalid_u32;    // ident: function-frame slot (k_invalid_u32 = env)
        uint32_t symbol = k_invalid_u32;  // ident: index into chunk::symbols
        int64_t literal = 0;              // literal: the int index
    };

    struct fused_operand {
        uint32_t slot = k_invalid_u32;        // function-frame slot (k_invalid_u32 = env)
        uint32_t symbol = k_invalid_u32;      // index into chunk::symbols; invalid = literal
        uint32_t const_index = k_invalid_u32; // index into chunk::constants; invalid = identifier
        uint32_t load_flags = 0;              // load_flag_* (type-ctor names)

        // Subscript-read operand `base[i][j]...[k]` (every level transient-marked, so the
        // elision ruling applies): slot/symbol above name the BASE container; the chain
        // holds one entry per level, OUTERMOST-application order (base[chain[0]][chain[1]]...).
        // Truly N-level: array levels resolve in place, any other level (map key, borrow,
        // OOB) replays that one level through the real op_index and the walk continues.
        // Only exec_binary_fused resolves these; fused_cmp_operand and the counted-for /
        // call-site consumers never see one (compiler emits them for binary operands only).
        std::vector<fused_subscript_index> subscript_chain;
        bool is_subscript() const noexcept { return !subscript_chain.empty(); }
    };

    // Per-call-site argument metadata: symbol id per argument (UINT64_MAX = not an identifier)
    struct call_site {
        std::vector<uint64_t> arg_symbols;
        std::vector<uint32_t> arg_slots;           // caller frame slot per argument (k_invalid_u32 = env-resolved)
        // chunk::nodes index per ref-bindable lvalue argument (k_invalid_u32 = none);
        // lazily materialized - empty when no argument qualifies
        std::vector<uint32_t> arg_lvalue_nodes;
        uint32_t member_node = k_invalid_u32;      // op_call_method: member_expr node index
        uint64_t receiver_symbol = UINT64_MAX;     // op_call_method: identifier receiver symbol id
        // Monomorphic in-loop callee cache (flat-stack stage 1): keyed on the address of
        // the loaded function value's shared script_function payload; ic_pin keeps that
        // object alive so a hit can never be a recycled address (ABA) — a redefined name
        // mints a new script_function, so the site misses and revalidates (at most one
        // superseded function stays pinned per site until the refill). Never WRITTEN on
        // parallel workers (chunks are shared across worker function copies; same
        // cached_global_env_ gate as the env-lookup caches).
        // Identifier callee operand (op_probe_callee sites): resolved like op_load
        fused_operand callee;
        mutable const script_function* ic_identity = nullptr;
        mutable const script_defined_function* ic_fn = nullptr;
        mutable strong_ptr<script_function> ic_pin;
        mutable uint32_t ic_argc = 0;
    };

    struct function_proto {
        std::shared_ptr<script_defined_function> fn;
        std::shared_ptr<function_decl> decl;
        bool is_coroutine = false;
        // Coroutines only: enclosing-frame slot locals the body references (compiler-
        // derived, like closure_proto::outer_slot_plan) - snapshot at declaration
        std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
    };

    struct closure_proto {
        std::shared_ptr<lambda_expr> lambda;
        std::shared_ptr<block_stmt> body;                  // block-wrapped lambda body
        std::shared_ptr<chunk> body_chunk;
        // Compiler-derived capture plan (never reads interpreter AST patches):
        // outer-frame locals referenced by the body, as (symbol id, outer slot).
        std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
        std::vector<uint64_t> used_variables;              // for [=]/[&] default capture
        struct explicit_capture {
            uint64_t symbol_id;
            bool by_reference;
        };
        std::vector<explicit_capture> captures;
        uint8_t default_capture = 0;                       // lambda_expr::capture_default
        bool captures_this = false;
    };

    struct destructure_proto {
        std::vector<std::pair<uint64_t, size_t>> names;    // (symbol id, slot index)
    };

    struct iter_proto {
        uint64_t var_symbol = UINT64_MAX;
        size_t slot = SIZE_MAX;                            // SIZE_MAX = define through the environment
        bool is_reference = false;
    };

    // op_binary_fused: evaluates `left op right` without materializing operand loads
    // on the value stack (the vm's analog of the interpreter's AST-shape fast paths)
    struct fused_binary_proto {
        uint8_t op = 0;                        // token_type
        fused_operand left;
        fused_operand right;
    };

    // Counting-loop pattern `for (i = lit; i cmp end; i +=/-= step)` — the vm analog of
    // the interpreter's native-loop fast path. When the runtime types don't cooperate
    // the loop falls back to the generic cond/update bytecode (targets below).
    struct counted_for_proto {
        fused_operand var;                     // loop variable (identifier)
        fused_operand end;                     // identifier or int literal
        fused_operand step;                    // identifier or int literal
        uint8_t cmp = 0;                       // token_type of the comparison
        bool step_subtract = false;            // -= / -- update
        bool step_incdec = false;              // ++/-- update (overflow error names the real op)
        // Jump targets patched at compile time
        uint32_t body_ip = k_invalid_u32;
        uint32_t exit_ip = k_invalid_u32;      // past the op_cfor_pop (fast exits pop in-op)
        uint32_t generic_cond_ip = k_invalid_u32;
        uint32_t generic_update_ip = k_invalid_u32;
    };

    // Per-instruction env-lookup inline cache: valid while the same environment is in
    // effect and no environment anywhere was constructed/reset/cleared, gained a new
    // define, or changed parent (string_symbolizer::env_epoch). Only pointers into an
    // environment's own local_storage_ (stable deque) are ever stored.
    struct env_lookup_cache_entry {
        const environment* env = nullptr;
        uint64_t epoch = 0;
        script_value* ptr = nullptr;
    };

    // op_compound_fused: `target op= <rhs>` in one dispatch. Binary mode: rhs_proto indexes
    // fused_binary_protos (identifier operands in any mix — rhs cache roles 0/1 plus target
    // role 2 stay within the per-ip budget). Bare mode (rhs_proto == k_invalid_u32): rhs is
    // a single identifier or constant operand (`sum += i` / `sum += 1`).
    struct compound_fused_proto {
        uint32_t rhs_proto = 0;         // fused_binary_protos index; k_invalid_u32 = bare rhs
        fused_operand rhs;              // bare-mode operand (identifier or constant)
        uint32_t symbol = 0;            // chunk::symbols index (store target)
        uint32_t slot = k_invalid_u32;  // target frame slot (k_invalid_u32 = env)
        uint32_t kind_flags = 0;        // compound kind | compound_flag_*
    };

    struct chunk {
        std::vector<vm_instruction> code;
        // Lazily sized to 3*code.size(): role slots per ip (0=left/load, 1=fused right,
        // 2=store target) so fused superinstructions never collide
        mutable std::vector<env_lookup_cache_entry> env_lookup_cache;
        // Per instruction: the statement node in effect (raw pointer into the pinned tree)
        std::vector<const ast_node*> stmt_nodes;

        std::vector<script_value> constants;               // engine-less AST-literal templates
        // Instruction ranges [begin, end) evaluating call arguments; consulted only on the
        // C++-exception path (interpreter parity: visit_call converts arg-eval throws into
        // script unwinding at the frame making the call, so its own try/catch can fire)
        std::vector<std::pair<uint32_t, uint32_t>> call_arg_zones;
        std::vector<uint64_t> symbols;
        std::deque<std::string> messages;                  // stable storage: checked_result keeps string_views
        std::vector<std::shared_ptr<ast_node>> nodes;      // pins referenced AST nodes alive
        std::vector<call_site> call_sites;
        std::vector<function_proto> function_protos;
        std::vector<closure_proto> closure_protos;
        std::vector<destructure_proto> destructure_protos;
        std::vector<iter_proto> iter_protos;
        std::vector<fused_binary_proto> fused_binary_protos;
        std::vector<compound_fused_proto> compound_fused_protos;
        std::vector<counted_for_proto> counted_for_protos;

        // Default-argument expressions compiled per parameter (function-body chunks only)
        std::vector<std::shared_ptr<chunk>> param_default_chunks;

        std::string function_name;                         // empty = top level
        size_t local_count = 0;
        bool is_function_body = false;
        // Compile-time lazy-env gate: false = every op proven not to need the per-call
        // scope environment, so plain in-loop callees skip creating it (fail-closed default)
        bool needs_frame_env = true;

        // Pins the whole program tree for top-level chunks
        std::vector<declaration_ptr> pinned_decls;
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_CHUNK_HPP__
