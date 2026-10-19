#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/string_symbolizer.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <jaiscript/detail/class_runtime.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/member_scope.hpp>
#include <jaiscript/detail/typed_slot.hpp>
#include <algorithm>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// GetCurrentThreadStackLimits needs the Windows 8 API surface; MinGW headers
// default _WIN32_WINNT lower and hide it (MSVC's default is already high enough)
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#endif

namespace {
// The method-kind fallback reaches the receiver's members by bare name: access applies as at every bare-name site
// (detail::bare_member_access's rule; ctx = the class whose method body runs)
inline jai::checked_result<void> receiver_member_access(const jai::environment& env, const jai::class_instance& instance, uint64_t id) {
    const jai::class_definition* def = instance.get_class_definition();
    if (!def || !def->chain_has_nonpublic()) [[likely]] { return {}; }
    return jai::detail::enforce_member_access(def, id, env.find_access_context());
}

inline jai::script_value* method_static_cell(jai::class_definition* declaring, const jai::class_instance& instance, uint64_t id) {
    jai::class_definition* owner = jai::detail::bare_static_owner(declaring, instance.get_class_definition(), id);
    return owner ? owner->get_static_field_ptr(id) : nullptr;
}
}

namespace jai {

namespace detail {

// True when the current thread's remaining native stack is below the safety margin.
// Guards the recursive native call paths (constructor bodies in particular stack
// 20-25KB per level in Debug), converting an imminent 0xC00000FD process kill into
// the catchable max_recursion_depth error. Per-thread OS query - no static state.
bool native_stack_low() noexcept {
#if defined(_WIN32)
    static constexpr uintptr_t k_stack_margin = 256 * 1024;
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    char probe;
    return reinterpret_cast<uintptr_t>(&probe) - static_cast<uintptr_t>(low) < k_stack_margin;
#else
    return false;
#endif
}

// Out-of-line door for class_definition.hpp's field kernel (typed_slot.hpp holds the body)
checked_result<slot_conversion> convert_typed_slot_script_rows(slot_conversion pending, const script_value& value,
                                                              const type_info_ptr& target_type, engine* eng) {
    return convert_typed_slot_script_rows_impl(std::move(pending), value, target_type, eng);
}

} // namespace detail

// The environment a dying function value alone keeps alive: a lambda's captures (and its unique enclosing scopes)
environment* environment::sole_closure_env(const script_value& a_value) noexcept {
    if (a_value.raw_storage_index() != script_value::TYPEID_FUNCTION || !a_value.release_cascades()) { return nullptr; }
    const auto* thunk = a_value.unchecked_as_function().target<script_callable_thunk>();
    if (!thunk || thunk->payload.kind != script_callable::kind_type::function || !thunk->payload.fn || thunk->payload.fn.use_count() != 1) {
        return nullptr;
    }
    const auto& env = thunk->payload.fn->closure_env;
    return env && env.use_count() == 1 ? env.get() : nullptr;
}

// A closure's captures live in its environment, so a chain of closures each capturing the last nests one environment per
// link: the values of an environment only a dying function holds die here, on a work list, instead of inside its destructor
environment::~environment() {
    std::vector<script_value> work;
    std::vector<std::shared_ptr<environment>> emptied;
    while (!local_storage_.empty()) {
        if (!sole_closure_env(local_storage_.back())) {
            local_storage_.pop_back();
            continue;
        }
        work.push_back(std::move(local_storage_.back()));
        local_storage_.pop_back();
        while (!work.empty()) {
            script_value dying = std::move(work.back());
            work.pop_back();
            for (environment* env = sole_closure_env(dying); env;) {
                for (auto& value : env->local_storage_) { work.push_back(std::move(value)); }
                env->local_storage_.clear();
                if (!env->parent_ || env->parent_.use_count() != 1) { break; }
                emptied.push_back(std::move(env->parent_));
                env = emptied.back().get();
            }
        }
        while (!emptied.empty()) { emptied.pop_back(); }
    }
}

void environment::define(const std::string& name, const script_value& value) {
    JAI_CENSUS(env_get_name);
    uint64_t id = symbolizer_->intern(name);
    define(id, value);
}

void environment::define(const std::string& name, script_value&& value) {
    JAI_CENSUS(env_get_name);
    uint64_t id = symbolizer_->intern(name);
    define(id, std::move(value));
}

void environment::define(uint64_t id, const script_value& value) {
    JAI_CENSUS(env_define_id);
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.contains(id)) {
        // Redefining local variable - update in place via flat_lookup_
        if (auto* it = flat_lookup_.find(id)) {
            **it = value;
            return;
        }
    }

