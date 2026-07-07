#pragma once

#ifndef __JAISCRIPT_CORE_ENGINE_HPP__
#define __JAISCRIPT_CORE_ENGINE_HPP__

#include "types.hpp"
#include "value.hpp"
#include "function_binder.hpp"
#include "binding_traits.hpp"
#include "conversion_registry.hpp"
#include "conversion_registry_impl.hpp"
// conversion_registry_templates.hpp moved to after engine class definition
#include "bound_array.hpp"
#include "bound_map.hpp"
#include "jaibite.hpp"
#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/serialization/serialization_metadata.hpp>
#include <memory>
#include <optional>
#include <unordered_set>
#include <typeindex>
#include <iosfwd>
#include <chrono>

namespace jai {

    // Class constants namespace (shared with dynamic_binder.hpp)
    namespace class_constants {
        inline const std::string CPP_OBJECT_FIELD = "_cpp_object";
    }

    // Forward declarations
    class class_definition;
    class class_registry;
    class execution_backend;
    class string_symbolizer;
    namespace vm { class vm_backend; }
    namespace debug { class controller; class transport; }
    struct script_namespace_data;

    namespace detail {
        struct execution_limits;
        struct parallel_engine_state;
    }

    namespace serialization {
        class serialization_registry;
        // Note: serialization metadata structs (property_metadata, class_metadata, etc.)
        // are defined in serialization_metadata.hpp which is included above
    }
    
    // Backend type enumeration
    enum class backend_type {
        interpreter,    // Default tree-walk interpreter
        vm,             // Bytecode virtual machine
        auto_select,    // Legacy alias for interpreter (implicit length-based switching removed)
        custom          // Externally supplied via set_backend(std::unique_ptr<execution_backend>)
    };

    // Extended type info for overload resolution - stores base type + optional C++ type for objects
    struct param_type_info {
        script_value_type base_type;
        std::type_index cpp_type{typeid(void)};  // For object types, used to lookup class def

        param_type_info() : base_type(script_value_type::jai_null_type) {}
        param_type_info(script_value_type t) : base_type(t), cpp_type(typeid(void)) {}
        param_type_info(script_value_type t, std::type_index idx) : base_type(t), cpp_type(idx) {}

        bool operator==(const param_type_info& other) const {
            return base_type == other.base_type && cpp_type == other.cpp_type;
        }
    };

    class engine : public std::enable_shared_from_this<engine> {
    private:
        engine();
        
    public:
        ~engine();
        
        // Factory method to ensure engines are always created as shared_ptr
        static std::shared_ptr<engine> make() {
            auto eng = std::shared_ptr<engine>(new engine());
            eng->initialize_engine_reference();
            return eng;
        }

        // Helper methods for getting engine references
        // Use these when you need to pass engine references to systems that need weak_ptr or shared_ptr
        std::weak_ptr<engine> get_weak_engine() {
            return weak_from_this();
        }

        std::shared_ptr<engine> get_shared_engine() {
            return shared_from_this();
        }

        // Non-copyable, moveable
        engine(const engine&) = delete;
        engine& operator=(const engine&) = delete;
        engine(engine&&) noexcept;
        engine& operator=(engine&&) noexcept;
        
        
        // Primary API - execute methods
        virtual script_value execute(const std::string& scriptContent);
        script_value execute(const std::string& scriptContent, const instance_variables& instanceVars);

        // execute_file attributes the script to `scriptPath`: the path is stamped onto
        // every AST node (file:line:col), so stack traces, error messages, and the
        // debugger name the originating file. Prefer this over execute() for files —
        // the source path is carried for free, no debugging-only parameter to pass.
        script_value execute_file(const std::string& scriptPath);
        script_value execute_file(const std::string& scriptPath, const instance_variables& instanceVars);

        // Automatic jaibite disk cache (default ON). Every FILE-based load — execute_file,
        // script include (all forms), script import — transparently maintains a sibling
        // cache next to the source (foo.jai -> foo.jaibite): a sibling that is strictly
        // newer by mtime (equal = stale) with a matching registration fingerprint loads
        // instead of parsing; otherwise the source parses and the sibling is rewritten.
        // Cache writes are best-effort and SILENT on failure (read-only asset dirs,
        // shipped games) — jaibite_cache_write_failures() counts them for diagnostics.
        // String executes never touch disk. Backend-agnostic: the sibling holds the parsed
        // AST (filename stamps included, so traces still name the .jai); the vm compiles
        // its chunk lazily and reuses it in-memory as usual.
        void jaibite_cache(bool enabled);
        bool jaibite_cache() const;
        size_t jaibite_cache_write_failures() const;

        // Execute already-read script file contents with jaibite disk-cache maintenance
        // keyed beside resolvedPath — for hosts that read files through their own VFS but
        // still want the sibling cache (engine-side file loads use it internally).
        // Identical to execute(content) + path attribution when the cache is disabled or
        // the path is a .jaibite itself.
        script_value execute_file_source(const std::string& resolvedPath, const std::string& content);

