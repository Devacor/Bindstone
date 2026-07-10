#include <jaiscript/jaiscript.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <sstream>

namespace jai {

script_value::script_value(script_int i, engine* eng) : type_info_(nullptr), engine_(eng), storage_(i) {
    if (eng) {
        type_info_ = eng->get_type_info_int();
    }
}

script_value::script_value(script_float f, engine* eng) : type_info_(nullptr), engine_(eng), storage_(f) {
    if (eng) {
        type_info_ = eng->get_type_info_float();
    }
}

script_value::script_value(const script_string& s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(s)) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(script_string&& s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(std::move(s))) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(const char* s, engine* eng) : type_info_(nullptr), engine_(eng), storage_(make_strong<script_string>(s)) {
    if (eng) {
        type_info_ = eng->get_type_info_string();
    }
}

script_value::script_value(script_char c, engine* eng) : type_info_(nullptr), engine_(eng), storage_(c) {
    if (eng) {
        type_info_ = eng->get_type_info_char();
    }
}

script_value::script_value(script_bool b, engine* eng) : type_info_(nullptr), engine_(eng), storage_(b) {
    if (eng) {
        type_info_ = eng->get_type_info_bool();
    }
}


script_value::reference_holder::~reference_holder() {
    if (has_cell || has_map_key || has_elem_scratch) {
        cell()->~script_value();
        has_cell = false;
        has_map_key = false;
        has_elem_scratch = false;
    }
}

const script_value& script_value::reference_holder::materialize_typed_element(engine* eng) {
    script_value v = container->get(container_index, eng);
    if (has_elem_scratch) {
        *cell() = std::move(v);
    } else {
        new (cell_storage) script_value(std::move(v));
        has_elem_scratch = true;
    }
    return *cell();
}

script_value* script_value::reference_holder::resolve_target() {
    if (has_cell) {
        return cell();
    }
    if (has_map_key) {
        auto it = container_map->find(*cell());
        return it != container_map->end() ? &it->second : nullptr;
    }
    if (container) {
        if (container->is_typed()) {
            // no script_value element exists - callers pre-check typed_element() and
            // take the scratch+commit path; null here means a missed caller fails
            // loudly ("Invalid reference") instead of corrupting memory
            return nullptr;
        }
        return container_index < container->size() ? &container->values()[container_index] : nullptr;
    }
    if (owner_instance) {
        return owner_instance->find_field_value(field_id);
    }
    return nullptr;   // unreachable: every factory sets a mode
}

// Per-engine free-list of reference_holder control blocks: a mint is a pop +
// placement-new instead of a heap allocation (the hot element/field/cell paths).
// Blocks self-describe their release through dealloc_fn, so plain make_strong
// holders and pooled holders coexist - which is what keeps this single-threaded:
// parallel workers DO mint cell references inside their bodies, so acquire falls
// back to plain make_strong while a region is active (main thread parked). Releases
// inherit the value system's existing thread contract (non-atomic strong_ptr counts:
// a value tree sharing storage with the engine's world must not be released off the
// engine's thread - true before the pool existed). If the engine dies while
// user-held references are still out, the pool is orphaned and the last returning
// block frees it.
struct script_value::reference_holder_pool {
    struct block : jai::detail::control_block<reference_holder> {
        reference_holder_pool* home = nullptr;
    };
    static_assert(offsetof(block, storage) == jai::detail::cb_storage_offset<reference_holder>,
                  "pooled blocks must keep storage at control_block<T>'s head offset (cb_from_object)");

    static constexpr size_t max_free_blocks = 256;
    std::vector<block*> free_blocks;
    size_t outstanding = 0;      // checked-out blocks (live holders)
    bool engine_alive = true;

    // Reserve up front: return_block runs inside ~strong_ptr (noexcept), so the
    // free-list push must never allocate
    reference_holder_pool() { free_blocks.reserve(max_free_blocks); }

    static void return_block(jai::detail::control_block_base* base) {
        block* b = static_cast<block*>(static_cast<jai::detail::control_block<reference_holder>*>(base));
        reference_holder_pool* pool = b->home;
        --pool->outstanding;
#ifndef NDEBUG
        base->magic = jai::detail::cb_magic_dead;   // scrambled parked OR freed (use-after-free canary)
#endif
        if (pool->engine_alive && pool->free_blocks.size() < max_free_blocks) {
            pool->free_blocks.push_back(b);
        } else {
            delete b;
            if (!pool->engine_alive && pool->outstanding == 0) {
                delete pool;
            }
        }
    }

    strong_ptr<reference_holder> acquire() {
        block* b;
        if (!free_blocks.empty()) {
            b = free_blocks.back();
            free_blocks.pop_back();
            b->strong_count = 1;
            b->weak_count = 1;
#ifndef NDEBUG
            b->magic = jai::detail::cb_magic_live;
#endif
        } else {
            b = new block();
            b->home = this;
            b->dealloc_fn = &return_block;
        }
        ++outstanding;
        return jai::detail::adopt_pooled(new (b->storage) reference_holder());
    }

