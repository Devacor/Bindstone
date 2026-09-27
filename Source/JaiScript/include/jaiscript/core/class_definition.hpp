#pragma once

#ifndef __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__
#define __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__

#include "engine.hpp"
#include "native_cast.hpp"
#include <jaiscript/detail/fault_injection.hpp>
#include <jaiscript/detail/numeric_conversion.hpp>
#include <jaiscript/detail/container_enforce.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/slot_site.hpp>
#include <set>
#include <algorithm>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>

namespace jai {

class function_decl;
class environment;
class class_definition;

namespace detail {
    // Typed-slot conversion table (defined below class_definition): the ONE table for every
    // declared-type slot; the field store and the C++ set_field API enter through these.
    inline checked_result<script_value> field_store_convert(const script_value& value, const type_info_ptr& declared, engine* eng,
                                                            uint64_t field_id = 0, const class_definition* owner = nullptr,
                                                            bool is_static = false);
    inline std::optional<script_value> field_store_convert_no_script(const script_value& value, const type_info_ptr& declared, engine* eng);
    // Overload-miss text (docs/diagnostics.md family C), defined below with the slot texts
    inline error_propagator overload_miss(std::error_code ec, uint64_t name_id, const script_value* args, size_t argc,
                                          engine* eng, bool constructor = false);
}

enum class access_level {
    public_access,
    private_access,
    protected_access
};

enum class delegation_type {
    none,
    same_class,
    base_class
};

class class_definition;

class class_instance : public std::enable_shared_from_this<class_instance> {
public:
    class_instance(std::string_view class_name, engine* eng = nullptr);
    struct interned_name_t {};
    // a name already in the engine's interned table (a class definition's cached view): no copy, no lookup
    class_instance(interned_name_t, std::string_view interned, engine* eng)
        : class_name_(interned), engine_(eng), invalid_value_(std::monostate{}, eng) {}

    ~class_instance();

    // Enforcing write (C++ API surface): converts to the field's declared type like '='
    // on locals, throws runtime_error on an impossible conversion. Null passes through
    // (typed-null placeholders: create_instance defaults, uninitialized fields).
    // Defined out-of-line (needs class_definition + the conversion kernel).
    void set_field(uint64_t id, const script_value& value);

    void set_field_unchecked(uint64_t id, const script_value& value);
    // create_instance's plan landing: fresh instance, no references, no overflow - one
    // cell copy per planned slot (materialize_field's slot arm without the hash)
    void seed_fields_from_plan(const std::vector<std::pair<uint32_t, script_value>>& plan, uint32_t slot_count);
    bool seeded_from_plan() const { return seeded_from_plan_; }

    // Records this instance as its C++ object's script layer when _cpp_object lands on a
    // script class deriving a bound C++ class: into the object's script_extensible slot
    // when its base registered one, else the engine's weak map. Called from set_field so
    // every path — binder-ctor super() in both backends, extend, archive load — registers
    // identically (docs/script_extensible.md).
    void note_script_layer_owner(const script_value& value);

    // Script-write enforcement: declared-type fields convert like '=' on locals or error
    // with the locals-identical text; 'var' (any) fields pass through; undeclared ('auto')
    // fields infer from the current value's tag then enforce. Callers store the returned
    // value via set_field_unchecked. Defined out-of-line.
    checked_result<script_value> enforce_field_write(uint64_t id, script_value value) const;

    const script_value& get_field(uint64_t id, bool throw_if_missing = true) const;
    script_value& get_field(uint64_t id, bool throw_if_missing = true);

    bool has_field(uint64_t id) const;

    bool has_field_value(uint64_t id) const { return find_field_value(id) != nullptr; }
    // Runtime-added fields other than a hybrid's _cpp_object (a user field can shadow
    // a method by name; the C++ object slot never can)
    bool has_user_overflow_fields() const {
        if (!overflow_fields_ || overflow_fields_->empty()) { return false; }
        return overflow_fields_->size() != 1 || overflow_fields_->begin()->first != get_cpp_object_field_id();
    }

    engine* get_engine() const { return engine_; }

    // A retired layer (engine::script_layer_detach, or a move to another engine) keeps
    // owning its object so old handles never dangle, but every C++ extraction through it
    // throws instead of reaching an object another layer now speaks for.
    void retire_script_layer() { script_layer_retired_ = true; }
    bool script_layer_retired() const { return script_layer_retired_; }
    // Live field references pinned on this instance (make_field_reference / ~reference_holder).
    // Zero = no reference can alias a field: a parallel fork may treat the instance and its
    // value-semantic subtree as exclusive without the per-node use_count walk.
    uint32_t live_field_refs() const noexcept { return live_field_refs_; }
    void note_field_ref_minted() noexcept { ++live_field_refs_; }
    void note_field_ref_released() noexcept { if (live_field_refs_) { --live_field_refs_; } }
    [[noreturn]] void throw_retired_script_layer() const {
        throw runtime_error("Script layer '" + std::string(get_class_name()) + "' was detached (script_layer_detach, or a move to another engine): this handle no longer reaches its object");
    }

    // Field storage lookup WITHOUT lazy default insertion (field-reference resolution:
    // a reload-removed field must error, not silently resurrect as a default)
    script_value* find_field_value(uint64_t id);
    const script_value* find_field_value(uint64_t id) const;

    // Slot fast path (vm site ICs, tier-1 stage B): present-cell lookup by a
    // pre-resolved slot — no hash; absent (null cell / raw-invalid) returns null so
    // the caller falls back to the resolve ladder (chain defaults, errors).
    script_value* field_slot_value(uint32_t slot) {
        if (slot >= flat_fields_.size()) {
            return nullptr;
        }
        script_value* v = flat_fields_[slot].get();
        return (v && v->raw_storage_index() != script_value::TYPEID_INVALID) ? v : nullptr;
    }

    // Slot store (write-side IC): set_field_unchecked's exact semantics through a
    // pre-resolved slot — writes through a held reference, materializes absent cells.
    void field_slot_write(uint32_t slot, script_value&& v) {
        if (slot >= flat_fields_.size()) {
            flat_fields_.resize(static_cast<size_t>(slot) + 1);
        }
        auto& cell = flat_fields_[slot];
        if (!cell) {
            cell = make_cell(std::move(v));
        } else if (cell->is_reference()) {
            cell->deref() = std::move(v);
        } else {
            *cell = std::move(v);
        }
    }

    std::string_view get_class_name() const { return class_name_; }

    // Hot-reload field migration; defined out-of-line (needs class_definition + the
    // conversion kernel for declared-type retypes).
    void migrate_fields(const std::set<uint64_t>& old_field_ids,
                       const std::unordered_map<uint64_t, script_value>& new_field_defaults);

    script_value get_method(uint64_t id, bool throw_if_missing = true) const;

    void set_class_definition(std::shared_ptr<class_definition> class_def);

    // Re-class this instance in place (extend's re-derive): every field cell moves to the
    // new definition's slot for its id - cells never relocate, so captured field addresses
    // survive exactly as under hot reload - and ids the new class does not declare stay
    // reachable in overflow. Every handle to this instance sees the new class.
    void reclass(const std::shared_ptr<class_definition>& class_def);

    // Field iteration, slot order (first build ≈ declaration order, base-first) then
    // runtime-overflow entries; replaces get_fields()/get_fields_mutable() — flat
    // storage has no map to hand out. fn(uint64_t id, script_value& value).
    template <typename F> void for_each_field(F&& fn);
    template <typename F> void for_each_field(F&& fn) const;
    size_t field_value_count() const;

    class_definition* get_class_definition() const {
        return class_def_;
    }

    bool is_script_class() const;
    bool is_cpp_class() const;

    std::shared_ptr<void> get_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return nullptr;
        if (script_layer_retired_) { throw_retired_script_layer(); }

        auto cpp_field = get_field(field_id);
        if (!cpp_field.is_null() && cpp_field.is_object()) {
            return extract_cpp_object_impl(cpp_field);
        }
        return nullptr;
    }

	template<typename T>
	std::shared_ptr<T> get_cpp_object_as() const {
		if (script_layer_retired_) {
			throw_retired_script_layer();
		}
		auto holder = get_field(get_cpp_object_field_id()).get_object_holder();
		return holder ? std::static_pointer_cast<T>(detail::native_cast_or_throw(
			detail::checked_native_object(engine_, *holder, typeid(T)), engine_)) : nullptr;
	}

    bool has_cpp_object() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return false;
        const script_value* held = find_field_value(field_id);
        return held && !held->is_null();
    }

    // The C++ object's address as _cpp_object holds it (owning or not); null without one.
    // A raw read - no handle copies - for the slot lookups on member resolution.
    void* cpp_object_pointer() const {
        uint64_t field_id = get_cpp_object_field_id();
        if (field_id == 0) return nullptr;
        const script_value* held = find_field_value(field_id);
        if (!held || !held->is_object()) return nullptr;
        auto holder = held->get_object_holder();
        if (!holder) return nullptr;
        return holder->data ? holder->data.get() : holder->bound_ptr;
    }

    std::shared_ptr<class_instance> deep_copy() const;

    void copy_fields_from(const class_instance& other);

    uint64_t get_cpp_object_field_id() const;

    const script_value& get_cached_bound_method(uint64_t method_id) const {
        auto it = bound_method_cache_.find(method_id);
        if (it != bound_method_cache_.end()) {
            return it->second;
        }
        return invalid_value_;
    }

    void cache_bound_method(uint64_t method_id, const script_value& bound_method) const {
        bound_method_cache_[method_id] = bound_method;
    }

    bool has_cached_bound_method(uint64_t method_id) const {
        return bound_method_cache_.find(method_id) != bound_method_cache_.end();
    }

private:
    std::string_view class_name_;   // the engine's interned text (empty for an instance built without an engine)
    // Tier-1 flat fields: slot-indexed per class_definition's append-only registry.
    // unique_ptr pointees NEVER move (vector growth relocates the pointers only), so
    // cached field addresses — suspended coroutines' env chains, this-capturing
    // closures — survive resize AND hot-reload migration, matching the old map's
    // node-stability contract. null pointer OR raw-invalid pointee = ABSENT (raw
    // check, NOT is_invalid(): a reference field derefs). Runtime-added fields and
    // pre-definition writes (deserialization) live in the lazily-built overflow map;
    // set_class_definition re-homes them into slots.
    // One cell arena per instance (allocated when the definition attaches, sized to the
    // class's slot count); slots the class gains afterwards (reload) get heap cells. Either
    // way a pointee never moves - reclass/migration move the POINTERS - and the arena
    // frees after every cell died (declared before flat_fields_ on purpose).
    struct cell_deleter {
        bool heap = true;
        void operator()(script_value* p) const noexcept { if (heap) { delete p; } else { p->~script_value(); } }
    };
    using field_cell = std::unique_ptr<script_value, cell_deleter>;
    std::unique_ptr<std::byte[]> cell_arena_;
    uint32_t arena_cells_ = 0;
    uint32_t arena_used_ = 0;
    void reserve_cell_arena(size_t n) {
        if (!cell_arena_ && n) {
            cell_arena_.reset(new std::byte[n * sizeof(script_value)]);
            arena_cells_ = static_cast<uint32_t>(n);
        }
    }
    field_cell make_cell(script_value&& v) {
        if (arena_used_ < arena_cells_) {
            auto* p = reinterpret_cast<script_value*>(cell_arena_.get()) + arena_used_++;
            new (p) script_value(std::move(v));
            return field_cell(p, cell_deleter{false});
        }
        return field_cell(new script_value(std::move(v)), cell_deleter{true});
    }
    std::vector<field_cell> flat_fields_;
    std::unique_ptr<std::unordered_map<uint64_t, script_value>> overflow_fields_;
    // Store WITHOUT reference write-through (the old map's raw insert_or_assign shape:
    // copy_fields_from and lazy-default materialization replace a held reference value
    // itself); returns the stored cell. set_field_unchecked keeps write-through.
    script_value& materialize_field(uint64_t id, script_value&& v);
    void rehome_overflow_fields();
    class_definition* class_def_ = nullptr;
    engine* engine_ = nullptr;
    bool script_layer_retired_ = false;
    bool seeded_from_plan_ = false;   // create_instance landed every default through the class plan
    uint32_t live_field_refs_ = 0;
    // Index in class_def_->instances_ (SIZE_MAX = not registered there): the destructor
    // swap-removes in O(1) instead of scanning and locking the whole list
    size_t registry_slot_ = SIZE_MAX;
    friend class class_definition;
    mutable uint64_t cpp_object_field_id_ = 0;
    mutable script_value invalid_value_;
    mutable std::unordered_map<uint64_t, script_value> bound_method_cache_;

    static std::shared_ptr<void> extract_cpp_object_impl(const script_value& val);
};

class class_definition : public std::enable_shared_from_this<class_definition> {
protected:
    template<size_t ExpectedArgs>
    static void validate_method_args(const std::vector<script_value>& args,
                                    const std::string& method_name) {
        if (args.empty()) {
            throw runtime_error("Method '" + method_name + "' called without 'this' object");
        }
        if (args.size() != ExpectedArgs + 1) {
            throw runtime_error("Method '" + method_name + "' expects " +
                              std::to_string(ExpectedArgs) +
                              " arguments, got " + std::to_string(args.size() - 1));
        }
    }

public:
    enum class_type { cpp_class, script_class, vm_class };

    // An instance that outlives its definition or its engine (a host object holding a script
    // value past engine teardown, a signal receiver capturing one) must not reach back into
    // either from its own destructor: orphan every live registrant (unregister is then a
    // no-op) and forget the engine. The definition's destructor does it when it dies first;
    // engine teardown does it for every definition still alive, since a captured environment
    // can keep one alive past the engine.
    void detach_for_teardown() noexcept {
        for (auto& entry : instances_) {
            if (auto live = entry.lock()) {
                live->class_def_ = nullptr;
                live->registry_slot_ = SIZE_MAX;
            }
        }
        instances_.clear();
        engine_ = nullptr;
    }
    virtual ~class_definition() { detach_for_teardown(); }

    class_definition(std::string_view name, uint64_t type_id, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(cpp_class) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }

    class_definition(std::string_view name, uint64_t type_id, class_type type, engine* eng)
        : name_(name), type_id_(type_id), type_info_(nullptr), engine_(eng),
          null_field_value_(std::monostate{}, eng), class_type_(type) {
        if (eng) {
            type_info_ = eng->get_type_info_object(type_id_);
        }
    }

    class_type get_class_type() const { return class_type_; }
    bool is_script_class() const { return class_type_ == script_class; }
    bool is_cpp_class() const { return class_type_ == cpp_class; }
    bool is_vm_class() const { return class_type_ == vm_class; }

    bool has_property_getters() const { return has_property_getters_; }

