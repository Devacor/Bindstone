#pragma once

#include "value.hpp"
#include "runtime_errors.hpp"
#include <jaiscript/jaiscript_fwd.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <memory>
#include <optional>
#include <vector>
#include <string>
#include <system_error>
#include <chrono>

namespace jai {

// Forward declarations
class string_symbolizer;
class class_definition;
class class_instance;
class coroutine_handle;
namespace debug { class controller; }
namespace detail { class engine_operator_table; }

struct script_callable;

// The declarations of one function name with distinct signatures (detail/function_overloads.hpp): each member's own function
// value beside it, so a call that selects a member runs exactly what a lone declaration of it would
struct function_overload_set {
    uint64_t name_id = 0;
    std::vector<std::shared_ptr<script_defined_function>> members;
    std::vector<script_value> values;
};

// Backend-neutral payload describing a script-defined callable. Stored thunks capture
// (engine*, payload) and resolve the live backend at call time via
// engine::get_execution_backend()->execute_callable(), so no callable ever holds a
// backend or interpreter pointer.
struct script_callable {
    // bound_method: a receiver-bound method VALUE (the env method-fallback mint for
    // bare sibling calls) — this_obj + bound_dispatch (the class's methods_ dispatcher
    // value, which carries the sticky-env/body-cache state and pins the class). The
    // thunk's operator() replicates make_bound_method's exact behavior; the vm's call
    // path recovers the payload and enters the method in-loop instead.
    // overload_set: the program's or a namespace scope's declarations of one name; a call selects one member
    enum class kind_type : uint8_t { function, method, static_method, constructor, bound_method, overload_set };
    kind_type kind = kind_type::function;

    std::shared_ptr<script_defined_function> fn;   // function (free functions, lambdas)
    std::shared_ptr<function_decl> ast;            // method / static_method (resolved overload)
    std::shared_ptr<class_definition> cls;         // method / static_method / constructor
    std::shared_ptr<environment> definition_env;   // method / static_method / constructor
    std::optional<script_value> this_obj;          // method / bound_method ('this'; args exclude it)
    std::optional<script_value> bound_dispatch;    // bound_method: the method value to invoke
    std::shared_ptr<const function_overload_set> overloads;   // overload_set
};

// Named trampoline for script-defined callables. Backends mint function values through
// this type (instead of an anonymous lambda) so hot call paths can recover the payload
// via std::function::target<script_callable_thunk>() and dispatch straight into their
// own call machinery — the analog of a bytecode VM's direct closure call — while any
// other caller still goes through operator() with identical behavior.
struct script_callable_thunk {
    engine* eng = nullptr;
    script_callable payload;
    checked_result<script_value> operator()(const std::vector<script_value>& args) const;
};

// Named dispatcher for script-class instance methods, stored in
// class_definition::methods_ (operator() defined in script_class.hpp). Same pattern as
// script_callable_thunk: hot call paths recover it via
// std::function::target<script_method_dispatch>() and enter the resolved method body
// directly; every other caller goes through operator() with identical behavior.
struct script_method_dispatch {
    engine* eng = nullptr;
    std::shared_ptr<class_definition> cls;
    uint64_t name_id = 0;
    std::shared_ptr<environment> definition_env;
    // Backend-owned compiled-body slot keyed by body identity, so the hot path skips
    // its body-keyed hash lookup. Method RESOLUTION still walks the live class every
    // call (hot reload). mutable: filled through the const target<>() pointer.
    mutable std::shared_ptr<void> body_cache;
    mutable const void* body_cache_key = nullptr;
    // vm sticky method scope: ONE persistent method env (parent = the engine's global
    // env) reused across calls when the compiled body proves chunk::method_env_reusable
    // and the env is idle (use_count()==1; recursion/reentry falls back to the pooled
    // path). Function-value copies share the underlying dispatcher storage, so this slot
    // is per-method like body_cache. The interpreter ignores it.
    mutable std::shared_ptr<environment> backend_scope_env;
    // the overload an argument count alone picks, valid while the class's method epoch holds (class_definition's memo pattern)
    mutable const std::shared_ptr<function_decl>* resolved_memo = nullptr;
    mutable uint64_t resolved_memo_epoch = 0;
    mutable size_t resolved_memo_argc = 0;
    checked_result<script_value> operator()(const std::vector<script_value>& args) const;
    // The overload the arguments args[first..] call
    checked_result<const std::shared_ptr<function_decl>*> resolve(const std::vector<script_value>& args, size_t first) const;
    // The method on this_obj with the arguments args[first..]
    checked_result<script_value> invoke(const script_value& this_obj, const std::vector<script_value>& args, size_t first) const;
};

// Script static overloads retain their identity through aliases, so argument capture can distinguish them from host calls.
struct script_static_method_dispatch {
    engine* eng = nullptr;
    std::shared_ptr<class_definition> cls;
    uint64_t name_id = 0;
    std::shared_ptr<environment> definition_env;
    std::shared_ptr<function_decl> fallback;
    // the vm's compiled-body slot for the overload last entered in-loop, keyed by its body (script_method_dispatch's)
    mutable std::shared_ptr<void> body_cache;
    mutable const void* body_cache_key = nullptr;
    checked_result<script_value> operator()(const std::vector<script_value>& args) const;
};

namespace detail {
    struct parallel_method_pin_table;