    static void teardown(void* p) {
        auto* pool = static_cast<reference_holder_pool*>(p);
        for (block* b : pool->free_blocks) {
            delete b;
        }
        pool->free_blocks.clear();
        if (pool->outstanding == 0) {
            delete pool;
        } else {
            pool->engine_alive = false;
        }
    }
};

strong_ptr<script_value::reference_holder> script_value::acquire_reference_holder(engine* eng) {
    // Worker-side mint (cell refs inside parallel bodies): the pool is single-threaded
    // (main thread is parked while workers run), so take a plain self-deleting block
    if (eng->parallel_region_active()) [[unlikely]] {
        return make_strong<reference_holder>();
    }
    void*& slot = eng->reference_holder_pool_slot();
    if (!slot) [[unlikely]] {
        slot = new reference_holder_pool();
        eng->set_reference_holder_pool_teardown(&reference_holder_pool::teardown);
    }
    return static_cast<reference_holder_pool*>(slot)->acquire();
}

script_value script_value::make_cell_reference(script_value&& boxed, engine* eng) {
    static_assert(sizeof(script_value) == reference_holder::cell_storage_size &&
                  alignof(script_value) <= 8, "cell inline storage must fit a script_value");
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_reference(boxed.get_type_info());
    // engine::memory_cap: cells are heap boxes (raised at the next loop back-edge)
    eng->execution_limits().memory_charge_deferred(sizeof(reference_holder));
    auto ref = acquire_reference_holder(eng);
    new (ref->cell_storage) script_value(std::move(boxed));
    ref->has_cell = true;
    v.storage_ = ref;
    return v;
}

script_value script_value::make_map_entry_reference(const strong_ptr<std::map<script_value, script_value>>& map_storage,
                                                    const script_value& key, engine* eng, type_info_ptr value_type) {
    if (!map_storage) {
        throw runtime_error("Cannot create reference to null map");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    auto it = map_storage->find(key);
    v.type_info_ = eng->get_type_info_reference(it != map_storage->end() ? it->second.get_type_info() : nullptr);
    eng->execution_limits().memory_charge_deferred(sizeof(reference_holder));
    auto ref = acquire_reference_holder(eng);
    new (ref->cell_storage) script_value(key.is_reference() ? key.deref() : key);
    ref->has_map_key = true;
    ref->container_map = map_storage;
    ref->container_element_type = value_type;
    v.storage_ = ref;
    return v;
}

script_value script_value::make_array(type_info_ptr element_type, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create array with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_array(element_type);

    // Typed flat storage (stage 2, behind the scaffold flag): int/float element types
    // mint raw-buffer nodes. The airtight tag (stage 0) is what makes this sound.
    script_array::kind_t node_kind = script_array::kind_t::hetero;
    if (eng->typed_array_storage() && element_type) {
        if (element_type->base_type == script_value_type::jai_int_type) {
            node_kind = script_array::kind_t::i64;
        } else if (element_type->base_type == script_value_type::jai_float_type) {
            node_kind = script_array::kind_t::f64;
        }
    }

    // Create node with small default capacity to avoid first few reallocations
    auto node = make_strong<script_array>(node_kind);
    node->reserve(8);
    v.storage_ = node;
    return v;
}

script_value script_value::make_map(type_info_ptr keyType, type_info_ptr valueType, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create map with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_map(keyType, valueType);
    v.storage_ = make_strong<std::map<script_value, script_value>>();
    return v;
}

script_value script_value::make_object(const std::string& type_name, std::shared_ptr<void> data, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create object with null engine pointer");
    }

    uint64_t type_id = eng->get_symbolizer()->intern(type_name);
    return make_object(type_name, type_id, data, eng);
}

script_value script_value::make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng, bool is_cpp_class) {
    if (!eng) {
        throw runtime_error("Cannot create object with null engine pointer");
    }
    // CRITICAL: Get persistent type_info from class registry (fast O(1) lookup by type_id)
    // NEVER use type_info::make_object() - it creates a TEMPORARY that gets freed (0xDDDDDDDD)

    auto class_def = eng->get_class_definition(type_id);
    if (!class_def) {
        throw runtime_error("Cannot create object of unregistered class '" + type_name +
            "'. Classes must be registered with engine.register_class() before instantiation.");
    }

    script_value v(std::monostate{}, eng);
    v.type_info_ = class_def->get_type_info();

    auto obj = make_strong<object_holder>();
    obj->type_name = type_name;
    obj->type_id = type_id;  // Set the cached type_id for fast comparison
    obj->data = data;
    obj->is_class_instance_wrapper = is_cpp_class;  // True when data is a class_instance object (both C++ and script classes), false for raw data
    v.storage_ = obj;
    return v;
}

script_value script_value::make_coroutine_handle(uint64_t type_id, std::shared_ptr<void> handle, engine* eng) {
    script_value v(std::monostate{}, eng);
    auto obj = make_strong<object_holder>();
    obj->type_name = "coroutine_handle";
    obj->type_id = type_id;
    obj->data = std::move(handle);
    obj->is_class_instance_wrapper = false;
    obj->is_coroutine_handle = true;
    v.storage_ = obj;
    return v;
}

script_value script_value::make_cpp_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create cpp_object with null engine pointer");
    }

    // CRITICAL: Get persistent type_info from class registry (fast O(1) lookup by type_id)

    auto class_def = eng->get_class_definition(type_id);
    if (!class_def) {
        throw runtime_error("Cannot create cpp_object of unregistered class '" + type_name +
            "'. Classes must be registered with engine.register_class() before instantiation.");
    }

    script_value v(std::monostate{}, eng);
    v.type_info_ = class_def->get_type_info();

    auto obj = make_strong<object_holder>();
    obj->type_name = type_name;
    obj->type_id = type_id;  // Use the provided type_id directly (no re-interning)
    obj->data = data;
    obj->is_class_instance_wrapper = false;  // make_cpp_object is for raw C++ objects
    v.storage_ = obj;
    return v;
}

