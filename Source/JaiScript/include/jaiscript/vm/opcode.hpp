#pragma once

#ifndef __JAISCRIPT_VM_OPCODE_HPP__
#define __JAISCRIPT_VM_OPCODE_HPP__

#include <cstdint>
#include <string_view>

namespace jai::vm {

    inline constexpr uint32_t k_invalid_u32 = 0xFFFFFFFFu;

    enum class opcode : uint8_t {
        op_const,           // a=const index: materialize constants[a] with the engine, push
        op_null,            // push null
        op_true,            // push true
        op_false,           // push false
        op_pop,             // pop and discard
        op_dup,             // duplicate top of stack
        op_to_bool,         // pop v, push bool(is_truthy(v))

        op_load,            // a=slot (k_invalid_u32 = env), b=symbol index, c=flags(load_flag_*)
        op_store,           // a=symbol index, b=slot, c=flags(store_flag_*); pops value, pushes result
        op_compound_store,  // a=symbol index, b=slot, c=(compound kind | flags); pops rhs, pushes result
        op_incdec,          // a=symbol index, b=slot, c=flags(incdec_flag_*); pushes result

        op_decl_var,        // a=node index (variable_decl), b=has_initializer, c=initializer-is-lvalue
        op_decl_ref_ident,  // a=node index (variable_decl), b=target symbol index
        op_decl_ref_value,  // a=node index (variable_decl); pops evaluated initializer
        op_destructure,     // a=proto index; pops source array

        op_binary,          // a=token_type, b=operand shape (binary_shape_*); pops rhs,lhs, pushes result
        op_binary_fused,    // a=fused_binary_proto index, b=shape; resolves operands in place, pushes result
        op_index,           // a=flags(index_flag_*); pops index,container, pushes element/reference
        op_index_assign,    // pops reference,value; assigns through, pushes value
        op_index_compound,  // a=compound kind; pops rhs,reference; computes, assigns through, pushes result
        op_unary,           // a=token_type; pops operand, pushes result
        op_array,           // a=element count; pops elements, pushes array
        op_map,             // a=entry count; pops key/value pairs, pushes map

        op_jump,            // a=target instruction index
        op_jump_if_false,   // a=target, b=condition proved bool at compile time; pops condition
        op_jump_if_true,    // a=target, b=proved bool; pops condition
        op_loop_back,       // a=target; execution-budget check on the back edge

        op_call,            // a=argc, b=call site index; pops args+callee, pushes result
        op_return,          // a=has value (pops it)
        op_implicit_return, // pops value into the program result (deref'd)

        op_scope_push,      // push a child environment
        op_scope_pop,       // pop one environment
        op_scope_pop_n,     // a=count

        op_func_decl,       // a=function proto index; defines the thunk in the current env
        op_closure,         // a=closure proto index; pushes the closure function value

        op_this,            // push 'this' from the environment (error outside methods)
        op_super,           // push 'this' for super:: access (distinct error text)
        op_from_this,       // a=1 weak_from_this / 0 shared_from_this, b=symbol index (name for errors)
        op_null_guard,      // a=target; if top derefs to null, replace top with null and jump
        op_get_member,      // a=node index (member_expr); pops object, pushes member/bound method
        op_get_static,      // a=node index (member_expr with ::); pushes static member
        op_set_member,      // a=node index; stack [value, object] -> pushes value
        op_set_static,      // a=node index; stack [value] -> pushes value
        op_member_compound, // a=node index, c=compound kind; stack [current, rhs, object] -> pushes result
        op_call_method,     // a=argc, b=call site (member_node/receiver_symbol); stack [object, args...]
        op_new,             // a=node index (new_expr), b=argc; pops args, pushes instance
        op_class_decl,      // a=node index (class_decl); defines/redefines the class
        op_namespace_decl,  // a=node index (namespace_decl)
        op_enum_decl,       // a=node index (enum_decl)

        op_throw,           // a=has value (pops it), b=node index for location; sets unwinding
        op_try_push,        // a=handler instruction index, b=catch var symbol index
        op_try_pop,         // normal try-block completion
        op_catch_end,       // normal catch-block completion
        op_case_eq,         // pops case value + switch value copy, pushes bool(v == cv); compare failure = false
        op_iter_init,       // a=iter proto index; pops container, pushes iteration state + loop-var scope
        op_iter_next,       // a=iter proto index; assigns loop var, pushes bool(has element); budget check
        op_iter_pop,        // pops iteration state + loop-var scope

