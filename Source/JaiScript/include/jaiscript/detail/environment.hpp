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
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {}

        // Standard environment with parent
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::standard),
              this_object_(std::monostate{}, static_cast<engine*>(nullptr)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            validate_parent_chain(parent);
        }

        // Method environment constructor (with 'this' object)
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, script_value this_obj)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::method),
              this_object_(std::move(this_obj)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            validate_parent_chain(parent);
        }

        // Static method environment constructor (with class definition)
        environment(std::shared_ptr<environment> parent, string_symbolizer* symbolizer, std::shared_ptr<class_definition> class_def)
            : parent_(parent), symbolizer_(symbolizer), kind_(env_kind::static_method),
              class_def_(class_def),
              this_object_(std::monostate{}, static_cast<engine*>(nullptr)),
              bound_method_storage_(std::monostate{}, static_cast<engine*>(nullptr)) {
            validate_parent_chain(parent);
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

        // Static method environment accessors (only valid when kind == static_method)
        std::shared_ptr<class_definition> get_class_definition() const { return class_def_; }

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
        }

    protected:
        void validate_parent_chain(std::shared_ptr<environment> /*new_parent*/) const {
            // Intentionally empty - enable JAISCRIPT_DEBUG_ENVIRONMENT_CYCLES for cycle detection
        }

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

        // Storage for bound methods (used by method and static_method kinds)
        mutable script_value bound_method_storage_;

        // Internal helpers for kind-specific lookups
        // These check this_object_ fields (for method kind) or class_def_ static fields (for static_method kind)
        script_value* get_this_field_ptr(uint64_t id);
        script_value* get_static_field_ptr(uint64_t id);
    };

    // Create a bound method - binds 'this' as the first argument
    script_value make_bound_method(const script_value& this_obj, const script_value& method);

    // Clone a value for field assignment - respects shared_ptr reference semantics
    inline script_value clone_for_assignment(const script_value& value) {
        // shared_ptr types should NOT be cloned - they have reference semantics
        auto type_info = value.get_type_info();
        if (type_info && type_info->base_type == script_value_type::jai_shared_ptr_type) {
            return value;  // Share reference, don't clone
        }
        return value.clone();
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

        std::string_view function_name;
        const ast_node* current_node = nullptr;

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
            this_object_ptr = std::make_unique<script_value>(std::move(this_obj));
            is_method = true;
        }

        /// Get 'this' object (only valid if is_method is true)
        script_value& get_this() {
            return *this_object_ptr;
        }

        const script_value& get_this() const {
            return *this_object_ptr;
        }

        /// Liveness token for references bound to this frame's slot storage: it dies
        /// with the frame (or its pooled record's recycling), so an escaped reference
        /// errors cleanly instead of dangling into freed/reused slot storage.
        std::shared_ptr<environment> ref_anchor;

        const std::shared_ptr<environment>& ensure_ref_anchor(string_symbolizer* symbolizer) {
            if (!ref_anchor) {
                ref_anchor = std::make_shared<environment>(symbolizer);
            }
            return ref_anchor;
        }
    };

    /// Per-argument call-site metadata for reference-parameter binding. When the
    /// argument was a bare identifier resolving to a caller frame slot, the slot
    /// coordinates are recorded too (env lookup alone would miss slot locals and walk
    /// to an unrelated same-named outer variable). The vm records the caller's
    /// call_frame directly (its records are address-stable); the interpreter records
    /// an index into its call_stack_ (whose frames move on vector growth).
    struct arg_ref_metadata {
        uint64_t symbol_id = UINT64_MAX;
        environment* env = nullptr;
        call_frame* caller_locals = nullptr;
        size_t caller_frame_index = SIZE_MAX;
        size_t slot = SIZE_MAX;

        arg_ref_metadata() = default;
        arg_ref_metadata(uint64_t symbol, environment* environment_ptr)
            : symbol_id(symbol), env(environment_ptr) {}
    };

    // Script-defined function storage
    struct script_defined_function {
        std::string_view name;  // Points to symbolizer storage (permanent)
        std::vector<parameter> parameters;
        type_info_ptr return_type;
        std::shared_ptr<block_stmt> body;
        std::shared_ptr<environment> closure_env;  // capture environment for closures
        size_t local_count = 0;  // Total slots needed (params + locals) for stack allocation

        // Backend-owned compiled artifact for the body (the vm caches its chunk here so a
        // call skips the body-keyed cache lookup; the interpreter ignores it). mutable:
        // filled through const refs on the call path; per-engine, single backend.
        mutable std::shared_ptr<void> backend_body_cache;

        // Precomputed so ref-free call sites can skip the per-call arg-metadata build
        bool has_reference_parameters = false;

        script_defined_function(std::string_view n, std::vector<parameter> params,
                            type_info_ptr retType, std::shared_ptr<block_stmt> b,
                            std::shared_ptr<environment> env = nullptr, size_t slots = 0)
            : name(n), parameters(std::move(params)), return_type(retType), body(b), closure_env(env), local_count(slots) {
            for (const auto& p : parameters) {
                if (p.is_reference) { has_reference_parameters = true; break; }
            }
        }
    };

} // namespace jai

#endif // __JAISCRIPT_DETAIL_ENVIRONMENT_HPP__