script_value script_value::make_empty_weak_ptr(type_info_ptr weak_ptr_type, engine* eng) {
    script_value v(std::monostate{}, eng);
    if (weak_ptr_type) {
        v.type_info_ = weak_ptr_type;
    } else if (eng) {
        v.type_info_ = eng->get_type_info_weak_ptr(nullptr);
    }
    v.storage_ = jai::weaker_ptr<object_holder>();
    return v;
}

script_value script_value::make_invalid(engine* eng) {
    script_value val(std::monostate{}, eng);  // Start with null
    val.storage_ = invalid_tag{};  // Change to invalid
    if (eng) {
        val.type_info_ = eng->get_type_info_invalid();  // Set proper type info
    }
    return val;
}

checked_result<script_value> script_value::make_weak_ptr(const script_value& value, engine* eng) {
    if (!eng) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::unsupported_operation),
            "Cannot create weak_ptr with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_weak_ptr(value.get_type_info());

    // Only accept shared_ptr types for weak_ptr creation
    // Regular objects have value semantics and get cloned when passed as parameters,
    // so creating a weak_ptr from them doesn't work correctly
    if (value.type() == script_value_type::jai_shared_ptr_type) {
        auto holder = value.get_object_holder();
        if (!holder) {
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unsupported_operation),
                "Failed to get object_holder from script_value");
        }

        jai::weaker_ptr<object_holder> weak = holder;
        v.storage_ = weak;
    } else if (value.type() == script_value_type::jai_object_type) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "Cannot create weak_ptr from a value-semantic object. Use shared_ptr<T> to enable reference semantics: auto obj = shared_ptr<T>(...); auto weak = weak_ptr<T>(obj);");
    } else {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "weak_ptr can only be created from shared_ptr<T>");
    }

    return checked_result<script_value>(v);
}

