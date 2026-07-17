#pragma once

#ifndef __JAISCRIPT_DETAIL_ENVIRONMENT_HPP__
#define __JAISCRIPT_DETAIL_ENVIRONMENT_HPP__

#include "ast.hpp"
#include <jaiscript/core/value.hpp>
#include <jaiscript/core/checked_result.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jai {

    class string_symbolizer;
    class class_definition;
    class class_instance;

    namespace detail {
        // True when the current thread's remaining native stack is under the safety
        // margin - recursive native call paths raise the catchable recursion error
        // instead of dying on a hardware stack overflow (implemented in environment.cpp)
        bool native_stack_low() noexcept;
    }

    // environment for storing variables in a scope
    // Environment kind - replaces virtual dispatch with simple enum check
    enum class env_kind {
        standard,       // Regular scope (function, block, global)
        method,         // Instance method - has 'this' object for field access
        static_method   // Static method - has class_def for static field access
    };

    class environment {
    public:
        // Standard environment constructor
        environment(string_symbolizer* symbolizer)
            : symbolizer_(symbolizer), kind_(env_kind::standard),
              this_object_(std::monostate{}, static_cast<engine*>(nullptr)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            bump_env_epoch();
        }

        // Standard environment with parent
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::standard),
              this_object_(std::monostate{}, static_cast<engine*>(nullptr)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            validate_parent_chain(parent);
            bump_env_epoch();
        }

        // Method environment constructor (with 'this' object)
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, script_value this_obj)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::method),
              this_object_(std::move(this_obj)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            validate_parent_chain(parent);
            bump_env_epoch();
        }

        // Static method environment constructor (with class definition)
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, std::shared_ptr<class_definition> class_def)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::static_method),
              class_def_(class_def),
              this_object_(std::monostate{}, static_cast<engine*>(nullptr)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)),
              access_context_(class_def.get()) {
            validate_parent_chain(parent);
            bump_env_epoch();
        }

        ~environment() = default;

        // Reference tracking for proper reference parameter support
        struct ref_info {
            uint64_t target_id;  // Symbol ID of the target variable
            std::shared_ptr<environment> target_env;  // environment containing the target
        };

        // Define a variable in the current scope
        void define(const std::string& name, const script_value& value);
        void define(const std::string& name, script_value&& value);
        void define(uint64_t id, const script_value& value);
        void define(uint64_t id, script_value&& value);

        // Get a variable value (searches parent scopes, then this/static fields based on kind)
        // Returns checked_result to avoid exceptions in regular control flow
        checked_result<script_value> get(const std::string& name) const;
        checked_result<script_value> get(uint64_t id) const;

        // Get a reference to variable value (for avoiding copies)
        // Returns checked_result to avoid exceptions in regular control flow
        checked_result<std::reference_wrapper<const script_value>> get_ref(const std::string& name) const;
        checked_result<std::reference_wrapper<const script_value>> get_ref(uint64_t id) const;
        checked_result<std::reference_wrapper<script_value>> get_ref(const std::string& name);
        checked_result<std::reference_wrapper<script_value>> get_ref(uint64_t id);

        // Assign to an existing variable (searches parent scopes, then this/static fields based on kind)
        // Returns checked_result to avoid exceptions in regular control flow
        [[nodiscard]] checked_result<void> assign(const std::string& name, const script_value& value);
        [[nodiscard]] checked_result<void> assign(const std::string& name, script_value&& value);
        [[nodiscard]] checked_result<void> assign(uint64_t id, const script_value& value);
        [[nodiscard]] checked_result<void> assign(uint64_t id, script_value&& value);

        // Check if variable exists in current or parent scopes
        bool contains(const std::string& name) const;
        bool contains(uint64_t id) const;

        // Get a pointer to a value (for references)
        // Checks local storage, parent chain, and this/static fields based on kind
        script_value* get_value_ptr(uint64_t id);

        // get_value_ptr WITHOUT this env's kind fallbacks (this/static fields):
        // non-null exactly when contains(id) — the store path's single-walk resolve
        script_value* get_env_var_ptr(uint64_t id);

        // Get all variables in this scope (not including parent scopes)
        // Returns a map with string_view keys pointing into the symbolizer (stable until engine destruction)
        std::unordered_map<std::string_view, script_value> get_local_variables() const;

        // Get all variables including parent scopes
        std::unordered_map<std::string_view, script_value> get_all_variables() const;

        // Reset environment for reuse (optimization helper)
        void reset(std::shared_ptr<environment> new_parent);

        // Reset for method environment reuse (sets kind to method)
        void reset_as_method(std::shared_ptr<environment> parent, script_value this_obj);

        // Reset for static method environment reuse (sets kind to static_method)
        void reset_as_static_method(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def);

        // Environment kind accessors
        env_kind get_kind() const { return kind_; }
        bool is_method_env() const { return kind_ == env_kind::method; }
        bool is_static_method_env() const { return kind_ == env_kind::static_method; }

        // Method environment accessors (only valid when kind == method)
        const script_value& get_this_object() const { return this_object_; }

        // Clear just the this_object without clearing local values
        // Called when a method/constructor returns to ensure timely destruction
        void clear_this_reference() {
            this_object_ = script_value::make_null(static_cast<engine*>(nullptr));
            bound_method_storage_ = script_value::make_null(static_cast<engine*>(nullptr));
        }

        // Sticky method-scope rebind (vm fast path): swap the receiver on a persistent
        // per-dispatch method env between calls. Deliberately NO epoch bump - 'this' is
        // never memoized by the vm lookup caches (vm_storage_lookup marks it uncacheable)
        // and nothing name-resolvable changed.
        void rebind_method_this(script_value this_obj) { this_object_ = std::move(this_obj); }

        // vm ip-cache pin: identity+epoch of this env are stable across calls (persistent
        // method scope parented on the global env), so env_lookup_cached may memoize
        // against it exactly like the global env itself.
        bool vm_pinned_scope() const noexcept { return vm_pinned_scope_; }
        void set_vm_pinned_scope(bool pinned) noexcept { vm_pinned_scope_ = pinned; }

        // vm ip-cache transparency: a standard scope env that has DEFINED nothing of its
        // own (local_ids_ — the flat memo map doesn't count) cannot shadow any name, so
        // lookups provably continue past it. Callers re-prove this per access: a
        // mid-body define flips it and the site falls back to the full walk.
        bool vm_transparent_for_lookup() const noexcept {
            return kind_ == env_kind::standard && local_ids_.empty();
        }
        environment* parent_raw() const noexcept { return parent_.get(); }

        // Static method environment accessors (only valid when kind == static_method)
        std::shared_ptr<class_definition> get_class_definition() const { return class_def_; }

        // Member-shadowing ladder (detail/member_scope.hpp): stable storage of a method
        // env's receiver, so closure frames can probe the creation-site 'this' without
        // copying it per identifier. Null for non-method envs.
        script_value* method_this_storage() { return kind_ == env_kind::method ? &this_object_ : nullptr; }

        // OWN-storage probe for the member-shadowing ladder's function-scope phase:
        // names DEFINED in this env only (local_ids_-gated) - never ancestor cache
        // entries (their provenance spans the whole chain), never kind fallbacks.
        script_value* get_own_value_ptr(uint64_t id) {
            if (local_ids_.count(id) == 0) return nullptr;
            auto it = flat_lookup_.find(id);
            return it != flat_lookup_.end() ? it->second : nullptr;
        }

        // Generation SERIAL for the vm's env-lookup inline caches (chunk.hpp
        // env_lookup_cache_entry): re-drawn from the per-engine monotonic counter on
        // construction, reset (pool reuse), clear, parent change, and define - the
        // events that can invalidate a cached pointer RESOLVED AGAINST THIS ENV.
        // Per-env (not the old global bump) on purpose: a method scope resetting
        // ~900x/tick must not invalidate entries cached against the global env
        // (measured cache poisoning; see the b377b5ac scope-elision commit). A serial
        // rather than a per-env counter because the allocator recycles env addresses:
        // {address, count} can repeat across lifetimes, {address, serial} cannot.
        // Cached pointers target storage in the entry's env or an ANCESTOR (a parent
        // can't be pool-recycled while its child chain is live; the global env is
        // engine-lifetime with in-place redefinition into stable deque storage).
        uint64_t local_epoch() const noexcept { return local_epoch_; }

        // Access-enforcement context: the class whose method/ctor body this scope env
        // executes (stamped by the backend at method-scope creation; nullptr elsewhere).
        // Lambdas inherit through the parent chain — the walk only runs on the
        // enforcement slow path (target class has nonpublic members). Raw pointer:
        // class_definitions are engine-lifetime and hot reload mutates them in place.
        void set_access_context(class_definition* cls) { access_context_ = cls; }
        class_definition* get_access_context() const { return access_context_; }
        const class_definition* find_access_context() const {
            for (const environment* env = this; env; env = env->parent_.get()) {
                if (env->access_context_) return env->access_context_;
            }
            return nullptr;
        }

        // Clear all values but keep parent chain intact (for proper scope cleanup)
        void clear_values();

        // Clear cached parent pointers (keeps local variable pointers which are stable)
        // Call on hot reload to invalidate stale pointers to object fields
        void clear_parent_cache();

        // Clear parent caches in this environment and all parents (for hot reload)
        void clear_all_parent_caches() {
            clear_parent_cache();
            if (parent_) {
                parent_->clear_all_parent_caches();
            }
        }

        // Get parent environment (needed for method_environment handling)
        std::shared_ptr<environment> get_parent() const { return parent_; }

        // Set parent environment with validation (use this instead of directly setting parent_)
        void set_parent(std::shared_ptr<environment> new_parent) {
            validate_parent_chain(new_parent);
            parent_ = new_parent;
            bump_env_epoch();
        }

        // The pure env-storage prefix of get_ref/get_value_ptr (this special-case, flat
        // caches, parent-chain walk) with provenance: cacheable is true only when the
        // pointer is a found-env LOCAL (stable deque storage), so the vm's per-instruction
        // lookup caches never memoize this/static-field fallbacks. nullptr = the prefix is
        // exhausted; the caller runs its original full lookup for the fallback tails.
        script_value* vm_storage_lookup(uint64_t id, bool& cacheable);

    protected:
        void validate_parent_chain(std::shared_ptr<environment> /*new_parent*/) const {
            // Intentionally empty - enable JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES for cycle detection
        }

        // Invalidate the vm's env-lookup inline caches (defined in environment.cpp:
        // symbolizer is forward-declared here)
        void bump_env_epoch() noexcept;

    private:
        // === Lazy-Cached Flat Lookup ===
        // Stable storage for THIS scope's values - deque doesn't invalidate pointers on push_back
        std::deque<script_value> local_storage_;

        // Lazy lookup cache - starts empty, populated on first access to each variable
        // Key: symbol ID, Value: pointer to script_value in some environment's local_storage_
        // mutable because const lookup methods cache for performance (doesn't change logical state)
        mutable std::unordered_map<uint64_t, script_value*> flat_lookup_;

        // Track which IDs we defined locally (for O(1) "is local" checks)
        std::unordered_set<uint64_t> local_ids_;

        // Parent pointer kept for: closure semantics, 'this' lookup, debugging
        std::shared_ptr<environment> parent_;
        string_symbolizer* symbolizer_;

        // === Unified environment fields (replaces method_environment / static_method_environment) ===
        env_kind kind_;

        // For method environments: the 'this' object for field access
        script_value this_object_;

        // For static method environments: the class definition for static field access
        std::shared_ptr<class_definition> class_def_;

        // Access-enforcement context (see set_access_context above). NOT owning: the
        // pooled-env reset paths null it so a recycled env never leaks a stale context.
        class_definition* access_context_ = nullptr;
        uint64_t local_epoch_ = 0;   // generation serial; 0 = never cache (see local_epoch())
        bool vm_pinned_scope_ = false;   // see vm_pinned_scope(); reset paths clear it

        // Storage for bound methods (used by method and static_method kinds)
        mutable script_value bound_method_storage_;

        // Internal helpers for kind-specific lookups
        // These check this_object_ fields (for method kind) or class_def_ static fields (for static_method kind)
        script_value* get_this_field_ptr(uint64_t id);
        script_value* get_static_field_ptr(uint64_t id);
    };

    // Create a bound method - binds 'this' as the first argument
    script_value make_bound_method(const script_value& this_obj, const script_value& method);

    // The ONE implicit-copy kernel for assignment-shaped store boundaries (decl from
    // lvalue, identifier/field/element assign, range-for bindings, destructuring,
    // by-value parameter binding), shared by both backends: plain values deep-copy
    // (script value semantics), shared_ptr<T>-marked values copy the HANDLE (guide
    // ch03: "assignment shares instead of deep-copying" - clone() stays the explicit
    // deep copy). A store site calling clone() directly is the GLOOM bug class:
    // `var e = arr[i]; e.hurt()` silently mutated a deep copy.
    inline script_value clone_for_assignment(const script_value& value) {
        // Element/map-entry/field reads arrive as reference wrappers - deref() reads
        // through them (and returns *this for plain values), so the shared_ptr marker
        // check sees the referent, not 'reference'. Copy-only consumption: safe to
        // use the bare-deref idiom (no move of the deref result back into value).
        const script_value& target = value.deref();
        // clone() of a plain scalar/string IS `storage_ = storage_` (strings share
        // storage by design) - return the copy inline instead of the out-of-line
        // clone() call every store-shaped boundary otherwise pays. These raw indexes
        // can't be borrow/bound/shared-marked, so no clone() special case is skipped.
        switch (target.raw_storage_index()) {
            case script_value::TYPEID_NULL:
            case script_value::TYPEID_INT:
            case script_value::TYPEID_FLOAT:
            case script_value::TYPEID_STRING:
            case script_value::TYPEID_CHAR:
            case script_value::TYPEID_BOOL:
                assert(target.has_valid_engine());   // clone() would throw; copies are for live values only
                return target;
            default:
                break;
        }
        auto type_info = target.get_type_info();
        if (type_info && type_info->base_type == script_value_type::jai_shared_ptr_type) {
            return target;  // Share the handle, don't clone
        }
        return target.clone();
    }

    /// Stack-based call frame for fast function parameter access - avoids hash map
    /// overhead for the most frequently accessed variables.
    /// Defined at namespace scope so coroutine_handle can reference it without circular includes.
    struct call_frame {
        std::shared_ptr<environment> closure_env;

        /// We store a pointer to avoid script_value default construction issues
        std::unique_ptr<script_value> this_object_ptr;
        bool is_method = false;

        std::shared_ptr<class_definition> static_class_def;
        bool is_static_method = false;

        // Member-shadowing scope (detail/member_scope.hpp): C++ unqualified-lookup
        // order makes class members resolve between the function's own scopes and the
        // definition/global environments. scope_floor = the first env in the chain
        // OUTSIDE the function scope (the member probe runs when a lookup crosses it);
        // scope_this/scope_static = what to probe. The interpreter stamps these at
        // frame push (the call branch knows the callee kind); the vm computes them
        // lazily from entry_env/locals on first use. 0=uncomputed, 1=none,
        // 2=instance, 3=static (member_scope_state).
        uint8_t scope_state = 0;
        environment* scope_floor = nullptr;
        script_value* scope_this = nullptr;
        class_definition* scope_static = nullptr;

        void reset_member_scope() noexcept {
            scope_state = 0;
            scope_floor = nullptr;
            scope_this = nullptr;
            scope_static = nullptr;
        }

        std::string_view function_name;
        const ast_node* current_node = nullptr;

        // Debug-only: the detail::script_defined_function this frame is executing, used to
        // name the slot-based locals below when a debugger stops here. Typed as void* to
        // avoid a header ordering dependency; the interpreter casts it back. Null for frames
        // not pushed by call_function. One pointer store per call — no measurable cost.
        const void* debug_function = nullptr;

        /// O(1) access by slot index - much faster than hash map or linear search
        std::vector<script_value> locals;

        /// Invalid slot constant - SIZE_MAX naturally fails bounds check
        static constexpr size_t INVALID_SLOT = SIZE_MAX;

        script_value* get_local(size_t slot) noexcept {
            if (slot < locals.size()) {
                return &locals[slot];
            }
            return nullptr;
        }

        const script_value* get_local(size_t slot) const noexcept {
            if (slot < locals.size()) {
                return &locals[slot];
            }
            return nullptr;
        }

        void set_local(size_t slot, script_value value) {
            if (slot == locals.size()) {
                locals.push_back(std::move(value));
            } else if (slot < locals.size()) {
                locals[slot] = std::move(value);
            } else {
                // slot > size: a lower-numbered slot belongs to a declaration in a
                // block that was not executed (e.g. an untaken if-branch). The parser
                // assigns slots monotonically across the whole function body, so such
                // gaps are legal. Grow the locals vector with null placeholders so the
                // target slot exists, then store the value. Placeholders carry the same
                // engine context as the value being stored.
                locals.reserve(slot + 1);
                while (locals.size() < slot) {
                    locals.emplace_back(std::monostate{}, value.get_engine());
                }
                locals.push_back(std::move(value));
            }
        }

        void reserve_locals(size_t count) {
            if (count > 0) {
                locals.reserve(count);
            }
        }

        /// Number of live local slots in this frame (used by closure capture to
        /// identify slots that belong to this frame vs. an inner lambda's scope).
        size_t local_count() const noexcept { return locals.size(); }

        void set_this(script_value this_obj) {
            // Reused frames keep the allocation (the vm's pooled records null the VALUE on
            // pop, not the pointer) - one heap round trip per method call saved
            if (this_object_ptr) {
                *this_object_ptr = std::move(this_obj);
            } else {
                this_object_ptr = std::make_unique<script_value>(std::move(this_obj));
            }
            is_method = true;
        }

        /// Get 'this' object (only valid if is_method is true)
        script_value& get_this() {
            return *this_object_ptr;
        }

        const script_value& get_this() const {
            return *this_object_ptr;
        }

    };

    namespace detail {
        /// Call-site context handed from the caller's invoke to the callee's parameter
        /// binding (a single save/restored pointer, consumed once): the argument
        /// expressions live in the caller's AST and outlive the call; the caller's
        /// environment is the callee's previousEnv at bind time.
        struct call_site_context {
            const std::vector<expression_ptr>* arg_exprs = nullptr;
        };
    }

    // Script-defined function storage. Parameters live behind a shared immutable vector:
    // every function value minted from the same AST node shares ONE storage (the vm's
    // compile-time protos always did this implicitly; the interpreter's per-execution
    // lambda/method/namespace mints used to deep-copy the vector each time). parameter's
    // mutable symbol_id/slot_index caches are warmed single-threaded (parse or the
    // parallel admission walk), so sharing is read-safe; parallel workers still take
    // private copies (provision_worker) so their lazy fills never cross threads.
    struct script_defined_function {
        std::string_view name;  // Points to symbolizer storage (permanent)
        std::shared_ptr<const std::vector<parameter>> shared_parameters;
        type_info_ptr return_type;
        std::shared_ptr<block_stmt> body;
        std::shared_ptr<environment> closure_env;  // capture environment for closures
        size_t local_count = 0;  // Total slots needed (params + locals) for stack allocation

        // Backend-owned compiled artifact for the body (the vm caches its chunk here so a
        // call skips the body-keyed cache lookup; the interpreter ignores it). mutable:
        // filled through const refs on the call path; per-engine, single backend.
        mutable std::shared_ptr<void> backend_body_cache;

        // Backend-cached return-type classification (vm flat-stack stage 1): values are
        // vm_backend's return_conv enum, 0 = not yet classified. Derived purely from
        // return_type on first call; mutable like backend_body_cache (workers hold
        // private function copies, so lazy fills never cross threads). Interpreter
        // ignores it.
        mutable uint8_t backend_return_conv = 0;

        // Precomputed so ref-free call sites can skip the per-call arg-metadata build
        bool has_reference_parameters = false;

        const std::vector<parameter>& parameters() const noexcept { return *shared_parameters; }

        script_defined_function(std::string_view n, std::vector<parameter> params,
                            type_info_ptr retType, std::shared_ptr<block_stmt> b,
                            std::shared_ptr<environment> env = nullptr, size_t slots = 0)
            : script_defined_function(n, std::make_shared<const std::vector<parameter>>(std::move(params)),
                                      retType, std::move(b), std::move(env), slots) {}

        // Sharing constructor: node-cached parameter storage, zero per-mint copies
        script_defined_function(std::string_view n, std::shared_ptr<const std::vector<parameter>> params,
                            type_info_ptr retType, std::shared_ptr<block_stmt> b,
                            std::shared_ptr<environment> env = nullptr, size_t slots = 0)
            : name(n), shared_parameters(std::move(params)), return_type(retType), body(std::move(b)), closure_env(std::move(env)), local_count(slots) {
            for (const auto& p : *shared_parameters) {
                if (p.is_reference) { has_reference_parameters = true; break; }
            }
        }
    };

} // namespace jai

#endif // __JAISCRIPT_DETAIL_ENVIRONMENT_HPP__