    // New local variable - add to stable storage
    // engine::memory_cap: environment growth counts toward the script's allowance
    // (approximate slot cost; the raise happens at the next loop back-edge)
    if (engine* eng = value.get_engine()) {
        eng->execution_limits().memory_charge_deferred(sizeof(script_value) + 64);
    }
    flat_lookup_[id] = &local_storage_.push_back(value);  // Update/shadow in flat lookup
    local_ids_[id] = true;
    bump_env_epoch();   // a new name can shadow what the vm's lookup caches resolved
}

void environment::define(uint64_t id, script_value&& value) {
    JAI_CENSUS(env_define_id);
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.contains(id)) {
        // Redefining local variable - update in place via flat_lookup_
        if (auto* it = flat_lookup_.find(id)) {
            **it = std::move(value);
            return;
        }
    }

    // New local variable - add to stable storage
    // engine::memory_cap: environment growth counts toward the script's allowance
    // (approximate slot cost; the raise happens at the next loop back-edge)
    if (engine* eng = value.get_engine()) {
        eng->execution_limits().memory_charge_deferred(sizeof(script_value) + 64);
    }
    flat_lookup_[id] = &local_storage_.push_back(std::move(value));  // Update/shadow in flat lookup
    local_ids_[id] = true;
    bump_env_epoch();   // a new name can shadow what the vm's lookup caches resolved
}

void environment::bump_env_epoch() noexcept {
    // Re-serialize from the per-engine monotonic counter (see local_epoch() in the
    // header): invalidates only the vm lookup-cache entries resolved against THIS env,
    // instead of the old global bump that poisoned every cache in the engine on every
    // scope/method env reset. A monotonic SERIAL (not a per-env counter) on purpose:
    // two env lifetimes at the same recycled heap address would both count 1,2,3... and
    // a stale entry could false-hit dead storage; serials never repeat within an engine.
    local_epoch_ = symbolizer_ ? symbolizer_->next_env_serial() : 0;
}

script_value* environment::vm_storage_lookup(uint64_t id, bool& cacheable) {
    JAI_CENSUS(env_vm_lookup);
    cacheable = false;
    // Byte-parallel with the get_ref/get_value_ptr prefix: 'this' special-case first
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return &this_object_;
    }
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { cacheable = true; return cell; }
    }
    auto* it = flat_lookup_.find(id);
    if (it) {
        cacheable = local_ids_.contains(id);
        if (!cacheable) {
            // a memo of a parent's cell: cacheable when it is the cell of the nearest environment holding the name locally
            for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
                if (!p->local_ids_.contains(id)) { continue; }
                auto* owner = p->flat_lookup_.find(id);
                cacheable = owner && (*owner) == (*it);
                break;
            }
        }
        return (*it);
    }
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            cacheable = p->local_ids_.contains(id);
            return (*parent_it);
        }
    }
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return ptr;   // provenance unknown (may be a this/static-field fallback): not cacheable
        }
    }
    return nullptr;
}

// By-name lookups ask the interner without inserting: a name it never saw is defined nowhere, and its undefined text
// carries the name parked (never interned; the slot-less text while frozen)
JAI_NOINLINE std::pair<std::string_view, uint64_t> environment::unknown_name_error(std::string_view name) const {
    if (symbolizer_->frozen()) { return {"Undefined variable", 0}; }
    const uint64_t parked = symbolizer_->park(name);
    return {undefined_text(parked), parked};
}

checked_result<script_value> environment::get(const std::string& name) const {
    JAI_CENSUS(env_get_name);
    if (const auto id = symbolizer_->find(name)) { return get(*id); }
    const auto [text, parked] = unknown_name_error(name);
    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable), text, parked);
}

checked_result<script_value> environment::get(const std::string& name, const string_symbolizer& program_symbols) const {
    if (const auto id = program_symbols.find(name)) { return get(*id); }
    return get(name);
}