script_value script_value::make_element_reference(const strong_ptr<script_array>& container, size_t index,
                                                  engine* eng, type_info_ptr element_type) {
    if (!container || index >= container->size()) {
        throw runtime_error("Cannot create reference to out-of-range array element");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    type_info_ptr referent_tag;
    if (container->is_typed()) {
        referent_tag = container->kind() == script_array::kind_t::i64 ? eng->get_type_info_int()
                                                                      : eng->get_type_info_float();
    } else {
        referent_tag = container->values()[index].get_type_info();
    }
    v.type_info_ = eng->get_type_info_reference(referent_tag);
    auto ref = acquire_reference_holder(eng);
    ref->container_element_type = element_type;
    ref->container = container;          // owns the node (keeps it alive) + enables re-resolve
    ref->container_index = index;
    v.storage_ = ref;
    return v;
}

script_value script_value::make_field_reference(const std::shared_ptr<class_instance>& owner, uint64_t field_id,
                                                engine* eng, type_info_ptr field_type) {
    if (!owner) {
        throw runtime_error("Cannot create reference to null");
    }
    if (!eng) {
        throw runtime_error("Cannot create reference: null engine pointer");
    }
    script_value* field_value = owner->find_field_value(field_id);
    if (!field_value) {
        throw runtime_error("Reference to a removed field");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_reference(field_value->get_type_info());
    auto ref = acquire_reference_holder(eng);
    ref->owner_instance = owner;          // pins the instance for the reference's lifetime
    ref->field_id = field_id;
    ref->container_element_type = field_type;
    v.storage_ = ref;
    return v;
}

script_value script_value::make_function(const script_function& func, engine* eng) {
    return make_function(script_function(func), eng);
}

script_value script_value::make_function(script_function&& func, engine* eng) {
    if (!eng) {
        throw runtime_error("Cannot create function with null engine pointer");
    }
    script_value v(std::monostate{}, eng);
    v.type_info_ = eng->get_type_info_function(eng->get_type_info_void(), {}); // TODO: Proper type info
    v.storage_ = make_strong<script_function>(std::move(func));
    return v;
}

script_value::script_value(const script_value& other)
    : type_info_(other.type_info_),
      engine_(other.engine_),
      storage_(other.storage_) {
    // The variant copy bumps the (immutable) binding box refcount, so copies of bound values remain aliases
    // NOTE: Raw engine* is much faster to copy than weak_ptr (no atomic ops)
}

// NOTE: Intentionally shallow; the interpreter handles cloning based on JaiScript value/reference semantics.
script_value& script_value::operator=(const script_value& other) {
    if (this != &other) {
        type_info_ = other.type_info_;
        engine_ = other.engine_;
        storage_ = other.storage_;
    }
    return *this;
}

script_value script_value::clone() const {
    if (!engine_) {
        throw runtime_error("Cannot clone script_value: missing engine pointer");
    }

    // Parallel captured-read borrow: cloning IS the tier-3 materialization boundary
    // (store kernel / explicit clone inside a region). The silent deep clone reads the
    // shared container by const& only - no transient handle copies, no refcount race.
    if (raw_storage_index() == TYPEID_PARALLEL_BORROW) {
        return parallel_detached_copy();
    }

    // shared_ptr<T> is a TYPE MARKER that indicates reference semantics for normal operations,
    // but clone() should perform a deep copy to create an independent instance
    if (type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type) {
        auto obj_holder = std::get<strong_ptr<object_holder>>(storage_);
        if (!obj_holder) {
            throw runtime_error("Cannot clone shared_ptr: null object_holder");
        }

        if (obj_holder->is_class_instance_wrapper) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
            auto new_instance = instance->deep_copy();

            auto new_holder = make_strong<object_holder>();
            new_holder->type_name = obj_holder->type_name;
            new_holder->type_id = obj_holder->type_id;
            new_holder->data = new_instance;
            new_holder->is_class_instance_wrapper = true;

            script_value result(std::monostate{}, engine_);
            result.type_info_ = type_info_;
            result.storage_ = new_holder;
            return result;
        }

        auto class_def = engine_->get_class_definition(obj_holder->type_id);
        if (class_def && class_def->has_copy_function()) {
            auto new_cpp_obj = class_def->copy_object(obj_holder->data.get());
            if (new_cpp_obj) {
                auto new_holder = make_strong<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->type_id = obj_holder->type_id;
                new_holder->data = new_cpp_obj;
                new_holder->is_class_instance_wrapper = false;

                script_value result(std::monostate{}, engine_);
                result.type_info_ = type_info_;
                result.storage_ = new_holder;
                return result;
            }
        }

        throw runtime_error(
            "Cannot clone shared_ptr<" + obj_holder->type_name + ">: type is non-copyable. "
            "Register a copy constructor with dynamic_binder<" + obj_holder->type_name + ">::copy_constructor() "
            "to enable deep copying.");
    }

    script_value result(std::monostate{}, engine_);  // Preserve engine pointer!
    result.type_info_ = type_info_;

    // Use current_type() to check what's actually stored, not the declared type
    switch (current_type()) {
        case script_value_type::jai_array_type: {
            const auto& other_node = *std::get<strong_ptr<script_array>>(storage_);
            if (other_node.is_typed()) {
                // kind-preserving deep copy = one buffer copy (8B/element)
                engine_->execution_limits().memory_charge_deferred(sizeof(script_int) * (other_node.size() + 1));
                auto new_node = make_strong<script_array>(other_node.kind());
                if (other_node.kind() == script_array::kind_t::i64) {
                    new_node->ints() = other_node.ints();
                } else {
                    new_node->floats() = other_node.floats();
                }
                result.storage_ = new_node;
                break;
            }
            const auto& other_array = other_node.values();
            // engine::memory_cap: count the fresh container storage (raised at the next
            // loop back-edge); element clones charge themselves recursively
            engine_->execution_limits().memory_charge_deferred(sizeof(script_value) * (other_array.size() + 1));
            auto new_node = make_strong<script_array>();
            new_node->values().reserve(other_array.size());
            for (const auto& elem : other_array) {
                new_node->values().push_back(elem.clone());
            }
            result.storage_ = new_node;
            break;
        }
        case script_value_type::jai_map_type: {
            auto& other_map = *std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
            // engine::memory_cap: count the fresh container storage (raised at the next
            // loop back-edge); key/value clones charge themselves recursively
            engine_->execution_limits().memory_charge_deferred(2 * sizeof(script_value) * (other_map.size() + 1));
            auto new_map = make_strong<std::map<script_value, script_value>>();
            for (const auto& [key, value] : other_map) {
                new_map->emplace(key.clone(), value.clone());
            }
            result.storage_ = new_map;
            break;
        }
        case script_value_type::jai_object_type: {
            // Regular objects have VALUE semantics by default (deep copy)
            // Only shared_ptr<T> has reference semantics (handled by early return above)
            auto obj_holder = std::get<strong_ptr<object_holder>>(storage_);

            // ...and coroutine handles: a handle references a RUNNING computation, so
            // every copy shares it (field stores, aliases, params, containers all see
            // the same resume/done state). Deep-copying a suspended coroutine is
            // meaningless - this is the ruled reference-semantics-on-copy design.
            if (obj_holder->is_coroutine_handle) {
                return *this;
            }

            if (obj_holder->is_class_instance_wrapper) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                auto new_instance = instance->deep_copy();

                auto new_holder = make_strong<object_holder>();
                new_holder->type_name = obj_holder->type_name;
                new_holder->type_id = obj_holder->type_id;  // Preserve the cached type_id
                new_holder->data = new_instance;
                new_holder->is_class_instance_wrapper = true;
                result.storage_ = new_holder;
            } else {
                bool copied = false;
                if (engine_) {
                    auto class_def = engine_->get_class_definition(obj_holder->type_id);
                    if (class_def && class_def->has_copy_function()) {
                        // cpp_bound references own nothing (data null) - the live object is
                        // bound_ptr. Deep copy from it and drop the binding so the
                        // clone is an independent owned object, not an alias.
                        const void* copy_src = obj_holder->data ? obj_holder->data.get() : obj_holder->bound_ptr;
                        auto new_cpp_obj = copy_src ? class_def->copy_object(copy_src) : nullptr;
                        if (new_cpp_obj) {
                            auto new_holder = make_strong<object_holder>();
                            new_holder->type_name = obj_holder->type_name;
                            new_holder->type_id = obj_holder->type_id;
                            new_holder->data = new_cpp_obj;
                            new_holder->is_class_instance_wrapper = false;
                            result.storage_ = new_holder;  // fresh holder: bound_ptr stays null (clone detaches)
                            copied = true;
                        }
                    }
                }

                if (!copied) {
                    throw runtime_error(
                        "Cannot deep copy C++ object of type '" + obj_holder->type_name +
                        "'. Register a copy constructor with dynamic_binder<T>::copy_constructor() "
                        "or use shared_ptr<" + obj_holder->type_name + "> for reference semantics.");
                }
            }
            break;
        }
        case script_value_type::jai_reference_type: {
            // When cloning a reference, we want to clone the referenced value,
            // not create another reference to the same value
            return deref().clone();
        }
        case script_value_type::jai_weak_ptr_type: {
            // For weak_ptr, shallow copy the weak_ptr itself (copy the weak reference)
            result.storage_ = storage_;
            break;
        }
        case script_value_type::jai_shared_ptr_type: {
            // For shared_ptr, shallow copy the shared_ptr itself (copy the shared reference)
            result.storage_ = storage_;
            break;
        }
        default:
            // For cpp_bound values, we need to read the actual value and create an independent copy
            if (is_cpp_bound()) {
                // Read the actual value from the C++ variable and create a new independent value
                // Assigning fresh storage inherently detaches the clone from the C++ variable
                if (is_int()) {
                    result.storage_ = as_int();
                } else if (is_float()) {
                    result.storage_ = as_float();
                } else if (is_bool()) {
                    result.storage_ = as_bool();
                } else if (is_char()) {
                    result.storage_ = as_char();
                } else if (is_string()) {
                    result.storage_ = make_strong<script_string>(as_string());
                } else {
                    // For other cpp_bound types, fall back to shallow copy (opaque bounds stay bound)
                    result.storage_ = storage_;
                }
            } else {
                // For primitive types and functions, shallow copy is fine
                result.storage_ = storage_;
            }
            break;
    }

    return result;
}