        // Pre-parsed script handle: parse once here, then run it repeatedly via
        // bite.execute() or engine->execute(bite) without re-lexing/parsing (the vm
        // backend also caches its compiled bytecode inside the bite). Engine-bound —
        // never share a jaibite across engines. Parse errors throw here.
        jai::jaibite jaibite(const std::string& scriptContent);
        script_value execute(jai::jaibite& bite);

        // Load a jaibite saved via jaibite::save()/save_bytes(). Symbols and type_info
        // are re-interned into THIS engine, so a bite saved from another engine loads
        // correctly. Throws jai::runtime_error on missing file / bad format / version
        // mismatch / corrupt data. Check bite.registration_mismatch() for an advisory
        // registered-class/function fingerprint difference.
        jai::jaibite jaibite_load(const std::string& path);
        jai::jaibite jaibite_load_bytes(const uint8_t* data, size_t size);
        jai::jaibite jaibite_load_bytes(const std::vector<uint8_t>& bytes) {
            return jaibite_load_bytes(bytes.data(), bytes.size());
        }

        // FNV-1a over the sorted registered function/class/template-type names — the
        // parse-affecting registration surface. Stamped into saved jaibites; compared on
        // load to set jaibite::registration_mismatch().
        uint64_t registration_fingerprint() const;

        // === STATIC TYPE CHECKING (opt-in; docs/static_checking.md) ===
        // Off by default (zero behavior change, one branch per parse-cache entry).
        // warn: diagnostics collected at parse-cache-entry creation, retrievable via
        //       last_check_diagnostics(); execution proceeds.
        // strict: any error-severity diagnostic makes execute()/jaibite creation/load
        //       throw static_check_error BEFORE anything runs; what() is the full
        //       accumulated, location-sorted, deduplicated listing with source + carets.
        // Checks are amortized once per unique source (cached beside the parse) and
        // re-run when the registration surface changes.
        void static_checking(check_mode mode);
        check_mode static_checking() const;

        // Parse (cached) + check, independent of the current mode — tooling entry point.
        // Throws parse_error if the source doesn't parse (checking is post-parse).
        check_report check(const std::string& scriptContent);

        // Diagnostics from the most recent checked execute()/jaibite path (warn/strict).
        const check_report& last_check_diagnostics() const;

        // Host-function surface for the static checker: per-overload arity (SIZE_MAX =
        // variadic) + C++ parameter signature (empty = untyped legacy binding). Returns
        // false when the name has no registered C++ function entry.
        struct host_overload_signature {
            size_t arity = 0;
            std::vector<param_type_info> param_types;
        };
        bool host_function_signatures(const std::string& name, std::vector<host_overload_signature>& out) const;

        // Global registration
        void add_global(const std::string& name, script_value value, bool is_serializable = true);
        
        // Convenience overload for add_global that automatically wraps values
        template<typename T>
        void add_global(const std::string& name, const T& value, bool is_serializable = true) {
            add_global(name, make_value(value), is_serializable);
        }
        
        // Add a global reference that binds to a C++ variable
        template<typename T>
        void add_global_ref(const std::string& name, T& value, bool is_serializable = false) {
            // For primitive types, use cpp_bound for direct binding
            if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T> ||
                          std::is_same_v<T, bool> || std::is_same_v<T, std::string> ||
                          std::is_same_v<T, char>) {
                auto ref = script_value::make_cpp_bound(&value, this);
                add_global(name, std::move(ref), is_serializable);
            } else {
                // For custom types, create a non-owning shared_ptr using aliasing constructor
                // This allows script to access the object without taking ownership
                // WARNING: Caller must ensure 'value' outlives the script engine!
                auto lifetime_tracker = std::make_shared<bool>(true);
                std::shared_ptr<T> non_owning_ptr(lifetime_tracker, &value);
                add_global(name, make_object(non_owning_ptr), is_serializable);
            }
        }
        
        // Reports this build's COMPILE-TIME integer-overflow policy:
        //   true  => overflowing integer +, -, *, unary -, and INT64_MIN/-1 division
        //            raise a catchable runtime error (the default, "safe by default").
        //   false => those operations wrap with well-defined two's-complement semantics.
        // Chosen at build time via JAISCRIPT_CHECKED_OVERFLOW / JAISCRIPT_WRAP_ON_OVERFLOW;
        // there is no runtime toggle (so the hot path carries no policy branch).
        bool throw_on_overflow() const;

        // Wall-clock budget in seconds for one execute()/resume() call (fractional is
        // fine — e.g. 1.0/60 for a frame). A script exceeding it raises a TERMINAL
        // runtime error (execution_budget_exceeded): script catch handlers are skipped
        // and execute() reports the failure to the host — a script can never swallow a
        // timeout. Zero disables the limit. Default 1. Checked at loop back-edges and
        // call entry, so straight-line C++ bindings are never interrupted mid-call.
        void execution_budget(double seconds);
        double execution_budget() const;