checked_result<script_value> environment::get(uint64_t id) const {
    JAI_CENSUS(env_get_id);
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return this_object_;
    }
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { return *cell; }
    }

    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        return **it;
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            // Found in ancestor cache - copy to our cache and return
            flat_lookup_[id] = (*parent_it);
            return *((*parent_it));
        }
    }

    // SLOW PATH: No ancestor had it cached - do full lookup
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            // Cache for future O(1) access
            flat_lookup_[id] = ptr;
            return *ptr;
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                if (auto access = receiver_member_access(*this, *instance, id); !access) [[unlikely]] {
                    return access.error_value();
                }
                // Try to get field first (non-throwing). A C++ property's placeholder is
                // never an env cell: the this-tails read it through its getter
                const script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid() && !detail::cpp_property_getter_id(instance.get(), id)) {
                    return field_ref;
                }

                // Try to get method (non-throwing)
                script_value method = instance->get_method(id, false);
                if (!method.is_invalid()) {
                    // Typed thunk, not the anonymous make_bound_method lambda: hot vm
                    // call paths recover the payload (kind bound_method) and enter the
                    // method IN-LOOP — bare sibling calls stop paying the opaque
                    // lambda → dispatcher → execute_callable → env-acquire recursion.
                    // Opaque callers get make_bound_method's exact behavior from the
                    // thunk's operator() (engine.cpp), keep-alive anchor included.
                    script_callable payload;
                    payload.kind = script_callable::kind_type::bound_method;
                    payload.this_obj = this_object_;
                    payload.bound_dispatch = std::move(method);
                    bound_method_storage_ = script_value::make_function(
                        script_callable_thunk{this_object_.get_engine(), std::move(payload)},
                        this_object_.get_engine());
                    return bound_method_storage_;
                }

                // Try to get static field from class definition
                if (script_value* static_ptr = method_static_cell(access_context_, *instance, id)) {
                    return *static_ptr;
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        // Check static fields
        if (script_value* static_ptr = class_def_->find_static_field_in_chain(id)) {
            return *static_ptr;
        }

        // Check static methods (for calling other static methods)
        if (const script_value* method = class_def_->find_static_method_in_chain(id)) {
            return *method;
        }
    }

    // Not found anywhere - id is already the interned variable name
    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
        undefined_text(id), id);
}

environment::root_tables& environment::root_tables_mut() {
    if (!root_) { root_ = std::make_unique<root_tables>(); }
    return *root_;
}

void environment::share(uint64_t id) {
    if (!local_ids_.contains(id)) { return; }
    root_tables& r = root_tables_mut();
    r.unit_of.erase(id);   // the cell belongs to unit 0
    r.inline_cells[id] = { symbolizer_->current_unit().id };
}

environment::inline_verdict environment::inline_declare(uint64_t id, uint32_t& owner) {
    if (!get_own_value_ptr(id)) { return inline_verdict::create; }
    const uint32_t unit = symbolizer_->current_unit().id;
    root_tables& r = root_tables_mut();
    if (auto cell = r.inline_cells.find(id); cell != r.inline_cells.end()) {
        if (std::find(cell->second.begin(), cell->second.end(), unit) == cell->second.end()) { cell->second.push_back(unit); }
        return inline_verdict::adopt;
    }
    auto plain = r.unit_of.find(id);
    if (plain != r.unit_of.end()) {
        auto versions = r.versions.find(id);
        if (plain->second.id != unit || versions != r.versions.end()) {
            owner = plain->second.id != unit ? plain->second.id : versions->second.front().unit.id;
            return inline_verdict::conflict;
        }
        r.unit_of.erase(plain);   // the running unit's own plain cell turns inline
    }
    r.inline_cells[id] = { unit };
    return inline_verdict::adopt;
}

bool environment::plain_declare(uint64_t id) {
    const unit_stamp unit = symbolizer_->current_unit();
    root_tables& r = root_tables_mut();
    if (auto cell = r.inline_cells.find(id); cell != r.inline_cells.end()) {
        if (cell->second.size() != 1 || cell->second.front() != unit.id) { return false; }
        r.inline_cells.erase(cell);
    }
    r.unit_of[id] = unit;
    return true;
}

void environment::note_extern(uint64_t id, bool function) {
    environment* root = this;
    while (root->parent_) { root = root->parent_.get(); }
    root->root_tables_mut().extern_names[id] = function;
}