// Parallel-region detach (see value.hpp): fresh strong_ptr for EVERY heavy node
// (strings included), bound primitives decoded, type_info preserved, value-semantic
// content only. The one kernel the parallel_transform barrier trusts for exclusivity,
// and the region's REFCOUNT-SILENT deep clone: every element is read by const& and
// rebuilt fresh - no transient shallow copy of a shared handle ever exists, so this
// may traverse a borrowed (region-frozen) container from a worker thread.
script_value script_value::parallel_detached_copy(std::vector<type_info*>* collected_types,
                                                  std::vector<const void*>* collected_nodes) const {
    if (!engine_) {
        throw runtime_error("Cannot detach script_value: missing engine pointer");
    }
    const size_t idx = raw_storage_index();
    if (idx == TYPEID_REFERENCE) {
        return deref().parallel_detached_copy(collected_types, collected_nodes);
    }
    if (collected_types && type_info_.get()) {
        collected_types->push_back(type_info_.get());
    }
    script_value result(std::monostate{}, engine_);
    result.type_info_ = type_info_;
    switch (idx) {
        case TYPEID_NULL:
        case TYPEID_INT:
        case TYPEID_FLOAT:
        case TYPEID_CHAR:
        case TYPEID_BOOL:
            result.storage_ = storage_;
            break;
        case TYPEID_STRING: {
            const auto& handle = std::get<strong_ptr<script_string>>(storage_);
            if (!handle) { break; }   // null handle shares nothing
            engine_->execution_limits().memory_charge_deferred(handle->size() + sizeof(script_string));
            auto fresh = make_strong<script_string>(*handle);
            if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
            result.storage_ = fresh;
            break;
        }
        case TYPEID_ARRAY: {
            const auto& handle = std::get<strong_ptr<script_array>>(storage_);
            if (!handle) { break; }
            if (handle->is_typed()) {
                // all-primitive by construction: detach = one buffer copy
                engine_->execution_limits().memory_charge_deferred(sizeof(script_int) * (handle->size() + 1));
                auto fresh = make_strong<script_array>(handle->kind());
                if (handle->kind() == script_array::kind_t::i64) {
                    fresh->ints() = handle->ints();
                } else {
                    fresh->floats() = handle->floats();
                }
                if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
                result.storage_ = fresh;
                break;
            }
            engine_->execution_limits().memory_charge_deferred(sizeof(script_value) * (handle->size() + 1));
            auto fresh = make_strong<script_array>();
            fresh->values().reserve(handle->size());
            for (const auto& elem : handle->values()) {
                fresh->values().push_back(elem.parallel_detached_copy(collected_types, collected_nodes));
            }
            if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
            result.storage_ = fresh;
            break;
        }
        case TYPEID_MAP: {
            const auto& handle = std::get<strong_ptr<std::map<script_value, script_value>>>(storage_);
            if (!handle) { break; }
            engine_->execution_limits().memory_charge_deferred(2 * sizeof(script_value) * (handle->size() + 1));
            auto fresh = make_strong<std::map<script_value, script_value>>();
            for (const auto& [key, val] : *handle) {
                fresh->emplace(key.parallel_detached_copy(collected_types, collected_nodes),
                               val.parallel_detached_copy(collected_types, collected_nodes));
            }
            if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
            result.storage_ = fresh;
            break;
        }
        case TYPEID_CPP_BOUND: {
            if (bound_semantic_index() != TYPEID_NULL) {
                // Decode, then re-detach so bound strings get fresh storage too
                return bound_decoded_temp().parallel_detached_copy(collected_types, collected_nodes);
            }
            throw runtime_error("value is not value-semantic (bound host object)");
        }
        case TYPEID_PARALLEL_BORROW: {
            // Materialize the borrow: silent deep clone of the VIEWED container. The
            // viewed structure is all-primitive by the barrier's classification, and the
            // traversal below reads it by const& only.
            const auto& tag = std::get<parallel_borrow_tag>(storage_);
            if (const auto* arr = parallel_borrow_array()) {
                engine_->execution_limits().memory_charge_deferred(sizeof(script_value) * (arr->size() + 1));
                auto fresh = make_strong<script_array>();
                fresh->values().reserve(arr->size());
                for (const auto& elem : arr->values()) {
                    fresh->values().push_back(elem.parallel_detached_copy(collected_types, collected_nodes));
                }
                if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
                result.storage_ = fresh;
            } else if (const auto* map = parallel_borrow_map()) {
                engine_->execution_limits().memory_charge_deferred(2 * sizeof(script_value) * (map->size() + 1));
                auto fresh = make_strong<std::map<script_value, script_value>>();
                for (const auto& [key, val] : *map) {
                    fresh->emplace(key.parallel_detached_copy(collected_types, collected_nodes),
                                   val.parallel_detached_copy(collected_types, collected_nodes));
                }
                if (collected_nodes) { collected_nodes->push_back(fresh.get()); }
                result.storage_ = fresh;
            }
            (void)tag;
            break;
        }
        default: {
            const char* what = idx == TYPEID_OBJECT ? "object"
                : idx == TYPEID_FUNCTION ? "function"
                : idx == TYPEID_SHARED_PTR ? "shared_ptr"
                : idx == TYPEID_WEAK_PTR ? "weak_ptr"
                : "unsupported type";
            throw runtime_error(std::string("value is not value-semantic (") + what + ")");
        }
    }
    return result;
}