    // Method-dispatch epoch: bumps on any change that can alter method resolution for
    // this class (method add/replace, hot reload rebuild, access labels, parent wiring)
    // and propagates through derived_classes_. Per-call-site inline caches validate
    // (class identity, epoch) against it; sound because script class_definitions are
    // pinned for the engine's lifetime (methods_ -> dispatcher -> class cycle), so a
    // cached identity can never be a recycled address with a coincidental epoch.
    uint64_t method_epoch() const { return method_epoch_; }
    unit_stamp unit() const { return unit_; }
    void set_unit(unit_stamp unit) { unit_ = unit; }
    void bump_method_epoch() {
        ++method_epoch_;
        for (auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->bump_method_epoch();
            }
        }
    }

    // Field-shape epoch (type proofs): moves only when a field proof's facts can change - field
    // set, declared types, resolution chain, access labels, custom accessors. Unlike
    // method_epoch it does NOT move on an identical reload. Propagates to derived like
    // bump_method_epoch (same engine-lifetime/no-ABA contract).
    uint64_t field_shape_epoch() const { return field_shape_epoch_; }
    void bump_field_shape_epoch() {
        ++field_shape_epoch_;
        for (auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->bump_field_shape_epoch();
            }
        }
    }

    type_info_ptr get_type_info() const { return type_info_; }

    void set_type_info(type_info_ptr type_info) { type_info_ = type_info; }

    // is_auto_accessor: the registration is a SYNTHESIZED field accessor (_get_/_set_
    // minted by class build — verbatim get_field / enforce+set_field), which the
    // member site ICs may bypass. The only callers are the two backends' class-build
    // accessor loops; everything else registers unflagged and reclaims the id.
    void add_method_by_id(uint64_t name_id, script_function func, bool is_auto_accessor = false) {
        auto eng = engine_;
        if (!eng) return;

        if (is_auto_accessor) {
            has_property_getters_ = true;
        }

        methods_.insert_or_assign(name_id, script_value::make_function(func, engine_));
        if (is_auto_accessor) {
            auto_accessor_ids_.insert(name_id);
        } else {
            auto_accessor_ids_.erase(name_id);
            bump_field_shape_epoch();   // a custom _get_/_set_ or a field-named method changes the field ladder
        }
        bump_method_epoch();
    }

    // The nearest USER-defined _get_/_set_ for id in get_method order (own methods_, parents
    // in declaration order, the C++ base), skipping entries flagged synthesized in their
    // owning class; null when none. On a C++ class (empty auto set) this is get_method. A
    // synthesized entry anywhere in the script chain is a script field declaration, which
    // shadows a same-named C++ .property() (docs/script_extensible.md S10).
    const script_value* user_accessor(uint64_t id) const {
        bool script_field_seen = false;
        return user_accessor_walk(id, script_field_seen);
    }
    const script_value* user_accessor_walk(uint64_t id, bool& script_field_seen) const {
        if (is_cpp_class() && script_field_seen) { return nullptr; }   // the C++ base sits in parent_classes_ too
        if (auto it = methods_.find(id); it != methods_.end()) {
            if (auto_accessor_ids_.find(id) == auto_accessor_ids_.end()) { return &it->second; }
            script_field_seen = true;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (const script_value* found = parent->user_accessor_walk(id, script_field_seen)) { return found; }
            }
        }
        if (cpp_base_class_) { return cpp_base_class_->user_accessor_walk(id, script_field_seen); }
        return nullptr;
    }
    script_value find_user_accessor(uint64_t id) const {
        const script_value* found = user_accessor(id);
        return found ? *found : script_value::make_invalid(engine_);
    }

    // No user accessor anywhere in the chain: the accessor for id (if any) is the synthesized
    // field accessor, whose body is verbatim get_field / enforce+set_field, so member-site
    // caches and field proofs may bypass it.
    bool is_auto_accessor(uint64_t id) const { return user_accessor(id) == nullptr; }
    bool owns_auto_accessor(uint64_t id) const { return auto_accessor_ids_.find(id) != auto_accessor_ids_.end(); }
    // Replaces the synthesized set after redefine_class installs new_methods (no epoch move:
    // identical reloads must keep field proofs armed)
    void set_auto_accessor_ids(std::unordered_set<uint64_t> ids) { auto_accessor_ids_ = std::move(ids); }

    void add_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        add_method(name, std::move(func), arity, {});
    }

    // Typed overload: param_types is the C++ parameter signature (one entry per script argument,
    // excluding 'this'). Same-arity overloads with DISTINCT signatures coexist and are resolved by
    // argument type at call time; an identical signature re-binds. Empty param_types is a legacy
    // untyped binding (single overload per arity, as before).
    void add_method(const std::string& name, script_function func, size_t arity,
                    std::vector<param_type_info> param_types) {
        auto eng = engine_;
        if (!eng) return;

        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);
        // A first user _get_/_set_ (or one replacing the synthesized accessor) changes the field
        // ladder: armed field proofs and member caches must miss
        const bool accessor_set_changes = (name.rfind("_get_", 0) == 0 || name.rfind("_set_", 0) == 0) &&
            (methods_.find(name_id) == methods_.end() || auto_accessor_ids_.find(name_id) != auto_accessor_ids_.end());

        if (arity != SIZE_MAX) {
            auto& bucket = cpp_method_overloads_[name_id][arity];
            auto existing = std::find_if(bucket.begin(), bucket.end(),
                [&](const cpp_overload_entry& e) { return e.param_types == param_types; });
            if (existing != bucket.end()) {
                existing->func = func;  // re-bind same signature
            } else {
                bucket.push_back(cpp_overload_entry{ std::move(param_types), func });
            }
            method_arities_[name_id].push_back(arity);

            class_definition* class_def_ptr = this;

            methods_.insert_or_assign(name_id, script_value::make_function(
                [class_def_ptr, name_id](const std::vector<script_value>& raw_args) -> checked_result<script_value> {
                    if (raw_args.empty() || raw_args[0].get_engine() == nullptr) {
                        return checked_result<script_value>(make_error_code(runtime_error_code::class_not_found),
                                                            "Class definition no longer exists");
                    }
                    // C++ boundary: escape-boxed locals arrive as CELL references (script
                    // parameter binding derefs them; the bound-method mint path does not) —
                    // normalize so overload scoring and converters see hoisted-temp values.
                    std::vector<script_value> scratch;
                    const std::vector<script_value>& args = detail::deref_builtin_args(raw_args, scratch);

                    size_t script_arg_count = args.size() - 1;

                    auto& overloads_for_name = class_def_ptr->cpp_method_overloads_[name_id];
                    auto it = overloads_for_name.find(script_arg_count);
                    if (it == overloads_for_name.end() || it->second.empty()) {
                        return detail::overload_miss(make_error_code(runtime_error_code::argument_count_mismatch),
                                                     name_id, args.data() + 1, script_arg_count, args[0].get_engine());
                    }

                    auto& entries = it->second;
                    if (entries.size() == 1) {
                        return entries.front().func(args);
                    }

                    std::vector<std::vector<param_type_info>> sigs;
                    sigs.reserve(entries.size());
                    for (const auto& e : entries) {
                        sigs.push_back(e.param_types);
                    }
                    int best = args[0].get_engine()->select_cpp_overload(args, sigs);
                    if (best < 0) {
                        return detail::overload_miss(make_error_code(runtime_error_code::argument_count_mismatch),
                                                     name_id, args.data() + 1, script_arg_count, args[0].get_engine());
                    }
                    return entries[static_cast<size_t>(best)].func(args);
                },
                engine_
            ));
        } else {
            methods_.insert_or_assign(name_id, script_value::make_function(
                [func](const std::vector<script_value>& raw_args) -> checked_result<script_value> {
                    std::vector<script_value> scratch;
                    return func(detail::deref_builtin_args(raw_args, scratch));
                },
                engine_
            ));
        }
        auto_accessor_ids_.erase(name_id);
        if (accessor_set_changes) { bump_field_shape_epoch(); }
        bump_method_epoch();
    }

    void add_static_method(const std::string& name, script_function func, size_t arity = SIZE_MAX) {
        auto eng = engine_;
        if (!eng) return;

        if (name.size() > 5 && name.substr(0, 5) == "_get_") {
            has_property_getters_ = true;
        }

        uint64_t name_id = eng->symbolize(name);
        add_static_method(name_id, std::move(func), arity);
    }

    // C++-only registration door (dynamic_binder statics; script statics take
    // add_static_script_method) — args normalize like add_method's.
    void add_static_method(uint64_t name_id, script_function func, size_t arity = SIZE_MAX) {
        static_methods_.insert_or_assign(name_id, script_value::make_function(
            [func](const std::vector<script_value>& raw_args) -> checked_result<script_value> {
                std::vector<script_value> scratch;
                return func(detail::deref_builtin_args(raw_args, scratch));
            },
            engine_
        ));

        if (arity != SIZE_MAX) {
            static_method_arities_[name_id].push_back(arity);
        }
    }

    void add_script_method(std::string_view name, std::shared_ptr<function_decl> ast, std::shared_ptr<environment> definition_env);

    void add_static_script_method(std::string_view name, std::shared_ptr<function_decl> ast, std::shared_ptr<environment> definition_env);

    // Shared cache-aware overload resolution for script methods (defined in
    // overload_resolution.hpp). Span core resolves against a caller stack slice
    // (call-arg windows); the vector overload forwards.
    checked_result<std::shared_ptr<function_decl>> resolve_method_overload(uint64_t name_id, const script_value* args, size_t argc) const;
    checked_result<std::shared_ptr<function_decl>> resolve_method_overload(uint64_t name_id, const std::vector<script_value>& args) const;
    // Same resolution, answered as a pointer to the class-owned handle (overload table
    // or resolution cache): the in-loop call paths pin through the record instead of
    // paying a shared_ptr copy per call
    checked_result<const std::shared_ptr<function_decl>*> resolve_method_overload_ref(uint64_t name_id, const script_value* args, size_t argc) const;
    const std::shared_ptr<function_decl>* resolve_method_overload_core(uint64_t name_id, const script_value* args, size_t argc, std::error_code& ec) const;

    // Single-overload probe for inline-cache fill: the lone overload for name_id, or
    // nullptr when the name has zero or multiple overloads.
    function_decl* single_method_overload(uint64_t name_id) const {
        auto it = method_overloads_.find(name_id);
        return (it != method_overloads_.end() && it->second.size() == 1) ? it->second[0].get() : nullptr;
    }
    std::shared_ptr<function_decl> single_method_overload_ptr(uint64_t name_id) const {
        auto it = method_overloads_.find(name_id);
        return (it != method_overloads_.end() && it->second.size() == 1) ? it->second[0] : nullptr;
    }

    // Reflection enumeration (reflect::methods / method_arguments): this class's OWN
    // tables — script overload sets (AST decls carry names/types/defaults), the typed
    // C++ overload table, and the raw method-value maps for names in neither
    const auto& script_method_overloads() const { return method_overloads_; }
    const auto& script_static_method_overloads() const { return static_method_overloads_; }
    const auto& cpp_method_overload_table() const { return cpp_method_overloads_; }
    const auto& method_values() const { return methods_; }
    const auto& static_method_values() const { return static_methods_; }

    void add_field(std::string_view name, const script_value& default_value) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);

        if (default_value.get_engine() == nullptr && !!engine_) {
            script_value value_with_engine(default_value);
            value_with_engine.set_engine(engine_);
            field_defaults_.insert_or_assign(name_id, value_with_engine);
        } else {
            field_defaults_.insert_or_assign(name_id, default_value);
        }
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        bump_field_shape_epoch();
        bump_method_epoch();   // method ICs memoize the field-shadow answer per class
    }

    void add_field(const std::string& name) {
        auto eng = engine_;
        if (!eng) return;

        uint64_t name_id = eng->symbolize(name);
        field_defaults_.insert_or_assign(name_id, script_value(std::monostate{}, engine_));
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        bump_field_shape_epoch();
        bump_method_epoch();
    }

    // Declared field types (ruling 2026-07: typed fields enforce like locals). 'auto'
    // fields store no entry (they infer from the initialized value); 'var' stores the
    // any tag explicitly (dynamic stays dynamic). Replaced wholesale on definition and
    // on hot reload; a semantic change on a REDEFINITION (mark_changes) flags the next
    // redefine_class as fields_changed so instances re-migrate (converting per the
    // retype ruling). The initial definition must NOT flag: a spurious first-reload
    // migration would rebuild fields_ and dangle field pointers cached by suspended
    // coroutines' env chains.
    void replace_field_declared_types(std::unordered_map<uint64_t, type_info_ptr> new_types, bool mark_changes) {
        auto same_type = [](type_info_ptr a, type_info_ptr b) {
            if (!a || !b) return a == b;
            return a->base_type == b->base_type && a->type_name == b->type_name;
        };
        bool changed = field_declared_types_.size() != new_types.size();
        if (!changed) {
            for (const auto& [id, t] : new_types) {
                auto it = field_declared_types_.find(id);
                if (it == field_declared_types_.end() || !same_type(it->second, t)) {
                    changed = true;
                    break;
                }
            }
        }
        field_declared_types_ = std::move(new_types);
        if (changed) { bump_field_shape_epoch(); }   // declared type changed => epoch moved (proof guards)
        if (changed && mark_changes) {
            declared_types_dirty_ = true;
        }
    }

    // TRUE when instances are value-closed (parallel_for v1, Dev rulings 2026-07-09):
    // a script class whose every instance field is store-enforced to a value-semantic
    // shape — primitive (int/float/bool/char), STRING (engine value type, per-instance
    // by ruling; the rare shared string node is detached by the region barrier's
    // normalization pass, not banned here), typed primitive/string container, or a
    // nested script class that is itself flat — with a flat parent chain and no C++
    // base/data. Because those stores all deep-clone, two live instances can never
    // share reachable mutable structure (strings excepted, hence the barrier detach),
    // so a parallel worker owning an instance may mutate it in place with no alias
    // walk. What excludes a class: shared_ptr/weak_ptr/function/reference fields
    // (reference semantics IS sharing) and auto/var fields (no enforced shape).
    // Cached against engine::class_definition_epoch (a nested field class redefining
    // must invalidate containing classes). Defined out of line in class_registry.cpp.
    bool flat_value_semantics() const;
    // The flat slots whose declared type can reach a refcount (strings, containers, objects);
    // a parallel fork visits only these when normalizing an element of this class. Scalar-only
    // classes answer an empty list. Memoized under the class-definition epoch like flat_value_semantics.
    const std::vector<uint32_t>& handle_bearing_slots() const;
    // The flat slots whose declared type can hold a STRING anywhere in its value closure (a
    // string, a container of strings, a nested value class with one): the only alias a
    // reference-free value-semantic subtree can still carry is a shared string buffer.
    const std::vector<uint32_t>& string_reachable_slots() const;
    bool type_can_hold_string(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const;

    type_info_ptr get_field_declared_type(uint64_t id) const {
        auto it = field_declared_types_.find(id);
        if (it != field_declared_types_.end()) {
            return it->second;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (type_info_ptr t = parent->get_field_declared_type(id)) {
                    return t;
                }
            }
        }
        if (cpp_base_class_) {
            return cpp_base_class_->get_field_declared_type(id);
        }
        return nullptr;
    }

    void add_static_field(uint64_t id, const script_value& initial_value) {
        static_fields_.insert(id);

        if (initial_value.get_engine() == nullptr && !!engine_) {
            script_value value_with_engine(initial_value);
            value_with_engine.set_engine(engine_);
            static_field_values_.insert_or_assign(id, value_with_engine);
        } else {
            static_field_values_.insert_or_assign(id, initial_value);
        }
    }

    // Declared static types, replaced wholesale at every script (re)definition: 'auto'
    // stores a null entry (infers from the held value's tag), 'var' the any tag; a static
    // with NO entry (C++ static_property) stays lenient. No epoch: statics carry no proofs.
    void replace_static_declared_types(std::unordered_map<uint64_t, type_info_ptr> new_types) {
        static_declared_types_ = std::move(new_types);
    }

    type_info_ptr get_static_declared_type(uint64_t id) const {
        auto it = static_declared_types_.find(id);
        return it != static_declared_types_.end() ? it->second : nullptr;
    }

    // Script-write enforcement for statics: enforce_field_write's ladder over the static's
    // declared type (its held tag when 'auto'). Callers store the returned value via
    // set_static_field. Defined out-of-line.
    checked_result<script_value> enforce_static_write(uint64_t id, script_value value) const;

    // Hot reload: clear the instance-method overload sets + resolution cache so a reload
    // rebuilds them fresh (re-adding each method via add_script_method) instead of appending
    // to the previous definition's overloads.
    void clear_instance_method_overloads() {
        method_overloads_.clear();
        overload_resolution_cache_.clear();
        bump_method_epoch();
    }

    // Hot reload: same for script static-method overload sets (C++ static arities untouched).
    void clear_static_method_overloads() {
        static_method_overloads_.clear();
    }

    // Hot reload: drop statics absent from the new definition so they're no longer accessible.
    void retain_static_fields(const std::set<uint64_t>& keep) {
        for (auto it = static_fields_.begin(); it != static_fields_.end();) {
            if (keep.find(*it) == keep.end()) {
                static_field_values_.erase(*it);
                static_declared_types_.erase(*it);
                it = static_fields_.erase(it);
            } else {
                ++it;
            }
        }
    }

    const script_value* get_static_field_ptr(uint64_t id) const {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    script_value* get_static_field_ptr(uint64_t id) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    const script_value& get_static_field(uint64_t id) const {
        const script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }

    script_value& get_static_field(uint64_t id) {
        script_value* ptr = get_static_field_ptr(id);
        if (ptr) {
            return *ptr;
        }
        null_field_value_ = script_value::make_null(engine_);
        return null_field_value_;
    }

    [[nodiscard]] bool set_static_field(uint64_t id, const script_value& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = value;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool set_static_field(uint64_t id, script_value&& value) {
        auto it = static_field_values_.find(id);
        if (it != static_field_values_.end()) {
            it->second = std::move(value);
            return true;
        }
        return false;
    }

    bool is_static_field(uint64_t id) const {
        return static_fields_.find(id) != static_fields_.end();
    }

    bool has_static_field(uint64_t id) const {
        return static_field_values_.find(id) != static_field_values_.end();
    }

    script_value get_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = methods_.find(id);
        if (it != methods_.end()) {
            return it->second;
        }

        for (const auto& parent : parent_classes_) {
            if (parent) {
                auto result = parent->get_method(id, false);
                if (!result.is_invalid()) {
                    return result;
                }
            }
        }

        if (cpp_base_class_) {
            auto result = cpp_base_class_->get_method(id, false);
            if (!result.is_invalid()) {
                return result;
            }
        }

        if (throw_if_missing) {
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Method '" + name + "' not found in class '" + name_ + "'");
        }

        return script_value::make_invalid(engine_);
    }

    bool has_method(uint64_t id) const {
        return methods_.find(id) != methods_.end();
    }

    // Chain-aware (this class + script parents + cpp base): does ANY resolvable user
    // method with this name exist? Used to let a user class method WIN over a same-named
    // builtin handle method (Dev ruling 2026-07). Cheap: no symbolizer / no throw.
    bool defines_method(uint64_t id) const {
        if (methods_.find(id) != methods_.end()) { return true; }
        for (const auto& parent : parent_classes_) {
            if (parent && parent->defines_method(id)) { return true; }
        }
        if (cpp_base_class_ && cpp_base_class_->defines_method(id)) { return true; }
        return false;
    }

    // Truthiness probe memo (is_truthy on objects): does the chain define to_bool()?
    // get_method's exact chain (defines_method); valid while method_epoch_ holds
    bool may_define_to_bool() const {
        if (to_bool_memo_epoch_ != method_epoch_) {
            uint64_t id = 0;
            if (auto eng = engine_) { id = eng->symbolize("to_bool"); }
            may_define_to_bool_ = id == 0 || defines_method(id);
            to_bool_memo_epoch_ = method_epoch_;
        }
        return may_define_to_bool_;
    }

    script_value get_static_method(uint64_t id, bool throw_if_missing = true) const {
        auto it = static_methods_.find(id);
        if (it != static_methods_.end()) {
            return it->second;
        }

        if (throw_if_missing) {
            std::string name;
            if (auto eng = engine_) {
                name = eng->get_symbolizer()->get_string(id);
            } else {
                name = std::to_string(id);
            }
            throw runtime_error("Static method '" + name + "' not found in class '" + name_ + "'");
        }
        return script_value::make_null(engine_);
    }

    bool has_static_method(uint64_t id) const {
        return static_methods_.find(id) != static_methods_.end();
    }

    bool has_static_method_with_arity(uint64_t name_id, size_t arity) const;

    std::vector<std::string_view> get_static_field_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_fields_.size());
        if (auto eng = engine_) {
            for (const auto& field_id : static_fields_) {
                names.push_back(eng->get_symbolizer()->get_string(field_id));
            }
        }
        return names;
    }

    std::vector<std::string_view> get_static_method_names() const {
        std::vector<std::string_view> names;
        names.reserve(static_methods_.size());
        if (auto eng = engine_) {
            for (const auto& [method_id, _] : static_methods_) {
                names.push_back(eng->get_symbolizer()->get_string(method_id));
            }
        }
        return names;
    }

    uint64_t chain_defaults_version() const {
        uint64_t v = defaults_version_;
        for (const auto& parent : parent_classes_) { if (parent) { v += parent->chain_defaults_version() * 1315423911u; } }
        return v;
    }
    // The plan's values: the chain's merged defaults (a typed-null placeholder for an
    // instance field - its initializer is per-construction code) with every LITERAL
    // initializer baked in, each through the enforcing store's conversion. The script layer
    // supplies the literals (script_class.hpp); a plain class_definition has none.
    virtual const script_value* literal_initializer_for(uint64_t) const { return nullptr; }
    virtual bool chain_literal_plan_ok() const { return true; }
    const std::vector<std::pair<uint32_t, script_value>>& instance_plan() const {
        const uint64_t want = chain_defaults_version() ^ (field_shape_epoch_ << 32) ^ (method_epoch_ << 48);
        if (instance_plan_version_ == want && !instance_plan_.empty()) { return instance_plan_; }
        instance_plan_.clear();
        if (!chain_literal_plan_ok()) { instance_plan_version_ = 0; return instance_plan_; }
        std::unordered_map<uint64_t, script_value> merged;
        std::vector<const class_definition*> chain;
        std::function<void(const class_definition&)> walk = [&](const class_definition& cd) {
            for (const auto& parent : cd.parent_classes_) { if (parent) { walk(*parent); } }
            chain.push_back(&cd);
        };
        walk(*this);
        // base-first: a child's default (and literal) overrides its parent's, like the
        // create_instance loops and the initializer passes that follow them
        for (const class_definition* cd : chain) {
            for (const auto& [id, value] : cd->field_defaults_) {
                const script_value* lit = cd->literal_initializer_for(id);
                if (lit) {
                    // the stamp's materialize_constant: engine-tagged, strings re-minted so
                    // instances never share a literal's buffer
                    // exactly visit_literal_expr / materialize_constant: a parser literal has no
                    // engine or tag; the value is recreated with the canonical primitive tag
                    script_value baked(std::monostate{}, engine_);
                    switch (lit->raw_storage_index()) {
                        case script_value::TYPEID_INT: baked = script_value(lit->unchecked_as_int(), engine_); break;
                        case script_value::TYPEID_FLOAT: baked = script_value(lit->unchecked_as_float(), engine_); break;
                        case script_value::TYPEID_STRING: baked = script_value(lit->unchecked_as_string(), engine_); break;
                        case script_value::TYPEID_CHAR: baked = script_value(lit->unchecked_as_char(), engine_); break;
                        case script_value::TYPEID_BOOL: baked = script_value(lit->unchecked_as_bool(), engine_); break;
                        default: instance_plan_.clear(); instance_plan_version_ = 0; return instance_plan_;   // not a scalar literal: the slow path
                    }
                    merged.insert_or_assign(id, std::move(baked));
                } else {
                    merged.insert_or_assign(id, value);
                }
            }
        }
        instance_plan_.reserve(merged.size());
        for (const auto& [id, value] : merged) {
            const uint32_t slot = field_slot(id);
            if (slot == k_no_field_slot) { instance_plan_.clear(); instance_plan_version_ = 0; return instance_plan_; }
            script_value stored = value;
            if (!value.is_null()) {
                type_info_ptr declared;
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    if (auto d = (*it)->field_declared_types_.find(id); d != (*it)->field_declared_types_.end()) { declared = d->second; break; }
                }
                if (declared && declared->base_type != script_value_type::jai_any_type) {
                    if (auto converted = detail::field_store_convert_no_script(value, declared, engine_)) { stored = std::move(*converted); }
                    else { instance_plan_.clear(); instance_plan_version_ = 0; return instance_plan_; }
                }
            }
            instance_plan_.emplace_back(slot, std::move(stored));
        }
        std::sort(instance_plan_.begin(), instance_plan_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        instance_plan_version_ = want;
        return instance_plan_;
    }
    std::shared_ptr<class_instance> create_instance() {
        std::shared_ptr<class_instance> instance;
        // Destructor probe memo: "~"+name_ alloc + symbolizer intern + has_method are
        // class-static facts, recomputed per MINT before this (measured 2026-07-12 in
        // the construct_instance section profile). method_epoch_ already cascades to
        // derived classes on every method-set mutation, so it is the exact validity key.
        if (dtor_memo_epoch_ != method_epoch_) {
            dtor_id_ = 0;
            has_script_dtor_ = false;
            if (auto eng = engine_) {
                dtor_id_ = eng->symbolize("~" + name_);
                has_script_dtor_ = is_script_class() && dtor_id_ != 0 && has_method(dtor_id_);
            }
            dtor_memo_epoch_ = method_epoch_;
        }

        if (has_script_dtor_) {
            auto* raw_instance = new class_instance(class_instance::interned_name_t{}, interned_name(), engine_);
            auto class_def = shared_from_this();

            instance = std::shared_ptr<class_instance>(raw_instance,
                [class_def](class_instance* ptr) {
                    if (ptr) {
                        std::function<void(std::shared_ptr<class_definition>)> call_destructors;
                        call_destructors = [&](std::shared_ptr<class_definition> current_class) {
                            if (!current_class) return;

                            auto current_destructor_name = "~" + current_class->name_;
                            uint64_t destructor_id = 0;
                            if (auto eng = current_class->get_engine()) {
                                destructor_id = eng->symbolize(current_destructor_name);
                            }
                            if (destructor_id == 0) return;

                            auto method_it = current_class->methods_.find(destructor_id);

                            if (method_it != current_class->methods_.end()) {
                                try {
                                    std::shared_ptr<class_instance> temp_this(ptr, [](class_instance*){});

                                    const script_function& destructor_func = method_it->second.as_function();
                                    auto result = destructor_func({script_value::make_object(current_class->name_, temp_this, current_class->get_engine())});
                                    (void)result;
                                } catch (...) {
                                }
                            }

                            for (const auto& parent : current_class->parent_classes_) {
                                call_destructors(parent);
                            }
                        };

                        call_destructors(class_def);

                        delete ptr;
                    }
                });
        } else {
            instance = std::make_shared<class_instance>(class_instance::interned_name_t{}, interned_name(), engine_);   // instance + control block, one allocation
        }

        instance->set_class_definition(shared_from_this());

        register_instance(std::weak_ptr<class_instance>(instance), instance.get());

        if (const auto& plan = instance_plan(); !plan.empty()) {
            instance->seed_fields_from_plan(plan, field_slot_count());
            return instance;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                const auto& parent_fields = parent->get_all_field_defaults();
                for (const auto& [field_id, default_value] : parent_fields) {
                    if (!instance->has_field(field_id)) {
                        instance->set_field(field_id, default_value);
                    }
                }
            }
        }

        for (const auto& [field_id, default_value] : field_defaults_) {
            instance->set_field(field_id, default_value);
        }

        return instance;
    }

    const std::unordered_map<uint64_t, script_value>& get_all_field_defaults() const {
        if (!field_defaults_cache_valid_) {
            all_field_defaults_cache_.clear();

            for (const auto& parent : parent_classes_) {
                if (parent) {
                    const auto& parent_fields = parent->get_all_field_defaults();
                    for (const auto& [id, value] : parent_fields) {
                        if (all_field_defaults_cache_.find(id) == all_field_defaults_cache_.end()) {
                            all_field_defaults_cache_[id] = value;
                        }
                    }
                }
            }

            for (const auto& [id, value] : field_defaults_) {
                all_field_defaults_cache_.insert_or_assign(id, value);
            }

            field_defaults_cache_valid_ = true;
        }

        return all_field_defaults_cache_;
    }

    // Flat-field slots (see registry members): base-first chain walk appends unseen
    // ids; existing assignments never move. k_no_field_slot = never declared anywhere
    // in the chain (runtime-added fields live in the instance overflow map).
    static constexpr uint32_t k_no_field_slot = UINT32_MAX;
    void ensure_field_slots() const {
        if (field_slots_current_) return;
        cpp_property_slots_.assign(slot_to_field_.size(), 0);
        std::function<void(const class_definition&)> walk = [&](const class_definition& cd) {
            for (const auto& parent : cd.parent_classes_) {
                if (parent) walk(*parent);
            }
            const uint8_t cpp_declared = cd.is_cpp_class() ? 1 : 0;
            for (const auto& [id, value] : cd.field_defaults_) {
                auto it = field_slots_.find(id);
                if (it == field_slots_.end()) {
                    it = field_slots_.emplace(id, static_cast<uint32_t>(slot_to_field_.size())).first;
                    slot_to_field_.push_back(id);
                    cpp_property_slots_.push_back(0);
                }
                // A binder .property() placeholder marks its slot; a script declaration
                // anywhere in the chain (walked base-first) shadows it
                cpp_property_slots_[it->second] = cpp_declared;
            }
        };
        walk(*this);
        has_cpp_property_slots_ = std::find(cpp_property_slots_.begin(), cpp_property_slots_.end(), 1) != cpp_property_slots_.end();
        field_slots_current_ = true;
    }
    uint32_t field_slot(uint64_t id) const {
        ensure_field_slots();
        auto it = field_slots_.find(id);
        return it != field_slots_.end() ? it->second : k_no_field_slot;
    }
    // The slot holds a C++ property's placeholder (its truth lives in the C++ object,
    // reached through _get_/_set_) and no script class in the chain shadows the name.
    // Valid right after field_slot() (the same recompute fills both).
    bool slot_is_cpp_property(uint32_t slot) const {
        return has_cpp_property_slots_ && cpp_property_slots_[slot] != 0;
    }
    uint64_t slot_field(uint32_t slot) const { return slot_to_field_[slot]; }
    uint32_t field_slot_count() const {
        ensure_field_slots();
        return static_cast<uint32_t>(slot_to_field_.size());
    }
    // The class-level half of class_instance::has_field (declared anywhere in the
    // chain, script or bound C++ base)
    bool chain_declares_field(uint64_t id) const {
        const auto& all_fields = get_all_field_defaults();
        if (all_fields.find(id) != all_fields.end()) {
            return true;
        }
        if (auto cpp_base = find_cpp_base_class()) {
            const auto& cpp_fields = cpp_base->get_all_field_defaults();
            return cpp_fields.find(id) != cpp_fields.end();
        }
        return false;
    }

    /// Class that DECLARED field `id` (own defaults first, then parents, then the C++
    /// base). Field-reference bindability needs the declarer's kind: a C++ .property()
    /// member keeps a dead fields_ placeholder whose truth lives in the C++ object.
    const class_definition* find_field_declaring_class(uint64_t id) const {
        if (field_defaults_.find(id) != field_defaults_.end()) {
            return this;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (const class_definition* found = parent->find_field_declaring_class(id)) {
                    return found;
                }
            }
        }
        if (cpp_base_class_) {
            return cpp_base_class_->find_field_declaring_class(id);
        }
        return nullptr;
    }

    void set_parent(std::shared_ptr<class_definition> parent) {
        const bool chain_changed = parent_classes_.size() != (parent ? 1u : 0u) || (parent && parent_classes_[0] != parent);
        parent_classes_.clear();
        if (parent) {
            parent_classes_.push_back(parent);
            parent->add_derived_class(shared_from_this());
            if (parent->has_property_getters()) {
                has_property_getters_ = true;
            }
        }
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        refresh_access_chain_flag();
        if (chain_changed) { bump_field_shape_epoch(); }
        bump_method_epoch();   // resolution chain changed
    }

    [[nodiscard]] bool add_parent(std::shared_ptr<class_definition> parent) {
        if (!parent) return true;

        for (const auto& existing : parent_classes_) {
            if (existing.get() == parent.get()) {
                return true;
            }
        }

        parent_classes_.push_back(parent);
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;

        if (has_diamond_inheritance()) {
            parent_classes_.pop_back();
            return false;
        }

        parent->add_derived_class(shared_from_this());
        if (parent->has_property_getters()) {
            has_property_getters_ = true;
        }
        refresh_access_chain_flag();
        bump_field_shape_epoch();
        bump_method_epoch();   // resolution chain changed

        return true;
    }

    [[nodiscard]] bool set_parents(const std::vector<std::shared_ptr<class_definition>>& parents) {
        const bool chain_changed = parent_classes_ != parents;   // every (re)definition rewires; identical reloads must not move the shape epoch
        parent_classes_ = parents;
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;

        if (has_diamond_inheritance()) {
            parent_classes_.clear();
            return false;
        }

        for (auto& parent : parent_classes_) {
            if (parent) {
                parent->add_derived_class(shared_from_this());
                if (parent->has_property_getters()) {
                    has_property_getters_ = true;
                }
            }
        }
        refresh_access_chain_flag();
        if (chain_changed) { bump_field_shape_epoch(); }
        bump_method_epoch();   // resolution chain changed

        return true;
    }

    const std::vector<std::shared_ptr<class_definition>>& get_parent_classes() const {
        return parent_classes_;
    }

    std::shared_ptr<class_definition> get_parent() const {
        if (!parent_classes_.empty()) {
            return parent_classes_[0];
        }
        if (cpp_base_class_) {
            return cpp_base_class_;
        }
        return nullptr;
    }

    bool has_diamond_inheritance() const {
        if (parent_classes_.size() <= 1) {
            return false;
        }

        std::unordered_set<const class_definition*> visited;
        std::vector<const class_definition*> to_visit;

        for (const auto& parent : parent_classes_) {
            if (parent) {
                to_visit.push_back(parent.get());
                visited.insert(parent.get());
            }
        }

        size_t index = 0;
        while (index < to_visit.size()) {
            const class_definition* current = to_visit[index++];

            for (const auto& parent : current->parent_classes_) {
                if (parent) {
                    if (visited.count(parent.get()) > 0) {
                        return true;
                    }
                    visited.insert(parent.get());
                    to_visit.push_back(parent.get());
                }
            }
        }

        return false;
    }

    void add_derived_class(std::weak_ptr<class_definition> derived) {
        derived_classes_.push_back(derived);
    }
    const std::vector<std::weak_ptr<class_definition>>& derived_classes() const { return derived_classes_; }

    void set_cpp_base_class(std::shared_ptr<class_definition> cpp_base) {
        if (cpp_base_class_ != cpp_base) { bump_field_shape_epoch(); }
        cpp_base_class_ = cpp_base;
        if (cpp_base && cpp_base->has_property_getters()) {
            has_property_getters_ = true;
        }
        bump_method_epoch();   // resolution chain changed
    }

    std::shared_ptr<class_definition> get_cpp_base_class() const { return cpp_base_class_; }

    // Slot access for a bound C++ type deriving script_extensible, installed by its
    // dynamic_binder; the void* is _cpp_object's pointer, a T* by the holder contract.
    struct script_slot_hooks {
        void (*attach)(void* object, const std::weak_ptr<class_instance>& layer) = nullptr;
        void (*own)(void* object, const std::shared_ptr<class_instance>& layer) = nullptr;
        void (*detach)(void* object) = nullptr;
        std::shared_ptr<class_instance> (*read)(const void* object) = nullptr;
    };
    void set_script_slot_hooks(const script_slot_hooks& hooks) { script_slot_hooks_ = hooks; }
    const script_slot_hooks* slot_hooks() const { return script_slot_hooks_.attach ? &script_slot_hooks_ : nullptr; }
    // Set once an object of this C++ class has been extended: only then does member
    // resolution on a plain wrapper of the class consult the object's slot (a wrapper made
    // before the extend still resolves to the layer). Definitions never extended pay nothing.
    void note_extended() { extended_ever_ = true; }
    bool extended_ever() const { return extended_ever_; }

    // Nearest C++ base anywhere up the script parent chain — the class an instance's
    // _cpp_object is typed as. cpp_base_class_ is only set on the script class whose DIRECT
    // first base is C++, so multi-level hybrids (script : script : C++) must walk.
    std::shared_ptr<class_definition> find_cpp_base_class() const {
        if (cpp_base_class_) { return cpp_base_class_; }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (auto found = parent->find_cpp_base_class()) { return found; }
            }
        }
        return nullptr;
    }

    void register_property_setter(uint64_t field_id, uint64_t setter_id) {
        property_setter_ids_[field_id] = setter_id;
        read_only_property_ids_.erase(field_id);
        bump_field_shape_epoch();
    }

    // A getter-only .property() registers a REJECTING setter (dynamic_binder
    // register_read_only_setter), so its setter id is non-zero like any other and every
    // store spelling raises through it; this set is the checker's writability answer.
    void register_property_read_only(uint64_t field_id) {
        read_only_property_ids_.insert(field_id);
        bump_field_shape_epoch();
    }

    bool owns_read_only_property(uint64_t field_id) const {
        return read_only_property_ids_.find(field_id) != read_only_property_ids_.end();
    }
    // The class whose setter answers for field_id (own table, then the bases in
    // get_property_setter_id order) decides: a derived binding re-registering the
    // property writable is writable
    bool is_read_only_property(uint64_t field_id) const {
        if (property_setter_ids_.find(field_id) != property_setter_ids_.end()) { return owns_read_only_property(field_id); }
        for (const auto& parent : parent_classes_) {
            if (parent && parent->get_property_setter_id(field_id)) { return parent->is_read_only_property(field_id); }
        }
        return false;
    }

    // Chain-aware (this class, then its bound C++ bases): a property registered on a
    // binder base_class<> answers for the derived binding too
    uint64_t get_property_setter_id(uint64_t field_id) const {
        auto it = property_setter_ids_.find(field_id);
        if (it != property_setter_ids_.end()) {
            return it->second;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (uint64_t inherited = parent->get_property_setter_id(field_id)) { return inherited; }
            }
        }
        return 0;
    }

    // Binder .property() registry (the getter side): the "this name is a C++ property of
    // this class" test for the bare-name ladder (detail/member_scope.hpp)
    void register_property_getter(uint64_t field_id, uint64_t getter_id) {
        property_getter_ids_[field_id] = getter_id;
        bump_field_shape_epoch();
    }

    uint64_t get_property_getter_id(uint64_t field_id) const {
        auto it = property_getter_ids_.find(field_id);
        if (it != property_getter_ids_.end()) {
            return it->second;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (uint64_t inherited = parent->get_property_getter_id(field_id)) { return inherited; }
            }
        }
        return 0;
    }

    bool has_property_setter(uint64_t field_id) const {
        return property_setter_ids_.find(field_id) != property_setter_ids_.end();
    }

    // Member-pointer property whose type the cpp_bound box decodes: the address a
    // by-reference binding aims its box at (docs/reference_model.md CPP-MEMBER). Encoded
    // exactly as script_value::cpp_bound_encoding / make_cpp_bound.
    struct cpp_member_accessor {
        std::function<void*(const script_value& receiver)> address;
        uint8_t size_and_sign = 0;
        uint8_t semantic_index = 0;
    };
    void register_property_member(uint64_t field_id, cpp_member_accessor accessor) {
        property_member_accessors_[field_id] = std::move(accessor);
        bump_field_shape_epoch();
    }
    // Chain-aware like the getter/setter ids
    const cpp_member_accessor* get_property_member_accessor(uint64_t field_id) const {
        auto it = property_member_accessors_.find(field_id);
        if (it != property_member_accessors_.end()) {
            return &it->second;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (const auto* inherited = parent->get_property_member_accessor(field_id)) { return inherited; }
            }
        }
        return nullptr;
    }

    bool is_transparent_wrapper() const { return is_transparent_wrapper_; }

    script_value unwrap(script_value& wrapper_value) const {
        if (!is_transparent_wrapper_ || !unwrap_function_) {
            return script_value(std::monostate{}, engine_);
        }
        return unwrap_function_(wrapper_value, engine_);
    }

    void set_unwrap_function(std::function<script_value(script_value&, engine*)> fn) {
        is_transparent_wrapper_ = true;
        unwrap_function_ = std::move(fn);
    }

    const std::string& get_name() const { return name_; }
    // the name's view in the engine's interned table, looked up once (instances and handles name their type by it)
    std::string_view interned_name() const { intern_name_once(); return interned_name_; }
    uint64_t interned_name_id() const { intern_name_once(); return interned_name_id_; }
    void intern_name_once() const {
        if (interned_name_.data() == nullptr && engine_) {
            const auto [id, view] = engine_->get_symbolizer()->intern_with_view(name_);
            interned_name_id_ = id;
            interned_name_ = view;
        }
    }
    uint64_t get_type_id() const { return type_id_; }

    void set_type_id(uint64_t type_id) { type_id_ = type_id; }

    // Auto-registration lifecycle: base_class<B>() registers an unregistered
    // property_owner base under its C++ identity so the chain edge always exists;
    // a later explicit dynamic_binder for the same type ADOPTS this definition and
    // renames it (registration phase — no live instances carry the old identity).
    bool auto_registered() const { return auto_registered_; }
    void set_auto_registered(bool v) { auto_registered_ = v; }
    void rename(const std::string& name, uint64_t type_id) {
        name_ = name;
        type_id_ = type_id;
    }
    bool has_live_instances() const {
        for (const auto& weak_instance : instances_) {
            if (!weak_instance.expired()) { return true; }
        }
        return false;
    }

    // Reflection surface (docs/reflection.md): dynamic_binder marks host-backed classes;
    // the generation counter bumps on every redefinition so tools poll one integer and
    // re-query only when it moved (the live-query/recapture model).
    bool cpp_backed() const { return cpp_backed_; }
    void set_cpp_backed(bool v) { cpp_backed_ = v; }
    uint64_t reflection_generation() const { return reflection_generation_; }
    // The C++ identity behind a host-backed class (nullptr for script classes) — lets
    // reflection reach the type_registry schema (property kinds: field/observable/signal)
    const std::type_info* cpp_type() const { return cpp_type_; }
    void set_cpp_type(const std::type_info* t) { cpp_type_ = t; }

	// Engine-local native base edges. Registration preserves the C++ cast before
	// erasing the pointer; transitive casts compose edges without heap allocation.
	template<typename Derived, typename Base>
	void add_cpp_base_cast() {
		const std::type_index base{typeid(Base)};
		for (const auto& edge : cpp_base_casts_) {
			if (edge.base == base) {
				return;
			}
		}
		cpp_base_casts_.push_back({base, +[](void* pointer) -> void* {
			return static_cast<Base*>(static_cast<Derived*>(pointer));
		}});
	}

	void* adjust_cpp_pointer(void* pointer, const std::type_info& target) const {
		if (cpp_type_ && *cpp_type_ == target) {
			return pointer;
		}
		for (const auto& edge : cpp_base_casts_) {
			if (edge.base == std::type_index(target)) {
				return edge.adjust(pointer);
			}
		}
		for (const auto& edge : cpp_base_casts_) {
			auto base = engine_->get_class_definition_by_type(edge.base);
			if (base) {
				if (auto* result = base->adjust_cpp_pointer(edge.adjust(pointer), target)) {
					return result;
				}
			}
		}
		return nullptr;
	}

    // Field DECLARATION order, this class's own fields (instance fields first, then
    // statics, each in source order) — replaced wholesale on every (re)definition by
    // both backends' class-decl builders (same-commit twins). Required for reflection's
    // determinism contract: symbol ids are interning-order-dependent per engine, so
    // unordered_map iteration is not reproducible across engines or backends.
    void set_field_order(std::vector<uint64_t> order) { field_order_ = std::move(order); }
    const std::vector<uint64_t>& field_order() const { return field_order_; }

    std::vector<std::shared_ptr<class_instance>> live_instances() const {
        std::vector<std::shared_ptr<class_instance>> live;
        live.reserve(instances_.size());
        for (const auto& weak_instance : instances_) {
            if (auto locked = weak_instance.lock()) { live.push_back(std::move(locked)); }
        }
        return live;
    }

    engine* get_engine() const { return engine_; }

    uint64_t get_cpp_object_field_id() const {
        if (cpp_object_field_id_ == 0) {
            if (auto eng = engine_) {
                cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
            }
        }
        return cpp_object_field_id_;
    }

    std::vector<std::string> get_property_names(bool include_inherited = true) const {
        std::vector<std::string> properties;
        std::unordered_set<std::string> seen;

        if (auto eng = engine_) {
            if (include_inherited) {
                for (const auto& parent : parent_classes_) {
                    if (parent) {
                        for (const auto& prop : parent->get_property_names(true)) {
                            if (seen.insert(prop).second) {
                                properties.push_back(prop);
                            }
                        }
                    }
                }
                if (cpp_base_class_) {
                    for (const auto& prop : cpp_base_class_->get_property_names(true)) {
                        if (seen.insert(prop).second) {
                            properties.push_back(prop);
                        }
                    }
                }
            }

            for (const auto& [id, default_value] : field_defaults_) {
                std::string prop_name(eng->get_symbolizer()->get_string(id));
                if (seen.insert(prop_name).second) {
                    properties.push_back(prop_name);
                }
            }
        }
        return properties;
    }

    bool is_subtype_of(const std::string& base_class_name) const {
        if (name_ == base_class_name) {
            return true;
        }

        for (const auto& parent : parent_classes_) {
            if (parent && parent->is_subtype_of(base_class_name)) {
                return true;
            }
        }

        if (cpp_base_class_ && cpp_base_class_->is_subtype_of(base_class_name)) {
            return true;
        }

        return false;
    }

    void set_default_access(access_level access) { default_access_ = access; }
    access_level get_default_access() const { return default_access_; }

    // === Script member access enforcement (public:/private:/protected: labels) ===
    // nonpublic_members_ holds ONLY the private/protected member ids THIS class declares
    // (fields, methods, statics). chain_has_nonpublic_ is the hot-path guard: true when
    // this class or any ancestor declares a nonpublic member — maintained eagerly at
    // class (re)definition and parent wiring, propagated down through derived_classes_,
    // so the common all-public case costs one bool load per member access.
    void set_nonpublic_members(std::unordered_map<uint64_t, access_level> members) {
        if (nonpublic_members_ != members) { bump_field_shape_epoch(); }   // labels reapply on every (re)definition
        nonpublic_members_ = std::move(members);
        refresh_access_chain_flag();
        bump_method_epoch();   // inline caches stamp a needs-access-check flag at fill
    }

    bool chain_has_nonpublic() const { return chain_has_nonpublic_; }

    // Walks the inheritance chain for the class DECLARING member_id as nonpublic.
    // nullptr = the member is public everywhere (or unknown — access checks pass).
    const class_definition* find_nonpublic_declarer(uint64_t member_id, access_level& level_out) const {
        auto it = nonpublic_members_.find(member_id);
        if (it != nonpublic_members_.end()) {
            level_out = it->second;
            return this;
        }
        for (const auto& parent : parent_classes_) {
            if (parent) {
                if (const auto* declarer = parent->find_nonpublic_declarer(member_id, level_out)) {
                    return declarer;
                }
            }
        }
        if (cpp_base_class_) {
            return cpp_base_class_->find_nonpublic_declarer(member_id, level_out);
        }
        return nullptr;
    }

    bool derives_from(const class_definition* base) const {
        if (this == base) return true;
        for (const auto& parent : parent_classes_) {
            if (parent && parent->derives_from(base)) return true;
        }
        return cpp_base_class_ && cpp_base_class_->derives_from(base);
    }

    void refresh_access_chain_flag() {
        bool flag = !nonpublic_members_.empty();
        if (!flag) {
            for (const auto& parent : parent_classes_) {
                if (parent && parent->chain_has_nonpublic_) { flag = true; break; }
            }
            if (!flag && cpp_base_class_ && cpp_base_class_->chain_has_nonpublic_) flag = true;
        }
        chain_has_nonpublic_ = flag;
        for (const auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->refresh_access_chain_flag();
            }
        }
    }

    using copy_function = std::function<std::shared_ptr<void>(const void*)>;

    void set_copy_function(copy_function copier) {
        copy_function_ = std::move(copier);
        bump_method_epoch();   // nocopy verdicts of script classes over this base re-evaluate
    }

    bool has_copy_function() const {
        return copy_function_ != nullptr;
    }

    // 'nocopy class': instances refuse by-value copies, like a deleted C++ copy constructor.
    // Inherited: a class derived from a nocopy class is nocopy, and so is a script class over
    // a C++ base that has no copy function (a deleted copy constructor reads the same way).
    // Consulted on every object copy, so the chain verdict is memoized against method_epoch_,
    // which every base-class change and hot reload cascades into derived classes.
    void nocopy(bool value) { nocopy_ = value; nocopy_memo_epoch_ = 0; }
    bool nocopy() const {
        if (nocopy_memo_epoch_ != method_epoch_) {
            bool verdict = nocopy_;
            for (const auto& parent : parent_classes_) {
                if (verdict) { break; }
                verdict = parent && parent->nocopy();
            }
            if (!verdict && cpp_base_class_) { verdict = !cpp_base_class_->has_copy_function(); }
            nocopy_memo_ = verdict;
            nocopy_memo_epoch_ = method_epoch_;
        }
        return nocopy_memo_;
    }

    std::shared_ptr<void> copy_object(const void* src) const {
        if (!copy_function_) {
            throw runtime_error("No copy constructor available for class " + name_);
        }
        return copy_function_(src);
    }

    struct method_metadata {
        bool is_virtual = false;
        access_level access = access_level::public_access;
        size_t vtable_index = 0;
    };

    void set_method_metadata(std::string_view name, const method_metadata& metadata) {
        auto eng = engine_;
        if (!eng) return;
        uint64_t name_id = eng->symbolize(name);
        method_metadata_[name_id] = metadata;
    }

    const method_metadata* get_method_metadata(std::string_view name) const {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }

    method_metadata* get_method_metadata_mutable(std::string_view name) {
        auto eng = engine_;
        if (!eng) return nullptr;
        uint64_t name_id = eng->symbolize(name);
        auto it = method_metadata_.find(name_id);
        return it != method_metadata_.end() ? &it->second : nullptr;
    }

    struct method_info {
        std::shared_ptr<class_definition> owner_class;
        method_metadata* metadata;
    };

    method_info find_method(std::string_view name) {
        auto eng = engine_;
        if (!eng) return { nullptr, nullptr };

        uint64_t name_id = eng->symbolize(name);

        if (has_method(name_id)) {
            return { shared_from_this(), get_method_metadata_mutable(name) };
        }

        auto parent = get_parent();
        while (parent) {
            if (parent->has_method(name_id)) {
                return { parent, parent->get_method_metadata_mutable(name) };
            }
            parent = parent->get_parent();
        }

        return { nullptr, nullptr };
    }

    // Structural identity for hot reload: the class_decl subtree serialized WITHOUT
    // source locations (detail::structural_node_key). Exact compare — no collision
    // risk — and comment/whitespace/line-shift edits still match. Stored lazily by the
    // backends after each redefinition; empty = no key yet (first definition).
    const std::vector<uint8_t>& structural_key() const { return structural_key_; }
    void set_structural_key(std::vector<uint8_t> key, size_t member_count, size_t base_count) {
        structural_key_ = std::move(key);
        key_member_count_ = member_count;
        key_base_count_ = base_count;
    }
    // O(1) discriminators — only walk the new AST when these already agree.
    bool structural_key_shape_matches(size_t member_count, size_t base_count) const {
        return !structural_key_.empty() && key_member_count_ == member_count && key_base_count_ == base_count;
    }
    // Deterministic fast-path observability (tests assert this instead of wall-clock).
    size_t identical_redefinitions() const { return identical_redefinitions_; }

    void redefine_class(const std::unordered_map<uint64_t, script_value>& new_field_defaults,
                        const std::unordered_map<uint64_t, script_value>& new_methods,
                        const std::unordered_map<uint64_t, script_value>& new_static_methods,
                        engine* engine_ref,
                        bool structurally_identical = false) {
        if (engine_ref) { engine_ref->recover_parallel_registrations(); }
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        ++reflection_generation_;
        if (engine_ref) { engine_ref->bump_class_definition_epoch(); }

        if (structurally_identical) {
            // Identical reload (structural AST equality, established by the caller):
            // ADOPT the new ASTs — methods were re-minted from the fresh parse, so
            // debugger/error positions track cosmetic shifts — and skip all
            // per-instance and derived-class work (no field or body changed;
            // set_class_definition would re-point instances at this same object).
            methods_ = new_methods;
            static_methods_ = new_static_methods;
            bump_method_epoch();   // dispatchers re-minted: per-site inline caches must refill
            recompute_property_getters(new_methods, engine_ref);
            store_field_defaults(new_field_defaults, engine_ref);
            method_metadata_.clear();
            ++identical_redefinitions_;
            return;
        }
        bool fields_changed = false;

        if (field_defaults_.size() != new_field_defaults.size()) {
            fields_changed = true;
        } else {
            for (const auto& [id, _] : field_defaults_) {
                if (new_field_defaults.find(id) == new_field_defaults.end()) {
                    fields_changed = true;
                    break;
                }
            }
            if (!fields_changed) {
                for (const auto& [id, _] : new_field_defaults) {
                    if (field_defaults_.find(id) == field_defaults_.end()) {
                        fields_changed = true;
                        break;
                    }
                }
            }
        }

        // A field that kept its id but changed declared type (int -> string) leaves the id-set
        // identical, so the comparison above stays false and migration would be skipped. The old
        // stored default can't reveal this (instance-field defaults are null placeholders until
        // the first reload), so detect it against a live instance: a held value whose kind no
        // longer matches the new non-null default means the type changed. migrate_fields then
        // resets exactly those fields.
        if (!fields_changed) {
            for (const auto& weak_instance : instances_) {
                auto instance = weak_instance.lock();
                if (!instance) continue;
                for (const auto& [id, new_default] : new_field_defaults) {
                    if (new_default.is_null() || !instance->has_field_value(id)) continue;
                    if (instance->get_field(id).deref().current_type() != new_default.deref().current_type()) {
                        fields_changed = true;
                        break;
                    }
                }
                if (fields_changed) break;
            }
        }

        // A declared-type change (int f -> string f) with an identical id-set is invisible
        // to the comparisons above; replace_field_declared_types flagged it.
        if (declared_types_dirty_) {
            fields_changed = true;
            declared_types_dirty_ = false;
        }

        std::set<uint64_t> old_fields;
        if (fields_changed) {
            for (const auto& [id, _] : field_defaults_) {
                old_fields.insert(id);
            }
        }

        uint64_t migrate_method_id = 0;
        if (engine_ref) {
            migrate_method_id = engine_ref->symbolize("hot_reload_migrate");
        }
        auto migrate_method_it = new_methods.find(migrate_method_id);

        methods_ = new_methods;
        static_methods_ = new_static_methods;
        bump_method_epoch();   // dispatchers replaced: per-site inline caches must refill
        bump_field_shape_epoch();
        if (engine_ref) { engine_ref->note_class_shape_change(); }

        recompute_property_getters(new_methods, engine_ref);

        store_field_defaults(new_field_defaults, engine_ref);

        // Migrate against a SNAPSHOT of the current instances. A hot_reload_migrate hook can
        // construct new same-class instances, which register into instances_ mid-migration;
        // iterating the live vector would migrate those too (re-invoking the hook) and loop
        // unboundedly. Snapshotted instances built against the new definition need no
        // migration, so skipping them is also correct.
        std::vector<std::weak_ptr<class_instance>> to_migrate = instances_;
        for (auto& weak_instance : to_migrate) {
            auto instance = weak_instance.lock();
            if (!instance) {
                continue;
            }
            if (fields_changed) {
                for (const auto& [name, default_value] : new_field_defaults) {
                    if (!instance->has_field(name)) {
                        instance->set_field(name, default_value.clone());
                    }
                }

                if (migrate_method_it != new_methods.end() && !migrate_method_it->second.is_null()) {
                    // Per-instance isolation: one instance's migrate hook erroring (throwing,
                    // or returning an error) must not abort the reload for the others.
                    try {
                        auto instance_value = script_value::make_object(name_, instance, engine_ref);
                        const script_function& migrate_func = migrate_method_it->second.as_function();
                        std::vector<script_value> args = {instance_value};
                        auto result = migrate_func(args);
                        (void)result;
                    } catch (...) {
                        // A migrate hook that throws (script throw, runtime error, anything)
                        // must not abort the reload for the other instances. Catch-all because
                        // a script throw is not necessarily a std::exception.
                    }
                }

                instance->migrate_fields(old_fields, new_field_defaults);
                if (engine_) { engine_->count_hot_reload_field_migration(); }
            }

            try {
                instance->set_class_definition(shared_from_this());
            } catch (const std::exception& e) {
                (void)e;
            }
        }
        // Drop dead weak refs; keep live ones plus any registered during migration.
        instances_.erase(
            std::remove_if(instances_.begin(), instances_.end(),
                [](const std::weak_ptr<class_instance>& w) { return w.expired(); }),
            instances_.end());
        reindex_instances();

        method_metadata_.clear();

        // Propagate to derived classes (their inherited fields changed), snapshotting for the
        // same reason as above, then drop any dead derived refs.
        std::vector<std::weak_ptr<class_definition>> derived_snapshot = derived_classes_;
        for (auto& weak_derived : derived_snapshot) {
            if (auto derived = weak_derived.lock()) {
                derived->update_instances_from_base();
            }
        }
        derived_classes_.erase(
            std::remove_if(derived_classes_.begin(), derived_classes_.end(),
                [](const std::weak_ptr<class_definition>& w) { return w.expired(); }),
            derived_classes_.end());
    }

    void update_instances_from_base() {
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        const auto& all_fields = get_all_field_defaults();
        std::set<uint64_t> dummy_old_fields;

        // migrate_fields keeps each instance's runtime value but resets any field whose declared
        // type changed against its new non-null default — so a base reload that retypes an
        // inherited field resets it on derived instances, while a base field still carrying a
        // null placeholder (never reloaded) leaves derived runtime data untouched.
        for (auto& weak_instance : instances_) {
            if (auto instance = weak_instance.lock()) {
                instance->migrate_fields(dummy_old_fields, all_fields);
                if (engine_) { engine_->count_hot_reload_field_migration(); }
            }
        }

        for (auto& weak_derived : derived_classes_) {
            if (auto derived = weak_derived.lock()) {
                derived->update_instances_from_base();
            }
        }
    }

    // Parallel workers construct and copy instances: registry mutations serialize on the
    // engine's region lock while a region runs (a never-taken branch otherwise).
    std::unique_lock<std::mutex> registry_lock() const {
        return engine_ && engine_->parallel_region_active() ? engine_->parallel_instance_registry_lock()
                                                            : std::unique_lock<std::mutex>();
    }

    void register_instance(std::weak_ptr<class_instance> instance, class_instance* live = nullptr) {
        if (engine_ && engine_->parallel_region_active()) {
            // A pool worker parks the registration (no lock; the join registers survivors);
            // main's own chunk registers under the lock a worker's rare registered-instance
            // death also takes
            if (auto* pending = engine_->parallel_pending_registrations()) {
                pending->push_back(std::move(instance));
                return;
            }
        }
        auto lock = registry_lock();
        register_instance_after_region(std::move(instance), nullptr, live);
    }

    // The join (main thread, region over: the plain unlocked path): a worker's surviving instance
    void register_instance_after_region(std::weak_ptr<class_instance> instance,
                                        const detail::fault_injection* faults = nullptr, class_instance* live_hint = nullptr) {
        const size_t index = instances_.size();
        if (faults) { faults->checkpoint("registry.instance_append", index); }
        instances_.push_back(std::move(instance));
        // Publish the index only after the vector owns the entry. A failed append
        // must leave the instance unregistered, not pointing at a later survivor.
        if (live_hint) { live_hint->registry_slot_ = index; }   // the caller owns it: no lock round trip
        else if (auto live = instances_.back().lock()) { live->registry_slot_ = index; }
    }

    // O(1) swap-remove by the instance's remembered index. Called from ~class_instance.
    // The caller's own entry expired the moment its last owner let go, BEFORE this lock: while
    // a region runs, another instance dying under the lock may have swap-removed across it,
    // moving the expired entry or popping it and letting a fresh registration refill the
    // index. So the entry is removed only when it is this instance's own (control-block
    // identity survives expiry), and expired tails come off first so whatever moves into the
    // hole is live and re-points; a mismatch means the entry already went or lingers elsewhere
    // as an expired weak, which every reader skips. Never-registered instances skip.
    void unregister_instance(class_instance* instance) {
        if (instance->registry_slot_ == SIZE_MAX) { return; }   // pending or never registered: nothing to remove, no lock
        std::shared_ptr<class_instance> moved;   // may be the last owner (a worker's copy dying concurrently): released outside the lock
        {
            auto lock = registry_lock();
            const size_t i = instance->registry_slot_;
            instance->registry_slot_ = SIZE_MAX;
            if (i >= instances_.size()) { return; }
            const std::weak_ptr<class_instance> self = instance->weak_from_this();
            const auto& entry = instances_[i];
            if (entry.owner_before(self) || self.owner_before(entry)) { return; }   // not this instance's entry
            while (instances_.size() - 1 > i && instances_.back().expired()) { instances_.pop_back(); }
            const size_t last = instances_.size() - 1;
            if (i != last) {
                instances_[i] = std::move(instances_[last]);
                moved = instances_[i].lock();
                if (moved) { moved->registry_slot_ = i; }
            }
            instances_.pop_back();
        }
    }

    // After a compaction the surviving entries' indexes must be reassigned
    void reindex_instances() {
        for (size_t k = 0; k < instances_.size(); ++k) {
            if (auto live = instances_[k].lock()) { live->registry_slot_ = k; }
        }
    }