std::string_view environment::undefined_text(uint64_t id, std::string_view otherwise) const {
    const environment* root = this;
    while (root->parent_) { root = root->parent_.get(); }
    if (root->root_) {
        const auto& externs = root->root_->extern_names;
        if (auto it = externs.find(id); it != externs.end()) {
            return it->second ? "'{0}' is declared extern but never defined"
                              : "'{0}' is declared extern but never defined (an 'inline' declaration creates it)";
        }
    }
    return otherwise;
}

bool environment::define_variable(uint64_t id, const script_value& value) {
    JAI_CENSUS(env_define_id);
    return define_variable(id, script_value(value));
}

bool environment::define_variable(uint64_t id, script_value&& value) {
    JAI_CENSUS(env_define_id);
    if (parent_) {
        define(id, std::move(value));
        return true;
    }
    const unit_stamp unit = symbolizer_->current_unit();
    root_tables& r = root_tables_mut();
    if (!local_ids_.contains(id)) {
        define(id, std::move(value));
        r.unit_of[id] = unit;
        return true;
    }
    if (auto cell = r.inline_cells.find(id); cell != r.inline_cells.end()) {
        if (cell->second.size() != 1 || cell->second.front() != unit.id) { return false; }
        r.inline_cells.erase(cell);
        define(id, std::move(value));   // the running unit's own inline cell turns plain in place
        r.unit_of[id] = unit;
        return true;
    }
    auto owner = r.unit_of.find(id);
    const unit_stamp flat_unit = owner == r.unit_of.end() ? unit_stamp{} : owner->second;
    if (flat_unit.id == unit.id) {
        define(id, std::move(value));   // the same unit writes in place; the cell keeps the seq of its first declaration
        return true;
    }
    auto versions = r.versions.find(id);
    if (versions != r.versions.end()) {
        for (auto& v : versions->second) {
            if (v.unit.id == unit.id) {
                *v.cell = std::move(value);
                return true;
            }
        }
    }
    auto* flat = flat_lookup_.find(id);
    if (!flat) {
        define(id, std::move(value));
        r.unit_of[id] = unit;
        return true;
    }
    r.versions[id].push_back({flat_unit, *flat});
    if (engine* eng = value.get_engine()) {
        eng->execution_limits().memory_charge_deferred(sizeof(script_value) + 64);
    }
    *flat = &local_storage_.push_back(std::move(value));
    r.unit_of[id] = unit;
    symbolizer_->note_shadow();
    bump_env_epoch();
    return true;
}

std::vector<std::pair<uint64_t, const script_value*>> environment::unit_variables(uint32_t unit) const {
    std::vector<std::pair<uint64_t, const script_value*>> out;
    if (parent_ || !root_) { return out; }
    for (const auto& [id, owner] : root_->unit_of) {
        if (owner.id != unit || root_->inline_cells.count(id)) { continue; }
        auto* flat = flat_lookup_.find(id);
        if (flat && *flat) { out.emplace_back(id, *flat); }
    }
    for (const auto& [id, versions] : root_->versions) {
        for (const auto& v : versions) { if (v.unit.id == unit && v.cell) { out.emplace_back(id, v.cell); } }
    }
    return out;
}

script_value* environment::same_unit_cell(uint64_t id) {
    if (parent_ || !local_ids_.contains(id)) { return nullptr; }
    const unit_stamp unit = symbolizer_->current_unit();
    root_tables& r = root_tables_mut();
    if (auto cell = r.inline_cells.find(id); cell != r.inline_cells.end()) {
        if (cell->second.size() != 1 || cell->second.front() != unit.id) { return nullptr; }
        r.inline_cells.erase(cell);   // the running unit's own inline cell turns plain in place
        r.unit_of[id] = unit;
    }
    auto owner = r.unit_of.find(id);
    if (owner != r.unit_of.end() && owner->second.id == unit.id) {
        auto* flat = flat_lookup_.find(id);
        return flat ? *flat : nullptr;
    }
    if (auto versions = r.versions.find(id); versions != r.versions.end()) {
        for (auto& v : versions->second) { if (v.unit.id == unit.id) { return v.cell; } }
    }
    return nullptr;
}