// === Parallel captured-read borrow (see value.hpp) ===

script_value script_value::make_parallel_borrow(const script_value& source, engine* eng) {
    const script_value& v = source.deref();
    script_value result(std::monostate{}, eng);
    result.type_info_ = v.type_info_;
    parallel_borrow_tag tag;
    if (v.raw_storage_index() == TYPEID_ARRAY) {
        const auto& handle = std::get<strong_ptr<script_array>>(v.storage_);
        tag.bits = reinterpret_cast<uintptr_t>(static_cast<const void*>(handle.get()));
    } else if (v.raw_storage_index() == TYPEID_MAP) {
        const auto& handle = std::get<strong_ptr<std::map<script_value, script_value>>>(v.storage_);
        tag.bits = reinterpret_cast<uintptr_t>(static_cast<const void*>(handle.get())) | parallel_borrow_tag::k_map_bit;
    } else {
        throw runtime_error("make_parallel_borrow: source must be an array or map");
    }
    result.storage_ = tag;
    return result;
}

const script_array* script_value::parallel_borrow_array() const noexcept {
    if (const auto* tag = std::get_if<TYPEID_PARALLEL_BORROW>(&storage_)) {
        if (!tag->is_map_kind()) {
            return static_cast<const script_array*>(tag->pointer());
        }
    }
    return nullptr;
}

const std::map<script_value, script_value>* script_value::parallel_borrow_map() const noexcept {
    if (const auto* tag = std::get_if<TYPEID_PARALLEL_BORROW>(&storage_)) {
        if (tag->is_map_kind()) {
            return static_cast<const std::map<script_value, script_value>*>(tag->pointer());
        }
    }
    return nullptr;
}

const script_function& script_value::as_function() const {
    const script_value& val = deref();
    if (val.current_type() != script_value_type::jai_function_type) {
        throw runtime_error("script_value is not a function");
    }
    return *std::get<strong_ptr<script_function>>(val.storage_);
}

script_value script_value::try_unwrap_transparent_wrapper() const {
    // Only objects can be transparent wrappers
    if (!is_object() || !engine_) {
        return *this;  // Return self unchanged
    }

    auto holder = get_object_holder();
    if (!holder) {
        return *this;
    }

    auto class_def = engine_->get_class_definition(holder->type_id);
    if (!class_def || !class_def->is_transparent_wrapper()) {
        return *this;
    }

    script_value mutable_self = *this;
    script_value unwrapped = class_def->unwrap(mutable_self);
    if (!unwrapped.is_null()) {
        return unwrapped;
    }
    return *this;
}

std::string script_value::to_string() const {
    // Special handling for references to show what they point to
    if (current_type() == script_value_type::jai_reference_type) {
        return deref().to_string();
    }

    switch (current_type()) {
        case script_value_type::jai_null_type:
            return "null";
        case script_value_type::jai_int_type:
            return std::to_string(as_int());
        case script_value_type::jai_float_type:
            return std::to_string(as_float());
        case script_value_type::jai_string_type:
            return as_string();
        case script_value_type::jai_char_type:
            return std::string(1, as_char());
        case script_value_type::jai_bool_type:
            return as_bool() ? "true" : "false";
        case script_value_type::jai_array_type:
            return "[array]";
        case script_value_type::jai_map_type:
            return "[map]";
        case script_value_type::jai_object_type:
            return "[object]";
        case script_value_type::jai_function_type:
            return "[function]";
        default:
            return "[unknown]";
    }
}

const script_value& script_value::deref() const {
    // Use current_type() not defined_type() - references may have type_info with different base_type
    if (current_type() == script_value_type::jai_reference_type) {
        // Bind a reference to the held strong_ptr (do NOT copy it): copying bumps and
        // then drops the (non-atomic) refcount on every deref, which is pure overhead
        // on this hot path.
        const auto& refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference");
        }
        if (refHolder->has_cell) {
            // Cell reference: the holder owns the value - always alive, no liveness check
            return refHolder->cell()->deref();
        }
        if (refHolder->has_map_key) {
            // Map-entry reference: re-resolve via find(key) (pinned map, erase-safe)
            auto it = refHolder->container_map->find(*refHolder->cell());
            if (it == refHolder->container_map->end()) {
                throw runtime_error("Reference to a removed map entry");
            }
            return it->second.deref();
        }
        if (refHolder->container) {
            // Vector-element reference: recompute the element address from container+index
            // each time so it survives reallocation (push), and bounds-check so a shrink
            // (pop/erase/clear) throws instead of reading freed memory (#41).
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Reference to a removed array element");
            }
            if (refHolder->container->is_typed()) {
                return refHolder->materialize_typed_element(engine_);
            }
            return refHolder->container->values()[refHolder->container_index].deref();
        }
        if (refHolder->owner_instance) {
            // Field reference: re-resolve by id (no lazy default insert - the pinned
            // instance IS the lifetime)
            const script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            return field_value->deref();
        }
        throw runtime_error("Null reference");   // unreachable: every factory sets a mode
    }
    // For C++ references, we don't deref here - they need special handling
    // The interpreter will handle them specially
    return *this;
}