private:
    std::string name_;
    mutable std::string_view interned_name_;
    mutable uint64_t interned_name_id_ = UINT64_MAX;
    uint64_t type_id_;
    bool auto_registered_ = false;
    bool cpp_backed_ = false;
    uint64_t reflection_generation_ = 0;
    std::vector<uint64_t> field_order_;
    const std::type_info* cpp_type_ = nullptr;
	struct cpp_base_cast {
		std::type_index base;
		void* (*adjust)(void*);
	};
	std::vector<cpp_base_cast> cpp_base_casts_;
    type_info_ptr type_info_;
    engine* engine_ = nullptr;
    std::unordered_map<uint64_t, script_value> methods_;
    std::unordered_map<uint64_t, std::vector<size_t>> method_arities_;
    // One bound C++ method overload: its parameter-type signature (empty = untyped/legacy) and the
    // type-erased call. Multiple entries per (name, arity) are same-arity overloads resolved by type.
    struct cpp_overload_entry {
        std::vector<param_type_info> param_types;
        script_function func;
    };
    std::unordered_map<uint64_t, std::unordered_map<size_t, std::vector<cpp_overload_entry>>> cpp_method_overloads_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> method_overloads_;

    struct overload_cache_key {
        uint64_t name_id;
        size_t arity;
        bool operator==(const overload_cache_key& other) const noexcept {
            return name_id == other.name_id && arity == other.arity;
        }
    };
    struct overload_cache_hash {
        size_t operator()(const overload_cache_key& k) const noexcept {
            return std::hash<uint64_t>{}(k.name_id) ^ (std::hash<size_t>{}(k.arity) << 32);
        }
    };
    mutable std::unordered_map<overload_cache_key, std::shared_ptr<function_decl>, overload_cache_hash> overload_resolution_cache_;

    std::unordered_map<uint64_t, script_value> static_methods_;
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> static_method_overloads_;
    std::unordered_map<uint64_t, std::vector<size_t>> static_method_arities_;
    std::unordered_map<uint64_t, script_value> field_defaults_;
    std::unordered_map<uint64_t, type_info_ptr> field_declared_types_;
    bool declared_types_dirty_ = false;
    std::unordered_map<uint64_t, script_value> static_field_values_;
    std::unordered_set<uint64_t> static_fields_;
    std::unordered_map<uint64_t, type_info_ptr> static_declared_types_;
    std::vector<std::shared_ptr<class_definition>> parent_classes_;
    mutable std::unordered_map<uint64_t, script_value> all_field_defaults_cache_;
    uint64_t defaults_version_ = 1;                       // bumps with every defaults-cache invalidation (own class)
    // Instance plan: the chain's merged defaults as (slot, value) in slot order, so
    // create_instance is one resize + one cell copy per slot instead of three hashes
    // and an enforcing store per field. Valid while every class in the chain keeps the
    // defaults_version_ it was built under (chain_defaults_version sums them).
    mutable std::vector<std::pair<uint32_t, script_value>> instance_plan_;
    mutable uint64_t instance_plan_version_ = 0;
    mutable bool field_defaults_cache_valid_ = false;
    // Ids whose methods_ entry is the SYNTHESIZED field accessor (see user_accessor)
    std::unordered_set<uint64_t> auto_accessor_ids_;
    // Flat-field slot registry (tier-1 flat classes): APPEND-ONLY — a field id keeps
    // its slot for the class's lifetime (re-added fields REUSE their retired slot), so
    // instance storage never re-indexes across hot reloads. Slot existence is NOT
    // liveness (get_all_field_defaults is); ensure_field_slots walks the chain
    // base-first, so first-build slot order ≈ declaration order.
    mutable std::unordered_map<uint64_t, uint32_t> field_slots_;
    mutable std::vector<uint64_t> slot_to_field_;
    mutable std::vector<uint8_t> cpp_property_slots_;   // see slot_is_cpp_property
    mutable bool has_cpp_property_slots_ = false;
    mutable bool field_slots_current_ = false;   // re-walk the chain on next query
    mutable uint64_t cpp_object_field_id_ = 0;
    mutable script_value null_field_value_;
    mutable uint64_t to_bool_memo_epoch_ = UINT64_MAX;
    mutable bool may_define_to_bool_ = true;

    class_type class_type_;
    access_level default_access_ = access_level::public_access;
    std::unordered_map<uint64_t, access_level> nonpublic_members_;
    bool chain_has_nonpublic_ = false;

    std::shared_ptr<class_definition> cpp_base_class_;
    script_slot_hooks script_slot_hooks_;
    bool extended_ever_ = false;

    std::unordered_map<uint64_t, uint64_t> property_setter_ids_;
    std::unordered_map<uint64_t, uint64_t> property_getter_ids_;
    std::unordered_set<uint64_t> read_only_property_ids_;
    std::unordered_map<uint64_t, cpp_member_accessor> property_member_accessors_;

    bool has_property_getters_ = false;

    // See method_epoch(): per-call-site inline-cache validity stamp
    uint64_t method_epoch_ = 1;
    unit_stamp unit_{};   // the unit and execution of the class declaration: field initializers resolve in it
    // See field_shape_epoch(): field-proof validity stamp
    uint64_t field_shape_epoch_ = 1;

    // Destructor probe memo (see create_instance): valid while dtor_memo_epoch_
    // matches method_epoch_. mutable: create_instance is the hot mint path.
    mutable uint64_t dtor_memo_epoch_ = 0;   // method_epoch_ starts at 1 => first mint fills
    mutable uint64_t dtor_id_ = 0;
    mutable bool has_script_dtor_ = false;

    bool is_transparent_wrapper_ = false;
    std::function<script_value(script_value&, engine*)> unwrap_function_;

    copy_function copy_function_;
    bool nocopy_ = false;
    mutable bool nocopy_memo_ = false;
    mutable uint64_t nocopy_memo_epoch_ = 0;

    std::unordered_map<uint64_t, method_metadata> method_metadata_;

    std::vector<std::weak_ptr<class_instance>> instances_;

    std::vector<std::weak_ptr<class_definition>> derived_classes_;

    std::vector<uint8_t> structural_key_;
    size_t key_member_count_ = 0;
    size_t key_base_count_ = 0;
    size_t identical_redefinitions_ = 0;

    // flat_value_semantics() cache + recursion internals (class_registry.cpp)
    mutable bool flat_value_cached_ = false;
    mutable uint64_t flat_value_epoch_ = 0;
    bool flat_value_semantics_impl(std::vector<const class_definition*>& in_progress) const;
    mutable std::vector<uint32_t> handle_slots_cached_;
    mutable std::vector<uint32_t> string_slots_cached_;
    mutable uint64_t string_slots_epoch_ = 0;
    mutable uint64_t handle_slots_epoch_ = 0;
    bool flat_field_type(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const;

    void recompute_property_getters(const std::unordered_map<uint64_t, script_value>& new_methods, engine* engine_ref) {
        has_property_getters_ = false;
        for (const auto& parent : parent_classes_) {
            if (parent && parent->has_property_getters()) {
                has_property_getters_ = true;
                break;
            }
        }
        if (!has_property_getters_ && cpp_base_class_ && cpp_base_class_->has_property_getters()) {
            has_property_getters_ = true;
        }
        if (!has_property_getters_ && engine_ref) {
            for (const auto& [method_id, _] : new_methods) {
                std::string_view name = engine_ref->get_symbolizer()->get_string(method_id);
                if (name.size() > 5 && name.substr(0, 5) == "_get_") {
                    has_property_getters_ = true;
                    break;
                }
            }
        }
    }

    void store_field_defaults(const std::unordered_map<uint64_t, script_value>& new_field_defaults, engine* engine_ref) {
        field_defaults_.clear();
        field_defaults_cache_valid_ = false; ++defaults_version_;
        field_slots_current_ = false;
        bump_method_epoch();
        for (const auto& [id, value] : new_field_defaults) {
            if (value.get_engine() == nullptr && engine_ref) {
                script_value value_with_engine(value);
                value_with_engine.set_engine(engine_ref);
                field_defaults_[id] = value_with_engine;
            } else {
                field_defaults_[id] = value;
            }
        }
    }
};

