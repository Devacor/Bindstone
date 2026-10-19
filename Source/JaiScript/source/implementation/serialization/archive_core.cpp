#include <jaiscript/serialization/archive_impl.hpp>

namespace jai::serialization {

archive_writer_core::archive_writer_core(engine* eng) : engine_ref_(eng) {
    context_stack_.push(SerializationContext::Root);
}

archive_writer_core::~archive_writer_core() = default;

std::pair<uint32_t, bool> archive_writer_core::track_shared_ptr(const void* raw_ptr) {
    if (raw_ptr == nullptr) {
        return {0, false};
    }
    auto [it, inserted] = shared_ptr_ids_.try_emplace(raw_ptr, next_shared_id_);
    if (inserted) {
        ++next_shared_id_;
    }
    return {it->second, inserted};
}

SerializationContext archive_writer_core::current_context() const {
    return context_stack_.empty() ? SerializationContext::Root : context_stack_.top();
}

void archive_writer_core::push_context(SerializationContext ctx) {
    context_stack_.push(ctx);
}

void archive_writer_core::pop_context() {
    if (!context_stack_.empty() && context_stack_.top() != SerializationContext::Root) {
        context_stack_.pop();
    }
}

bool archive_writer_core::needs_object_wrapper() const {
    return current_context() != SerializationContext::ObjectBody;
}

void archive_writer_core::consume_value_context() {
    auto ctx = current_context();
    if (ctx == SerializationContext::PropertyValue || ctx == SerializationContext::MapValue) {
        pop_context();
    }
}

void* archive_writer_core::get_user_context_raw(std::type_index ti) const {
    auto it = user_contexts_.find(ti);
    if (it != user_contexts_.end()) {
        return it->second;
    }
    return ti == engine_context_type_index() ? static_cast<void*>(engine_ref_) : nullptr;
}

void archive_writer_core::set_user_context_raw(std::type_index ti, void* context) {
    user_contexts_[ti] = context;
}

uint32_t archive_writer_core::lookup_shared_id(const void* raw_ptr) const {
    if (raw_ptr == nullptr) {
        return 0;
    }
    auto it = shared_ptr_ids_.find(raw_ptr);
    return it != shared_ptr_ids_.end() ? it->second : 0;
}

archive_reader_core::archive_reader_core(engine* eng) : engine_ref_(eng) {}

archive_reader_core::~archive_reader_core() = default;

void archive_reader_core::store_deserialized(uint32_t id, std::shared_ptr<void> complete, std::type_index type) {
    cpp_shared_ptrs_[id] = deserialized_entry{ std::move(complete), type };
}

bool archive_reader_core::has_deserialized_shared(uint32_t id) const {
    return id != 0 && cpp_shared_ptrs_.find(id) != cpp_shared_ptrs_.end();
}

std::shared_ptr<void> archive_reader_core::get_deserialized_shared_adjusted(uint32_t id, std::type_index target) const {
    if (id == 0) {
        return nullptr;
    }
    auto it = cpp_shared_ptrs_.find(id);
    if (it == cpp_shared_ptrs_.end()) {
        return nullptr;
    }
    // A back-reference may request a different base view than the original load; reject it
    // unless the stored dynamic type is actually assignable to that base, so a tampered id
    // can't reinterpret an unrelated object through `target` (type confusion). B5.
    if (!polymorphic_registry::instance().is_assignable(it->second.type, target)) {
        throw serialization_error("Back-referenced object is not a subtype of the requested type");
    }
    void* adjusted = polymorphic_registry::instance().adjust_to_base(it->second.type, target, it->second.object.get());
    return std::shared_ptr<void>(it->second.object, adjusted);
}

void* archive_reader_core::get_user_context_raw(std::type_index ti) const {
    auto it = user_contexts_.find(ti);
    if (it != user_contexts_.end()) {
        return it->second;
    }
    return ti == engine_context_type_index() ? static_cast<void*>(engine_ref_) : nullptr;
}

void archive_reader_core::set_user_context_raw(std::type_index ti, void* context) {
    user_contexts_[ti] = context;
}

} // namespace jai::serialization