script_value& script_value::deref() {
    // Use current_type() not defined_type() - references may have type_info with different base_type
    if (current_type() == script_value_type::jai_reference_type) {
        // See const overload: bind, don't copy, the strong_ptr (avoids refcount churn).
        auto& refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference");
        }
        if (refHolder->has_cell) {
            return refHolder->cell()->deref();
        }
        if (refHolder->has_map_key) {
            auto it = refHolder->container_map->find(*refHolder->cell());
            if (it == refHolder->container_map->end()) {
                throw runtime_error("Reference to a removed map entry");
            }
            return it->second.deref();
        }
        if (refHolder->container) {
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Reference to a removed array element");
            }
            if (refHolder->container->is_typed()) {
                // Materialized VIEW: reads are correct (refreshed per touch); WRITERS
                // must never come through mutable deref on a typed element (audited -
                // they use assign_through / the typed store chokepoints)
                return const_cast<script_value&>(refHolder->materialize_typed_element(engine_));
            }
            return refHolder->container->values()[refHolder->container_index].deref();
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            return field_value->deref();
        }
        throw runtime_error("Null reference");   // unreachable: every factory sets a mode
    }
    return *this;
}

void script_value::assign_through(const script_value& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->has_cell) {
            *refHolder->cell() = value;
            return;
        }
        if (refHolder->has_map_key) {
            auto it = refHolder->container_map->find(*refHolder->cell());
            if (it == refHolder->container_map->end()) {
                throw runtime_error("Assignment to a removed map entry");
            }
            it->second = value;
            return;
        }
        if (refHolder->container) {
            // Vector-element reference: resolve fresh + bounds-check before writing,
            // so a write-through after a reallocation/shrink can't corrupt the heap (#41).
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Assignment to a removed array element");
            }
            if (refHolder->container->is_typed()) {
                // constraint enforcement ran upstream (the tag is airtight) - defensive
                // wall for the unreachable non-numeric case
                const script_value& v = value.is_reference() ? value.deref() : value;
                const size_t vi = v.raw_storage_index();
                if (vi != TYPEID_INT && vi != TYPEID_FLOAT) {
                    throw runtime_error("Type mismatch in assignment");
                }
                refHolder->container->set(refHolder->container_index, v);
                return;
            }
            refHolder->container->values()[refHolder->container_index] = value;
            return;
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            *field_value = value;
            return;
        }
        throw runtime_error("Null reference in assign_through");   // unreachable: every factory sets a mode
    } else if (is_cpp_bound()) {
        void* boundPtr = bound_target_ptr();
        const uint8_t sizeAndSign = bound_size_and_sign();
        switch (type()) {
            case script_value_type::jai_int_type: {
                auto v = value.as<script_int>();
                const uint8_t size = sizeAndSign & 0x7F;
                if (sizeAndSign & 0x80) {
                    switch (size) {
                        case 1: *static_cast<uint8_t*>(boundPtr) = static_cast<uint8_t>(v); break;
                        case 2: *static_cast<uint16_t*>(boundPtr) = static_cast<uint16_t>(v); break;
                        case 4: *static_cast<uint32_t*>(boundPtr) = static_cast<uint32_t>(v); break;
                        default: *static_cast<uint64_t*>(boundPtr) = static_cast<uint64_t>(v); break;
                    }
                } else {
                    switch (size) {
                        case 1: *static_cast<int8_t*>(boundPtr) = static_cast<int8_t>(v); break;
                        case 2: *static_cast<int16_t*>(boundPtr) = static_cast<int16_t>(v); break;
                        case 4: *static_cast<int32_t*>(boundPtr) = static_cast<int32_t>(v); break;
                        default: *static_cast<script_int*>(boundPtr) = v; break;
                    }
                }
                break;
            }
            case script_value_type::jai_float_type:
                if ((sizeAndSign & 0x7F) == sizeof(float))
                    *static_cast<float*>(boundPtr) = static_cast<float>(value.as<script_float>());
                else
                    *static_cast<script_float*>(boundPtr) = value.as<script_float>();
                break;
            case script_value_type::jai_string_type:
                *static_cast<std::string*>(boundPtr) = value.as<script_string>();
                break;
            case script_value_type::jai_bool_type:
                *static_cast<bool*>(boundPtr) = value.as<script_bool>();
                break;
            case script_value_type::jai_char_type:
                *static_cast<char*>(boundPtr) = value.as<script_char>();
                break;
            default:
                // For complex types, we'll need to use the conversion registry
                throw runtime_error("assign_through not yet implemented for this cpp_bound type");
        }
    } else {
        *this = value;
    }
}