inline std::shared_ptr<class_definition> make_script_class_definition(const std::string& class_name, engine* eng) {
    return std::make_shared<class_definition>(class_name, class_definition::script_class, eng);
}

// Out-of-line class_instance implementations (need class_definition to be complete)

inline script_value* class_instance::find_field_value(uint64_t id) {
    if (class_def_) {
        const uint32_t slot = class_def_->field_slot(id);
        if (slot != class_definition::k_no_field_slot) {
            if (slot >= flat_fields_.size()) {
                return nullptr;
            }
            script_value* v = flat_fields_[slot].get();
            return (v && v->raw_storage_index() != script_value::TYPEID_INVALID) ? v : nullptr;
        }
    }
    if (overflow_fields_) {
        auto it = overflow_fields_->find(id);
        if (it != overflow_fields_->end()) {
            return &it->second;
        }
    }
    return nullptr;
}

inline const script_value* class_instance::find_field_value(uint64_t id) const {
    return const_cast<class_instance*>(this)->find_field_value(id);
}

inline script_value& class_instance::materialize_field(uint64_t id, script_value&& v) {
    if (class_def_) {
        const uint32_t slot = class_def_->field_slot(id);
        if (slot != class_definition::k_no_field_slot) {
            if (slot >= flat_fields_.size()) {
                flat_fields_.resize((std::max)(static_cast<size_t>(class_def_->field_slot_count()),
                                               static_cast<size_t>(slot) + 1));
            }
            auto& cell = flat_fields_[slot];
            if (!cell) {
                cell = make_cell(std::move(v));
            } else {
                *cell = std::move(v);
            }
            return *cell;
        }
    }
    if (!overflow_fields_) {
        overflow_fields_ = std::make_unique<std::unordered_map<uint64_t, script_value>>();
    }
    return overflow_fields_->insert_or_assign(id, std::move(v)).first->second;
}