        // The step-debugger controller for this engine (lazily created, engine-owned).
        // First call wires the active backend's statement hook. Arm a session with
        // debugger().set_enabled(true). See jaiscript/debug/controller.hpp.
        debug::controller& debugger();

        // Attach an optional debug transport (typically jai::debug::debug_connector, which
        // speaks DAP over a TCP socket so VS Code can attach). The engine keeps it alive and
        // stops it — joining its thread and detaching the controller — before teardown, so a
        // paused script can never wedge the host. Passing nullptr stops and releases any
        // current one. One connector per engine is the norm; see docs/DEBUGGER_DESIGN.md.
        void set_debug_connector(std::shared_ptr<debug::transport> connector);

        // Approximate script memory cap in bytes for one top-level execute()/resume()
        // call. Accounting is a HIGH-WATER MARK charged at the heavy-value chokepoints
        // (container growth, string concat, clone, environment growth); frees are not
        // credited back. An allocation that would exceed the cap is DENIED and raises a
        // memory_cap error: the FIRST raise per execute is an ordinary catchable script
        // error (the counter re-arms so the script can free caches and continue); the
        // SECOND raise in the same execute is TERMINAL like a budget overrun. Reentrant
        // executes share the outer call's allowance. Zero disables the cap. Default 0.
        void memory_cap(size_t bytes);
        size_t memory_cap() const;

        // Per-engine execution-limit state (terminal-error latch, memory accounting)
        // shared by the backend and the builtin-method chokepoints. Internal. While a
        // parallel region's workers are running this resolves to the CALLING WORKER's
        // own accounting (docs/parallel_design.md §2); outside a region it is the plain
        // engine instance with one never-taken null test.
        detail::execution_limits& execution_limits() noexcept;

        // === PARALLEL EXECUTION (parallel_transform v0; docs/parallel_design.md) ===
        // Worker count the next parallel_transform uses (also the thread_count() builtin).
        // 0 = auto (hardware concurrency). The calling thread runs one chunk; the pool
        // supplies the rest.
        void parallel_thread_count(size_t workers);
        size_t parallel_thread_count() const;

        // Chunk boundaries chosen by the most recent parallel_transform (n+1 bounds for n
        // chunks). Instrumentation for tests/benchmarks — never observable from script.
        const std::vector<size_t>& last_parallel_chunk_bounds() const;

        // Internal: engine-owned parallel machinery (lazily created) and the per-worker
        // provisioning helper (fresh copies of registered callables whose invocation
        // shares no refcounts with the engine's stored values).
        detail::parallel_engine_state& parallel_state();
        script_value make_parallel_host_function_copy(const std::string& name);

        using stack_frame = ::jai::stack_frame;
        std::vector<stack_frame> last_stack_trace() const;
        std::string format_stack_trace() const;

        // Object creation through registered class system
        template<typename T, typename... Args>
        script_value make_object(Args&&... args) {
            auto data = std::make_shared<T>(std::forward<Args>(args)...);
            // Delegate to the shared_ptr overload which handles wrapping properly
            return make_object(data);
        }
        
        // Object creation from existing shared_ptr
        // NOTE: This method currently doesn't support property access on C++ objects
        // For C++ objects with properties, create them via script constructor instead
        // Defined in engine_impl.hpp where class_definition is complete.
        template<typename T>
        script_value make_object(std::shared_ptr<T> data);

        // Convenient script_value creation methods
        // Usage: engine->make_value(42) instead of script_value(42, engine)
        template<typename T>
        requires (std::is_arithmetic_v<std::remove_cvref_t<T>> ||
                  std::is_same_v<std::remove_cvref_t<T>, script_string> ||
                  std::is_same_v<std::remove_cvref_t<T>, std::string> ||
                  std::is_convertible_v<T, const char*> ||  // Handles const char*, const char[N], etc.
                  std::is_same_v<std::remove_cvref_t<T>, std::monostate>)
        script_value make_value(T&& value) {
            return script_value(std::forward<T>(value), this);
        }