void script_value::assign_through(script_value&& value) {
    if (type() == script_value_type::jai_reference_type) {
        auto refHolder = std::get<strong_ptr<reference_holder>>(storage_);
        if (!refHolder) {
            throw runtime_error("Null reference in assign_through");
        }
        if (refHolder->has_cell) {
            *refHolder->cell() = std::move(value);
            return;
        }
        if (refHolder->has_map_key) {
            auto it = refHolder->container_map->find(*refHolder->cell());
            if (it == refHolder->container_map->end()) {
                throw runtime_error("Assignment to a removed map entry");
            }
            it->second = std::move(value);
            return;
        }
        if (refHolder->container) {
            if (refHolder->container_index >= refHolder->container->size()) {
                throw runtime_error("Assignment to a removed array element");
            }
            if (refHolder->container->is_typed()) {
                const script_value& v = value.is_reference() ? value.deref() : value;
                const size_t vi = v.raw_storage_index();
                if (vi != TYPEID_INT && vi != TYPEID_FLOAT) {
                    throw runtime_error("Type mismatch in assignment");
                }
                refHolder->container->set(refHolder->container_index, v);
                return;
            }
            refHolder->container->values()[refHolder->container_index] = std::move(value);
            return;
        }
        if (refHolder->owner_instance) {
            script_value* field_value = refHolder->owner_instance->find_field_value(refHolder->field_id);
            if (!field_value) {
                throw runtime_error("Reference to a removed field");
            }
            *field_value = std::move(value);
            return;
        }
        throw runtime_error("Null reference in assign_through");   // unreachable: every factory sets a mode
    } else if (is_cpp_bound()) {
        // can't move into a C++ variable — fall back to copy
        assign_through(value);
    } else {
        *this = std::move(value);
    }
}

bool script_value::operator==(const script_value& other) const {
    const script_value& lhs = deref();
    const script_value& rhs = other.deref();

    if (lhs.type() != rhs.type()) {
        return false;
    }

    switch (lhs.type()) {
        case script_value_type::jai_null_type:
            return true;
        case script_value_type::jai_int_type:
            return lhs.unchecked_as_int() == rhs.unchecked_as_int();
        case script_value_type::jai_float_type:
            return lhs.unchecked_as_float() == rhs.unchecked_as_float();
        case script_value_type::jai_string_type:
            return lhs.unchecked_as_string() == rhs.unchecked_as_string();
        case script_value_type::jai_char_type:
            return lhs.unchecked_as_char() == rhs.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return lhs.unchecked_as_bool() == rhs.unchecked_as_bool();
        default:
            return false;
    }
}

std::strong_ordering script_value::operator<=>(const script_value& other) const {
    if (auto cmp = type() <=> other.type(); cmp != 0) {
        return cmp;
    }
    switch (type()) {
        case script_value_type::jai_null_type:
            return std::strong_ordering::equal; // All nulls are equal
        case script_value_type::jai_int_type:
            return unchecked_as_int() <=> other.unchecked_as_int();
        case script_value_type::jai_float_type:
            // Use a TOTAL order over doubles (IEEE-754 totalOrder). The previous
            // code mapped every NaN comparison to `less`, which made NaN < NaN
            // true — an irreflexivity violation that is undefined behavior when a
            // float (or NaN) is used as a std::map key. std::strong_order makes
            // all NaNs compare equal to each other and orders them deterministically.
            return std::strong_order(unchecked_as_float(), other.unchecked_as_float());
        case script_value_type::jai_string_type:
            return unchecked_as_string() <=> other.unchecked_as_string();
        case script_value_type::jai_char_type:
            return unchecked_as_char() <=> other.unchecked_as_char();
        case script_value_type::jai_bool_type:
            return unchecked_as_bool() <=> other.unchecked_as_bool();
        default: {
            // Complex types (array/map/object/function/shared_ptr/reference): order
            // by the address of the HELD object, which is a STABLE identity for a
            // given value. The previous code compared &storage_ — the address of the
            // operand's own member — which is a transient property of the comparison,
            // not of the value, so the ordering of a logical key changed between
            // operations and violated std::map's strict-weak-ordering invariant
            // (corrupting maps with complex-typed keys). Use std::less for a
            // guaranteed total order over pointers.
            auto identity = [](const script_value& v) -> const void* {
                switch (v.raw_storage_index()) {
                    case TYPEID_ARRAY:      return std::get_if<TYPEID_ARRAY>(&v.storage_)->get();
                    case TYPEID_MAP:        return std::get_if<TYPEID_MAP>(&v.storage_)->get();
                    case TYPEID_OBJECT:     return std::get_if<TYPEID_OBJECT>(&v.storage_)->get();
                    case TYPEID_FUNCTION:   return std::get_if<TYPEID_FUNCTION>(&v.storage_)->get();
                    case TYPEID_SHARED_PTR: return std::get_if<TYPEID_SHARED_PTR>(&v.storage_)->get();  // holder identity; runtime-unreachable (alt 11 never constructed)
                    case TYPEID_REFERENCE:  return std::get_if<TYPEID_REFERENCE>(&v.storage_)->get();
                    case TYPEID_PARALLEL_BORROW: return std::get_if<TYPEID_PARALLEL_BORROW>(&v.storage_)->pointer();  // viewed container = stable identity (region-internal, defensive)
                    default:                return nullptr;  // invalid/weak_ptr: treated as one equivalence class
                }
            };
            const void* lp = identity(*this);
            const void* rp = identity(other);
            std::less<const void*> ptr_less;
            if (ptr_less(lp, rp)) return std::strong_ordering::less;
            if (ptr_less(rp, lp)) return std::strong_ordering::greater;
            return std::strong_ordering::equal;
        }
    }
}


} // namespace jai