inline void class_instance::seed_fields_from_plan(const std::vector<std::pair<uint32_t, script_value>>& plan, uint32_t slot_count) {
    if (flat_fields_.size() < slot_count) { flat_fields_.resize(slot_count); }
    seeded_from_plan_ = true;
    for (const auto& [slot, value] : plan) {
        auto& cell = flat_fields_[slot];
        if (!cell) { cell = make_cell(script_value(value)); } else { *cell = value; }
    }
}

inline void class_instance::set_field_unchecked(uint64_t id, const script_value& value) {
    if (script_value* existing = find_field_value(id); existing && existing->is_reference()) {
        existing->deref() = value;
        return;
    }
    materialize_field(id, script_value(value));
}

inline void class_instance::rehome_overflow_fields() {
    // Declared ids written before the definition arrived (deserialization constructs,
    // then classifies) move into their slots; runtime-only ids stay in overflow.
    if (!class_def_ || !overflow_fields_) {
        return;
    }
    for (auto it = overflow_fields_->begin(); it != overflow_fields_->end();) {
        if (class_def_->field_slot(it->first) != class_definition::k_no_field_slot) {
            materialize_field(it->first, std::move(it->second));
            it = overflow_fields_->erase(it);
        } else {
            ++it;
        }
    }
    if (overflow_fields_->empty()) {
        overflow_fields_.reset();
    }
}

