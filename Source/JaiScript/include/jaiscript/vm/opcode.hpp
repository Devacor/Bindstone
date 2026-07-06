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

    // op_index a flags
    inline constexpr uint32_t index_flag_lvalue_shape = 1; // container expr was identifier/member/subscript
    inline constexpr uint32_t index_flag_lvalue_write = 2; // outermost target of an assignment

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
            case opcode::op_error: return "ERROR";
            case opcode::op_halt: return "HALT";
        }
        return "UNKNOWN";
    }

} // namespace jai::vm

#endif // __JAISCRIPT_VM_OPCODE_HPP__
