#pragma once

#include "value.hpp"
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
class coroutine_handle;
namespace debug { class controller; }
namespace detail { class engine_operator_table; }

// Backend-neutral payload describing a script-defined callable. Stored thunks capture
// (engine*, payload) and resolve the live backend at call time via
// engine::get_execution_backend()->execute_callable(), so no callable ever holds a
// backend or interpreter pointer.
struct script_callable {
    enum class kind_type : uint8_t { function, method, static_method, constructor };
    kind_type kind = kind_type::function;

    std::shared_ptr<script_defined_function> fn;   // function (free functions, lambdas)
    std::shared_ptr<function_decl> ast;            // method / static_method (resolved overload)
    std::shared_ptr<class_definition> cls;         // method / static_method / constructor
    std::shared_ptr<environment> definition_env;   // method / static_method / constructor
    std::optional<script_value> this_obj;          // method ('this' receiver; args exclude it)
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
    checked_result<script_value> operator()(const std::vector<script_value>& args) const;
};

namespace detail { struct parallel_method_pin_table; }

/**
 * Abstract interface for script execution backends.
 * This allows us to have both interpreter and bytecode VM implementations.
 */
class execution_backend {
public:
    virtual ~execution_backend() = default;

    // Parallel workers: barrier-provisioned class-method pins for the active region
    // (null outside regions and on engine backends). A worker method wall consults it —
    // a hit dispatches on worker-private state, a miss is an admission verdict.
    virtual void set_parallel_method_pins(const detail::parallel_method_pin_table*) {}

    // Debugger hook: engine::debugger() wires the controller here. Default no-op, so the
    // vm backend inherits it unchanged until phase 5 adds its statement-boundary hook.
    virtual void set_debug_controller(debug::controller*) {}

    // Innermost active environment during execution — the debugger enumerates in-scope
    // locals from it while parked. Default null (vm until phase 5); the interpreter
    // returns its live scope.
    virtual std::shared_ptr<environment> get_current_environment() const { return nullptr; }

    // Named slot-based locals of the current (innermost) call frame — params + reached body
    // locals, which the environment's flat map does not hold. The debugger prefers this for
    // the Locals view. Default empty (vm until phase 5; interpreter reconstructs from the frame).
    virtual std::vector<std::pair<std::string, script_value>> get_current_frame_locals() const { return {}; }

    // Core execution
    virtual script_value execute(const std::vector<declaration_ptr>& declarations) = 0;

    // Pre-parsed execution (engine::execute(jaibite&)). compiled_slot is a caller-owned
    // cache the backend may fill with a compiled artifact keyed to these declarations
    // (the vm stores its chunk there; the default ignores it and re-walks the AST).
    virtual script_value execute(const std::vector<declaration_ptr>& declarations, std::shared_ptr<void>& compiled_slot) {
        (void)compiled_slot;
        return execute(declarations);
    }

    virtual void prepare_for_execution() = 0;

    // Execute a script-defined callable (see script_callable). For kind::method the
    // receiver rides in payload.this_obj and args exclude it.
    virtual checked_result<script_value> execute_callable(const script_callable& payload, const std::vector<script_value>& args) = 0;
    
    // Variable access (needed by engine)
    virtual script_value get_variable(const std::string& name) const = 0;
    virtual bool has_variable(const std::string& name) const = 0;
    
    // Scope management (for instance variables)
    virtual void push_scope() = 0;
    virtual void pop_scope() = 0;
    virtual void define_variable(const std::string& name, const script_value& value) = 0;
    
    // Configuration
    virtual void set_execution_budget(std::chrono::nanoseconds) = 0;
    virtual void set_has_custom_numeric_ops(bool value) = 0;
    // ANY user binary-operator / "[]" override registered: transient subscript reads fall
    // back to the reference mint (see detail/transient_read.hpp). Non-pure so custom
    // backends (which have no such fast path) are unaffected.
    virtual void set_has_custom_binary_ops(bool) {}
    // Engine-owned flat operator dispatch table (detail/operator_table.hpp): backends
    // resolve global operator overrides through it instead of probing environments.
    // Outlives the backend; non-pure so custom backends are unaffected.
    virtual void set_operator_table(const detail::engine_operator_table*) {}
    virtual void set_subscript_resolver(std::function<checked_result<script_value>(const std::vector<script_value>&)> resolver) = 0;
    virtual void set_class_lookup_callback(std::function<std::shared_ptr<class_definition>(const std::string&)> callback) = 0;
    virtual void set_engine_reference(engine* engine_ref) = 0;
    
    // Exception handling
    virtual bool is_unwinding() const = 0;
    virtual const script_exception& get_current_exception() const = 0;

    // True while a script is running on this backend (live frames / a top-level
    // execute in flight). The host-boundary discriminator: a stored-callable
    // invocation at is_executing()==false is a fresh host entry (same rule as the
    // backends' host-level resume detection), so an uncaught script throw converts
    // to a thrown script_exception there instead of staying a latched flag.
    virtual bool is_executing() const { return false; }

    // Stack trace captured at the last unwind (empty if the backend doesn't track one)
    virtual std::vector<stack_frame> last_stack_trace() const { return {}; }
    virtual std::string format_stack_trace() const { return {}; }

    // Shelve/restore the in-flight call's argument metadata around an external (C++)
    // invocation of a script function (see engine::external_call_guard)
    virtual void push_external_call_scope() {}
    virtual void pop_external_call_scope() {}

    // Coroutine resume; default body (coroutine.cpp) fails the handle with
    // "Coroutines are not supported by this backend"
    virtual checked_result<script_value> resume_coroutine(coroutine_handle& handle);

    // Optional: Get backend name for debugging/logging
    virtual std::string get_backend_name() const = 0;
};

using execution_backend_ptr = std::unique_ptr<execution_backend>;

} // namespace jai