inline void class_instance::set_class_definition(std::shared_ptr<class_definition> class_def) {
    class_def_ = class_def.get();
    if (class_def_) { reserve_cell_arena(class_def_->field_slot_count()); }
    rehome_overflow_fields();
}

inline void class_instance::reclass(const std::shared_ptr<class_definition>& class_def) {
    if (!class_def || class_def.get() == class_def_) { return; }
    std::vector<std::pair<uint64_t, field_cell>> cells;
    if (class_def_) {
        cells.reserve(flat_fields_.size());
        for (size_t slot = 0; slot < flat_fields_.size(); ++slot) {
            if (flat_fields_[slot]) {
                cells.emplace_back(class_def_->slot_field(static_cast<uint32_t>(slot)), std::move(flat_fields_[slot]));
            }
        }
        class_def_->unregister_instance(this);
    }
    flat_fields_.clear();
    class_def_ = class_def.get();
    class_name_ = class_def->interned_name();
    bound_method_cache_.clear();
    flat_fields_.resize(class_def_->field_slot_count());
    for (auto& [id, cell] : cells) {
        const uint32_t slot = class_def_->field_slot(id);
        if (slot != class_definition::k_no_field_slot) {
            if (slot >= flat_fields_.size()) { flat_fields_.resize(static_cast<size_t>(slot) + 1); }
            flat_fields_[slot] = std::move(cell);
        } else if (cell->raw_storage_index() != script_value::TYPEID_INVALID) {
            if (!overflow_fields_) {
                overflow_fields_ = std::make_unique<std::unordered_map<uint64_t, script_value>>();
            }
            overflow_fields_->insert_or_assign(id, std::move(*cell));
        }
    }
    rehome_overflow_fields();
    class_def_->register_instance(weak_from_this());
}

template <typename F>
inline void class_instance::for_each_field(F&& fn) {
    if (class_def_) {
        const size_t n = flat_fields_.size();
        for (size_t slot = 0; slot < n; ++slot) {
            script_value* v = flat_fields_[slot].get();
            if (v && v->raw_storage_index() != script_value::TYPEID_INVALID) {
                fn(class_def_->slot_field(static_cast<uint32_t>(slot)), *v);
            }
        }
    }
    if (overflow_fields_) {
        for (auto& [id, value] : *overflow_fields_) {
            fn(id, value);
        }
    }
}

template <typename F>
inline void class_instance::for_each_field(F&& fn) const {
    if (class_def_) {
        const size_t n = flat_fields_.size();
        for (size_t slot = 0; slot < n; ++slot) {
            const script_value* v = flat_fields_[slot].get();
            if (v && v->raw_storage_index() != script_value::TYPEID_INVALID) {
                fn(class_def_->slot_field(static_cast<uint32_t>(slot)), *v);
            }
        }
    }
    if (overflow_fields_) {
        for (const auto& [id, value] : *overflow_fields_) {
            fn(id, value);
        }
    }
}

inline size_t class_instance::field_value_count() const {
    size_t n = 0;
    for_each_field([&](uint64_t, const script_value&) { ++n; });
    return n;
}

inline void class_instance::copy_fields_from(const class_instance& other) {
    other.for_each_field([&](uint64_t id, const script_value& value) {
        materialize_field(id, value.clone_element());
    });
}

namespace detail {

// The ONE conversion table for every typed slot (docs/static_checking.md rule table):
// declaration/initializer, `=`, compound store-back, the cast<T> value form, by-value
// parameter binding, typed return and field store all classify the source by STORAGE
// (never by its tag - a shared_ptr<C> handle IS a C) and run these rows. This is the
// script-free half; rows that must run script (a converting ctor, to_X(), object
// to_bool()) come back as needs_* verdicts for detail::convert_for_typed_slot
// (typed_slot.hpp) to resolve. The C++ set_field API and hot-reload migration stop
// here by policy. Identity never touches the value, so a parameter site keeps its
// arg / derefed-arg return shapes; converted payloads come back UNTAGGED (each site
// tags as it always did). Each site owns its rejection texts.
struct slot_conversion {
    enum verdict : uint8_t {
        identity,            // the slot holds the value as it is
        identity_exact,      // identity by an exact class-name tag (parameter sites return the derefed arg)
        converted,           // `value` holds the converted payload
        reject,              // no row (non-class target; primitive into a class without a viable ctor)
        reject_class,        // class into an unrelated class without a viable ctor
        container_mismatch,  // element boundary failed: offending / expected filled
        handle_target,       // non-null into shared_ptr<T> / weak_ptr<T>: the site's own rows (outside the table)
        needs_ctor,          // class target: only a true 1-param ctor can answer (script)
        needs_to_x,          // primitive target from an object: to_int/to_float/to_char/to_string (script)
        needs_to_bool        // bool target from an object: to_bool() or truthy (script)
    };
    verdict v = reject;
    std::optional<script_value> value;
    std::unique_ptr<std::pair<std::string, std::string>> mismatch;   // container_mismatch only: offending / expected