// The binding of a versioned name for the running code: its own unit's cell when that unit declared the
// name, else the cell declared most recently among those declared no later than the code's execution
// (top-level code: its current run; a function: the run that minted it), else the flat cell (a forward
// reference keeps today's late binding); the host outside any execution (unit 0) reads the newest
script_value* environment::shadowed_cell(uint64_t id) const {
    const environment* root = this;
    while (root->parent_) { root = root->parent_.get(); }
    if (!root->root_ || root->root_->versions.empty()) { return nullptr; }
    const root_tables& r = *root->root_;
    auto versions = r.versions.find(id);
    if (versions == r.versions.end()) { return nullptr; }
    auto* flat = root->flat_lookup_.find(id);
    if (!flat) { return nullptr; }
    const unit_stamp me = symbolizer_->current_unit();
    if (me.id == 0) { return *flat; }
    auto owner = r.unit_of.find(id);
    const unit_stamp flat_unit = owner == r.unit_of.end() ? unit_stamp{} : owner->second;
    if (flat_unit.id == me.id) { return *flat; }
    for (const auto& v : versions->second) {
        if (v.unit.id == me.id) { return v.cell; }
    }
    script_value* pick = flat_unit.seq <= me.seq ? *flat : nullptr;
    unsigned int pick_seq = flat_unit.seq;
    for (const auto& v : versions->second) {
        if (v.unit.seq <= me.seq && (!pick || v.unit.seq > pick_seq)) { pick = v.cell; pick_seq = v.unit.seq; }
    }
    return pick ? pick : *flat;
}

checked_result<void> environment::assign(const std::string& name, const script_value& value) {
    JAI_CENSUS(env_get_name);
    if (const auto id = symbolizer_->find(name)) { return assign(*id, value); }
    const auto [text, parked] = unknown_name_error(name);
    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), text, parked);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(const std::string& name) const {
    JAI_CENSUS(env_get_name);
    if (const auto id = symbolizer_->find(name)) { return get_ref(*id); }
    const auto [text, parked] = unknown_name_error(name);
    return checked_result<std::reference_wrapper<const script_value>>(make_error_code(runtime_error_code::undefined_variable), text, parked);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(uint64_t id) const {
    JAI_CENSUS(env_get_ref_id);
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::cref(this_object_);
    }
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { return std::cref(*cell); }
    }

    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        return std::cref(**it);
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            return std::cref(*((*parent_it)));
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::cref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                if (auto access = receiver_member_access(*this, *instance, id); !access) [[unlikely]] {
                    return access.error_value();
                }
                // Try to get instance field first (return const reference)
                const script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid() && !detail::cpp_property_getter_id(instance.get(), id)) {
                    return std::cref(inst_field_ref);
                }

                // Try to get static field from class definition (return const reference)
                if (const script_value* field_ptr = method_static_cell(access_context_, *instance, id)) {
                    return std::cref(*field_ptr);
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        const script_value* field_ptr = class_def_->find_static_field_in_chain(id);
        if (field_ptr) {
            return std::cref(*field_ptr);
        }
    }

    return checked_result<std::reference_wrapper<const script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        undefined_text(id), id);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(const std::string& name) {
    JAI_CENSUS(env_get_name);
    if (const auto id = symbolizer_->find(name)) { return get_ref(*id); }
    const auto [text, parked] = unknown_name_error(name);
    return checked_result<std::reference_wrapper<script_value>>(make_error_code(runtime_error_code::undefined_variable), text, parked);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(uint64_t id) {
    JAI_CENSUS(env_get_ref_id);
    // For method environments, handle 'this' specially (note: this is non-const so we return mutable ref)
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::ref(this_object_);
    }

    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        return std::ref(**it);
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            return std::ref(*((*parent_it)));
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::ref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                if (auto access = receiver_member_access(*this, *instance, id); !access) [[unlikely]] {
                    return access.error_value();
                }
                // Try to get instance field first (return non-const reference)
                script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid() && !detail::cpp_property_getter_id(instance.get(), id)) {
                    return std::ref(inst_field_ref);
                }

                // Try to get static field from class definition (return non-const reference)
                if (script_value* field_ptr = method_static_cell(access_context_, *instance, id)) {
                    return std::ref(*field_ptr);
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->find_static_field_in_chain(id);
        if (field_ptr) {
            return std::ref(*field_ptr);
        }
    }

    return checked_result<std::reference_wrapper<script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        undefined_text(id), id);
}

checked_result<void> environment::assign(const std::string& name, script_value&& value) {
    JAI_CENSUS(env_get_name);
    if (const auto id = symbolizer_->find(name)) { return assign(*id, std::move(value)); }
    const auto [text, parked] = unknown_name_error(name);
    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable), text, parked);
}