    // A nested run's failure (an include or import started by the running code), parked by the backend before the engine
    // turns it into a C++ exception and taken back by the op that started the run: the runtime error itself or the thrown
    // value, and the frames the nested run captured (its own level only; the op adds the frames at its site)
    struct nested_failure {
        bool parked = false;
        std::optional<error_propagator> error;
        std::optional<script_value> value;
        std::optional<script_exception> exception;
        std::vector<stack_frame> trace;
    };
}

// A builtin's script callback (sort's comparator, filter's predicate) prepared once per builtin call: each call binds its
// arguments and runs the body on one reused frame, with the function value's observable behavior
class prepared_callback {
public:
    virtual ~prepared_callback() = default;
    virtual checked_result<script_value> call(const std::vector<script_value>& args) = 0;
};

/**
 * Abstract interface for script execution backends.
 * This allows us to have both interpreter and bytecode VM implementations.
 */
class execution_backend {
public:
    virtual ~execution_backend() = default;

	// Null: the callable runs through its function value. A prepared callback is released before the builtin returns.
	virtual prepared_callback* prepare_callback(const script_value&) { return nullptr; }
	virtual void release_callback(prepared_callback*) {}

	// Cold library bridge: sorting uses the backend's ordinary < expression semantics.
	// Primitive buffers bypass this only while the backend's < has no global override.
	virtual checked_result<bool> language_less(const script_value&, const script_value&) = 0;
	virtual bool native_less_available() const = 0;
	virtual checked_result<script_value> array_transform(uint64_t, script_value&, const std::vector<script_value>&) = 0;
	virtual checked_result<script_value> map_transform(uint64_t, script_value&, const std::vector<script_value>&) = 0;

    // Parallel workers: barrier-provisioned class-method pins for the active region
    // (null outside regions and on engine backends). A worker method wall consults it —
    // a hit dispatches on worker-private state, a miss is an admission verdict.
    virtual void set_parallel_method_pins(const detail::parallel_method_pin_table*) = 0;

    // Debugger hook: engine::debugger() wires the controller here.
    virtual void set_debug_controller(debug::controller*) = 0;
    // Diagnostic builds (JAISCRIPT_VM_PROFILE): zero the per-opcode and per-script-function
    // tables so a warm-up phase drops out of the report; a no-op everywhere else
    virtual void profile_reset() {}
    virtual cell_counters cells_counters() const { return {}; }

    // Innermost active environment during execution — the debugger enumerates in-scope
    // locals from it while parked.
    virtual std::shared_ptr<environment> get_current_environment() const = 0;

    // Named slot-based locals of the current (innermost) call frame — params + reached body
    // locals, which the environment's flat map does not hold. The debugger prefers this for
    // the Locals view.
    virtual std::vector<std::pair<std::string, script_value>> get_current_frame_locals() const = 0;

    // Core execution
    virtual script_value execute(const std::vector<declaration_ptr>& declarations) = 0;

    // Pre-parsed execution (engine::execute(jaibite&)). compiled_slot is a caller-owned
    // cache the backend may fill with a compiled artifact keyed to these declarations
    // (the vm stores its chunk there; the default ignores it and re-walks the AST).
    virtual script_value execute(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) {
        (void)compiled_slot;
        return execute(declarations);
    }

    // Textual execution (include): the declarations run in the backend's CURRENT environment under the running
    // unit, as if written at the include site (internal/global_shadowing.md)
    virtual script_value execute_textual(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) = 0;

    virtual void prepare_for_execution() = 0;

    // Execute a script-defined callable (see script_callable). For kind::method the
    // receiver rides in payload.this_obj and args exclude it.
    virtual checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args) = 0;
    // execute_callable over args[first..]: the arguments after a receiver the caller's list carries ahead of them
    virtual checked_result<script_value> execute_callable_from(const script_callable& payload, const std::vector<script_value>& args, size_t first) {
        if (first == 0) { return execute_callable(payload, args); }
        const std::vector<script_value> rest(args.begin() + static_cast<std::ptrdiff_t>(first), args.end());
        return execute_callable(payload, rest);
    }