    bool needs_script() const { return v == needs_ctor || v == needs_to_x || v == needs_to_bool; }
    static slot_conversion of(verdict verdict_) { slot_conversion out; out.v = verdict_; return out; }
    static slot_conversion converted_to(script_value v_) { slot_conversion out; out.v = converted; out.value = std::move(v_); return out; }
};

// The class behind an object-storage value: class_instance wrappers resolve through the
// instance (a plain wrapper of an extended object re-points to its script layer, exactly
// like the backends' resolve_member_target), raw holders through the engine registry.
struct slot_class_view {
    std::shared_ptr<class_instance> instance;
    std::shared_ptr<class_definition> engine_def;
    class_definition* def = nullptr;
};

inline slot_class_view resolve_slot_class(const script_value& v, engine* eng) {
    slot_class_view view;
    auto holder = v.get_object_holder();
    if (!holder) {
        return view;
    }
    if (holder->is_class_instance_wrapper) {
        view.instance = std::static_pointer_cast<class_instance>(holder->data);
        if (view.instance) {
            view.def = view.instance->get_class_definition();
            if (view.def && !view.def->is_script_class() && view.def->extended_ever()) {
                if (auto hooks = view.def->slot_hooks()) {
                    auto layer = hooks->read(view.instance->cpp_object_pointer());
                    if (layer && layer != view.instance && layer->get_engine() == eng) {
                        holder->data = layer;
                        view.instance = std::move(layer);
                        view.def = view.instance->get_class_definition();
                    }
                }
            }
        }
    } else if (eng) {
        view.engine_def = holder->type_id != UINT64_MAX ? eng->get_class_definition(holder->type_id)
                                                        : eng->get_class_definition(std::string(holder->type_name));
        view.def = view.engine_def.get();
    }
    return view;
}

// Does an object-storage value already satisfy a `target`-classed slot? Empty handles
// answer by their tag's class; live objects by their class (subtypes included), the
// holder's registered name, the tag, or - unknown host objects without a tag - by the
// permissive fallback the field-reference gate keeps.
inline bool class_slot_admits(const slot_class_view& view, const script_value& v, const type_info& target, engine* eng) {
    const std::string& want = target.type_name;
    if (want.empty()) {
        return true;
    }
    const type_info* tag = v.get_type_info().get();
    const bool tagged = tag && tag->base_type != script_value_type::jai_any_type;
    if (tagged && tag->type_name == want) {
        return true;
    }
    auto holder = v.get_object_holder();
    if (!holder) {
        if (!tagged || tag->type_name.empty()) {
            return true;   // an untagged empty handle is null-like
        }
        auto tag_def = eng ? eng->get_class_definition(tag->type_name) : nullptr;
        return tag_def && tag_def->is_subtype_of(want);
    }
    if (view.def && view.def->is_subtype_of(want)) {
        return true;
    }
    if (holder->type_name == want) {
        return true;
    }
    return !view.def && !tagged;
}

// A stored member an assignment expression can hand back: a field, a getter (`_get_x`, user or
// synthesized) or a method of that name somewhere in the chain. A setter-only member has no
// stored value to read, so its assignment yields the right-hand side. Host objects and unknowns
// answer yes (the read path decides).
inline bool member_reads_back(const script_value& v, uint64_t member_id, uint64_t getter_id, engine* eng) {
    auto view = resolve_slot_class(v, eng);
    if (!view.def) { return true; }
    std::vector<const class_definition*> stack{ view.def };
    int budget = 64;
    while (!stack.empty() && budget-- > 0) {
        const class_definition* cd = stack.back();
        stack.pop_back();
        if (cd->field_slot(member_id) != class_definition::k_no_field_slot || cd->has_method(getter_id) || cd->has_method(member_id)) { return true; }
        for (const auto& parent : cd->get_parent_classes()) { if (parent) { stack.push_back(parent.get()); } }
    }
    return false;
}

// weak_ptr<T> slot taking a shared_ptr handle (C++: weak = shared): the pointee class must be
// T or derive from it, then the handle downgrades to a weak one. false = not this row.
inline bool weak_slot_takes_shared(const script_value& value, const type_info_ptr& target_type, engine* eng) {
    if (!target_type || target_type->base_type != script_value_type::jai_weak_ptr_type ||
        value.type() != script_value_type::jai_shared_ptr_type) {
        return false;
    }
    auto expected_type = target_type->element_type();
    auto value_type_info = value.get_type_info();
    if (!expected_type || !value_type_info || expected_type->base_type == script_value_type::jai_any_type) {
        return true;
    }
    const std::string& expected_class = expected_type->type_name;
    const std::string& actual_class = value_type_info->element_type() ? value_type_info->element_type()->type_name
                                                                       : value_type_info->type_name;
    if (expected_class == actual_class) { return true; }
    auto actual_def = eng ? eng->get_class_definition(actual_class) : nullptr;
    return actual_def && actual_def->is_subtype_of(expected_class);
}

inline checked_result<slot_conversion> convert_for_typed_slot_core(const script_value& value_in, const type_info_ptr& target_type, engine* eng) {
    using verdict = slot_conversion::verdict;
    if (!target_type || target_type->base_type == script_value_type::jai_any_type) {
        return slot_conversion::of(verdict::identity);
    }
    const script_value& v = value_in.is_reference() ? value_in.deref() : value_in;
    const size_t ri = v.raw_storage_index();
    const auto target = target_type->base_type;
    // bound primitives classify by their semantic type; every other kind by storage
    const script_value_type src = v.current_type();
    const bool primitive_src = ri == script_value::TYPEID_INT || ri == script_value::TYPEID_FLOAT ||
                               ri == script_value::TYPEID_STRING || ri == script_value::TYPEID_BOOL ||
                               ri == script_value::TYPEID_CHAR ||
                               (ri == script_value::TYPEID_CPP_BOUND && src != script_value_type::jai_null_type);
    const bool object_src = ri == script_value::TYPEID_OBJECT ||
                            (ri == script_value::TYPEID_CPP_BOUND && src == script_value_type::jai_null_type);

    auto typed_null = [&]() {
        script_value null_val(std::monostate{}, eng);
        null_val.set_type_info(target_type);
        return slot_conversion::converted_to(std::move(null_val));
    };
    auto boundary = [&]() -> checked_result<slot_conversion> {
        if (v.get_type_info().get() == target_type.get()) {
            return slot_conversion::of(verdict::identity);   // the declared representation already
        }
        auto outcome = enforce_container_boundary(v, target_type, eng);
        if (outcome.error) {
            return checked_result<slot_conversion>(outcome.error->ec, outcome.error->static_msg, outcome.error->symbol_id, outcome.error->symbol_id2);
        }
        if (!outcome.value) {
            slot_conversion out = slot_conversion::of(verdict::container_mismatch);
            out.mismatch = std::make_unique<std::pair<std::string, std::string>>(std::move(outcome.offending), std::move(outcome.expected));
            return out;
        }
        return slot_conversion::converted_to(std::move(*outcome.value));
    };

    switch (target) {
        case script_value_type::jai_int_type:
            if (src == script_value_type::jai_int_type && primitive_src) return slot_conversion::of(verdict::identity);
			if (src == script_value_type::jai_float_type && primitive_src) {
				auto result = checked_float_to_integer(v.unchecked_as_float());
				if (!result) { return result.error_value(); }
				return slot_conversion::converted_to(script_value(result.value(), eng));
			}
            if (src == script_value_type::jai_bool_type && primitive_src) return slot_conversion::converted_to(script_value(static_cast<script_int>(v.unchecked_as_bool() ? 1 : 0), eng));
            if (object_src) return slot_conversion::of(verdict::needs_to_x);
            return slot_conversion::of(verdict::reject);
        case script_value_type::jai_float_type:
            if (src == script_value_type::jai_float_type && primitive_src) return slot_conversion::of(verdict::identity);
            if (src == script_value_type::jai_int_type && primitive_src) return slot_conversion::converted_to(script_value(static_cast<script_float>(v.unchecked_as_int()), eng));
            if (src == script_value_type::jai_bool_type && primitive_src) return slot_conversion::converted_to(script_value(static_cast<script_float>(v.unchecked_as_bool() ? 1.0 : 0.0), eng));
            if (object_src) return slot_conversion::of(verdict::needs_to_x);
            return slot_conversion::of(verdict::reject);
        case script_value_type::jai_char_type:
            if (src == script_value_type::jai_char_type && primitive_src) return slot_conversion::of(verdict::identity);
            if (object_src) return slot_conversion::of(verdict::needs_to_x);
            return slot_conversion::of(verdict::reject);
        case script_value_type::jai_string_type:
            if (src == script_value_type::jai_string_type && primitive_src) return slot_conversion::of(verdict::identity);
            if (object_src) return slot_conversion::of(verdict::needs_to_x);
            return slot_conversion::converted_to(script_value(v.to_string(), eng));
        case script_value_type::jai_utf8_string_type:
            if (ri == script_value::TYPEID_UTF8_STRING) return slot_conversion::of(verdict::identity);
            if (object_src) return slot_conversion::of(verdict::needs_to_x);
            return slot_conversion::converted_to(script_value::make_utf8(v.to_string(), eng));
        case script_value_type::jai_bool_type: {
            // is_truthy minus its object tail (KEEP BYTE-PARALLEL with the backends' is_truthy)
            if (src == script_value_type::jai_bool_type && primitive_src) return slot_conversion::of(verdict::identity);
            if (object_src) return slot_conversion::of(verdict::needs_to_bool);
            bool truthy = true;
            switch (ri) {
                case script_value::TYPEID_NULL: truthy = false; break;
                case script_value::TYPEID_INT: truthy = v.unchecked_as_int() != 0; break;
                case script_value::TYPEID_FLOAT: truthy = v.unchecked_as_float() != 0.0; break;
                case script_value::TYPEID_STRING: truthy = !v.unchecked_as_string().empty(); break;
                case script_value::TYPEID_UTF8_STRING: truthy = !v.unchecked_as_utf8().bytes.empty(); break;
                case script_value::TYPEID_CHAR: truthy = true; break;
                case script_value::TYPEID_ARRAY: { auto& arr = const_cast<script_value&>(v).get_array_storage(); truthy = arr && !arr->empty(); break; }
                case script_value::TYPEID_MAP: { auto& map = const_cast<script_value&>(v).get_map_storage(); truthy = map && !map->empty(); break; }
                case script_value::TYPEID_CPP_BOUND:
                    if (v.is_bool()) truthy = v.unchecked_as_bool();
                    else if (v.is_int()) truthy = v.unchecked_as_int() != 0;
                    else if (v.is_float()) truthy = v.unchecked_as_float() != 0.0;
                    else if (v.is_string()) truthy = !v.unchecked_as_string().empty();
                    else truthy = v.is_char() || v.get_cpp_bound_ptr() != nullptr;
                    break;
                default: break;
            }
            return slot_conversion::converted_to(script_value(truthy, eng));
        }
        case script_value_type::jai_array_type:
            if (ri == script_value::TYPEID_ARRAY) return boundary();
            return slot_conversion::of(verdict::reject);
        case script_value_type::jai_map_type:
            if (ri == script_value::TYPEID_MAP) return boundary();
            return slot_conversion::of(verdict::reject);
        case script_value_type::jai_object_type: {
            if (ri == script_value::TYPEID_NULL) return typed_null();
            if (object_src) {
                const type_info* tag = v.get_type_info().get();
                if (tag && !tag->type_name.empty() && !target_type->type_name.empty() && tag->type_name == target_type->type_name) {
                    return slot_conversion::of(verdict::identity_exact);
                }
                auto view = resolve_slot_class(v, eng);
                if (class_slot_admits(view, v, *target_type, eng)) {
                    return slot_conversion::of(verdict::identity);
                }
                return slot_conversion::of(v.get_object_holder() ? verdict::needs_ctor : verdict::reject_class);
            }
            if (primitive_src && !target_type->type_name.empty()) return slot_conversion::of(verdict::needs_ctor);
            return slot_conversion::of(verdict::reject);
        }
        case script_value_type::jai_shared_ptr_type:
        case script_value_type::jai_weak_ptr_type:
            if (ri == script_value::TYPEID_NULL) return typed_null();
            return slot_conversion::of(verdict::handle_target);
        case script_value_type::jai_function_type:
            // a callable slot resets to null and takes a callable again (a std::function's rule)
            if (ri == script_value::TYPEID_NULL) return typed_null();
            return slot_conversion::of(v.type() == target ? verdict::identity : verdict::reject);
        default:
            return slot_conversion::of(v.type() == target ? verdict::identity : verdict::reject);
    }
}

// The script rows (needs_* verdicts): a TRUE one-parameter ctor whose declared parameter
// accepts the source directly (no chaining), to_int/to_float/to_char/to_string, to_bool().
// Declared here so the field kernel below can reach it from class_definition.hpp; the body
// lives in typed_slot.hpp (defined out of line in environment.cpp).
checked_result<slot_conversion> convert_typed_slot_script_rows(slot_conversion pending, const script_value& value, const type_info_ptr& target_type, engine* eng);

// Field-store rows for shared_ptr<T> / weak_ptr<T> declared fields (the handle_target
// verdict): a same-kind tag stores as-is (its own tag), a live object or handle of T or a
// subclass stores into a shared_ptr<T> field, a dynamic holder is ground truth there.
inline std::optional<script_value> field_handle_rows(const script_value& v, const type_info_ptr& declared, engine* eng) {
    const auto target = declared->base_type;
    if (v.type() == target) {
        return v;
    }
    if (weak_slot_takes_shared(v, declared, eng)) {   // weak_ptr<T> field = shared handle (C++: weak = shared)
        auto weak = script_value::make_weak_ptr(v, eng);
        if (weak) { return std::move(weak.value()); }
        return std::nullopt;
    }
    if (target != script_value_type::jai_shared_ptr_type) {
        return std::nullopt;
    }
    const size_t ri = v.raw_storage_index();
    if (ri != script_value::TYPEID_OBJECT && ri != script_value::TYPEID_CPP_BOUND) {
        return std::nullopt;
    }
    if (v.get_object_holder() == nullptr || declared->type_name.empty()) {
        return v;
    }
    auto tag = v.get_type_info();
    if (!tag || tag->base_type == script_value_type::jai_any_type) {
        return v;
    }
    auto view = resolve_slot_class(v, eng);
    if (class_slot_admits(view, v, *declared, eng)) {
        return v;
    }
    return std::nullopt;
}

// STORE-READY field value for a resolved verdict: scalars/strings re-tag to the declared
// type (type_proofs.hpp retags_on_field_write), pointer-ish and container values keep
// their own tag (objects travel as object OR shared_ptr wrappers over one holder, so a
// retag would lie about the storage). A needs_* verdict here means the caller runs
// without script rows: no conversion. nullopt = no conversion.
inline std::optional<script_value> field_store_value(slot_conversion& out, const script_value& value_in, const type_info_ptr& declared, engine* eng) {
    using verdict = slot_conversion::verdict;
    const script_value& v = value_in.is_reference() ? value_in.deref() : value_in;
    std::optional<script_value> stored;
    switch (out.v) {
        case verdict::identity:
        case verdict::identity_exact:
            stored = v;
            break;
        case verdict::converted:
            stored = std::move(*out.value);
            break;
        case verdict::handle_target:
            stored = field_handle_rows(v, declared, eng);
            break;
        default:
            break;
    }
    if (stored) {
        switch (declared->base_type) {
            case script_value_type::jai_int_type: case script_value_type::jai_float_type:
            case script_value_type::jai_bool_type: case script_value_type::jai_char_type:
            case script_value_type::jai_string_type:
                stored->set_type_info(declared);
                break;
            default:
                break;
        }
    }
    return stored;
}

// ---- Diagnostic spellings shared by every typed-boundary and overload site (docs/diagnostics.md) ----

// Source spelling for rejection texts: the payload's own type - a class name for objects
// and handles, "null", the primitive names, "array" / "map" - never a var tag. Every spelling
// is already interned (base names at backend init, class names at definition), so an error
// path may intern it even while the symbolizer is frozen.
inline std::string slot_source_type_name(const script_value& value_in, engine* eng) {
    const script_value& v = value_in.is_reference() ? value_in.deref() : value_in;
    const type_info* tag = v.get_type_info().get();
    const bool named_tag = tag && tag->base_type != script_value_type::jai_any_type && !tag->type_name.empty();
    switch (v.raw_storage_index()) {
        case script_value::TYPEID_NULL: return "null";
        case script_value::TYPEID_OBJECT:
        case script_value::TYPEID_CPP_BOUND: {
            if (v.raw_storage_index() == script_value::TYPEID_CPP_BOUND && v.current_type() != script_value_type::jai_null_type) {
                return container_base_type_name(v.current_type());
            }
            if (named_tag) return tag->type_name;
            auto view = resolve_slot_class(v, eng);
            if (view.def) return view.def->get_name();
            if (auto holder = v.get_object_holder(); holder && !holder->type_name.empty()) return std::string(holder->type_name);
            return "object";
        }
        default:
            return container_base_type_name(v.current_type());
    }
}

// Declared-type spelling: the canonical name, shared_ptr<T> spelled in full
inline std::string slot_type_spelling(const type_info& t) {
    if (t.base_type == script_value_type::jai_shared_ptr_type) { return "shared_ptr<" + t.type_name + ">"; }
    return t.canonical_name();
}

// "(int, string)" for an overload miss: at most 6 spelled, then "..."
inline std::string argument_type_list(const script_value* args, size_t argc, engine* eng) {
    std::string out = "(";
    const size_t shown = argc < 6 ? argc : 6;
    for (size_t i = 0; i < shown; ++i) {
        if (i) { out += ", "; }
        out += slot_source_type_name(args[i], eng);
    }
    if (argc > shown) { out += ", ..."; }
    return out + ")";
}

// No overload / constructor takes these arguments: {0} = the callee, {1} = the argument list,
// composed only while the symbolizer is unfrozen (a parallel worker gets the list-free shape)
inline error_propagator overload_miss(std::error_code ec, uint64_t name_id, const script_value* args, size_t argc,
                                      engine* eng, bool constructor) {
    string_symbolizer* symbols = eng ? eng->get_symbolizer() : nullptr;
    if (symbols && !symbols->frozen()) {
        return {ec, constructor ? "No constructor of '{0}' takes {1}" : "No overload of '{0}' takes {1}",
                name_id, symbols->intern(argument_type_list(args, argc, eng))};
    }
    return {ec, constructor ? "No constructor of '{0}' matches this call" : "No overload of '{0}' matches this call", name_id, 0};
}

// Typed-boundary rejections (detail/slot_site.hpp): the site names its slot; the shape
// follows the site kind.
inline std::string callee_spelling(std::string_view callee) {
    return callee == "<lambda>" ? std::string("lambda") : "'" + std::string(callee) + "'";
}

inline std::string slot_description(const slot_site& site, const type_info& target) {
    const std::string declared = slot_type_spelling(target);
    switch (site.kind) {
        case slot_kind::parameter: {
            std::string out = "parameter '" + std::string(site.name) + "'";
            if (!site.owner.empty()) { out += " of " + callee_spelling(site.owner); }
            return out + " (declared " + declared + ")";
        }
        case slot_kind::return_value:
            return callee_spelling(site.owner) + " (declared -> " + declared + ")";
        case slot_kind::field:
        case slot_kind::static_field: {
            std::string out = std::string(site.kind == slot_kind::field ? "field '" : "static '") + std::string(site.name) + "'";
            if (!site.owner.empty()) { out += " of " + std::string(site.owner); }
            return out + " (declared " + declared + ")";
        }
        default:
            return "'" + std::string(site.name) + "' (declared " + declared + ")";
    }
}

// {0} = the source type, {1} = the slot description
inline std::string_view slot_rejection_message(slot_kind kind) {
    switch (kind) {
        case slot_kind::parameter: return "Cannot pass {0} to {1}";
        case slot_kind::return_value: return "Cannot return {0} from {1}";
        case slot_kind::declaration: return "Cannot initialize {1} with {0}";
        default: return "Cannot assign {0} to {1}";
    }
}

// Slot-less shapes ({1} = the declared type): an unnamed site, or a frozen symbolizer
inline std::string_view slot_rejection_message_unnamed(slot_kind kind, std::string_view declared) {
    const char c = declared.empty() ? 'x' : static_cast<char>(declared.front() | 0x20);
    const bool vowel = c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    switch (kind) {
        case slot_kind::parameter: return vowel ? "Cannot pass {0} to an {1} parameter" : "Cannot pass {0} to a {1} parameter";
        case slot_kind::return_value: return "Cannot return {0} from a function declared -> {1}";
        case slot_kind::declaration: return "Cannot initialize a variable declared {1} with {0}";
        case slot_kind::field: return "Cannot assign {0} to a field declared {1}";
        case slot_kind::static_field: return "Cannot assign {0} to a static declared {1}";
        default: return "Cannot assign {0} to a variable declared {1}";
    }
}

inline bool slot_site_named(const slot_site& site) {
    return site.kind == slot_kind::return_value ? !site.owner.empty() : !site.name.empty();
}

inline error_propagator typed_slot_rejection(const script_value& source, const type_info& target, const slot_site& site, engine* eng) {
    const std::error_code ec = make_error_code(runtime_error_code::type_mismatch);
    string_symbolizer* symbols = eng ? eng->get_symbolizer() : nullptr;
    if (!symbols) {
        return {ec, slot_rejection_message_unnamed(site.kind, target.canonical_name()), 0, target.id};
    }
    const uint64_t source_id = symbols->intern(slot_source_type_name(source, eng));
    if (slot_site_named(site) && !symbols->frozen()) {
        return {ec, slot_rejection_message(site.kind), source_id, symbols->intern(slot_description(site, target))};
    }
    return {ec, slot_rejection_message_unnamed(site.kind, target.canonical_name()), source_id, target.id};
}

// A compound assignment whose operator does not take its operands (docs/diagnostics.md, compound assignment): op 0..4 is
// += -= *= /= %=, {0} = the target's type, {1} = the operand's. Every spelling is already interned (see above).
inline constexpr std::string_view compound_mismatch_texts[5] = {
    "Cannot apply '+=' to {0} and {1}", "Cannot apply '-=' to {0} and {1}", "Cannot apply '*=' to {0} and {1}",
    "Cannot apply '/=' to {0} and {1}", "Cannot apply '%=' to {0} and {1}"};

inline error_propagator compound_mismatch(size_t op, const script_value& target, const script_value& operand, engine* eng) {
    const std::error_code ec = make_error_code(runtime_error_code::type_mismatch);
    const std::string_view text = compound_mismatch_texts[op < 5 ? op : 0];
    string_symbolizer* symbols = eng ? eng->get_symbolizer() : nullptr;
    if (!symbols) { return {ec, text}; }
    return {ec, text, symbols->intern(slot_source_type_name(target, eng)), symbols->intern(slot_source_type_name(operand, eng))};
}

// ++/-- on a target that is not a number: {0} = the target's type
inline error_propagator incdec_mismatch(bool increment, const script_value& target, engine* eng) {
    const std::error_code ec = make_error_code(runtime_error_code::invalid_numeric_operand);
    const std::string_view text = increment ? "Cannot apply '++' to {0}" : "Cannot apply '--' to {0}";
    string_symbolizer* symbols = eng ? eng->get_symbolizer() : nullptr;
    if (!symbols) { return {ec, text}; }
    return {ec, text, symbols->intern(slot_source_type_name(target, eng))};
}

// The same text as one string (host-side throws)
inline std::string typed_slot_rejection_text(const script_value& source, const type_info& target, const slot_site& site, engine* eng) {
    const std::string src = slot_source_type_name(source, eng);
    if (slot_site_named(site)) {
        return format_error_message(slot_rejection_message(site.kind), src, slot_description(site, target));
    }
    const std::string declared = target.canonical_name();
    return format_error_message(slot_rejection_message_unnamed(site.kind, declared), src, declared);
}

// The field store: the full table (script rows included) plus the store-ready shaping.
// KEEP the same-base head inline - every member store runs it. field_id / owner / is_static
// only name the slot in the rejection text (resolved on the error path alone).
inline checked_result<script_value> field_store_convert(const script_value& value, const type_info_ptr& declared, engine* eng,
                                                        uint64_t field_id, const class_definition* owner, bool is_static) {
    {
        const script_value& v = value.is_reference() ? value.deref() : value;
        size_t want = script_value::TYPEID_INVALID;
        switch (declared ? declared->base_type : script_value_type::jai_any_type) {
            case script_value_type::jai_int_type: want = script_value::TYPEID_INT; break;
            case script_value_type::jai_float_type: want = script_value::TYPEID_FLOAT; break;
            case script_value_type::jai_string_type: want = script_value::TYPEID_STRING; break;
            case script_value_type::jai_utf8_string_type: want = script_value::TYPEID_UTF8_STRING; break;
            case script_value_type::jai_bool_type: want = script_value::TYPEID_BOOL; break;
            case script_value_type::jai_char_type: want = script_value::TYPEID_CHAR; break;
            default: break;
        }
        if (want != script_value::TYPEID_INVALID && v.raw_storage_index() == want) {
            script_value stored = v;
            stored.set_type_info(declared);
            return stored;
        }
    }
	auto result = convert_for_typed_slot_core(value, declared, eng);
	if (!result) { return result.error_value(); }
	slot_conversion out = std::move(result.value());
    if (out.needs_script()) {
        auto resolved = convert_typed_slot_script_rows(std::move(out), value, declared, eng);
        if (!resolved) {
            return resolved.error_value();
        }
        out = std::move(resolved.value());
    }
    auto stored = field_store_value(out, value, declared, eng);
    if (!stored) {
        string_symbolizer* symbols = eng ? eng->get_symbolizer() : nullptr;
        return typed_slot_rejection(value, *declared,
            slot_site{is_static ? slot_kind::static_field : slot_kind::field,
                      field_id && symbols ? symbols->get_string(field_id) : std::string_view{},
                      owner ? std::string_view(owner->get_name()) : std::string_view{}}, eng);
    }
    return std::move(*stored);
}

// The no-script table (set_field, hot-reload migration): nullopt where a conversion would
// have to run script or does not exist.
inline std::optional<script_value> field_store_convert_no_script(const script_value& value, const type_info_ptr& declared, engine* eng) {
	auto result = convert_for_typed_slot_core(value, declared, eng);
	if (!result) { return std::nullopt; }
	return field_store_value(result.value(), value, declared, eng);
}

// Script-surface member access enforcement kernel — the ONE check both backends call
// verbatim at every member get/set/compound/call site (parity by construction).
// ctx = the class whose method body is currently executing (nullptr at top level /
// free functions). Rules: private = declaring class only; protected = declaring class
// and its subclasses. Host C++ APIs (get_field/set_field/get_fields, serialization)
// never call this — the host side stays unrestricted by design.
inline checked_result<void> enforce_member_access(const class_definition* cls, uint64_t member_id,
                                                  const class_definition* ctx) {
    access_level level = access_level::public_access;
    const class_definition* declarer = cls->find_nonpublic_declarer(member_id, level);
    if (!declarer || declarer == ctx) {
        return {};
    }
    if (level == access_level::protected_access && ctx && ctx->derives_from(declarer)) {
        return {};
    }
    return checked_result<void>(make_error_code(runtime_error_code::access_violation),
        level == access_level::private_access
            ? "Cannot access private member '{0}' of class '{1}'"
            : "Cannot access protected member '{0}' of class '{1}'",
        member_id, declarer->get_type_id());
}

} // namespace detail