        template<typename T>
        requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                  !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
                  !std::is_convertible_v<T, const char*> &&  // Exclude string literals and char pointers
                  !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
                  std::is_lvalue_reference_v<T>)
        script_value make_value(T&& value);  // Implemented in engine_impl.hpp

        template<typename T>
        requires (!std::is_arithmetic_v<std::remove_cvref_t<T>> &&
                  !std::is_same_v<std::remove_cvref_t<T>, script_string> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::string> &&
                  !std::is_convertible_v<T, const char*> &&
                  !std::is_same_v<std::remove_cvref_t<T>, std::monostate> &&
                  !std::is_pointer_v<std::remove_cvref_t<T>> &&
                  std::is_rvalue_reference_v<T&&> &&
                  !std::is_lvalue_reference_v<T>)
        script_value make_value(T&& value);  // Implemented in engine_impl.hpp

        template<typename T>
        script_value make_value(const std::shared_ptr<T>& value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return make_object(value);
        }

        template<typename T>
        script_value make_value(std::shared_ptr<T>&& value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return make_object(std::move(value));
        }

        template<typename T>
        requires (!std::is_same_v<T, char> && !std::is_same_v<T, const char>)  // Exclude char* (handled as strings)
        script_value make_value(T* value) {
            if (!value) {
                return script_value(std::monostate{}, this);
            }
            return script_value::make_cpp_bound(value, this);
        }

        // Create null/void value
        script_value make_null() {
            return script_value(std::monostate{}, this);
        }

        // Create array value
        script_value make_array() {
            return script_value::make_array(nullptr, this);
        }

        // Create map value
        script_value make_map() {
            return script_value::make_map(nullptr, nullptr, this);
        }

        // Create empty weak_ptr value with specified type
        script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type) {
            return script_value::make_empty_weak_ptr(weak_ptr_type, this);
        }
        
        // === FUNCTION REGISTRATION ===
        
        // Variadic functions - handle any number of arguments at runtime
        // Use this for functions like print(), max(), or functions with optional parameters
        // Example: engine.add_variadic_function("print", [](const std::vector<script_value>& args) { ... });
        void add_variadic_function(const std::string& name, script_function func);
        void add_functionWithArity(const std::string& name, script_function func, size_t arity);
        
        // Typed function registration with automatic overloading
        // Use this for functions with specific C++ signatures
        // Example: engine.add_function("add", [](int a, int b) -> int { return a + b; });
        template<typename Func>
        void add_function(const std::string& name, Func&& func) {
            // Check for rejected container references
            binding_traits::check_binding_validity<Func>(name);
            
            using traits = detail::function_traits<std::decay_t<Func>>;
            using return_type = typename traits::return_type;
            constexpr size_t arity = traits::arity;
            
            auto binder = make_functionBinder(std::forward<Func>(func), this);
            script_function boundFunc = binder.bind();
            
            // Wrap the function to handle type conversion for return values
            script_function wrappedFunc = wrapFunctionForTypeConversion<return_type>(std::move(boundFunc));
            
            std::vector<param_type_info> paramTypes = extract_parameter_types_with_info<Func>();

            // Auto-detect numeric operator overrides and set flag
            if (arity == 2 && isNumericOperator(name)) {
                if (hasNumericOperands(paramTypes)) {
                    setHasCustomNumericOps(true);
                }
            }

            // Check if this should be registered as an overload
            if (has_function(name)) {
                // Automatically convert to overloaded function with type info
                add_overloaded_function_with_full_types(name, arity, wrappedFunc, paramTypes);
            } else {
                // Register normally but store arity info for future overloading
                add_function_with_arity_and_full_types(name, wrappedFunc, arity, paramTypes);
            }
        }
        
    private:
        // Helper methods for numeric operator detection
        bool isNumericOperator(const std::string& name) const {
            return name == "+" || name == "-" || name == "*" || name == "/" || name == "%" ||
                   name == "<" || name == ">" || name == "<=" || name == ">=" || 
                   name == "==" || name == "!=" || name == "<=>";
        }
        
        bool hasNumericOperands(const std::vector<param_type_info>& paramTypes) const {
            if (paramTypes.size() != 2) return false;

            auto isNumericType = [](script_value_type t) {
                return t == script_value_type::jai_int_type ||
                       t == script_value_type::jai_float_type ||
                       t == script_value_type::jai_char_type ||
                       t == script_value_type::jai_bool_type;
            };
            
            return isNumericType(paramTypes[0].base_type) && isNumericType(paramTypes[1].base_type);
        }
        
        void setHasCustomNumericOps(bool value);

        // The single overloaded-function registration algorithm shared by every public entry point
        // (add_function*, add_overloaded_function*, add_variadic_function). createSetOnFirst is the
        // only policy difference between the entry points.
        void register_overload_impl(const std::string& name, size_t arity, script_function func,
                                    const std::vector<param_type_info>& paramTypes, bool createSetOnFirst);

    public:
        // Overloaded function registration
        void add_overloaded_function(const std::string& name, size_t argCount, script_function func);
        void add_overloaded_function_with_full_types(const std::string& name, size_t argCount, script_function func, const std::vector<param_type_info>& paramTypes);
        void add_function_with_arity_and_full_types(const std::string& name, script_function func, size_t arity, const std::vector<param_type_info>& paramTypes);
        
        // Class registration
        // Register a class with its type information
        template<typename T>
        void add_class(const std::string& name, std::shared_ptr<class_definition> classDef) {
            add_class_impl(name, classDef);
            register_type_name_impl(typeid(T).name(), name);
            register_class_by_type(std::type_index(typeid(T)), classDef);
        }
        
        // Template type registration for parsing
        // Registers base template names (e.g., "Point" from "Point<int>") 
        // so the parser knows which identifiers can be followed by template syntax
        void register_template_type(const std::string& baseTemplateName);
        
        // Get all registered template types for parser
        std::unordered_set<std::string> get_registered_template_types() const;
        
        // Get class definition by name
        std::shared_ptr<class_definition> get_class_definition(const std::string& name) const;

        // Get class definition by type_id (faster than string lookup)
        std::shared_ptr<class_definition> get_class_definition(uint64_t type_id) const;

        // Check if a type is registered (by name)
        bool is_type_registered(const std::string& name) const {
            return get_class_definition(name) != nullptr;
        }

        // Get registered script name for a C++ type
        template<typename T>
        std::string get_registered_name() const;

        // Check if type T has been registered via dynamic_binder
        template<typename T>
        bool has_registered_class() const {
            return get_class_definition_by_type(std::type_index(typeid(T))) != nullptr;
        }

        template<typename T>
        void bind_static_type();  // Implemented in static_binder_impl.hpp

        template<typename... Ts>
        void bind_static_types() {
            (bind_static_type<Ts>(), ...);
        }

        // Enhanced Conversion System
        // Get the conversion manager for this engine
        conversions::conversion_manager get_conversion_manager();
        
        // Register standard vector and map conversions
        void add_standard_conversions();
        
        // Register vector conversion for a specific type
        template<typename T>
        void add_vector_conversion() {
            auto manager = get_conversion_manager();
            manager.add_vector_conversion<T>();
        }
        
        // Register map conversion for specific key/value types
        template<typename K, typename V>
        void add_map_conversion() {
            auto manager = get_conversion_manager();
            manager.add_map_conversion<K, V>();
        }
        
        // Register bound_array conversion for a specific type
        template<typename T>
        void add_bound_array_conversion() {
            auto manager = get_conversion_manager();
            manager.add_bound_array_conversion<T>();
        }
        
        // Register bound_map conversion for specific key/value types
        template<typename K, typename V>
        void add_bound_map_conversion() {
            auto manager = get_conversion_manager();
            manager.add_bound_map_conversion<K, V>();
        }
        
        // Register custom conversion for any type
        template<typename T>
        void add_custom_conversion(
            std::function<T(const script_value&)> from_func,
            std::function<script_value(const T&)> to_func
        );
        
        // Type conversion registration
        // Register a conversion between JaiScript types with a cost for overload resolution
        // Lower costs are preferred (0 = exact match, 1 = promotion, 2 = standard conversion, etc.)
        void register_type_conversion(script_value_type from, script_value_type to, int cost, 
                                   std::function<script_value(const script_value&)> converter);
        
        // Template version for C++ type safety using std::is_convertible
        template<typename From, typename To>
        void register_type_conversion(int cost = -1) {
            static_assert(std::is_convertible_v<From, To>, 
                         "Types must be convertible according to C++ rules");
            
            script_value_type fromType = mapCppTypeToValueType<From>();
            script_value_type toType = mapCppTypeToValueType<To>();
            
            // Auto-determine cost if not specified
            if (cost < 0) {
                if constexpr (std::is_same_v<From, To>) {
                    cost = 0; // Exact match
                } else if constexpr (std::is_integral_v<From> && std::is_floating_point_v<To>) {
                    cost = 1; // Promotion
                } else if constexpr (std::is_floating_point_v<From> && std::is_integral_v<To>) {
                    cost = 2; // Standard conversion (may lose precision)
                } else {
                    cost = 3; // User-defined conversion
                }
            }
            
            register_type_conversion(fromType, toType, cost, 
                [](const script_value& v) { 
                    return script_value(static_cast<To>(v.as<From>())); 
                });
        }

        // Variable access
        script_value get_variable(const std::string& name) const;
        bool has_variable(const std::string& name) const;
        bool has_function(const std::string& name) const;
        bool is_type_name(const std::string& name) const;
        
        // Get the global environment (for internal use)
        std::shared_ptr<environment> get_global_environment() const;
        
        // Performance optimization
        void set_has_custom_numeric_operators(bool value);

        // Get registered type name from typeid
        std::string get_registered_type_name(const std::string& typeIdName) const;
        
        // Convert a registered type to value
        script_value convert_to_value(const std::type_info& type, const void* obj) const;
        
        // Backend configuration
        void set_backend(backend_type type);
        void set_backend(std::unique_ptr<execution_backend> backend);
        backend_type get_backend_type() const;
        std::string get_backend_name() const;
        execution_backend* get_execution_backend() const;

        // Engine-owned script namespace registry (namespace_id -> data); survives across backends
        std::unordered_map<uint64_t, std::shared_ptr<script_namespace_data>>& script_namespaces();

        // Get string symbolizer for interning identifiers
        string_symbolizer* get_symbolizer();

        // Intern a string and return its unique ID (const-safe)
        uint64_t symbolize(std::string_view str) const;

        // Format a checked_result error to a human-readable string
        // Resolves symbol IDs to their string representations
        template<typename T>
        std::string format_error(const checked_result<T>& result) {
            return jai::format_error(result, *get_symbolizer());
        }

        // Register C++ type converters for custom types
        template<typename T>
        void register_type_converter(const std::string& type_name) {
            // Register a converter that creates a script_value from this type
            engine* eng = this;
            auto type_id = get_symbolizer()->intern(type_name);
            auto toValue = [type_name, type_id, eng](const T& obj) -> script_value {
                auto sharedObj = std::make_shared<T>(obj);
                return script_value::make_cpp_object(type_name, type_id, std::static_pointer_cast<void>(sharedObj), eng);
            };
            
            // Store this converter (implementation will handle the storage)
            register_type_converter_impl(typeid(T), 
                [toValue](const void* obj) -> script_value {
                    return toValue(*static_cast<const T*>(obj));
                });
        }
        
        // Get class definition by type index
        std::shared_ptr<class_definition> get_class_definition_by_type(const std::type_index& type) const;
        
        // Get the conversion registry - shareable between engines
        std::shared_ptr<conversions::conversion_registry> get_conversion_registry() const;
        
        // Set a shared conversion registry (for sharing between engine instances)
        void set_conversion_registry(std::shared_ptr<conversions::conversion_registry> registry);
        
        // Get the serialization registry for this engine
        serialization::serialization_registry& get_serialization_registry();
        
        // Get the class registry for this engine
        class_registry& get_class_registry();

        // === TYPE INFO INTERNING ===
        // Get or create type_info for basic types (fast O(1) access for common types)
        type_info* get_type_info_int();
        type_info* get_type_info_float();
        type_info* get_type_info_string();
        type_info* get_type_info_bool();
        type_info* get_type_info_char();
        type_info* get_type_info_void();
        type_info* get_type_info_invalid();

        // Get or create type_info for arrays/maps (interned by type_id)
        type_info* get_type_info_array(type_info* element_type);
        type_info* get_type_info_map(type_info* key_type, type_info* value_type);
        type_info* get_type_info_weak_ptr(type_info* pointee_type);
        type_info* get_type_info_shared_ptr(type_info* pointee_type);
        type_info* get_type_info_reference(type_info* referenced_type);
        type_info* get_type_info_function(type_info* return_type, const std::vector<type_info*>& arg_types);

        // Get or create type_info for object types (uses class_definition's type_info)
        type_info* get_type_info_object(const std::string& class_name);
        type_info* get_type_info_object(uint64_t type_id);

        // Generic helper to intern any type_info
        type_info* get_type_info(const type_info& temp);

        // Direct lookup by interned ID (fastest - O(1) with no string operations)
        type_info* get_type_info_by_id(uint64_t type_id);

        // Lookup by canonical name (interns the name first, then does ID lookup)
        type_info* get_type_info_by_name(const std::string& canonical_name);

        // Template method to get type_info for any C++ type
        template<typename T>
        type_info* get_type_info_for_cpp_type() {
            if (!get_symbolizer()) return nullptr;
            type_info temp = type_info::make<T>(*get_symbolizer());
            return get_type_info(temp);
        }

        // Reference support for function parameters
        script_value try_create_reference(size_t arg_index, const script_value& fallback);

        // Script errors surfacing at the C++ boundary (converted callbacks, signal
        // receivers) have no error-value channel and must not throw into arbitrary game
        // callsites; they route here instead. Default sink is std::cerr; the host app
        // installs its own logger. The message includes the script stack trace if captured.
        void set_script_error_handler(std::function<void(const std::string&)> a_handler);
        void report_script_error(const std::string& a_message);

        // Shelve/restore the in-flight script call's argument metadata around an external
        // (C++) invocation of a script function - see external_call_guard below.
        void push_external_call_scope();
        void pop_external_call_scope();

        // RAII scope for C++ code invoking script functions directly (converted callbacks,
        // signal receivers): without it, reference parameters would bind against whatever
        // script call happens to be in flight.
        class external_call_guard {
        public:
            explicit external_call_guard(engine* a_engine) : engine_(a_engine) {
                if (engine_) { engine_->push_external_call_scope(); }
            }
            ~external_call_guard() {
                if (engine_) { engine_->pop_external_call_scope(); }
            }
            external_call_guard(const external_call_guard&) = delete;
            external_call_guard& operator=(const external_call_guard&) = delete;
        private:
            engine* engine_;
        };

        // Output stream redirection
        // Set a custom output stream for print() and related functions
        // Pass nullptr to reset to std::cout
        void set_output_stream(std::shared_ptr<std::ostream> stream);

        // Get the current output stream (defaults to std::cout)
        std::ostream& get_output_stream();

        // Include/Import path management
        void include_paths(const std::vector<std::string>& paths);
        void add_include_path(const std::string& path);
        void clear_include_paths();
        std::vector<std::string> get_include_paths() const;
        
        // Import behavior configuration
        enum class import_behavior {
            file_timestamp, // Re-import if file modified (DEFAULT)
            once,           // Import only once, ignore file changes
            always         // Treat import as include
        };
        
        void set_import_behavior(import_behavior behavior);
        import_behavior get_import_behavior() const;
        
        // Import management
        void reset_imports();
        void reset_import(const std::string& path);
        bool is_imported(const std::string& path) const;
        std::vector<std::string> get_imported_files() const;
        
        // Execute a file with import tracking
        script_value execute_import(const std::string& resolved_path);

        // Parameter storage for function calls (replaces thread_local)
        detail::parameter_storage* get_current_parameter_storage() const;
        void set_current_parameter_storage(detail::parameter_storage* storage);

        // Rank same-arity C++ method overload candidates (each a parameter-type signature) against
        // a call's actual arguments (args[0] = 'this'); returns the best candidate index, or -1.
        // Shares the free-function overload scorer so resolution stays consistent.
        int select_cpp_overload(const std::vector<script_value>& args,
                                const std::vector<std::vector<param_type_info>>& candidateParamTypes) const;

    private:
        struct implementation;
        std::unique_ptr<implementation> impl;

        // Single post-parse execution pipeline shared by execute(string) and
        // execute(jaibite&): every string execute exercises the same machinery a
        // jaibite does, so the pre-parsed path can never drift semantically.
        script_value execute_parsed(const std::vector<declaration_ptr>& declarations,
                                    std::shared_ptr<void>& compiled_slot,
                                    const instance_variables* instanceVars);

        // The one lex+parse+run entry: stamps `sourcePath` onto every AST node and keys
        // the parse cache on (sourcePath, content). Not public — attributing raw content
        // to a path is debugger plumbing, a sharp edge for callers. Reached via execute()
        // ("<script>"), execute_file() (the file path), and include/import (resolved path).
        script_value execute_source(const std::string& scriptContent,
                                    const instance_variables& instanceVars,
                                    const std::string& sourcePath);

        // Lazily constructs + wires the selected backend on first use, so creating an
        // engine (or switching backend type) costs nothing until a script actually runs.
        execution_backend* backend() const;

        // Static-check a jaibite once (creation/load/first checked execute) and gate
        // under strict; source is only available at creation time.
        void check_bite(jai::jaibite& bite, const std::string* source);

        void initialize_engine_reference();
        void wire_backend();
        void add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef);
        void register_type_name_impl(const std::string& typeIdName, const std::string& friendlyName);
        void register_type_converter_impl(const std::type_info& type, std::function<script_value(const void*)> converter);
        void register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef);
        
        // Allow dynamic_binder to access implementation details
        template<typename T> friend class dynamic_binder;
        // Allow function_binder to access conversion registry
        template<typename T> friend struct detail::value_converter;
        // Allow interpreter to access implementation for include/import
        friend class interpreter;
        // Allow the vm backend the same access (include/import call execute_source)
        friend class vm::vm_backend;
        
        // Helper to wrap functions with type conversion
        template<typename ReturnType>
        script_function wrapFunctionForTypeConversion(script_function func) {
            // For user-defined types, type conversion is handled by function binder
            // We just return the function as-is since value_converter in function_binder
            // already handles the conversion properly
            return func;
        }
        
        // Helper to extract parameter types from a function signature
        template<typename Func>
        std::vector<script_value_type> extractParameterTypes() {
            using traits = detail::function_traits<std::decay_t<Func>>;
            return extractParameterTypesImpl<typename traits::argument_types>(std::make_index_sequence<traits::arity>{});
        }
        
        template<typename ArgsTuple, size_t... Is>
        std::vector<script_value_type> extractParameterTypesImpl(std::index_sequence<Is...>) {
            return {mapCppTypeToValueType<std::tuple_element_t<Is, ArgsTuple>>()...};
        }
        
        // Map C++ types to JaiScript ValueType
        template<typename T>
        static constexpr script_value_type mapCppTypeToValueType() {
            using decay_t = std::decay_t<T>;
            
            if constexpr (std::is_same_v<decay_t, int> || std::is_same_v<decay_t, int64_t> || 
                          std::is_same_v<decay_t, script_int>) {
                return script_value_type::jai_int_type;
            } else if constexpr (std::is_same_v<decay_t, float> || std::is_same_v<decay_t, double> || 
                                 std::is_same_v<decay_t, script_float>) {
                return script_value_type::jai_float_type;
            } else if constexpr (std::is_same_v<decay_t, bool> || std::is_same_v<decay_t, script_bool>) {
                return script_value_type::jai_bool_type;
            } else if constexpr (std::is_same_v<decay_t, char> || std::is_same_v<decay_t, script_char>) {
                return script_value_type::jai_char_type;
            } else if constexpr (std::is_same_v<decay_t, std::string> || std::is_same_v<decay_t, script_string>) {
                return script_value_type::jai_string_type;
            } else if constexpr (is_specialization_v<decay_t, std::vector>) {
                return script_value_type::jai_array_type;
            } else if constexpr (is_specialization_v<decay_t, bound_array>) {
                // bound_array is a zero-copy wrapper for arrays
                return script_value_type::jai_array_type;
            } else if constexpr (is_specialization_v<decay_t, std::map>) {
                return script_value_type::jai_map_type;
            } else if constexpr (is_specialization_v<decay_t, bound_map>) {
                // bound_map is a zero-copy wrapper for maps
                return script_value_type::jai_map_type;
            } else {
                // For unknown types, return Object (could be improved)
                return script_value_type::jai_object_type;
            }
        }

        template<typename Func>
        std::vector<param_type_info> extract_parameter_types_with_info() {
            using traits = detail::function_traits<std::decay_t<Func>>;
            return extract_parameter_types_with_info_impl<typename traits::argument_types>(std::make_index_sequence<traits::arity>{});
        }

        template<typename ArgsTuple, size_t... Is>
        std::vector<param_type_info> extract_parameter_types_with_info_impl(std::index_sequence<Is...>) {
            return {map_cpp_type_to_param_info<std::tuple_element_t<Is, ArgsTuple>>()...};
        }

    public:
        // Map C++ types to param_type_info (with type_index for object types). Public so the
        // binding layer (dynamic_binder) can build per-overload C++ parameter signatures.
        template<typename T>
        static param_type_info map_cpp_type_to_param_info() {
            using decay_t = std::decay_t<T>;

            if constexpr (std::is_same_v<decay_t, script_value>) {
                // Wildcard: a script_value parameter accepts ANY argument type (a generic binding,
                // e.g. pair(script_value, script_value)). jai_null_type is the sentinel — no real
                // C++ binding parameter is genuinely null-typed — and the overload scorer gives it a
                // small fixed cost so a concrete-typed overload still ranks ahead.
                return param_type_info(script_value_type::jai_null_type);
            } else if constexpr (std::is_same_v<decay_t, int> || std::is_same_v<decay_t, int64_t> ||
                          std::is_same_v<decay_t, script_int>) {
                return param_type_info(script_value_type::jai_int_type);
            } else if constexpr (std::is_same_v<decay_t, float> || std::is_same_v<decay_t, double> ||
                                 std::is_same_v<decay_t, script_float>) {
                return param_type_info(script_value_type::jai_float_type);
            } else if constexpr (std::is_same_v<decay_t, bool> || std::is_same_v<decay_t, script_bool>) {
                return param_type_info(script_value_type::jai_bool_type);
            } else if constexpr (std::is_same_v<decay_t, char> || std::is_same_v<decay_t, script_char>) {
                return param_type_info(script_value_type::jai_char_type);
            } else if constexpr (std::is_same_v<decay_t, std::string> || std::is_same_v<decay_t, script_string>) {
                return param_type_info(script_value_type::jai_string_type);
            } else if constexpr (is_specialization_v<decay_t, std::vector>) {
                return param_type_info(script_value_type::jai_array_type);
            } else if constexpr (is_specialization_v<decay_t, bound_array>) {
                return param_type_info(script_value_type::jai_array_type);
            } else if constexpr (is_specialization_v<decay_t, std::map>) {
                return param_type_info(script_value_type::jai_map_type);
            } else if constexpr (is_specialization_v<decay_t, bound_map>) {
                return param_type_info(script_value_type::jai_map_type);
            } else if constexpr (is_specialization_v<decay_t, std::shared_ptr>) {
                using element_type = typename decay_t::element_type;
                return param_type_info(script_value_type::jai_shared_ptr_type, std::type_index(typeid(element_type)));
            } else {
                return param_type_info(script_value_type::jai_object_type, std::type_index(typeid(decay_t)));
            }
        }
    };

    // Resolve an include/import path against the engine's include paths. Shared by the
    // interpreter and vm backends (defined in interpreter.cpp) so path resolution stays
    // single-sourced.
    checked_result<std::string> resolve_include_path(const std::string& path, engine* engine_ptr);

} // namespace jai

#include "engine_impl.hpp"
#include "conversion_registry_templates.hpp"
#include "value_impl.hpp"

#endif // __JAISCRIPT_CORE_ENGINE_HPP__