        op_cfor_prep,       // a=counted_for_proto index; resolves loop state, runs first cmp, always jumps
        op_cfor_back,       // a=counted_for_proto index; steps + cmp via cached pointers, always jumps; budget check
        op_cfor_pop,        // pops the counted-for state (generic-exit / break label)

        op_yield,           // a=has value (pops it); suspends the active fiber, resume value is null
        op_include,         // a=literal path message index (k_invalid_u32 = expr), b=path-is-expr; pushes result
        op_import,          // a=literal path message index (k_invalid_u32 = expr), b=path-is-expr; pushes result

        op_fused_cmp_jump,  // a=jump-if-false target, b=fused_binary_proto index (comparison),
                            // c=condition proved bool; BINARY_FUSED+JUMP_IF_FALSE superinstruction
        op_compound_fused,  // a=compound_fused_proto index; BINARY_FUSED+COMPOUND_STORE superinstruction

        op_ref_return_bind,   // a=slot (k_invalid_u32 = env-only), b=symbol index; pushes a
                              // shared/boxed reference to the named storage (ref-return producer)
        op_ref_return_lvalue, // a=node index (lvalue expr); pushes an owner-pinned reference
                              // via detail::resolve_ref_lvalue (ref-return producer)

        op_return_ident,    // a=slot, b=symbol index, c=load flags: fused `return <ident>;`
                            // (LOAD+RETURN; resolution mirrors op_load via fused_ident_value)
        op_return_binary,   // a=token_type, b=operand shape: fused `return <a op b>;`
                            // (BINARY+RETURN; pops two, computes like op_binary, returns)

        op_probe_callee,    // a=site index, b=argc: resolve the identifier callee at the
                            // callee-first observation point (Dev ruling 2026-07-08:
                            // callee-before-args IS the language, matching C++17) into the
                            // pending-callee register stack - no value-stack materialization
        op_call_from_scratch, // a=argc, b=site index: call the pending callee (args at stack top)

        op_math,            // a=detail::math_fn id, b=argc: math:: language intrinsic; pops
                            // argc args, evaluates via the shared kernel, pushes the result

        op_index_store,     // a=index_flag_lvalue_shape?; pops index,container,value: in-place
                            // element store (array fast path skips the reference mint; other
                            // shapes replay the exact INDEX(lvalue_write)+INDEX_ASSIGN
                            // semantics); pushes the assigned value
        op_index_compound_fused, // a=compound kind, b=index_flag_lvalue_shape?; pops
                            // rhs,index,container: in-place compound element store (numeric
                            // array fast path skips mint + operator consult; other shapes
                            // replay INDEX(shape)+INDEX_COMPOUND); pushes the result

        op_parallel_for,    // a=node index (parallel_for_stmt); pops the container, runs the
                            // in-place fork-join region (detail::run_parallel_for)

        op_binary_fused_decl,  // a=fused_binary_dst_protos index: BINARY_FUSED whose result
                               // initializes a declaration directly (dest-addressed, flatstack
                               // stage 6) - scalar slot decls land via frame_slot_set with no
                               // push and no second dispatch; every other decl shape pushes and
                               // runs exec_decl_var verbatim (semantics by construction)
        op_binary_fused_store, // a=fused_binary_dst_protos index: BINARY_FUSED whose result
                               // stores to an identifier - computes, then runs op_store's exact
                               // post-pop tail (shared helper) with no push/pop round-trip
        op_binary_fused_temp,  // a=fused_binary_protos index, b=frame slot: BINARY_FUSED whose
                               // result lands RAW in a compiler temp slot (above local_count,
                               // inside the window) - the register-file wave's producer form;
                               // consumers read it back through the ordinary fused slot operand

        op_index_fused,     // a=fused_index_protos index, b=index flags: subscript read whose
                            // container+index resolve as fused operands (no LOAD dispatches);
                            // non-array/OOB shapes replay the verbatim op_index
        op_index_store_fused, // a=fused_index_protos index, b=index_flag_lvalue_shape?: a[i]=v
                            // with fused container+index (value from the stack); other shapes
                            // replay the verbatim op_index_store sequence
        op_index_fused_decl, // a=fused_index_protos index, b=index flags, c=chunk::nodes
                            // variable_decl: the subscript read lands straight in the decl
                            // (scalar slot fast path; push + op_decl_var replay otherwise)