inline checked_result<script_value> class_instance::enforce_field_write(uint64_t id, script_value value) const {
    if (!class_def_) {
        return std::move(value);
    }
    type_info_ptr declared = class_def_->get_field_declared_type(id);
    if (!declared) {
        // auto fields: inferred from the initialized value's tag, then enforced like locals
        const script_value* cur = find_field_value(id);
        type_info_ptr tag = (cur && !cur->is_reference()) ? cur->get_type_info() : nullptr;
        if (!tag || tag->base_type == script_value_type::jai_any_type ||
            tag->base_type == script_value_type::jai_null_type) {
            return std::move(value);
        }
        declared = tag;
    } else if (declared->base_type == script_value_type::jai_any_type) {
        return std::move(value);
    }
    return detail::field_store_convert(value, declared, engine_, id, class_def_);
}

inline checked_result<script_value> class_definition::enforce_static_write(uint64_t id, script_value value) const {
    auto it = static_declared_types_.find(id);
    if (it == static_declared_types_.end()) {
        return std::move(value);
    }
    type_info_ptr declared = it->second;
    if (!declared) {
        const script_value* cur = get_static_field_ptr(id);
        type_info_ptr tag = (cur && !cur->is_reference()) ? cur->get_type_info() : nullptr;
        if (!tag || tag->base_type == script_value_type::jai_any_type ||
            tag->base_type == script_value_type::jai_null_type) {
            return std::move(value);
        }
        declared = tag;
    } else if (declared->base_type == script_value_type::jai_any_type) {
        return std::move(value);
    }
    return detail::field_store_convert(value, declared, engine_, id, this, true);
}

inline void class_instance::note_script_layer_owner(const script_value& value) {
    if (!engine_ || !class_def_) { return; }
    auto cpp_base = class_def_->find_cpp_base_class();
    if (!cpp_base) { return; }
    auto holder = value.get_object_holder();
    if (!holder) { return; }
    void* object = holder->data ? holder->data.get() : holder->bound_ptr;
    if (!object) { return; }
    auto weak_self = weak_from_this();
    if (weak_self.expired()) { return; }
    if (auto hooks = cpp_base->slot_hooks()) {
        hooks->attach(object, weak_self);
        return;
    }
    engine_->note_script_layer(object, weak_self);
}

inline void class_instance::set_field(uint64_t id, const script_value& value) {
    // Null passes through: typed-null placeholders (create_instance defaults,
    // uninitialized fields) must remain storable from C++.
    if (class_def_ && !value.is_null()) {
        if (id == get_cpp_object_field_id()) [[unlikely]] {
            note_script_layer_owner(value);
        }
        if (type_info_ptr declared = class_def_->get_field_declared_type(id);
            declared && declared->base_type != script_value_type::jai_any_type) {
            auto converted = detail::field_store_convert_no_script(value, declared, engine_);
            if (!converted) {
                string_symbolizer* symbols = engine_ ? engine_->get_symbolizer() : nullptr;
                throw runtime_error(detail::typed_slot_rejection_text(value, *declared,
                    detail::slot_site{detail::slot_kind::field,
                                      symbols ? symbols->get_string(id) : std::string_view{},
                                      class_def_->get_name()}, engine_));
            }
            set_field_unchecked(id, *converted);
            return;
        }
    }
    set_field_unchecked(id, value);
}

inline void class_instance::migrate_fields(const std::set<uint64_t>& old_field_ids,
                                           const std::unordered_map<uint64_t, script_value>& new_field_defaults) {
    (void)old_field_ids;
    // IN-PLACE per-id migration: kept fields keep their storage CELL, so script_value
    // addresses stay stable — suspended coroutines' env chains and this-capturing
    // closures cache pointers to field cells, and a wholesale rebuild would dangle
    // every one of them. (Flat slots are append-only per class lifetime; a unique_ptr
    // pointee never moves.)
    for (const auto& [id, default_value] : new_field_defaults) {
        script_value* existing = find_field_value(id);
        type_info_ptr declared = class_def_ ? class_def_->get_field_declared_type(id) : nullptr;
        if (existing && !existing->is_reference() && !existing->is_null() &&
            declared && declared->base_type != script_value_type::jai_any_type) {
            // Hot-reload retype ruling (2026-07): a declared-type change CONVERTS the live
            // value wherever the assignment table allows (float->int truncates, int->string
            // via to_string, subtype objects stay); a value with NO conversion (object into
            // int, string into int) or one that converts only through script (a converting
            // ctor, to_X()) falls back to the new initializer default. Reloads stay permissive,
            // never run script and never brick an instance. Same-type fields pass the
            // kernel's same-type fast path and keep their value.
            if (auto converted = detail::field_store_convert_no_script(*existing, declared, engine_)) {
                *existing = std::move(*converted);
            } else {
                *existing = default_value.clone();
            }
            continue;
        }
        // Undeclared ('auto') / var / reference / null-valued fields keep the tag heuristic:
        // keep the existing runtime value, EXCEPT when a non-null new default's kind differs
        // from the held value (an auto field reloaded with a different-typed initializer), so
        // adopt the new default. A null default carries no type and never triggers a reset.
        if (existing) {
            bool retyped = !default_value.is_null() &&
                existing->deref().current_type() != default_value.deref().current_type();
            if (retyped) {
                *existing = default_value.clone();
            }
        } else {
            materialize_field(id, default_value.clone());
        }
    }

    // Drop fields absent from the new definition — EXCEPT the runtime-only _cpp_object
    // field (the C++ base object never appears in the declared defaults; dropping it
    // would destruct the C++ object and leave inherited C++ methods uncallable).
    // Slots drop IN PLACE (value overwritten with the invalid absent marker: the cell
    // address survives, the dropped value's resources free NOW like the old map erase).
    uint64_t cpp_id = get_cpp_object_field_id();
    if (class_def_) {
        const size_t n = flat_fields_.size();
        for (size_t slot = 0; slot < n; ++slot) {
            script_value* v = flat_fields_[slot].get();
            if (!v || v->raw_storage_index() == script_value::TYPEID_INVALID) {
                continue;
            }
            const uint64_t id = class_def_->slot_field(static_cast<uint32_t>(slot));
            if (id != cpp_id && new_field_defaults.find(id) == new_field_defaults.end()) {
                *v = script_value::make_invalid(engine_);
            }
        }
    }
    if (overflow_fields_) {
        for (auto it = overflow_fields_->begin(); it != overflow_fields_->end();) {
            if (it->first != cpp_id && new_field_defaults.find(it->first) == new_field_defaults.end()) {
                it = overflow_fields_->erase(it);
            } else {
                ++it;
            }
        }
    }
}

inline bool class_instance::is_script_class() const {
    return class_def_ && class_def_->is_script_class();
}

inline bool class_instance::is_cpp_class() const {
    return class_def_ && class_def_->is_cpp_class();
}

inline std::shared_ptr<void> class_instance::extract_cpp_object_impl(const script_value& val) {
    if (val.type() == script_value_type::jai_object_type) {
        auto obj_holder = val.get_object_holder();
        if (!obj_holder) {
            throw runtime_error("script_value is not an object type");
        }
        if (obj_holder->data) { return obj_holder->data; }
        // an extended object's non-owning holder: hand the pointer out as an unowned alias
        if (obj_holder->bound_ptr) { return std::shared_ptr<void>(std::shared_ptr<void>(), obj_holder->bound_ptr); }
        return nullptr;
    }
    return nullptr;
}

inline uint64_t class_instance::get_cpp_object_field_id() const {
    if (cpp_object_field_id_ == 0 && class_def_) {
        if (auto eng = class_def_->get_engine()) {
            cpp_object_field_id_ = eng->symbolize(class_constants::CPP_OBJECT_FIELD);
        }
    }
    return cpp_object_field_id_;
}

inline std::shared_ptr<class_instance> class_instance::deep_copy() const {
    if (class_def_ && class_def_->nocopy()) {
        throw runtime_error("Cannot copy an instance of nocopy class '" + std::string(get_class_name()) + "': hold it by reference (auto&/var&) or share it (shared_ptr)");
    }
    auto new_instance = std::make_shared<class_instance>(interned_name_t{}, class_name_, engine_);

    uint64_t cpp_obj_id = get_cpp_object_field_id();
    if (class_def_ && !overflow_fields_) {
        // Slot-to-slot copy with the definition attached up front: the general path
        // below routes every field through the overflow map and rehomes it (one map
        // node + one cell per field); flat instances copy their cells directly. Cells
        // clone through references exactly as set_field(id, value.clone()) did; the
        // C++ object field keeps its copy_object + script-layer hook route.
        new_instance->class_def_ = class_def_;
        new_instance->reserve_cell_arena(flat_fields_.size());
        new_instance->flat_fields_.resize(flat_fields_.size());
        for (size_t slot = 0; slot < flat_fields_.size(); ++slot) {
            const auto& cell = flat_fields_[slot];
            if (!cell || cell->raw_storage_index() == script_value::TYPEID_INVALID) { continue; }
            const uint64_t id = class_def_->slot_field(static_cast<uint32_t>(slot));
            if (id == cpp_obj_id && !cell->is_null()) {
                if (class_def_->has_copy_function()) {
                    auto cpp_obj = extract_cpp_object_impl(*cell);
                    if (cpp_obj) {
                        auto new_cpp_obj = class_def_->copy_object(cpp_obj.get());
                        if (!new_cpp_obj) {
                            throw runtime_error("Failed to copy C++ object of type '" + std::string(get_class_name()) +
                                "'. The registered copy function returned null.");
                        }
                        new_instance->set_field(cpp_obj_id,
                            script_value::make_cpp_object(std::string(get_class_name()), class_def_->get_type_id(), new_cpp_obj, cell->get_engine()));
                        continue;
                    }
                }
                new_instance->set_field(id, cell->clone_element());
                continue;
            }
            new_instance->flat_fields_[slot] = new_instance->make_cell(cell->clone_element());
        }
        class_def_->register_instance(std::weak_ptr<class_instance>(new_instance));
        return new_instance;
    }
    for_each_field([&](uint64_t id, const script_value& value) {
        if (id == cpp_obj_id && !value.is_null()) {
            if (class_def_ && class_def_->has_copy_function()) {
                auto cpp_obj = extract_cpp_object_impl(value);
                if (cpp_obj) {
                    auto new_cpp_obj = class_def_->copy_object(cpp_obj.get());

                    if (!new_cpp_obj) {
                        throw runtime_error("Failed to copy C++ object of type '" + std::string(get_class_name()) +
                            "'. The registered copy function returned null.");
                    }

                    auto eng_ref = value.get_engine();
                    new_instance->set_field(cpp_obj_id,
                        script_value::make_cpp_object(std::string(get_class_name()), class_def_->get_type_id(), new_cpp_obj, eng_ref));
                    return;
                }
            }
        }

        new_instance->set_field(id, value.clone_element());
    });

    // The copies above landed in overflow (no definition yet, matching the old
    // no-enforcement order); adopt the definition then slot them.
    new_instance->class_def_ = class_def_;
    new_instance->rehome_overflow_fields();
    // The copied C++ object is a new identity: register the copy as its script layer (the
    // set_field hook above ran before the definition existed and could not).
    if (cpp_obj_id != 0) {
        if (const script_value* copied = new_instance->find_field_value(cpp_obj_id); copied && !copied->is_null()) {
            new_instance->note_script_layer_owner(*copied);
        }
    }

    if (class_def_) {
        class_def_->register_instance(std::weak_ptr<class_instance>(new_instance));
    }

    return new_instance;
}

inline const script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) const {
    if (const script_value* v = find_field_value(id)) {
        return *v;
    }

    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            return default_it->second;
        }

        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    return parent_it->second;
                }
            }
        }
    }

    if (throw_if_missing) {
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline script_value& class_instance::get_field(uint64_t id, bool throw_if_missing) {
    if (script_value* v = find_field_value(id)) {
        return *v;
    }

    if (class_def_) {
        auto& field_defaults = class_def_->get_all_field_defaults();
        auto default_it = field_defaults.find(id);
        if (default_it != field_defaults.end()) {
            return materialize_field(id, default_it->second.clone());
        }

        for (const auto& parent : class_def_->get_parent_classes()) {
            if (parent) {
                auto& parent_defaults = parent->get_all_field_defaults();
                auto parent_it = parent_defaults.find(id);
                if (parent_it != parent_defaults.end()) {
                    return materialize_field(id, parent_it->second.clone());
                }
            }
        }

        auto cpp_base = class_def_->find_cpp_base_class();
        if (cpp_base) {
            auto& cpp_defaults = cpp_base->get_all_field_defaults();
            auto cpp_it = cpp_defaults.find(id);
            if (cpp_it != cpp_defaults.end()) {
                return materialize_field(id, cpp_it->second.clone());
            }
        }
    }

    if (throw_if_missing) {
        std::string field_name;
        if (class_def_) {
            if (auto eng = class_def_->get_engine()) {
                field_name = eng->get_symbolizer()->get_string(id);
            } else {
                field_name = std::to_string(id);
            }
        } else {
            field_name = std::to_string(id);
        }
        throw runtime_error("Field '" + field_name + "' not found and no default value available");
    }

    if (class_def_) {
        invalid_value_ = script_value::make_invalid(class_def_->get_engine());
    } else {
        invalid_value_ = script_value::make_invalid(nullptr);
    }
    return invalid_value_;
}

inline bool class_instance::has_field(uint64_t id) const {
    if (find_field_value(id) != nullptr) {
        return true;
    }
    return class_def_ && class_def_->chain_declares_field(id);
}

inline script_value class_instance::get_method(uint64_t id, bool throw_if_missing) const {
    if (class_def_) {
        return class_def_->get_method(id, throw_if_missing);
    }

    if (throw_if_missing) {
        throw runtime_error("Class definition not available for method with ID " + std::to_string(id) + " lookup");
    }
    return script_value::make_invalid(nullptr);
}

inline class_instance::class_instance(std::string_view class_name, engine* eng)
    : engine_(eng), invalid_value_(std::monostate{}, eng) {
    if (eng) { class_name_ = eng->get_symbolizer()->intern_with_view(class_name).second; }
}

inline class_instance::~class_instance() {
    if (class_def_) {
        class_def_->unregister_instance(this);
    }
}

} // namespace jai

#endif // __JAISCRIPT_CORE_CLASS_DEFINITION_HPP__