checked_result<void> environment::assign(uint64_t id, const script_value& value) {
    JAI_CENSUS(env_assign_id);
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { *cell = value; return {}; }
    }
    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        **it = value;
        return {};
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            *((*parent_it)) = value;
            return {};
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = value;
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    // A hybrid's C++ property stores through its setter (a same-named
                    // script field shadows it)
                    if (uint64_t setter_id = detail::cpp_property_setter_id(instance.get(), id)) {
                        return detail::write_cpp_property(this_object_, instance->get_class_definition(), setter_id, value);
                    }
                    // Typed fields enforce like locals (declared type; auto infers)
                    auto enforced = instance->field_store(id, value, [&] { return clone_for_assignment(value); });
                    if (!enforced) {
                        return enforced.error_value();
                    }
                    instance->set_field_unchecked(id, enforced.value());
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_definition* owner = detail::bare_static_owner(class_def_.get(), nullptr, id)) {
            auto enforced = owner->static_store(id, value, [&] { return clone_for_assignment(value); });
            if (!enforced) {
                return enforced.error_value();
            }
            if (owner->set_static_field(id, std::move(enforced.value()))) {
                return {};
            }
        }
    }

    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        undefined_text(id), id);
}

checked_result<void> environment::assign(uint64_t id, script_value&& value) {
    JAI_CENSUS(env_assign_id);
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { *cell = std::move(value); return {}; }
    }
    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        **it = std::move(value);
        return {};
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            *((*parent_it)) = std::move(value);
            return {};
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = std::move(value);
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    if (uint64_t setter_id = detail::cpp_property_setter_id(instance.get(), id)) {
                        return detail::write_cpp_property(this_object_, instance->get_class_definition(), setter_id, value);
                    }
                    // Typed fields enforce like locals (declared type; auto infers)
                    auto enforced = instance->field_store(id, value, [&] { return std::move(value); });
                    if (!enforced) {
                        return enforced.error_value();
                    }
                    instance->set_field_unchecked(id, enforced.value());
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_definition* owner = detail::bare_static_owner(class_def_.get(), nullptr, id)) {
            auto enforced = owner->static_store(id, value, [&] { return std::move(value); });
            if (!enforced) {
                return enforced.error_value();
            }
            if (owner->set_static_field(id, std::move(enforced.value()))) {
                return {};
            }
        }
    }

    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        undefined_text(id), id);
}

bool environment::contains(const std::string& name) const {
    JAI_CENSUS(env_get_name);
    const auto id = symbolizer_->find(name);
    return id && contains(*id);
}

bool environment::contains(uint64_t id) const {
    JAI_CENSUS(env_contains);
    // Check cache first (O(1))
    if (flat_lookup_.contains(id)) {
        return true;
    }

    // FAST PATH: Iterate parent caches without function call overhead
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        if (p->flat_lookup_.contains(id)) {
            // Found in ancestor cache - we know it exists
            // Note: we don't cache here since contains() is just a check
            return true;
        }
    }

    // SLOW PATH: Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return true;
        }
    }
    return false;
}

std::unordered_map<std::string_view, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string_view, script_value> result;
    local_ids_.for_each([&](uint64_t id, bool) {
        if (auto* it = flat_lookup_.find(id)) { result[symbolizer_->get_string(id)] = **it; }
    });
    return result;
}

void environment::clear_values() {
    if (local_ids_.empty() && local_storage_.empty()) {
        return;   // nothing to destroy, nothing for vm lookup caches to go stale on
    }
    local_ids_.for_each([&](uint64_t id, bool) { flat_lookup_.erase(id); });

    // Destroy locals in REVERSE declaration order (C++ LIFO semantics): script class
    // destructors observably fire here, and deque::clear()'s element destruction
    // order is implementation-defined (MS STL and libstdc++ disagree).
    while (!local_storage_.empty()) {
        local_storage_.pop_back();
    }
    local_ids_.clear();
    root_.reset();
    bump_env_epoch();   // local storage died: stale vm lookup-cache pointers must miss
}