        op_error,           // a=runtime_error_code, b=message index (k_invalid_u32 = none), c=symbol index
        op_halt,
    };

    // op_load c flags
    inline constexpr uint32_t load_flag_type_ctor = 1;   // name begins with weak_ptr</shared_ptr<

    // op_store c flags
    inline constexpr uint32_t store_flag_rhs_lvalue = 1; // RHS was identifier/member/subscript
    inline constexpr uint32_t store_flag_no_result = 2;  // statement position: run() pops the result
    inline constexpr uint32_t store_flag_ref_alias = 4;  // target names a reference decl (ref param /
                                                         // auto& decl): a cell in the slot stores THROUGH
                                                         // instead of writing the cell as the variable
    inline constexpr uint32_t store_flag_type_provable = 8; // parse-proven typed slot store
                                                         // (assignment_expr::typed_store_provable):
                                                         // exec_store may skip type enforcement after
                                                         // its runtime type-tag guard confirms

    // op_compound_store c layout: low 3 bits = kind, upper bits = flags
    inline constexpr uint32_t compound_kind_mask = 0x7;
    inline constexpr uint32_t compound_plus = 0;
    inline constexpr uint32_t compound_minus = 1;
    inline constexpr uint32_t compound_star = 2;
    inline constexpr uint32_t compound_slash = 3;
    inline constexpr uint32_t compound_percent = 4;
    inline constexpr uint32_t compound_flag_result_needed = 0x8;
    inline constexpr uint32_t compound_flag_no_result = 0x10; // statement position: nothing pushed

    // op_incdec c flags
    inline constexpr uint32_t incdec_flag_postfix = 1;
    inline constexpr uint32_t incdec_flag_increment = 2;
    inline constexpr uint32_t incdec_flag_no_result = 4;  // statement position: run() pops the result

    // op_binary b operand shapes (mirror the interpreter's AST-shape fast paths)
    inline constexpr uint32_t binary_shape_none = 0;
    inline constexpr uint32_t binary_shape_ident_ident = 1;
    inline constexpr uint32_t binary_shape_ident_literal = 2;
    inline constexpr uint32_t binary_shape_literal_ident = 3;
    inline constexpr uint32_t binary_shape_subscript = 4;   // >=1 operand is a fusable a[i] read
                                                            // (only op_binary_fused consumes it;
                                                            // binary_fast_shape ignores unknown codes)

    // op_index a flags
    inline constexpr uint32_t index_flag_lvalue_shape = 1; // container expr was identifier/member/subscript
    inline constexpr uint32_t index_flag_lvalue_write = 2; // outermost target of an assignment
    inline constexpr uint32_t index_flag_transient_read = 4; // read consumed transiently: elide the reference mint (transient_read.hpp)

    struct vm_instruction {
        opcode op;
        uint32_t a = 0;
        uint32_t b = 0;
        uint32_t c = 0;
    };

