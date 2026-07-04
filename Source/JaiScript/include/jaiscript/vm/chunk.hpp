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

    // Per-call-site argument metadata: symbol id per argument (UINT64_MAX = not an identifier)
    struct call_site {
        std::vector<uint64_t> arg_symbols;
        std::vector<uint32_t> arg_slots;           // caller frame slot per argument (k_invalid_u32 = env-resolved)
        // chunk::nodes index per ref-bindable lvalue argument (k_invalid_u32 = none);
        // lazily materialized - empty when no argument qualifies
        std::vector<uint32_t> arg_lvalue_nodes;
        uint32_t member_node = k_invalid_u32;      // op_call_method: member_expr node index
        uint64_t receiver_symbol = UINT64_MAX;     // op_call_method: identifier receiver symbol id
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

    // One side of a fused binary/compound operand: identifier (slot + symbol index)
    // or literal (constant index). Mirrors what op_load/op_const would resolve.
    struct fused_operand {
        uint32_t slot = k_invalid_u32;        // function-frame slot (k_invalid_u32 = env)
        uint32_t symbol = k_invalid_u32;      // index into chunk::symbols; invalid = literal
        uint32_t const_index = k_invalid_u32; // index into chunk::constants; invalid = identifier
        uint32_t load_flags = 0;              // load_flag_* (type-ctor names)
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
        // Jump targets patched at compile time
        uint32_t body_ip = k_invalid_u32;
        uint32_t exit_ip = k_invalid_u32;      // past the op_cfor_pop (fast exits pop in-op)
        uint32_t generic_cond_ip = k_invalid_u32;
        uint32_t generic_update_ip = k_invalid_u32;
    };

    struct chunk {
        std::vector<vm_instruction> code;
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