    // Field initializers for an instance created OUTSIDE construction (engine::extend,
    // archive load): the declared initializers run chain-root first in declaration order
    // with 'this' bound, exactly as construction seeds them, parented on the global
    // environment (a class declared inside a function sees globals here). No ctor body.
    virtual void apply_field_initializers(const std::shared_ptr<class_instance>& instance,
                                          const std::shared_ptr<class_definition>& class_def) = 0;
    
    // Variable access (needed by engine)
    virtual script_value get_variable(const std::string& name) const = 0;
    virtual bool has_variable(const std::string& name) const = 0;
    
    virtual void define_variable(const std::string& name, const script_value& value) = 0;
    // Instance-variable scope for execute(script, locals): rooted at the global env so a
    // reentrant run adopts it instead of bare global (receivers fired mid-script see their args)
    virtual void push_instance_scope() = 0;
    virtual void pop_instance_scope() = 0;
    
    // Configuration
    virtual void set_execution_budget(std::chrono::nanoseconds) = 0;
    virtual void set_has_custom_numeric_ops(bool value) = 0;
    // ANY user binary-operator / "[]" override registered: transient subscript reads fall
    // back to the reference mint (see detail/transient_read.hpp).
    virtual void set_has_custom_binary_ops(bool) = 0;
    // Engine-owned flat operator dispatch table (detail/operator_table.hpp): backends
    // resolve global operator overrides through it instead of probing environments.
    // Outlives the backend.
    virtual void set_operator_table(const detail::engine_operator_table*) = 0;
    virtual void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) = 0;
    virtual void set_engine_reference(engine* engine_ref) = 0;
    
    // Exception handling
    virtual bool is_unwinding() const = 0;
    virtual const script_exception& get_current_exception() const = 0;

    // Caller's access context at builtin-call time (reflect::set/invoke enforcement:
    // a builtin invocation does not push a script frame, so the backend's current
    // environment belongs to the caller). Null = top-level / free-function context.
    virtual const class_definition* current_access_context() const = 0;

    // True while a script is running on this backend (live frames / a top-level
    // execute in flight). The host-boundary discriminator: a stored-callable
    // invocation at is_executing()==false is a fresh host entry (same rule as the
    // backends' host-level resume detection), so an uncaught script throw converts
    // to a thrown script_exception there instead of staying a latched flag.
    virtual bool is_executing() const = 0;
    // The unit of the running code (internal/global_shadowing.md), derived from the executing frame: a function's own
    // stamp, the run's at top level. The symbolizer's ambient stamp is written only while a name has versions;
    // sync_unit re-aligns it with the executing frame after a nested run.
    virtual unit_stamp current_unit() const = 0;
    virtual void sync_unit() = 0;

    // A nested run (an include or import started by the running code) that ends unwinding parks its failure here before
    // the engine turns it into a C++ exception; the include/import op that started it takes it back into the outer run
    virtual void park_nested_failure() = 0;

    // Stack trace captured at the last unwind
    virtual std::vector<stack_frame> last_stack_trace() const = 0;
    virtual std::string format_stack_trace() const = 0;

    // The file stamped on the innermost executing statement (empty when no frame names one)
    virtual std::string current_source_file() const = 0;

    // Shelve/restore the in-flight call's argument metadata around an external (C++)
    // invocation of a script function (see engine::external_call_guard)
    virtual void push_external_call_scope() = 0;
    virtual void pop_external_call_scope() = 0;
    // A host callable's non-const T& argument: the reference a script T& parameter would bind for argument `index` of the
    // call in flight (internal/reference_model.md), or a non-reference when the argument names no lvalue or no script site
    // made this call; for an unboxed frame-slot local its value, with `slot` naming it for host_slot_store.
    // args is what the callable received, `offset` leading entries (a receiver) ahead of the arguments.
    virtual checked_result<script_value> host_reference_argument(const std::vector<script_value>& args, size_t index, size_t offset,
                                                                 host_slot& slot) = 0;
    // The store back into a local host_reference_argument named by its slot (the variable's own table)
    virtual checked_result<void> host_slot_store(const host_slot& slot, script_value& value) = 0;
    // A host calling a script function while no script runs: the deadline arms from the current budget and the entry's
    // limits reset, as at a top-level execute or resume
    virtual void begin_host_call() = 0;

    // Coroutine resume
    virtual checked_result<script_value> resume_coroutine(coroutine_handle& handle) = 0;

    // Backend name for debugging/logging
    virtual std::string get_backend_name() const = 0;
    // A value's text as string + value spells it: a class's own to_string() when it declares one
    virtual checked_result<std::string> text_of(const script_value& v) = 0;
};

using execution_backend_ptr = std::unique_ptr<execution_backend>;

} // namespace jai