    inline std::string_view opcode_name(opcode op) {
        switch (op) {
            case opcode::op_const: return "CONST";
            case opcode::op_null: return "NULL";
            case opcode::op_true: return "TRUE";
            case opcode::op_false: return "FALSE";
            case opcode::op_pop: return "POP";
            case opcode::op_dup: return "DUP";
            case opcode::op_to_bool: return "TO_BOOL";
            case opcode::op_load: return "LOAD";
            case opcode::op_store: return "STORE";
            case opcode::op_compound_store: return "COMPOUND_STORE";
            case opcode::op_incdec: return "INCDEC";
            case opcode::op_decl_var: return "DECL_VAR";
            case opcode::op_decl_ref_ident: return "DECL_REF_IDENT";
            case opcode::op_decl_ref_value: return "DECL_REF_VALUE";
            case opcode::op_destructure: return "DESTRUCTURE";
            case opcode::op_binary: return "BINARY";
            case opcode::op_binary_fused: return "BINARY_FUSED";
            case opcode::op_index: return "INDEX";
            case opcode::op_index_assign: return "INDEX_ASSIGN";
            case opcode::op_index_compound: return "INDEX_COMPOUND";
            case opcode::op_index_store: return "INDEX_STORE";
            case opcode::op_index_compound_fused: return "INDEX_COMPOUND_FUSED";
            case opcode::op_math: return "MATH";
            case opcode::op_unary: return "UNARY";
            case opcode::op_array: return "ARRAY";
            case opcode::op_map: return "MAP";
            case opcode::op_jump: return "JUMP";
            case opcode::op_jump_if_false: return "JUMP_IF_FALSE";
            case opcode::op_jump_if_true: return "JUMP_IF_TRUE";
            case opcode::op_loop_back: return "LOOP_BACK";
            case opcode::op_call: return "CALL";
            case opcode::op_return: return "RETURN";
            case opcode::op_implicit_return: return "IMPLICIT_RETURN";
            case opcode::op_scope_push: return "SCOPE_PUSH";
            case opcode::op_scope_pop: return "SCOPE_POP";
            case opcode::op_scope_pop_n: return "SCOPE_POP_N";
            case opcode::op_func_decl: return "FUNC_DECL";
            case opcode::op_closure: return "CLOSURE";
            case opcode::op_this: return "THIS";
            case opcode::op_super: return "SUPER";
            case opcode::op_from_this: return "FROM_THIS";
            case opcode::op_null_guard: return "NULL_GUARD";
            case opcode::op_get_member: return "GET_MEMBER";
            case opcode::op_get_static: return "GET_STATIC";
            case opcode::op_set_member: return "SET_MEMBER";
            case opcode::op_set_static: return "SET_STATIC";
            case opcode::op_member_compound: return "MEMBER_COMPOUND";
            case opcode::op_call_method: return "CALL_METHOD";
            case opcode::op_new: return "NEW";
            case opcode::op_class_decl: return "CLASS_DECL";
            case opcode::op_namespace_decl: return "NAMESPACE_DECL";
            case opcode::op_enum_decl: return "ENUM_DECL";
            case opcode::op_throw: return "THROW";
            case opcode::op_try_push: return "TRY_PUSH";
            case opcode::op_try_pop: return "TRY_POP";
            case opcode::op_catch_end: return "CATCH_END";
            case opcode::op_case_eq: return "CASE_EQ";
            case opcode::op_iter_init: return "ITER_INIT";
            case opcode::op_iter_next: return "ITER_NEXT";
            case opcode::op_iter_pop: return "ITER_POP";
            case opcode::op_cfor_prep: return "CFOR_PREP";
            case opcode::op_cfor_back: return "CFOR_BACK";
            case opcode::op_cfor_pop: return "CFOR_POP";
            case opcode::op_yield: return "YIELD";
            case opcode::op_include: return "INCLUDE";
            case opcode::op_import: return "IMPORT";
            case opcode::op_fused_cmp_jump: return "FUSED_CMP_JUMP";
            case opcode::op_compound_fused: return "COMPOUND_FUSED";
            case opcode::op_ref_return_bind: return "REF_RETURN_BIND";
            case opcode::op_ref_return_lvalue: return "REF_RETURN_LVALUE";
            case opcode::op_return_ident: return "RETURN_IDENT";
            case opcode::op_return_binary: return "RETURN_BINARY";
            case opcode::op_probe_callee: return "PROBE_CALLEE";
            case opcode::op_call_from_scratch: return "CALL_FROM_SCRATCH";
            case opcode::op_error: return "ERROR";
            case opcode::op_parallel_for: return "PARALLEL_FOR";
            case opcode::op_binary_fused_decl: return "BINARY_FUSED_DECL";
            case opcode::op_index_fused: return "INDEX_FUSED";
            case opcode::op_index_store_fused: return "INDEX_STORE_FUSED";
            case opcode::op_index_fused_decl: return "INDEX_FUSED_DECL";
            case opcode::op_binary_fused_store: return "BINARY_FUSED_STORE";
            case opcode::op_binary_fused_temp: return "BINARY_FUSED_TEMP";
            case opcode::op_halt: return "HALT";
        }
        return "UNKNOWN";
    }

} // namespace jai::vm

#endif // __JAISCRIPT_VM_OPCODE_HPP__
