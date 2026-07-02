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
        uint32_t member_node = k_invalid_u32;      // op_call_method: member_expr node index
        uint64_t receiver_symbol = UINT64_MAX;     // op_call_method: identifier receiver symbol id
    };

    struct function_proto {
        std::shared_ptr<script_defined_function> fn;
        std::shared_ptr<function_decl> decl;
        bool is_coroutine = false;
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

    struct chunk {
        std::vector<vm_instruction> code;
        // Per instruction: the statement node in effect (raw pointer into the pinned tree)
        std::vector<const ast_node*> stmt_nodes;

        std::vector<script_value> constants;               // engine-less AST-literal templates
        std::vector<uint64_t> symbols;
        std::deque<std::string> messages;                  // stable storage: checked_result keeps string_views
        std::vector<std::shared_ptr<ast_node>> nodes;      // pins referenced AST nodes alive
        std::vector<call_site> call_sites;
        std::vector<function_proto> function_protos;
        std::vector<closure_proto> closure_protos;
        std::vector<destructure_proto> destructure_protos;
        std::vector<iter_proto> iter_protos;

        // Default-argument expressions compiled per parameter (function-body chunks only)
        std::vector<std::shared_ptr<chunk>> param_default_chunks;

        std::string function_name;                         // empty = top level
        size_t local_count = 0;
        bool is_function_body = false;

        // Pins the whole program tree for top-level chunks
        std::vector<declaration_ptr> pinned_decls;
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_CHUNK_HPP__
