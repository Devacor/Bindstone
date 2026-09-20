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

namespace jai {
    class class_definition;
    struct script_method_dispatch;
}

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
        // Env-resolved plain identifier: chunk::nodes index of its identifier_expr, read live
        // for field proofs (docs/type_proofs_design.md); invalid = slotted / literal / subscript
        uint32_t ident_node = k_invalid_u32;

        // Subscript-read operand `base[i][j]...[k]` (every level transient-marked, so the
        // elision ruling applies): slot/symbol above name the BASE container; the chain
        // holds one entry per level, OUTERMOST-application order (base[chain[0]][chain[1]]...).
        // Truly N-level: array levels resolve in place, any other level (map key, borrow,
        // OOB) replays that one level through the real op_index and the walk continues.
        // Only exec_binary_fused resolves these; fused_cmp_operand and the counted-for /
        // call-site consumers never see one (compiler emits them for binary operands only).
        std::vector<fused_subscript_index> subscript_chain;
        bool is_subscript() const noexcept { return !subscript_chain.empty(); }

        // INDEX-fusion containers only: chunk::nodes index of a member_expr — the operand
        // is `base.member` where slot/symbol name the base identifier. The exec resolves
        // the member through its own member_ic row (fill = exec_get_member's exact probe)
        // or the member_access_value ladder, base→member→index order preserved.
        uint32_t member_node = k_invalid_u32;

        // A plain frame-slot identifier (or a compiler temp): the typed heads address it
        // straight off the frame, no resolution ladder (docs/typed_regions_design.md)
        bool direct = false;
    };

    // op_push_fused: `recv.push(v);` with an identifier receiver and a slotted-identifier or
    // literal value; site is the call_site the unfused sequence would have used (the replay
    // path and its argument metadata)
    // `base.member = value;` in statement position (op_set_member_value): the member node, the assignment's static store proof, the
    // base's frame slot and the value as an operand (an identifier, a literal, or the temp a computed value lands in)
    struct member_store_proto {
        uint32_t node = k_invalid_u32;
        uint32_t proof = 0;
        uint32_t base = k_invalid_u32;
        fused_operand value;
    };
    struct push_proto {
        fused_operand receiver;
        fused_operand value;              // ident / literal / one-level subscript (base ident, ident-or-literal index)
        uint32_t site = k_invalid_u32;
        // subscript value: the unfused sequence probes the member before the argument runs
        // (call_args_need_probe) and reads the element as an lvalue-shaped op_index; the
        // replay path reproduces both
        bool value_is_subscript = false;
        uint32_t value_index_flags = 0;
    };

    // Per-call-site argument metadata: symbol id per argument (UINT64_MAX = not an identifier)
    struct call_site {
        std::vector<uint64_t> arg_symbols;
        std::vector<uint32_t> arg_slots;           // caller frame slot per argument (k_invalid_u32 = env-resolved)
        std::vector<uint8_t> arg_global;           // ::name argument (ref binding = GLOBAL env only)
        // chunk::nodes index per ref-bindable lvalue argument (k_invalid_u32 = none);
        // lazily materialized - empty when no argument qualifies
        std::vector<uint32_t> arg_lvalue_nodes;
        uint32_t member_node = k_invalid_u32;      // op_call_method: member_expr node index
        uint32_t call_node = k_invalid_u32;        // chunk::nodes index of the call_expr (static_arg_proofs, read live)
        uint64_t receiver_symbol = UINT64_MAX;     // op_call_method: identifier receiver symbol id
        uint32_t receiver_slot = k_invalid_u32;    // op_call_method: caller frame slot of that receiver (k_invalid_u32 = env)
        // op_call_method whose receiver is a member-call chain rooted at an identifier
        // (`t.trim().to_upper()`): that root, resolved like receiver_symbol/receiver_slot
        // when a string mutator redirects to it (detail/string_mutator_receiver.hpp)
        uint64_t chain_root_symbol = UINT64_MAX;
        uint32_t chain_root_slot = k_invalid_u32;
        // Compile-time builtin dispatch indices (name-sorted, engine-independent — see
        // builtin_method_registries): 0xFFFF = the member is not a builtin of that
        // registry. Valid only when the owning chunk's builtin_indexed is set.
        uint16_t bi_array = 0xFFFF;
        uint16_t bi_map = 0xFFFF;
        uint16_t bi_string = 0xFFFF;
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
        // Monomorphic method-dispatch cache (flat-classes increment 1): validated on
        // (class identity, method_epoch) per class_definition::bump_method_epoch's
        // contract (derived-propagating; definitions engine-lifetime pinned, no ABA).
        // Caches the resolve ladder's outcome for one receiver class: the pre-minted
        // dispatcher value, its dispatch payload, the resolved single overload.
        // Instances can grow fields at runtime and a field shadows the method, so a
        // hit re-probes has_field; typed-parameter methods stay arg-type-sensitive
        // (pick_best_overload can reject a single overload), so a hit re-runs
        // resolve_method_overload unless mic_static proved resolution
        // arg-independent (every arg-bound param untyped). Same cached_global_env_
        // gate as the caches above.
        mutable const class_definition* mic_cd = nullptr;
        mutable uint64_t mic_epoch = 0;
        mutable script_value mic_method{std::monostate{}, nullptr};
        mutable const script_method_dispatch* mic_dispatch = nullptr;
        mutable std::shared_ptr<function_decl> mic_resolved;
        mutable bool mic_static = false;
        mutable bool mic_field_free = false;   // no field in the class chain is named like the method
        // op_probe_member memo for sites the mic never arms (multi-overload methods):
        // "the lookup succeeds for this class at this method_epoch"; same gates as mic_*
        mutable const class_definition* probe_cd = nullptr;
        mutable uint64_t probe_epoch = 0;
        mutable bool probe_field_free = false;
    };

    struct function_proto {
        std::shared_ptr<script_defined_function> fn;
        std::shared_ptr<function_decl> decl;
        bool is_coroutine = false;
        // Coroutines only: enclosing-frame slot locals the body references (derived
        // or restored, like closure_proto::outer_slot_plan) - snapshot at declaration
        std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
    };

    struct closure_proto {
        std::shared_ptr<lambda_expr> lambda;
        std::shared_ptr<block_stmt> body;                  // block-wrapped lambda body
        std::shared_ptr<chunk> body_chunk;
        // Capture plan derived by the compiler or copied from completed AST patches:
        // outer-frame locals referenced by the body, as (symbol id, outer slot).
        std::vector<std::pair<uint64_t, size_t>> outer_slot_plan;
        std::vector<uint64_t> used_variables;              // for [=]/[&] default capture
        struct explicit_capture {
            uint64_t symbol_id;
            bool by_reference;
            size_t outer_slot = SIZE_MAX;
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
        // The checker's proof-grade operand class (binary_expr::static_operand_class) when
        // both operands are direct slots or literals: 1 = int/int, 2 = float/float, 0 = none.
        // Selects the typed head; the head's tag guards keep it sound and the general
        // kernel below it runs on any miss (proofs off = 0 everywhere, byte-identical).
        uint8_t typed_class = 0;
        // an operand is a compiler temp holding an element read: an index fold over it may take the typed
        // lane without a proof (the load's guards decide, the original instruction runs on a miss)
        bool elem_read_operand = false;
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

    // Per-instruction env-lookup inline cache, two heads. FAST head {fast_env,
    // fast_serial, fast_ptr}: valid while the CURRENT env identity and the engine-wide
    // env serial (string_symbolizer::env_epoch - every env construct/reset/clear/define/
    // parent change draws from it) are both unchanged, which proves the whole chain
    // unmodified - two compares, no transparency walk, arbitrary (even shadowing) chains.
    // Armed only with env-LOCAL cells (vm_storage_lookup cacheable=true), never
    // this/static-field fallbacks: rebind_method_this deliberately doesn't advance the
    // serial. SLOW head {env, epoch, ptr}: valid while the resolve env's OWN epoch
    // (environment::local_epoch) is unchanged - survives the unrelated env churn that
    // advances the engine serial (method scopes resetting per call), so call-heavy code
    // keeps hitting when the fast head can't. Stored pointers target stable deque
    // storage in the entry's env or an ancestor (a parent cannot be pool-recycled while
    // its child chain lives; the global env is engine-lifetime with in-place
    // redefinition).
    struct env_lookup_cache_entry {
        const environment* env = nullptr;
        uint64_t epoch = 0;
        script_value* ptr = nullptr;
        const environment* fast_env = nullptr;
        uint64_t fast_serial = 0;
        script_value* fast_ptr = nullptr;
        // This-field IC (bare method-field reads): the engine-wide env serial recorded
        // when this site's FULL WALK missed and the this-fallback resolved a field.
        // While the serial holds (no define/reparent anywhere), the walk provably still
        // misses, so resolution may jump straight to the receiver's field cell. 0 = unarmed.
        // class+slot skip the per-access field_slot hash find: the id->slot mapping is
        // append-only for a class object's lifetime (ensure_field_slots only emplaces),
        // so a matching class pointer makes the slot valid forever; a different class
        // (polymorphic site) or no slot (overflow field) takes the generic find.
        uint64_t this_field_serial = 0;
        const void* this_field_class = nullptr;
        uint64_t this_field_sym = 0;       // the symbol that armed the proof: cache roles are per ip, not per symbol (invariants.md 9)
        uint32_t this_field_slot = 0xFFFFFFFFu;
        // Store twin (bare method-field WRITES): the same serial + class proof names the
        // cell; the arm banks what the store landing needs on top - no C++ property
        // setter for the id, the declared type (null = auto: infer from the cell's tag) -
        // under the class's method_epoch (declared types are replaced wholesale on reload)
        uint8_t this_store_state = 0;   // 0 unarmed, 1 armed
        uint64_t this_store_epoch = 0;
        type_info* this_store_type = nullptr;
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
        uint8_t typed_class = 0;        // the checker's class of target and value (assignment_expr::static_operand_class): the typed row
    };

    // op_binary_fused_decl / op_binary_fused_store: a fused binary whose result lands in
    // a compile-time-known sink with no push and no second dispatch (dest-addressed,
    // flatstack stage 6). decl mode uses node_index (the variable_decl); store mode uses
    // symbol/slot/store flags exactly like op_store's instruction fields.
    struct fused_binary_dst_proto {
        uint32_t binary_proto = 0;      // fused_binary_protos index (the computation)
        uint32_t node_index = k_invalid_u32;  // decl mode: chunk::nodes index of the variable_decl
        uint32_t symbol = 0;            // store mode: chunk::symbols index
        uint32_t slot = k_invalid_u32;  // store mode: target frame slot (k_invalid_u32 = env)
        uint32_t flags = 0;             // store mode: store_flag_*; decl mode: b/c of op_decl_var
    };

    // op_index_fused / op_index_store_fused: subscript read/store whose container and
    // index resolve as fused operands (identifier slots/env or literals) instead of two
    // LOAD dispatches + pushes. The in-bounds array+int shape runs inline; every other
    // shape (map/OOB/borrow/custom-[]) pushes the resolved operands and replays the
    // verbatim unfused op, so semantics and error text stay byte-identical.
    struct fused_index_proto {
        fused_operand container;
        fused_operand index;
        // Computed flat-binary index (plain-ident containers only): fused_binary_protos
        // entry evaluated in-op through binary_fused_compute (the ONE fused-binary body).
        // Cache roles at these sites: binary left/right own ip*3/ip*3+1, container parks
        // at ip*3+2 (entries are provenance-checked by {env, epoch} but not symbol).
        uint32_t index_binary = k_invalid_u32;
        // Left-spine arithmetic chain continuing index_binary: ((step0) op1 r1) op2 r2 …
        // fused_binary_protos entries whose LEFT is EMPTY (the accumulator stands in;
        // no resolver may touch it) — only op + right are read. Arithmetic ops only,
        // rights ident/literal. All-int chains fold to a raw index in-op; any other
        // shape replays the verbatim unfused stack sequence from the accumulator.
        std::vector<uint32_t> index_chain_ext;
        // Store-value operand (op_index_store_fused, STATEMENT position only — the
        // instruction's c=1 elides the result push): ident/literal RHS resolves in-op
        // instead of LOAD+push. Absent (all-invalid) = the value rides the stack.
        fused_operand store_value;
        // op_index_fused_decl lane facts, proven at the peephole (decl_fast_slot_store,
        // not ref-escaping, slot-resident): the read lands DIRECTLY in decl_slot when
        // the runtime value's raw index matches decl_expect (0xFF = any scalar) —
        // no per-exec decl checks, no landing round-trip. k_invalid_u32 = general
        // landing. decl_type = the decl's type tag to stamp (raw ptr; the chunk pins
        // the decl node which pins its type), null = no stamp.
        uint32_t decl_slot = k_invalid_u32;
        uint8_t decl_expect = 0xFF;
        type_info* decl_type = nullptr;
        // decl_slot is a compiler temp (no declaration node): the read lands there for the consuming
        // op (a binary operand, a store value, an outer index); a non-scalar element lands boxed
        bool decl_temp = false;
        // Typed element lanes (docs/typed_regions_design.md): the compiler proved the shape -
        // a plain identifier container, an index that is a direct slot, a literal or an
        // int/int-stamped fold whose chain steps are int/int with direct or literal rights.
        // A decl proto with the lane facts above opens exec_index_fused_decl with
        // typed_decl_read; a store proto opens exec_index_store_fused with typed_index_store;
        // any runtime miss runs the general kernel unchanged.
        bool typed_lane = false;
        bool speculative = false;   // the lane came from elem_read_operand, not a proof (census: misses per site)
        // a reference-slot container guarded ahead of the index (op_operand_temp c = 1): the temp that holds its
        // materialized value when the slot's reference is not a plain cell; the readers take the slot otherwise
        uint32_t container_guard = k_invalid_u32;
        // the guard's always form (op_operand_temp c = 2): the container's value is taken ahead of an index that may run user
        // code (a custom numeric operator, a getter, an operator[]), whatever the slot holds; the readers take the temp every time
        bool guard_always = false;
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
        std::vector<uint32_t> reference_slots;   // slots declared as references (a by-reference parameter, a local bound to something other than a value this unit declares): their load dereferences, possibly through a getter
        std::vector<uint64_t> symbols;
        std::deque<std::string> messages;                  // stable storage: checked_result keeps string_views
        std::vector<std::shared_ptr<ast_node>> nodes;      // pins referenced AST nodes alive
        std::vector<std::unique_ptr<type_info>> owned_types;   // the inliner's spill temps' primitive types (the compiler has no engine to intern one)
        // Per ip (lazily sized, never longer than code): nodes index of the env-resolved
        // identifier_expr behind an op_load / op_store / op_binary_fused_store /
        // op_return_ident, read live for field proofs; k_invalid_u32 = none
        std::vector<uint32_t> ident_nodes;
        std::vector<call_site> call_sites;
        std::vector<function_proto> function_protos;
        std::vector<closure_proto> closure_protos;
        std::vector<destructure_proto> destructure_protos;
        std::vector<iter_proto> iter_protos;
        // Closed typed loops as cell blocks: the loop's scalar slots as raw cells. The copy at [copy_head,
        // copy_back] is the cell form; the original instructions at [head, back] are the general form
        // a miss returns to (ip mapped by offset). Each cell records its slot, its expected kind, whether
        // the loop reads it before writing it (live-in: unboxed at entry), whether the body defines it
        // (flushed on exit or miss) and the landing that defines it (a decl carries its stamp).
        // a set of cells, up to 128: the dirty cells of a running block, the compiler values, the environment values,
        // the values live at a copy instruction
        struct cell_mask {
            uint64_t w[2] = {0, 0};
            void set(uint32_t k) noexcept { w[k >> 6] |= uint64_t(1) << (k & 63); }
            void clear(uint32_t k) noexcept { w[k >> 6] &= ~(uint64_t(1) << (k & 63)); }
            bool test(uint32_t k) const noexcept { return ((w[k >> 6] >> (k & 63)) & 1) != 0; }
            bool any() const noexcept { return (w[0] | w[1]) != 0; }
            void reset() noexcept { w[0] = 0; w[1] = 0; }
            cell_mask operator~() const noexcept { cell_mask r; r.w[0] = ~w[0]; r.w[1] = ~w[1]; return r; }
            cell_mask operator&(const cell_mask& o) const noexcept { cell_mask r; r.w[0] = w[0] & o.w[0]; r.w[1] = w[1] & o.w[1]; return r; }
            cell_mask operator|(const cell_mask& o) const noexcept { cell_mask r; r.w[0] = w[0] | o.w[0]; r.w[1] = w[1] | o.w[1]; return r; }
            cell_mask& operator|=(const cell_mask& o) noexcept { w[0] |= o.w[0]; w[1] |= o.w[1]; return *this; }
            cell_mask& operator&=(const cell_mask& o) noexcept { w[0] &= o.w[0]; w[1] &= o.w[1]; return *this; }
            static cell_mask all() noexcept { cell_mask r; r.w[0] = ~uint64_t(0); r.w[1] = ~uint64_t(0); return r; }
        };
        static constexpr uint32_t k_cell_limit = 128;
        struct cell_loop {
            // env_operand: the environment scalar this cell holds (index into env_operands; k_invalid_u32 = a frame
            // slot); such a cell has a synthetic slot above the frame's, is resolved once at entry and never written
            // temp: a compiler temp (written before read inside its statement, no declared type): no entry guard, no unbox,
            // any kind lands
            // first_decl: the loop's first write to the cell is its declaration (a local the body declares: it enters empty and its
            // flush stamps with decl_stamp, the declaration's type, whatever later stores land)
            // string_borrow: a temp that may hold a borrowed string node, because its only consumer replays from the producer on a miss
            // (the append pair); any other temp refuses a string element, since a miss at its consumer reads the temp's slot
            struct cell { uint32_t slot; uint8_t kind; uint8_t landing; bool live_in; bool defined; type_info* stamp; uint32_t env_operand = k_invalid_u32; bool temp = false; bool first_decl = false; type_info* decl_stamp = nullptr; bool string_borrow = false; bool iter_index = false; };   // iter_index: a range-for block's index, the iteration state's (never a slot's)
            uint32_t head, back, copy_head, copy_back, exit_ip;
            // counted: the counted_for_protos index of a counted loop (its back edge is the entry and its cell back edge
            // steps and compares; the copy's head slot is a placeholder); k_invalid_u32 = a compare-headed loop
            uint32_t counted = k_invalid_u32;
            // an element alias declared in the body (`var& strip = STRIPS[sidx]`): the copy instruction that rebinds it, its cell (a compiler value of
            // kind reference: the element index is its payload, the source table its own site), the site index of its source table and the site
            // indices of the loads and stores through the alias, whose containers the rebind sets and the entry never resolves
            struct rebind { uint32_t copy_ip = 0, cell = 0, source_site = k_invalid_u32; std::vector<uint32_t> dependents; bool iter = false; };   // iter: a range-for's by-reference loop variable (the reference carries the range-for's element constraint)
            std::vector<rebind> rebinds;
            std::vector<uint8_t> site_alias;   // per site: 1 = a site through an alias (its container the rebind's element; skipped by the entry and the hold check)
            std::vector<uint32_t> inline_sites;   // the inlined leaves inside the region (chunk::inline_sites indexes): their guards hold at the entry and after every effect point
            // a range-for head (`for (auto& p : pool)`): the iter_protos index (the loop variable's slot and binding), the index cell (the
            // iteration state's index at entry, stepped by the back edge, synced on a miss and at the exit) and the head's site (the state's container)
            uint32_t iter_proto = k_invalid_u32, iter_index_cell = k_invalid_u32, iter_site = k_invalid_u32;
            std::vector<uint32_t> inner_counted;   // the counted_for_protos indices of the inner counted loops the block took, outermost first: a miss inside one pushes the general form's state (cells_miss)
            std::vector<cell> cells;
            std::vector<uint32_t> env_cells;           // the cells with an environment operand: a landing after a call re-reads them (row 156.2)
            std::vector<uint32_t> string_candidates;   // the slot cells whose kind may be a string at run time: a landing re-borrows them
            // indexed by frame slot (and the env cells' synthetic slots): the slot's FIRST cell, k_invalid_u32 = not a cell. A
            // compiler temp slot may carry several cells, one per definition of another kind; the emission advances the
            // binding as it passes each definition and the copy names cells directly, so this table is emission-time only
            std::vector<uint32_t> slot_to_cell;
            std::vector<uint32_t> origin;         // per copy instruction (copy_head + k): the original instruction it stands for (the miss path's map)
            // per copy instruction: the temp cells whose definition is live there (defined earlier, read at or after it),
            // the only temps a miss at that instruction flushes; temp_mask = every temp cell
            std::vector<cell_mask> live_temps;
            cell_mask temp_mask;
            cell_mask env_mask;       // every environment cell: the ones a body writes are flushed ahead of a call and at every exit
            std::vector<uint32_t> sites;   // the load and store sites' copy ips: the entry resolves their containers, a landing after a call checks them
            struct env_operand_ref { fused_operand operand; uint32_t cache_slot; };
            std::vector<env_operand_ref> env_operands;
            // a call inside the block: the probe, the pushes, the call and the landing are four copy instructions over one
            // descriptor. An argument is a cell (boxed at the call; a slot's cell is flushed first so a by-reference parameter
            // binds the current value) or a push the block runs verbatim (a constant, an environment name, a slot the loop
            // holds no cell for). The consumer lands the result into dst by the declared-type rule (1 = a scalar declaration,
            // 2 = a no-result store, 3 = a compiler temp: op_call_temp, the expression around the call reads it as a slot) or drops it (0 = a statement).
            struct call_arg { uint8_t kind; uint32_t v; uint32_t ip; };   // kind 0 = a slot (a cell or verbatim, decided at emission), 1 = cell v, 2 = the push at ip verbatim
            struct cell_call { uint32_t probe_ip, call_ip, consumer_ip; uint8_t consumer = 0; uint32_t dst = k_invalid_u32; std::vector<call_arg> args; };
            std::vector<cell_call> calls;
            std::string ir_text;   // the region's canonical operations (index:kind, ->target, w slot, r slots), filled only when the compiler lists the IR
        };
        std::vector<cell_loop> cell_loops;
        // a loop the translation refused: its span, the first offending instruction and the refusal class (the profile's
        // open-loop table and the disassembly); a `translation` refusal is a supported core operation in an encoding the
        // translation does not normalize yet, a coverage gap rather than a limit of the model
        struct cell_refusal { uint32_t head, back, ip; uint8_t reason; uint8_t op; };
        static constexpr uint8_t cell_refusal_none = 0, cell_refusal_head = 1, cell_refusal_representation = 2, cell_refusal_effect = 3,
                                 cell_refusal_translation = 4, cell_refusal_malformed = 5, cell_refusal_no_cells = 6, cell_refusal_too_many_cells = 7;
        static const char* cell_refusal_name(uint8_t r) {
            switch (r) {
            case cell_refusal_head: return "head";
            case cell_refusal_representation: return "representation";
            case cell_refusal_effect: return "effect";
            case cell_refusal_translation: return "translation";
            case cell_refusal_malformed: return "malformed";
            case cell_refusal_no_cells: return "no cells";
            case cell_refusal_too_many_cells: return "too many cells";
            default: return "none";
            }
        }
        // the class of an instruction the translation has no operation for: a supported core operation in another
        // encoding is a translation gap; a call, an exit, a scope or a control effect is an effect; members, objects and
        // allocations are representations the model does not take
        static uint8_t cell_refusal_of(opcode op) {
            switch (op) {
            case opcode::op_const: case opcode::op_null: case opcode::op_true: case opcode::op_false: case opcode::op_pop: case opcode::op_dup:
            case opcode::op_to_bool: case opcode::op_load: case opcode::op_store: case opcode::op_compound_store: case opcode::op_incdec:
            case opcode::op_decl_var: case opcode::op_binary: case opcode::op_binary_fused: case opcode::op_index: case opcode::op_index_assign:
            case opcode::op_index_compound: case opcode::op_unary: case opcode::op_jump_if_false: case opcode::op_jump_if_true:
            case opcode::op_compound_fused: case opcode::op_math: case opcode::op_index_store: case opcode::op_index_compound_fused:
            case opcode::op_index_fused: case opcode::op_operand_temp: case opcode::op_call_temp: case opcode::op_member_temp: case opcode::op_set_member_fused: case opcode::op_math_temp: case opcode::op_t_push: case opcode::op_push_fused:
            case opcode::op_cfor_prep: case opcode::op_cfor_back: case opcode::op_cfor_pop: case opcode::op_loop_back: case opcode::op_cells_back:
            case opcode::op_cells_cfor_back: case opcode::op_fused_cmp_jump: case opcode::op_cast: case opcode::op_index_store_fused:
                return cell_refusal_translation;
            case opcode::op_get_member: case opcode::op_set_member: case opcode::op_member_compound: case opcode::op_get_static: case opcode::op_set_static:
            case opcode::op_this: case opcode::op_super: case opcode::op_from_this: case opcode::op_null_guard: case opcode::op_probe_member:
            case opcode::op_array: case opcode::op_map: case opcode::op_new: case opcode::op_decl_ref_ident: case opcode::op_decl_ref_value: case opcode::op_destructure:
                return cell_refusal_representation;
            default:
                return cell_refusal_effect;
            }
        }
        std::vector<cell_refusal> cell_refusals;
        std::vector<fused_binary_proto> fused_binary_protos;
        std::vector<compound_fused_proto> compound_fused_protos;
        std::vector<fused_binary_dst_proto> fused_binary_dst_protos;
        std::vector<fused_index_proto> fused_index_protos;
        std::vector<push_proto> push_protos;
        std::vector<member_store_proto> member_store_protos;
        // an inlined leaf (docs/typed_regions_design.md, Inlined leaves): the guard at guard_ip admits [begin_ip, fallback_ip) when the callee
        // name binds the function whose body is `body`; otherwise the fallback [fallback_ip, join_ip) runs the original statement
        struct inline_site {
            fused_operand callee;                 // the callee identifier, resolved as the probe resolves it
            uint32_t name_symbol = k_invalid_u32;
            const block_stmt* body = nullptr;
            const function_decl* decl = nullptr;   // the callee: its name for the trace
            const ast_node* call_stmt = nullptr;   // the caller's statement: the trace's line for the caller's frame
            uint32_t guard_ip = 0, begin_ip = 0, fallback_ip = 0, join_ip = 0;
        };
        std::vector<inline_site> inline_sites;
        struct inline_refusal { std::string callee; const char* reason = nullptr; uint32_t line = 0; };   // a call the inliner kept and the rule that kept it
        std::vector<inline_refusal> inline_refusals;   // the listing and the profile build name them; no instruction reads the table
        // Typed scalar ops (opcode.hpp op_t_*): the instruction each one was lowered from
        // (two for a move: the LOAD or CONST and the DECL_VAR or STORE it replaced) and the
        // decl's type stamp. Indexed by ip; rows for other instructions are empty.
        static constexpr uint32_t k_iter_container = 0xFFFFFFFEu;   // a site row's cont_slot: the container is the iteration state's (a range-for block's head and its landing)
        struct typed_site {
            vm_instruction orig{opcode::op_halt, 0, 0, 0};
            vm_instruction orig2{opcode::op_halt, 0, 0, 0};
            type_info* stamp = nullptr;
            // op_t_load: the fused index proto flattened into one row. cont_slot = the
            // container's frame slot, k_invalid_u32 = env-resident (resolved through the
            // original proto's operand); index = operands[0] when fold_ops == 0, else
            // operands[0] op[0] operands[1], then op[k] operands[k + 1] for each chain step;
            // an operand is a slot or a constant index behind k_typed_const_bit
            uint32_t cont_slot = k_invalid_u32;
            uint32_t operands[4] = {};
            uint8_t fold_ops[3] = {};
            uint8_t fold_count = 0;
            uint8_t decl_expect = 0xFF;
            // an op_operand_temp origin (materialize_typed_reads): the read resolves through this synthetic
            // proto and lands in a compiler temp with the canonical stamp (in place when the temp holds it)
            uint32_t index_proto = k_invalid_u32;
            bool temp = false;
            bool speculative = false;
            uint32_t guard_temp = k_invalid_u32;   // fused_index_proto::container_guard, flattened
            bool guard_always = false;             // fused_index_proto::guard_always, flattened
        };
        std::vector<typed_site> typed_sites;
        std::vector<counted_for_proto> counted_for_protos;

        // Default-argument expressions compiled per parameter (function-body chunks only)
        std::vector<std::shared_ptr<chunk>> param_default_chunks;

        std::string function_name;                         // empty = top level
        size_t local_count = 0;
        size_t first_temp_slot = 0;   // the parser's locals end here; every slot from it on is the compiler's (temps, a graft's slots)
        bool is_function_body = false;
#ifdef JAISCRIPT_VM_PROFILE
        // slot census input: each parameter's slot, declared type (null = untyped) and by-reference flag
        struct profile_param { uint32_t slot; type_info* type; bool is_reference; };
        std::vector<profile_param> profile_params;
#endif
        // Compile-time lazy-env gate: false = every op proven not to need the per-call
        // scope environment, so plain in-loop callees skip creating it (fail-closed default)
        bool needs_frame_env = true;
        // Compiler had the builtin registries: call_site bi_* indices are authoritative
        // (invalid = not a builtin). False (raw-compiler tests) keeps the find fallback.
        bool builtin_indexed = false;
        // GET_MEMBER field-read site cache (flat classes tier-1 stage B): per-ip
        // {receiver class, method_epoch, field slot}; slot UINT32_MAX = negative
        // (same_as / nonpublic chain / sp-builtin name / getter shadow / no slot —
        // the resolve ladder speaks). Lazily sized to code.size(); main-engine only
        // (same cached_global_env_ gate as the caches above).
        struct member_ic_entry {
            const class_definition* cd = nullptr;
            uint64_t epoch = 0;
            uint32_t slot = UINT32_MAX;
        };
        mutable std::vector<member_ic_entry> member_ic;
        // Compile-time sticky-method-scope gate: true = no op parks per-call state in the
        // scope env or captures its identity beyond the call (closures/refs/try/scopes/env
        // decls), so a method body may run in ONE persistent per-dispatch method env
        // (vm_backend::push_method_frame). Superset of the lazy list: member-access ops are
        // allowed here (they read this/access-context, both re-stamped per call).
        bool method_env_reusable = false;

        // Pins the whole program tree for top-level chunks
        std::vector<declaration_ptr> pinned_decls;
    };

} // namespace jai::vm

#endif // __JAISCRIPT_VM_CHUNK_HPP__