void environment::clear_parent_cache() {
    // Remove non-local entries (parent/field pointers that may be stale)
    // Keep local variable entries (which have stable pointers into local_storage_)
    flat_lookup_.erase_if([&](uint64_t id, script_value*) { return !local_ids_.contains(id); });
    bump_env_epoch();
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    JAI_CENSUS(env_reset);
    // Clear all local values first
    clear_values();

    // Only clear cache if parent actually changed
    // If same parent, cached pointers to parent chain remain valid
    // This is critical for recursive functions - avoids re-walking parent chain
    if (parent_.get() != new_parent.get()) {
        flat_lookup_.clear();
        bump_env_epoch();   // chain shape changed: vm lookup caches through this env are stale
    }

    parent_ = std::move(new_parent);

    // Reset to standard kind and clear kind-specific fields
    kind_ = env_kind::standard;
    this_object_ = script_value::make_null(nullptr);
    class_def_.reset();
    access_context_ = nullptr;
    vm_pinned_scope_ = false;
    block_scope_ = false;
    call_scope_ = false;
    bound_method_storage_ = script_value::make_null(nullptr);
}

void environment::reset_as_method(std::shared_ptr<environment> parent, script_value this_obj) {
    JAI_CENSUS(env_reset);
    // Clear all local values first
    clear_values();

    // Only clear cache if parent actually changed
    if (parent_.get() != parent.get()) {
        flat_lookup_.clear();
        bump_env_epoch();   // chain shape changed: vm lookup caches through this env are stale
    }

    parent_ = std::move(parent);

    // Set to method kind with this object (caller stamps set_access_context after)
    kind_ = env_kind::method;
    this_object_ = std::move(this_obj);
    class_def_.reset();
    access_context_ = nullptr;
    vm_pinned_scope_ = false;
    block_scope_ = false;
    call_scope_ = false;
    bound_method_storage_ = script_value::make_null(nullptr);
}

void environment::reset_as_static_method(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def) {
    JAI_CENSUS(env_reset);
    // Clear all local values first
    clear_values();

    // Only clear cache if parent actually changed
    if (parent_.get() != parent.get()) {
        flat_lookup_.clear();
        bump_env_epoch();   // chain shape changed: vm lookup caches through this env are stale
    }

    parent_ = std::move(parent);

    // Set to static_method kind with class definition
    kind_ = env_kind::static_method;
    this_object_ = script_value::make_null(nullptr);
    class_def_ = class_def;
    access_context_ = class_def_.get();
    vm_pinned_scope_ = false;
    block_scope_ = false;
    call_scope_ = false;
    bound_method_storage_ = script_value::make_null(nullptr);
}

std::unordered_map<std::string_view, script_value> environment::get_all_variables() const {
    // With lazy caching, we need to walk the parent chain to get all variables
    std::unordered_map<std::string_view, script_value> allVars;

    // First, get all from parent (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }

    // Then add/override with our local variables
    local_ids_.for_each([&](uint64_t id, bool) {
        if (auto* it = flat_lookup_.find(id)) { allVars[symbolizer_->get_string(id)] = **it; }
    });
    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    JAI_CENSUS(env_value_ptr);
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return &this_object_;
    }
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { return cell; }
    }

    // Check cache first (O(1))
    auto* it = flat_lookup_.find(id);
    if (it) {
        return (*it);
    }

    // FAST PATH: Iterate parent caches without function call overhead
    // This is critical for deep recursion (Fibonacci, BST) where the same
    // function name is looked up repeatedly at each call depth.
    // Once any ancestor has it cached, all descendants benefit.
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            // Found in ancestor cache - copy to our cache and return
            flat_lookup_[id] = (*parent_it);
            return (*parent_it);
        }
    }

    // SLOW PATH: No ancestor had it cached - do full lookup
    // This handles first-time lookups, locals, 'this' fields, static fields, etc.
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return ptr;
        }
    }
    return kind_fallback_ptr(id);
}

