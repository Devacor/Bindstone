#include "jaiscript/core/class_registry.hpp"
#include "jaiscript/core/dynamic_binder.hpp"
#include "jaiscript/core/engine.hpp"
#include "jaiscript/detail/release_machine.hpp"
#include <stdexcept>
#include <set>
#include <algorithm>

namespace jai {

// ===== flat_value_semantics (class_definition.hpp doc) =====
// Out of line: nested field classes resolve through the engine's registry, which the
// class_definition header cannot see.

bool class_definition::flat_value_semantics() const {
    if (!engine_) {
        return false;
    }
    const uint64_t epoch = engine_->class_definition_epoch();
    if (flat_value_epoch_ == epoch) {
        return flat_value_cached_;
    }
    std::vector<const class_definition*> in_progress;
    flat_value_cached_ = flat_value_semantics_impl(in_progress);
    flat_value_epoch_ = epoch;
    return flat_value_cached_;
}

const std::vector<uint32_t>& class_definition::handle_bearing_slots() const {
    const uint64_t epoch = engine_ ? engine_->class_definition_epoch() : 0;
    if (handle_slots_epoch_ == epoch && engine_) { return handle_slots_cached_; }
    handle_slots_cached_.clear();
    const uint32_t n = field_slot_count();
    for (uint32_t slot = 0; slot < n; ++slot) {
        const type_info_ptr t = get_field_declared_type(slot_field(slot));
        bool scalar = false;
        if (t) {
            switch (t->base_type) {
            case script_value_type::jai_int_type: case script_value_type::jai_float_type:
            case script_value_type::jai_bool_type: case script_value_type::jai_char_type:
                scalar = true; break;
            default: break;
            }
        }
        if (!scalar) { handle_slots_cached_.push_back(slot); }
    }
    handle_slots_epoch_ = epoch;
    return handle_slots_cached_;
}
bool class_definition::type_can_hold_string(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const {
    if (!t) { return true; }   // auto/var: no store-enforced shape
    switch (t->base_type) {
    case script_value_type::jai_string_type: return true;
    case script_value_type::jai_int_type: case script_value_type::jai_float_type:
    case script_value_type::jai_bool_type: case script_value_type::jai_char_type: return false;
    case script_value_type::jai_array_type: return type_can_hold_string(t->element_type(), in_progress);
    case script_value_type::jai_map_type: return type_can_hold_string(t->key_type(), in_progress) || type_can_hold_string(t->value_type(), in_progress);
    case script_value_type::jai_object_type: {
        if (!engine_ || t->type_name.empty()) { return true; }
        auto nested = engine_->get_class_registry().find_script_class(std::string(t->type_name));
        if (!nested) { return true; }
        for (const class_definition* visiting : in_progress) { if (visiting == nested.get()) { return false; } }
        in_progress.push_back(nested.get());
        bool any = false;
        const uint32_t n = nested->field_slot_count();
        for (uint32_t slot = 0; slot < n && !any; ++slot) {
            any = nested->type_can_hold_string(nested->get_field_declared_type(nested->slot_field(slot)), in_progress);
        }
        in_progress.pop_back();
        return any;
    }
    default: return true;
    }
}

bool class_definition::type_tree_runs_destructor(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const {
    if (!t) { return true; }
    switch (t->base_type) {
    case script_value_type::jai_null_type:
    case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
    case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_utf8_string_type:
    case script_value_type::jai_shared_ptr_type: case script_value_type::jai_weak_ptr_type: case script_value_type::jai_function_type:
    case script_value_type::jai_vec2_type: case script_value_type::jai_vec3_type: case script_value_type::jai_ivec2_type:
    case script_value_type::jai_ivec3_type: case script_value_type::jai_vec4_type: case script_value_type::jai_color_type:
        return false;
    case script_value_type::jai_array_type: return type_tree_runs_destructor(t->element_type(), in_progress);
    case script_value_type::jai_map_type: return type_tree_runs_destructor(t->key_type(), in_progress) || type_tree_runs_destructor(t->value_type(), in_progress);
    case script_value_type::jai_object_type: {
        if (!engine_ || t->type_name.empty()) { return true; }
        auto nested = engine_->get_class_registry().find_script_class(std::string(t->type_name));
        return nested && nested->value_tree_runs_destructor_impl(in_progress);   // a host class runs no script destructor
    }
    default: return true;
    }
}

bool class_definition::value_tree_runs_destructor_impl(std::vector<const class_definition*>& in_progress) const {
    for (const class_definition* visiting : in_progress) { if (visiting == this) { return false; } }
    in_progress.push_back(this);
    bool runs = engine_ && chain_declares_destructor(*engine_);
    const uint32_t n = field_slot_count();
    for (uint32_t slot = 0; slot < n && !runs; ++slot) {
        runs = type_tree_runs_destructor(get_field_declared_type(slot_field(slot)), in_progress);
    }
    if (!runs && engine_) {
        for (const auto& entry : engine_->get_class_registry().script_classes_) {
            const class_definition* other = entry.second.get();
            if (other && other != this && other->derives_from(this) && other->value_tree_runs_destructor_impl(in_progress)) { runs = true; break; }
        }
    }
    in_progress.pop_back();
    return runs;
}

void class_definition::run_destructor_body(class_instance* instance, const class_definition* level, const script_value& body) const {
    detail::release_machine* m = engine_->release_machine_here();
    std::vector<script_value> arguments;
    if (m && !m->spare_arguments.empty()) {
        arguments = std::move(m->spare_arguments.back());
        m->spare_arguments.pop_back();
    }
    try {
        std::shared_ptr<class_instance> self = instance->weak_from_this().lock();   // a living instance (a re-class) keeps its owner
        if (!self) { self = std::shared_ptr<class_instance>(instance, [](class_instance*) {}); }
        arguments.push_back(script_value::make_class_object(*level, self, engine_));
        auto ran = body.as_function()(arguments);
        arguments.clear();
        if (!ran) { engine_->report_script_error(format_error(ran, *engine_->get_symbolizer()), ran.error()); }   // reported, never propagated
    } catch (...) {
        arguments.clear();
    }
    if (m && m->spare_arguments.size() < 8) { m->spare_arguments.push_back(std::move(arguments)); }
}

void class_definition::expand_destroy_level(class_instance* instance, const class_definition* level, const class_definition* kept,
                                            std::vector<detail::release_action>& actions) const {
    if (!level || (kept && (kept == level || kept->derives_from(level)))) { return; }
    if (engine_) {
        const uint64_t id = level->destructor_id(*engine_);
        auto it = id != 0 ? level->methods_.find(id) : level->methods_.end();
        if (it != level->methods_.end()) { run_destructor_body(instance, level, it->second); }
    }
    for (const auto& parent : level->parent_classes_) {
        if (parent) { actions.push_back({parent.get(), 0}); }
    }
    level->for_each_own_field_id([&](uint64_t field) { actions.push_back({nullptr, field}); });
}

script_value class_definition::take_field_for_destroy(class_instance* instance, uint64_t field) const {
    const class_definition* layout = instance->get_class_definition() ? instance->get_class_definition() : this;
    const uint32_t slot = layout->field_slot(field);
    return slot != k_no_field_slot ? instance->take_field_slot(slot) : script_value(std::monostate{}, engine_);
}

void class_definition::destroy_levels(class_instance* instance, const class_definition* level, const class_definition* kept) const {
    if (!level) { return; }
    detail::release_sync sync(engine_);
    detail::release_machine* m = engine_ ? engine_->release_machine_here() : nullptr;
    std::vector<detail::release_action> actions;
    if (m && !m->spare_actions.empty()) {
        actions = std::move(m->spare_actions.back());
        m->spare_actions.pop_back();
    }
    actions.push_back({level, 0});
    while (!actions.empty()) {
        const detail::release_action action = actions.back();
        actions.pop_back();
        if (action.level) {
            expand_destroy_level(instance, action.level, kept, actions);
        } else {
            script_value dying = take_field_for_destroy(instance, action.field);
        }
    }
    if (m && m->spare_actions.size() < 8) { m->spare_actions.push_back(std::move(actions)); }
}

void class_instance::release_fields() noexcept {
    engine* eng = class_def_->get_engine();
    detail::release_machine* m = eng ? eng->release_machine_here() : nullptr;
    if (!m) { return; }
    if (!m->deferring && m->depth < m->depth_limit) {
        ++m->depth;
        while (!flat_fields_.empty()) { flat_fields_.pop_back(); }
        overflow_fields_.reset();
        extras_.reset();
        --m->depth;
        return;
    }
    const bool driver = !m->deferring;
    const size_t mark = m->stack.size();
    for (size_t slot = flat_fields_.size(); slot-- > 0;) {
        auto& cell = flat_fields_[slot];
        if (cell && cell->release_cascades()) { m->stack.push_back({std::move(*cell), detail::release_machine::k_no_frame}); }
    }
    if (overflow_fields_) {
        for (auto& [id, value] : *overflow_fields_) {
            if (value.release_cascades()) { m->stack.push_back({std::move(value), detail::release_machine::k_no_frame}); }
        }
    }
    if (const script_value* held = native(); held && held->release_cascades()) {
        m->stack.push_back({std::move(*extras_->native), detail::release_machine::k_no_frame});
    }
    if (driver) {
        std::reverse(m->stack.begin() + static_cast<ptrdiff_t>(mark), m->stack.end());
        detail::release_drive(*eng, *m, mark);
    }
}

void class_instance::release_orphan_fields() noexcept {
    std::vector<script_value> work;
    auto take_cells = [&work](class_instance& from) {
        for (size_t slot = 0; slot < from.flat_fields_.size(); ++slot) {
            auto& cell = from.flat_fields_[slot];
            if (cell && cell->release_cascades()) { work.push_back(std::move(*cell)); }
        }
        if (from.overflow_fields_) {
            for (auto& [id, value] : *from.overflow_fields_) {
                if (value.release_cascades()) { work.push_back(std::move(value)); }
            }
        }
        if (const script_value* held = from.native(); held && held->release_cascades()) { work.push_back(std::move(*from.extras_->native)); }
    };
    take_cells(*this);
    while (!work.empty()) {
        script_value v = std::move(work.back());
        work.pop_back();
        if (v.raw_storage_index() == script_value::TYPEID_OBJECT) {
            const auto& holder = v.unchecked_object_handle();
            if (holder.unique() && holder->is_class_instance_wrapper && holder->data && holder->data.use_count() == 1) {
                auto* inner = static_cast<class_instance*>(holder->data.get());
                if (!inner->class_def_ && !inner->destructor_armed_) { take_cells(*inner); }
            }
        } else if (v.release_flattens()) {
            std::vector<script_value> nested;
            nested.push_back(std::move(v));
            detail::release_work(nested);
        }
    }
}

namespace detail {

void instance_deleter::operator()(class_instance* ptr) const noexcept {
    if (!ptr) { return; }
    engine* eng = def->get_engine();
    release_machine* m = eng ? eng->release_machine_here() : nullptr;
    const bool run_chain = !ptr->construction_pending();
    if (m && (m->deferring || m->depth >= m->depth_limit)) {
        release_instance_frame(*eng, *m, ptr, def, run_chain);
        return;
    }
    if (m) { ++m->depth; }
    if (run_chain) {
        const class_definition* now = ptr->get_class_definition();   // a re-classed instance dies as its class now
        if (!now) { now = def.get(); }
        now->destroy_chain(ptr, now);
    }
    class_instance::free_allocation(ptr);
    if (m) { --m->depth; }
}

namespace {
template <typename F>
void release_step(release_machine& m, F&& drop) {
    const size_t mark = m.stack.size();
    const bool deferring = m.deferring;
    const size_t step_mark = m.step_mark;
    m.deferring = true;
    m.step_mark = mark;
    drop();
    m.deferring = deferring;
    m.step_mark = step_mark;
    if (m.stack.size() - mark > 1) { std::reverse(m.stack.begin() + static_cast<ptrdiff_t>(mark), m.stack.end()); }
}
}

void release_instance_frame(engine& eng, release_machine& m, class_instance* instance, std::shared_ptr<class_definition> owner,
                            bool run_chain) {
    const bool driver = !m.deferring;
    const size_t mark = m.stack.size();
    uint32_t index;
    if (!m.free_frames.empty()) {
        index = m.free_frames.back();
        m.free_frames.pop_back();
    } else {
        index = static_cast<uint32_t>(m.frames.size());
        m.frames.emplace_back();
    }
    release_frame& frame = m.frames[index];
    const class_definition* now = instance->get_class_definition();
    frame.instance = instance;
    frame.owner = std::move(owner);
    frame.allocation = instance->weak_from_this();
    frame.root = now ? now : frame.owner.get();
    frame.kept = nullptr;
    frame.actions.clear();
    if (run_chain) { frame.actions.push_back({frame.root, 0}); }
    frame.delete_at_end = true;
    m.stack.push_back({script_value(std::monostate{}, &eng), index});
    if (driver) { release_drive(eng, m, mark); }
}

void release_drive(engine& eng, release_machine& m, size_t mark) {
    std::vector<release_action> expanded;
    while (m.stack.size() > mark) {
        const uint32_t index = m.stack.back().frame;
        if (index == release_machine::k_no_frame) {
            script_value dying = std::move(m.stack.back().value);
            m.stack.pop_back();
            release_step(m, [&] { script_value gone = std::move(dying); });
            continue;
        }
        release_frame& frame = m.frames[index];
        class_instance* instance = frame.instance;
        if (frame.actions.empty()) {
            m.stack.pop_back();
            const bool del = frame.delete_at_end;
            std::shared_ptr<class_definition> owner = std::move(frame.owner);
            std::weak_ptr<class_instance> allocation = std::move(frame.allocation);
            frame.instance = nullptr;
            m.free_frames.push_back(index);
            if (del) { release_step(m, [&] { class_instance::free_allocation(instance); }); }
            continue;
        }
        const release_action action = frame.actions.back();
        frame.actions.pop_back();
        const class_definition* root = frame.root;
        if (action.level) {
            const class_definition* kept = frame.kept;
            expanded.clear();
            root->expand_destroy_level(instance, action.level, kept, expanded);   // the body may grow the frames: re-index after
            auto& actions = m.frames[index].actions;
            actions.insert(actions.end(), expanded.begin(), expanded.end());
        } else {
            script_value dying = root->take_field_for_destroy(instance, action.field);
            if (dying.release_cascades()) { release_step(m, [&] { script_value gone = std::move(dying); }); }
        }
    }
}

release_sync::release_sync(engine* eng) noexcept {
    release_machine* m = eng ? &eng->release_machine() : nullptr;
    if (!m || !m->deferring) { return; }
    m_ = m;
    const size_t step_mark = m->step_mark;
    if (m->stack.size() - step_mark > 1) { std::reverse(m->stack.begin() + static_cast<ptrdiff_t>(step_mark), m->stack.end()); }
    m->deferring = false;
    release_drive(*eng, *m, step_mark);
}

release_sync::~release_sync() {
    if (m_) { m_->deferring = true; }
}

} // namespace detail

bool slot_may_own_destructor(engine& eng, const type_info_ptr& t) {
    bool any = false;
    for (const auto& entry : eng.get_class_registry().script_classes_) {
        if (entry.second && entry.second->value_tree_runs_destructor()) { any = true; break; }
    }
    if (!any) { return false; }
    if (!t) { return true; }
    switch (t->base_type) {
    case script_value_type::jai_any_type: return true;
    case script_value_type::jai_reference_type: case script_value_type::jai_weak_ptr_type: return false;
    case script_value_type::jai_shared_ptr_type: {
        const type_info_ptr pointee = t->element_type();
        if (!pointee) { return true; }
        if (pointee->base_type != script_value_type::jai_object_type || pointee->type_name.empty()) { return true; }
        auto cls = eng.get_class_registry().find_script_class(std::string(pointee->type_name));
        return cls && cls->value_tree_runs_destructor();
    }
    case script_value_type::jai_object_type: case script_value_type::jai_array_type: case script_value_type::jai_map_type: {
        for (const auto& entry : eng.get_class_registry().script_classes_) {
            if (entry.second) {
                std::vector<const class_definition*> in_progress;
                return entry.second->type_tree_runs_destructor(t, in_progress);
            }
        }
        return false;
    }
    default: return false;
    }
}

bool class_definition::value_tree_runs_destructor() const {
    const uint64_t epoch = engine_ ? engine_->definition_epoch() : 0;
    if (destructor_tree_epoch_ == epoch && engine_) { return destructor_tree_cached_; }
    std::vector<const class_definition*> in_progress;
    destructor_tree_cached_ = value_tree_runs_destructor_impl(in_progress);
    destructor_tree_epoch_ = epoch;
    return destructor_tree_cached_;
}

const std::vector<uint32_t>& class_definition::string_reachable_slots() const {
    const uint64_t epoch = engine_ ? engine_->class_definition_epoch() : 0;
    if (string_slots_epoch_ == epoch && engine_) { return string_slots_cached_; }
    string_slots_cached_.clear();
    std::vector<const class_definition*> in_progress;
    const uint32_t n = field_slot_count();
    for (uint32_t slot = 0; slot < n; ++slot) {
        if (type_can_hold_string(get_field_declared_type(slot_field(slot)), in_progress)) { string_slots_cached_.push_back(slot); }
    }
    string_slots_epoch_ = epoch;
    return string_slots_cached_;
}
bool class_definition::flat_value_semantics_impl(std::vector<const class_definition*>& in_progress) const {
    if (class_type_ != script_class && class_type_ != vm_class) {
        return false;
    }
    if (cpp_base_class_) {
        return false;
    }
    for (const class_definition* visiting : in_progress) {
        if (visiting == this) {
            return false;   // declared-type cycle: unconstructible by value, never flat
        }
    }
    in_progress.push_back(this);
    bool flat = true;
    for (const auto& parent : parent_classes_) {
        if (!parent || !parent->flat_value_semantics_impl(in_progress)) {
            flat = false;
            break;
        }
    }
    if (flat) {
        for (const auto& [field_id, field_default] : field_defaults_) {
            (void)field_default;
            if (!flat_field_type(get_field_declared_type(field_id), in_progress)) {
                flat = false;
                break;
            }
        }
    }
    in_progress.pop_back();
    return flat;
}

// Transitive value-closure (any type that cannot point at something else
// qualifies - primitives, strings, containers OF value-closed types to any depth,
// nested flat classes). What poisons: shared_ptr/weak_ptr/function/reference (their
// semantics IS pointing elsewhere) and auto/var (no store-enforced shape).
bool class_definition::flat_field_type(const type_info_ptr& t, std::vector<const class_definition*>& in_progress) const {
    if (!t) {
        return false;   // auto/untyped field: no store-enforced shape to lean on
    }
    switch (t->base_type) {
    case script_value_type::jai_int_type:
    case script_value_type::jai_float_type:
    case script_value_type::jai_bool_type:
    case script_value_type::jai_char_type:
    case script_value_type::jai_string_type:
        return true;
    case script_value_type::jai_array_type:
        return flat_field_type(t->element_type(), in_progress);
    case script_value_type::jai_map_type:
        return flat_field_type(t->key_type(), in_progress) &&
               flat_field_type(t->value_type(), in_progress);
    case script_value_type::jai_object_type: {
        if (!engine_ || t->type_name.empty()) {
            return false;
        }
        auto nested = engine_->get_class_registry().find_script_class(std::string(t->type_name));
        return nested && nested->flat_value_semantics_impl(in_progress);
    }
    default:
        return false;   // function, shared_ptr/weak_ptr (reference semantics), reference, var
    }
}

checked_result<void> class_registry::register_script_class(std::shared_ptr<script_class_definition> class_def) {
    if (!class_def) {
        return checked_result<void>(make_error_code(runtime_error_code::class_not_found));
    }

    auto& slot = script_classes_[class_def->get_name()];
    const bool first_definition = slot != class_def;   // redefinitions re-register the same object
    slot = class_def;

    validate_class_hierarchy(class_def->get_name());

    if (first_definition) {
        if (engine* eng = class_def->get_engine()) { eng->note_class_shape_change(); }
    }
    return {};
}

checked_result<void> class_registry::register_cpp_class(std::shared_ptr<class_definition> class_def) {
    if (!class_def) {
        return checked_result<void>(make_error_code(runtime_error_code::class_not_found));
    }

    cpp_classes_[class_def->get_name()] = class_def;

    return {};
}

std::shared_ptr<script_class_definition> class_registry::find_script_class(const std::string& name) {
    auto it = script_classes_.find(name);
    return (it != script_classes_.end()) ? it->second : nullptr;
}

std::shared_ptr<class_definition> class_registry::find_cpp_class(const std::string& name) {
    auto it = cpp_classes_.find(name);
    return (it != cpp_classes_.end()) ? it->second : nullptr;
}

std::shared_ptr<base_class_definition> class_registry::find_class(const std::string& name) {
    auto script_class = find_script_class(name);
    if (script_class) {
        auto base_def = std::make_shared<base_class_definition>();
        base_def->name = script_class->get_name();
        return base_def;
    }
    
    auto cpp_class = find_cpp_class(name);
    if (cpp_class) {
        auto base_def = std::make_shared<base_class_definition>();
        base_def->name = cpp_class->get_name();
        return base_def;
    }
    
    return nullptr;
}

bool class_registry::is_assignable_from(const std::string& derived, const std::string& base) {
    return check_inheritance_chain(derived, base);
}

bool class_registry::is_script_class(const std::string& name) {
    return script_classes_.find(name) != script_classes_.end();
}

bool class_registry::is_cpp_class(const std::string& name) {
    return cpp_classes_.find(name) != cpp_classes_.end();
}

std::shared_ptr<class_instance> class_registry::create_script_instance(
    const std::string& class_name,
    const std::vector<script_value>& args
) {
    auto class_def = find_script_class(class_name);
    if (!class_def) {
        throw std::runtime_error("Script class not found: " + class_name);
    }
    
    auto instance = class_def->create_instance();
    
    // TODO: Handle constructor arguments
    // The current API doesn't provide a way to pass constructor args to create_instance
    // Constructor arguments would need to be handled by the caller after instance creation
    
    return instance;
}

void class_registry::validate_class_hierarchy(const std::string& class_name) {
    auto class_def = find_script_class(class_name);
    if (!class_def) {
        auto cpp_def = find_cpp_class(class_name);
        if (!cpp_def) return;
        
        // For C++ classes, we can't easily traverse the hierarchy
        // but we can at least check the immediate parent
        auto parent = cpp_def->get_cpp_base_class();
        if (parent && parent->get_name() == class_name) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        return;
    }
    
    std::set<std::string> visited;
    std::shared_ptr<class_definition> current = class_def;

    while (current) {
        if (visited.count(current->get_name())) {
            throw std::runtime_error("Circular inheritance detected in class: " + class_name);
        }
        visited.insert(current->get_name());
        current = current->get_parent();
    }
}

void class_registry::promote_methods_to_virtual(
    const std::string& base_class_name,
    const std::string& derived_class_name
) {
    auto base_class = find_script_class(base_class_name);
    auto derived_class = find_script_class(derived_class_name);
    
    if (!base_class || !derived_class) return;
    
    // The current API uses method_metadata to track virtual methods
    // but doesn't expose a way to iterate over methods or modify their metadata
    // This functionality would need to be added to class_definition if needed
}

bool class_registry::check_inheritance_chain(const std::string& derived, const std::string& base) {
    if (derived == base) return true;
    
    auto derived_script = find_script_class(derived);
    if (derived_script) {
        // Check if the derived class has the base as a parent
        // Since we can't access parent_class_ directly, we need to use
        // the inheritance information stored during class registration
        
        // For now, we can only check direct parent relationships
        // A full implementation would need to maintain an inheritance graph
        // or have access to the parent_class_ member
        auto cpp_base = derived_script->find_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }
    }
    
    auto derived_cpp = find_cpp_class(derived);
    if (derived_cpp) {
        auto cpp_base = derived_cpp->get_cpp_base_class();
        if (cpp_base && cpp_base->get_name() == base) {
            return true;
        }

        if (cpp_base) {
            return check_inheritance_chain(cpp_base->get_name(), base);
        }
    }
    
    // Check for cross-type inheritance (script class inheriting from C++ class)
    if (derived_script) {
        auto cpp_base = derived_script->find_cpp_base_class();
        if (cpp_base) {
            return check_inheritance_chain(cpp_base->get_name(), base);
        }
    }
    
    return false;
}

void class_registry::apply_default_field_values(
    std::shared_ptr<class_instance> instance,
    std::shared_ptr<script_class_definition> class_def
) {
    // The create_instance() method already applies default field values
}

} // namespace jai