// get_value_ptr's answer at this level once no cache from here up holds id (the recursion it replaces re-scanned every
// ancestor's cache at every level): each level's own 'this' and unit shadows nearest first, then the kind fallbacks farthest
// first; the levels between this one and the answering one memoize it, as the recursive form's returns did
script_value* environment::uncached_value_ptr(uint64_t id) {
    const uint64_t this_id = symbolizer_->get_this_id();
    const bool shadows = symbolizer_->shadow_count() != 0;
    auto memoize_below = [&](const environment* answering, script_value* ptr) {
        for (environment* q = this; q != answering; q = chain_step(q->parent_.get())) { q->flat_lookup_[id] = ptr; }
        return ptr;
    };
    std::vector<environment*> with_fallback;
    for (environment* q = this; q != nullptr; q = chain_step(q->parent_.get())) {
        if (q->kind_ == env_kind::method && id == this_id) { return memoize_below(q, &q->this_object_); }
        if (shadows) [[unlikely]] {
            if (script_value* cell = q->shadowed_cell(id)) { return memoize_below(q, cell); }
        }
        if (q->kind_ != env_kind::standard) { with_fallback.push_back(q); }
    }
    for (auto it = with_fallback.rbegin(); it != with_fallback.rend(); ++it) {
        if (script_value* ptr = (*it)->kind_fallback_ptr(id)) { return memoize_below(*it, ptr); }
    }
    return nullptr;
}

// The kind-specific tail of get_value_ptr: a method environment's 'this' fields and statics, a static method's statics
script_value* environment::kind_fallback_ptr(uint64_t id) {
    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                if (!receiver_member_access(*this, *instance, id)) [[unlikely]] {
                    return nullptr;   // out of reach: the value site raises the access text
                }
                // Try to get instance field - get_field returns a reference. A C++ property's
                // placeholder is never an env cell (contains/get_env_var_ptr/vm_storage_lookup
                // walk through here): the this-tails read it through its getter
                script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid() && !detail::cpp_property_getter_id(instance.get(), id)) {
                    return &field_ref;
                }

                // Try to get static field from class definition
                if (script_value* static_ptr = method_static_cell(access_context_, *instance, id)) {
                    return static_ptr;
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->find_static_field_in_chain(id);
        if (field_ptr) {
            return field_ptr;
        }
    }

    return nullptr;
}

script_value* environment::get_env_var_ptr(uint64_t id) {
    if (symbolizer_->shadow_count() != 0) [[unlikely]] {
        if (script_value* cell = shadowed_cell(id)) { return cell; }
    }
    auto* it = flat_lookup_.find(id);
    if (it) {
        return (*it);
    }
    for (auto* p = chain_step(parent_.get()); p != nullptr; p = chain_step(p->parent_.get())) {
        auto* parent_it = p->flat_lookup_.find(id);
        if (parent_it) {
            flat_lookup_[id] = (*parent_it);
            return (*parent_it);
        }
    }
    if (parent_) {
        script_value* ptr = parent_->uncached_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return ptr;
        }
    }
    return nullptr;
}

checked_result<script_value> bound_method_fn::operator()(const std::vector<script_value>& args) const {
    const auto& method_func = method.as_function();
    const auto* dispatch = method_func.target<script_method_dispatch>();
    auto result = dispatch ? dispatch->invoke(this_obj, args, 0) : [&]() {
        std::vector<script_value> method_args;
        method_args.reserve(args.size() + 1);
        method_args.push_back(this_obj);
        method_args.insert(method_args.end(), args.begin(), args.end());
        return method_func(method_args);
    }();

    detail::anchor_bound_method_result(this_obj, result);
    return result;
}

void detail::anchor_bound_method_result(const script_value& this_obj, checked_result<script_value>& result) {
    // If the method returned a NON-OWNING reference into its receiver (the
    // classic chaining idiom `Counter& increment() { ...; return *this; }`
    // produces a cpp-bound T& whose object_holder owns nothing), pin the
    // receiver's underlying object onto the result so it survives even when
    // the receiver was a temporary, e.g. `Counter().increment().add(5)`.
    // Without this, the temporary receiver is freed when this bound-method
    // closure is destroyed, leaving the returned reference dangling
    // (a use-after-free that Release builds happened to mask).
    if (result && this_obj.is_object()) {
        script_value& rv = result.value();
        if (rv.is_non_owning_object()) {
            auto rv_holder = rv.get_object_holder();
            auto recv_holder = this_obj.get_object_holder();
            if (rv_holder && recv_holder) {
                // Prefer the receiver's owning data; if the receiver is itself a
                // non-owning link in the chain, propagate its existing anchor.
                rv_holder->keep_alive = recv_holder->data ? recv_holder->data
                                                          : recv_holder->keep_alive;
            }
        }
    }
}

script_value make_bound_method(const script_value& this_obj, const script_value& method) {
    auto eng = this_obj.get_engine();
    return script_value::make_function(bound_method_fn{this_obj, method}, eng);
}

} // namespace jai
