#pragma once

#ifndef __JAISCRIPT_CORE_VALUE_HPP__
#define __JAISCRIPT_CORE_VALUE_HPP__

#include "types.hpp"
#include "type_info.hpp"
#include "conversion_registry.hpp"
#include "runtime_errors.hpp"
#include "strong_ptr.hpp"
#include <jaiscript/jaiscript_fwd.hpp>
#include <variant>
#include <memory>
#include <compare>
#include <cassert>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <cmath>
#include <set>

namespace jai {

    // Forward declaration
    class environment;
    class engine;
    class class_instance;
    
    // Forward declaration for conversion registry helper
    namespace conversions {
        class conversion_registry;
    }

    // Forward declarations for helper functions
    std::shared_ptr<conversions::conversion_registry> get_engine_conversion_registry(engine* eng);

    // as<T&> bridges (defined in engine.cpp): validate an object holder's registered type
    // against a requested C++ type, and extract the C++ payload behind a class_instance
    // wrapper (nullptr when the wrapper carries none, e.g. a pure script-class object).
    bool engine_holder_matches_type(engine* eng, const std::string& holder_type_name, const std::type_info& requested);
    std::shared_ptr<void> engine_extract_instance_cpp_object(engine* eng, const std::shared_ptr<void>& instance_data);
    
    // Forward declaration for friend access
    namespace detail {
        template<typename T> struct value_converter;
    }
    
    // Forward declarations for friend classes
    namespace serialization {
        class binary_archive_writer;
        class binary_archive_reader;
        class json_archive_writer;
        class json_archive_reader;
    }

    // Helper trait to detect shared_ptr specializations
    template<typename T, template<typename...> class Template>
    struct is_specialization : std::false_type {};
    
    template<template<typename...> class Template, typename... Args>
    struct is_specialization<Template<Args...>, Template> : std::true_type {};
    
    template<typename T, template<typename...> class Template>
    inline constexpr bool is_specialization_v = is_specialization<T, Template>::value;

    // Convert a double to script_int (int64) with DEFINED behavior for NaN, +/-inf,
    // and out-of-range values. A plain static_cast of those to a 64-bit integer is
    // undefined behavior in C++ (commonly yielding INT64_MIN or a trap). NaN -> 0,
    // and out-of-range saturates to INT64_MIN/MAX.
    inline script_int float_to_script_int(script_float f) noexcept {
        if (std::isnan(f)) return 0;
        if (f >= 9223372036854775808.0) return (std::numeric_limits<script_int>::max)();   // >= 2^63
        if (f <= -9223372036854775808.0) return (std::numeric_limits<script_int>::min)();  // <= -2^63
        return static_cast<script_int>(f);
    }

    class script_value {
    public:
        script_value(std::nullptr_t) = delete;
        
        // Friend std::map and std::unordered_map so they can use the private default constructor for operator[]
        template<typename K, typename V, typename C, typename A>
        friend class std::map;
        template<typename K, typename V, typename H, typename E, typename A>
        friend class std::unordered_map;
        
        // Friend std::pair and std::tuple for map's internal piecewise construction
        template<typename T1, typename T2>
        friend struct std::pair;
        template<typename... Types>
        friend class std::tuple;
        
    private:
        // Private default constructor - ONLY for std::map operator[] compatibility
        // This creates an invalid script_value that MUST be immediately assigned to
        script_value() : type_info_(nullptr), storage_(std::monostate{}) {}
        
    public:
        
        // ALL constructors require engine context - no exceptions!
        // Special constructor for null/void values
        explicit script_value(std::monostate, engine* eng) : type_info_(nullptr), engine_(eng), storage_(std::monostate{}) {}
        
        // AST literal constructor - ONLY for parser to create placeholder values in AST nodes
        // These values should NEVER be used directly, only as templates for interpreter
        // type_info_ is set to nullptr since these are temporary placeholders without engine context
        struct ast_literal_tag {};
        script_value(ast_literal_tag, script_int i) : type_info_(nullptr), storage_(i) {}
        script_value(ast_literal_tag, script_float f) : type_info_(nullptr), storage_(f) {}
        script_value(ast_literal_tag, const script_string& s) : type_info_(nullptr), storage_(make_strong<script_string>(s)) {}
        script_value(ast_literal_tag, script_char c) : type_info_(nullptr), storage_(c) {}
        script_value(ast_literal_tag, script_bool b) : type_info_(nullptr), storage_(b) {}
        script_value(ast_literal_tag, std::monostate) : type_info_(nullptr), storage_(std::monostate{}) {}
        script_value(script_int i, engine* eng);
        script_value(script_float f, engine* eng);
        script_value(const script_string& s, engine* eng);
        script_value(script_string&& s, engine* eng);
        script_value(const char* s, engine* eng);
        script_value(script_char c, engine* eng);
        script_value(script_bool b, engine* eng);
        
        // Template constructors for all numeric types - ALL require engine references
        // NOTE: Implementations are in value_impl.hpp (include after engine.hpp)

        // Engine-aware template constructors for integral types (preferred)
        template<typename T>
        requires (std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, char> && !std::is_same_v<T, script_int>)
        script_value(T i, engine* eng);

        // Engine-aware template constructors for floating point types (preferred)
        template<typename T>
        requires (std::is_floating_point_v<T> && !std::is_same_v<T, script_float>)
        script_value(T f, engine* eng);

        // Delete pointer constructors - use add_global_ref instead
        template<typename T>
        script_value(T* ptr, engine* eng) = delete;

        script_value(script_value&& other) noexcept
            : type_info_(std::move(other.type_info_)),
              engine_(other.engine_),
              storage_(std::move(other.storage_)) {
            other.type_info_ = nullptr;
            other.engine_ = nullptr;
            other.storage_ = std::monostate{};  // also clears any cpp_bound box (binding lives in the variant)
        }

        script_value& operator=(script_value&& other) noexcept {
            if (this != &other) {
                type_info_ = std::move(other.type_info_);
                engine_ = other.engine_;
                storage_ = std::move(other.storage_);
                other.type_info_ = nullptr;
                other.engine_ = nullptr;
                other.storage_ = std::monostate{};
            }
            return *this;
        }
        
        // Copy constructor (shallow copy for reference semantics)
        script_value(const script_value& other);
        script_value& operator=(const script_value& other);
        
        // Explicit deep copy method
        script_value clone() const;

        // Parallel-region detach (parallel_transform v0): deep copy where EVERY heavy
        // node gets a fresh strong_ptr - including strings, which clone() and plain
        // copies deliberately share - bound primitives decode to detached values, and
        // type_info tags are preserved on every node. The result's reachable set shares
        // no control block with the source, so another thread may copy/mutate it freely.
        // This is also the region's REFCOUNT-SILENT deep clone: traversal reads elements
        // by const& and constructs fresh nodes - it never makes even a transient shallow
        // copy of a shared handle, so it may read a borrowed (shared, region-frozen)
        // container from a worker thread without racing the non-atomic strong counts.
        // Value-semantic content only (null/int/float/string/char/bool + arrays/maps of
        // the same); throws jai::runtime_error naming the offending type otherwise.
        // collected_types (optional) receives every node's type_info for the region's
        // pre-warm pass; collected_nodes (optional) receives the address of every heavy
        // node the copy MINTED (string/array/map payloads) so the region can later
        // recognize results that alias a worker-provisioned value.
        script_value parallel_detached_copy(std::vector<type_info*>* collected_types = nullptr,
                                            std::vector<const void*>* collected_nodes = nullptr) const;

        // === Parallel captured-read borrow (docs/parallel_design.md, captured reads) ===
        // A borrow VIEW of an enclosing container: one raw tagged pointer, so copying the
        // value never touches a refcount - the one operation a worker may not perform on
        // shared structure (strong_ptr counts are non-atomic; even a read-only element
        // copy races). Sound because during a region (a) the write wall forbids every
        // mutation of enclosing state, so the viewed container never reallocates and its
        // interior pointers are stable, and (b) the barrier holds one anchor handle per
        // borrowed container, so the count is stationary and the container outlives the
        // region. Borrows are region-internal: the store kernel (clone_for_assignment)
        // and the region's result path materialize them via the silent deep clone, so no
        // borrow survives the join. `source` must be a (deref'd) array or map.
        static script_value make_parallel_borrow(const script_value& source, engine* eng);
        bool is_parallel_borrow() const noexcept { return raw_storage_index() == TYPEID_PARALLEL_BORROW; }
        // Viewed container accessors (nullptr when the borrow is of the other kind).
        // Arrays hand back the NODE (its address is the identity; kind dispatch in stage 2).
        const script_array* parallel_borrow_array() const noexcept;
        const script_map* parallel_borrow_map() const noexcept;

    public:
        // Engine-aware factory methods (preferred - ALWAYS use these)
        static script_value make_array(type_info_ptr element_type, engine* eng);
        static script_value make_map(type_info_ptr keyType, type_info_ptr valueType, engine* eng);
        static script_value make_object(const std::string& type_name, std::shared_ptr<void> data, engine* eng);
        // Optimized version with cached type_id
        static script_value make_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng, bool is_cpp_class = true);
        // Internal factory method for raw C++ objects - always requires type_id to avoid re-interning
        static script_value make_cpp_object(const std::string& type_name, uint64_t type_id, std::shared_ptr<void> data, engine* eng);
    public:
        // Reallocation-safe reference to a vector element (range-for auto&, arr[i] lvalue):
        // holds the owning container + index so deref/assign-through recompute the element
        // address each time and bounds-check it (see reference_holder::container).
        static script_value make_element_reference(const strong_ptr<script_array>& container, size_t index,
                                                   engine* eng, type_info_ptr element_type);
        // Instance-pinned reference to a class field: deref/assign-through re-resolve the
        // field node by id (never a cached address), so hot reload cannot dangle it.
        static script_value make_field_reference(const std::shared_ptr<class_instance>& owner, uint64_t field_id,
                                                 engine* eng, type_info_ptr field_type);
        // Cell reference (Lua-upvalue box): the reference OWNS the boxed value. Escape-marked
        // variables hold one from declaration; ref binding/aliasing shares the handle, and the
        // cell keeps the value alive for as long as any handle exists (escape-legal semantics).
        // `boxed` must not itself be a reference (callers deref first).
        static script_value make_cell_reference(script_value&& boxed, engine* eng);
        // Map-entry reference: pins the owning map (strong) and re-resolves find(key) on
        // every deref/assign-through - an erased entry errors instead of dangling, and a
        // reference into a temporary map keeps the map alive.
        static script_value make_map_entry_reference(const strong_ptr<script_map>& map_storage,
                                                     const script_value& key, engine* eng, type_info_ptr value_type);
        static script_value make_function(const script_function& func, engine* eng);
        // Rvalue mint: moves the std::function into its strong_ptr box (every backend
        // mint passes a temporary - the copy was pure churn)
        static script_value make_function(script_function&& func, engine* eng);

        // Mints an unregistered object holder wrapping a coroutine_handle; bypasses the
        // class-registry check the other make_object factories enforce (handles have no class).
        static script_value make_coroutine_handle(uint64_t type_id, std::shared_ptr<void> handle, engine* eng);

        // Factory method for C++ bound values
        // NOTE: Implementation is in value_impl.hpp (include after engine.hpp)
        template<typename T>
        static script_value make_cpp_bound(T* target, engine* eng);
        
        // Template factory methods removed - use engine->make_object instead
        // This ensures proper type name registration
    public:
        
        // Engine-aware object creation through registered class system
        template<typename T, typename... Args>
        static script_value make_registered_object(class engine* eng, Args&&... args);
        
        // Type information
        type_info_ptr get_type_info() const { return type_info_; }

        // Get interned type ID for error messages (falls back to enum value if type_info not set)
        uint64_t type_id() const {
            return type_info_ ? type_info_->id : static_cast<uint64_t>(current_type());
        }

        // current_type() - What type is ACTUALLY stored in the variant right now
        // Use this for runtime type checks (is_null, is_object, etc.)
        script_value_type current_type() const {
            switch (storage_.index()) {
                case 0: return script_value_type::jai_null_type;        // std::monostate
                case 1: return script_value_type::jai_int_type;         // script_int
                case 2: return script_value_type::jai_float_type;       // script_float
                case 3: return script_value_type::jai_string_type;      // script_string
                case 4: return script_value_type::jai_char_type;        // script_char
                case 5: return script_value_type::jai_bool_type;        // script_bool
                case 6: return script_value_type::jai_array_type;       // Array
                case 7: return script_value_type::jai_map_type;         // Map
                case 8: return script_value_type::jai_object_type;      // Object
                case 9: return script_value_type::jai_function_type;    // Function
                case 10: return script_value_type::jai_reference_type;  // Reference
                case 11: return script_value_type::jai_shared_ptr_type; // shared_ptr
                case 12: return script_value_type::jai_weak_ptr_type;   // weak_ptr
                case 13: return script_value_type::jai_invalid_type;    // invalid_tag
                case 14: {                                              // cpp_bound box: report the semantic type (the old shadow index)
                    switch (bound_semantic_index()) {
                        case TYPEID_INT: return script_value_type::jai_int_type;
                        case TYPEID_FLOAT: return script_value_type::jai_float_type;
                        case TYPEID_STRING: return script_value_type::jai_string_type;
                        case TYPEID_CHAR: return script_value_type::jai_char_type;
                        case TYPEID_BOOL: return script_value_type::jai_bool_type;
                        default: return script_value_type::jai_null_type;
                    }
                }
                case 15: return script_value_type::jai_invalid_type;    // parallel borrow: region-internal, never a script-visible type
                default: return script_value_type::jai_invalid_type;
            }
        }

        // defined_type() - What type the variable was DECLARED as (from type_info)
        // For any_type (var), returns current_type() since it's dynamically typed
        // Use this for type enforcement, assignment compatibility checks
        script_value_type defined_type() const {
            if (type_info_) {
                if (type_info_->base_type == script_value_type::jai_any_type) {
                    return current_type();  // Dynamic typing - use actual value type
                }
                return type_info_->base_type;
            }
            return current_type();
        }

        // type() - Legacy alias, returns defined_type() for backward compatibility
        // Prefer using current_type() or defined_type() for clarity
        script_value_type type() const { return defined_type(); }

        // storage_type() - Legacy alias for current_type()
        script_value_type storage_type() const { return current_type(); }

        // Ultra-fast raw storage index - no pointer chasing, single integer read
        // Use this in hot paths like is_truthy() to avoid type_info_ pointer dereference
        // PURE index read: bound primitives report TYPEID_CPP_BOUND (14), never their semantic type
        inline size_t raw_storage_index() const noexcept { return storage_.index(); }

#ifdef JAISCRIPT_DIAG_XTHREAD_RC
        // Canary build: tag this value's string block single-thread (AST literals) so a
        // cross-thread refcount touch aborts AT the racing site
        void diag_tag_single_thread() {
            if (storage_.index() == TYPEID_STRING) {
                if (auto* s = std::get<strong_ptr<script_string>>(storage_).get()) {
                    detail::cb_from_object(s)->diag_tag();
                }
            }
        }
#endif
        // Type checking methods use raw_storage_index() for fastest possible type checks
        // Deref-first, then translate the cpp_bound box through its semantic_index so bound
        // primitives (and refs to them) keep answering as their semantic type
        bool is_null() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            // Opaque host pointers (make_value(T*) on an unregistered type) are NON-null
            // while the pointer is live — they ARE objects to script (open question #13,
            // ruled 2026-07); a null host pointer still reads as null.
            return idx == TYPEID_NULL ||
                   (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_NULL) &&
                    d.get_cpp_bound_ptr() == nullptr);
        }
        bool is_invalid() const { return deref().raw_storage_index() == TYPEID_INVALID; }
        bool is_int() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            return idx == TYPEID_INT || (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_INT));
        }
        bool is_float() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            return idx == TYPEID_FLOAT || (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_FLOAT));
        }
        bool is_string() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            return idx == TYPEID_STRING || (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_STRING));
        }
        bool is_char() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            return idx == TYPEID_CHAR || (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_CHAR));
        }
        bool is_bool() const {
            const script_value& d = deref();
            const size_t idx = d.raw_storage_index();
            return idx == TYPEID_BOOL || (idx == TYPEID_CPP_BOUND && d.bound_semantic_is(TYPEID_BOOL));
        }
        bool is_array() const { return deref().raw_storage_index() == TYPEID_ARRAY; }
        bool is_map() const { return deref().raw_storage_index() == TYPEID_MAP; }
        bool is_object() const {
            auto idx = deref().raw_storage_index();
            return idx == TYPEID_OBJECT || idx == TYPEID_SHARED_PTR;
        }
        bool is_function() const { return deref().raw_storage_index() == TYPEID_FUNCTION; }
        bool is_reference() const { return raw_storage_index() == TYPEID_REFERENCE; }  // Don't deref for this check!
        // No deref (matches the old member test): a reference TO a bound global answers false
        bool is_cpp_bound() const {
            const size_t idx = raw_storage_index();
            if (idx == TYPEID_CPP_BOUND) return true;
            if (idx == TYPEID_OBJECT) {
                auto* h = std::get_if<TYPEID_OBJECT>(&storage_);
                return *h && (*h)->bound_ptr != nullptr;
            }
            return false;
        }
        // Pure index test for hot guards (bound primitive/string/opaque box only, no holder chase)
        bool is_cpp_bound_primitive() const noexcept { return raw_storage_index() == TYPEID_CPP_BOUND; }

        bool is_non_owning_object() const {
            auto* h = std::get_if<TYPEID_OBJECT>(&storage_);
            return h && *h && (*h)->bound_ptr != nullptr && !(*h)->data;
        }

        // Returns nullptr if not cpp_bound. Caller is responsible for type safety.
        void* get_cpp_bound_ptr() const {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) return *b ? (*b)->ptr : nullptr;
            if (auto* h = std::get_if<TYPEID_OBJECT>(&storage_)) return *h ? (*h)->bound_ptr : nullptr;
            return nullptr;
        }

        template<typename T>
        T* get_cpp_bound_as() const {
            return static_cast<T*>(get_cpp_bound_ptr());
        }

        // Precondition: raw_storage_index() == TYPEID_CPP_BOUND. Returns a DETACHED temporary
        // that behaves exactly like the old shadow-decoded operand:
        //   INT/FLOAT/BOOL/CHAR -> decoding accessor into a fresh primitive;
        //   STRING              -> fresh script_value copying the LIVE string;
        //   NULL (opaque)       -> null value (same "Invalid operands" type_mismatch as before).
        script_value bound_decoded_temp() const {
            switch (bound_semantic_index()) {
                case TYPEID_INT: return script_value(unchecked_as_int(), engine_);
                case TYPEID_FLOAT: return script_value(unchecked_as_float(), engine_);
                case TYPEID_BOOL: return script_value(unchecked_as_bool(), engine_);
                case TYPEID_CHAR: return script_value(unchecked_as_char(), engine_);
                case TYPEID_STRING: return script_value(unchecked_as_string(), engine_);
                default: return script_value(std::monostate{}, engine_);
            }
        }

        // Store-boundary normalization (§12 all-detach ruling, 2026-07): a bound
        // PRIMITIVE stored into a container slot or member detaches to a snapshot —
        // write-through belongs to the registered NAME (and refs/captures/by-ref params
        // bound to it), never to stored copies. Opaque host tokens (semantic NULL) keep
        // their box (§13: object-like handles; decoding would flatten them to null).
        // Call sites guard on raw_storage_index() == TYPEID_CPP_BOUND first (hot paths).
        script_value detached_for_store() const {
            if (raw_storage_index() == TYPEID_CPP_BOUND && bound_semantic_index() != TYPEID_NULL) {
                return bound_decoded_temp();
            }
            return *this;
        }

    public:
        // Invoke ON THE DEREF'D value (a reference-to-bound holds a reference_holder, so a
        // bare-this lookup would answer TYPEID_NULL). Public: ONE cached semantic-index read
        // then compare beats repeated is_int()/is_float()/... probes (each re-derefs) -
        // classification sites cache this once (hand-tuned dispatch discipline, §4).
        size_t bound_semantic_index() const noexcept {
            auto* box = bound_box();
            return box ? box->semantic_index : TYPEID_NULL;
        }
    private:
        struct cpp_bound_holder;  // defined next to object_holder below
        // Invoke these ON THE DEREF'D value: a reference-to-bound holds a reference_holder
        // (index 10), so a bare-this lookup would answer null/false.
        const cpp_bound_holder* bound_box() const noexcept {
            auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_);
            return b ? b->get() : nullptr;
        }
        bool bound_semantic_is(size_t idx) const noexcept {
            auto* box = bound_box();
            return box && box->semantic_index == idx;
        }
        // Two-shape: box ptr for index 14, holder->bound_ptr for object-bound (index 8)
        void* bound_target_ptr() const noexcept {
            return get_cpp_bound_ptr();
        }
        uint8_t bound_size_and_sign() const noexcept {
            auto* box = bound_box();
            return box ? box->size_and_sign : 0;
        }
    public:

        bool is_weak_ptr() const { return deref().raw_storage_index() == TYPEID_WEAK_PTR; }

        // Type marker helpers - check if value has shared_ptr<T> or weak_ptr<T> type info
        bool is_shared_ptr_type() const {
            return type_info_ && type_info_->base_type == script_value_type::jai_shared_ptr_type;
        }

        bool is_weak_ptr_type() const {
            return type_info_ && type_info_->base_type == script_value_type::jai_weak_ptr_type;
        }

        // Check if this type has reference semantics (shared ownership by default)
        bool has_reference_semantics() const {
            const auto t = deref().type();
            return t == script_value_type::jai_object_type ||
                   t == script_value_type::jai_shared_ptr_type ||
                   t == script_value_type::jai_array_type ||
                   t == script_value_type::jai_map_type ||
                   t == script_value_type::jai_function_type;
        }

        // script_value extraction (inlined for performance)
        // These throwing versions delegate to checked_* versions for consistency
        inline script_int as_int() const {
            auto result = checked_as_int();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline script_float as_float() const {
            auto result = checked_as_float();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline const script_string& as_string() const {
            auto result = checked_as_string();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return *result.value();
        }

        inline script_bool as_bool() const {
            auto result = checked_as_bool();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        inline script_char as_char() const {
            auto result = checked_as_char();
            if (!result) {
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return result.value();
        }

        // ============================================================================
        // UNCHECKED ACCESSORS - Fast direct access without type checking
        // ONLY use these when you've already verified the type (e.g., via type() switch)
        // These check for cpp_bound values and fall back to storage_ otherwise
        // Uses std::get_if which returns a pointer without throwing (faster than std::get)
        // ============================================================================

        inline script_bool unchecked_as_bool() const noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                return *static_cast<const bool*>((*b)->ptr);
            }
            return *std::get_if<TYPEID_BOOL>(&storage_);
        }

        inline script_int unchecked_as_int() const noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                const void* p = (*b)->ptr;
                const uint8_t ss = (*b)->size_and_sign;
                const uint8_t size = ss & 0x7F;
                if (ss & 0x80) {
                    switch (size) {
                        case 1: return static_cast<script_int>(*static_cast<const uint8_t*>(p));
                        case 2: return static_cast<script_int>(*static_cast<const uint16_t*>(p));
                        case 4: return static_cast<script_int>(*static_cast<const uint32_t*>(p));
                        default: return static_cast<script_int>(*static_cast<const uint64_t*>(p));
                    }
                }
                switch (size) {
                    case 1: return static_cast<script_int>(*static_cast<const int8_t*>(p));
                    case 2: return static_cast<script_int>(*static_cast<const int16_t*>(p));
                    case 4: return static_cast<script_int>(*static_cast<const int32_t*>(p));
                    default: return *static_cast<const script_int*>(p);
                }
            }
            return *std::get_if<TYPEID_INT>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead in loops)
        // Caller MUST be dominated by a raw-index gate: no bound decode is possible through a script_int&
        inline script_int& unchecked_as_int_ref() noexcept {
            return *std::get_if<TYPEID_INT>(&storage_);
        }

        inline script_float unchecked_as_float() const noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                if (((*b)->size_and_sign & 0x7F) == sizeof(script_float))
                    return *static_cast<const script_float*>((*b)->ptr);
                return static_cast<script_float>(*static_cast<const float*>((*b)->ptr));
            }
            return *std::get_if<TYPEID_FLOAT>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead in loops)
        // Caller MUST be dominated by a raw-index gate: no bound decode is possible through a script_float&
        inline script_float& unchecked_as_float_ref() noexcept {
            return *std::get_if<TYPEID_FLOAT>(&storage_);
        }

        inline const script_string& unchecked_as_string() const noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                // string bindings are only ever created over std::string/script_string - exact type
                return *static_cast<const script_string*>((*b)->ptr);
            }
            return **std::get_if<TYPEID_STRING>(&storage_);
        }

        // Mutable accessor for in-place modification (avoids make_value() overhead)
        // Bound strings return the LIVE C++ string (write-through)
        inline script_string& unchecked_as_string_ref() noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                return *static_cast<script_string*>((*b)->ptr);
            }
            return **std::get_if<TYPEID_STRING>(&storage_);
        }

        inline script_char unchecked_as_char() const noexcept {
            if (auto* b = std::get_if<TYPEID_CPP_BOUND>(&storage_)) [[unlikely]] {
                return *static_cast<const script_char*>((*b)->ptr);
            }
            return *std::get_if<TYPEID_CHAR>(&storage_);
        }

        // Unchecked array accessor - caller must verify is_array() first.
        // HETERO surface: a typed node reaching here means a path missing its kind
        // dispatch (typed_array_design.md stage 2) - loud in Debug, empty in Release.
        inline const std::vector<script_value>& unchecked_as_array() const noexcept {
            assert(!(*std::get_if<TYPEID_ARRAY>(&storage_))->is_typed() &&
                   "typed array reached a hetero-only path (missing kind dispatch)");
            return (*std::get_if<TYPEID_ARRAY>(&storage_))->values();
        }

        // Unchecked mutable array NODE handle - caller must verify is_array() first
        inline strong_ptr<script_array>& unchecked_get_array_storage() noexcept {
            return *std::get_if<TYPEID_ARRAY>(&storage_);
        }

        // Unchecked const array NODE pointer - the array's identity (node-alias keys,
        // borrow tags). Caller must verify is_array() first.
        inline const script_array* unchecked_array_node() const noexcept {
            return std::get_if<TYPEID_ARRAY>(&storage_)->get();
        }


        // Unchecked function accessor - caller must verify is_function() first
        inline const script_function& unchecked_as_function() const noexcept {
            return **std::get_if<TYPEID_FUNCTION>(&storage_);
        }

        // Unchecked map accessor - caller must verify is_map() first
        inline const script_map& unchecked_as_map() const noexcept {
            return **std::get_if<TYPEID_MAP>(&storage_);
        }

        // Unchecked mutable map storage accessor - caller must verify is_map() first
        inline strong_ptr<script_map>& unchecked_get_map_storage() noexcept {
            return *std::get_if<TYPEID_MAP>(&storage_);
        }

        // Unchecked mutable string storage accessor - caller must verify raw string storage first
        inline strong_ptr<script_string>& unchecked_get_string_storage() noexcept {
            return *std::get_if<TYPEID_STRING>(&storage_);
        }

        inline const std::vector<script_value>& as_array() const {
            auto result = checked_as_array();
            if (!result) {
                throw runtime_error(std::string(result.message()));
            }
            return *result.value();
        }
        
        inline std::vector<script_value>& as_array() {
            // For non-const, we can't use checked_as_array() which returns const&
            // So we check type and return mutable reference directly
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            assert(!std::get<strong_ptr<script_array>>(storage_)->is_typed() &&
                   "typed array reached a hetero-only path (missing kind dispatch)");
            return std::get<strong_ptr<script_array>>(storage_)->values();
        }
        
        inline const script_map& as_map() const {
            auto result = checked_as_map();
            if (!result) {
                throw runtime_error(std::string(result.message()));
            }
            return *result.value();
        }
        
        inline script_map& as_map() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return *std::get<strong_ptr<script_map>>(storage_);
        }

        const script_function& as_function() const;

        // Safe mutable reference accessors for zero-copy parameter binding
        // These encapsulate direct storage access and handle deref() properly
        // Bound targets: alias the LIVE C++ variable when the binding's width/signedness exactly
        // matches the script type; otherwise a catchable contract error (never bad_variant_access).
        inline script_int& as_int_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_int_type) {
                throw runtime_error("script_value is not an integer");
            }
            if (auto* box = val.bound_box()) {
                if (box->size_and_sign == sizeof(script_int)) {  // size 8, signed (no 0x80 bit)
                    return *static_cast<script_int*>(box->ptr);
                }
                throw runtime_error("cannot bind a non-const script reference to a C++-bound variable of a different width");
            }
            return std::get<script_int>(val.storage_);
        }

        inline script_float& as_float_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_float_type) {
                throw runtime_error("script_value is not a float");
            }
            if (auto* box = val.bound_box()) {
                if ((box->size_and_sign & 0x7F) == sizeof(script_float)) {
                    return *static_cast<script_float*>(box->ptr);
                }
                throw runtime_error("cannot bind a non-const script reference to a C++-bound variable of a different width");
            }
            return std::get<script_float>(val.storage_);
        }

        inline script_bool& as_bool_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_bool_type) {
                throw runtime_error("script_value is not a boolean");
            }
            if (auto* box = val.bound_box()) {
                if ((box->size_and_sign & 0x7F) == sizeof(script_bool)) {
                    return *static_cast<script_bool*>(box->ptr);
                }
                throw runtime_error("cannot bind a non-const script reference to a C++-bound variable of a different width");
            }
            return std::get<script_bool>(val.storage_);
        }

        inline script_char& as_char_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_char_type) {
                throw runtime_error("script_value is not a character");
            }
            if (auto* box = val.bound_box()) {
                if ((box->size_and_sign & 0x7F) == sizeof(script_char)) {
                    return *static_cast<script_char*>(box->ptr);
                }
                throw runtime_error("cannot bind a non-const script reference to a C++-bound variable of a different width");
            }
            return std::get<script_char>(val.storage_);
        }

        inline script_string& as_string_ref() {
            script_value& val = deref();
            if (val.type() != script_value_type::jai_string_type) {
                throw runtime_error("script_value is not a string");
            }
            if (auto* box = val.bound_box()) {
                // string bindings are only ever created over std::string/script_string - exact type
                return *static_cast<script_string*>(box->ptr);
            }
            return *std::get<strong_ptr<script_string>>(val.storage_);
        }
        
        // Generic extraction with type checking
        // Thin wrapper around checked_as<T>() that throws on error
        // NOTE: This throws for compatibility with C++ bindings (dynamic_binder, function_binder)
        // Internal interpreter code should use checked_as<T>() directly
        template<typename T>
        T as() const {
            auto result = checked_as<T>();
            if (!result) {
                if (result.message().empty()) {
                    throw runtime_error("Type conversion failed");
                }
                // Format the error message with symbol IDs
                throw runtime_error(format_error_message_numeric(result.message(), result.symbol_id(), result.symbol_id2()));
            }
            return std::move(result.value());
        }
        
        // Non-const version of as() for extracting non-const references
        template<typename T>
        T as() {
            // Handle reference types
            if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;
                
                // For const references, delegate to const version
                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    return const_cast<const script_value*>(this)->as<T>();
                } else {
                    // For non-const references - use safe accessors that handle deref()
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        return as_int_ref();
                    } else if constexpr (std::is_same_v<base_type, script_float> || std::is_same_v<base_type, double>) {
                        return as_float_ref();
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        return as_bool_ref();
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        return as_char_ref();
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        return as_string_ref();
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        return as_array();
                    } else if constexpr (std::is_same_v<base_type, script_map>) {
                        return as_map();
                    } else if constexpr (std::is_same_v<base_type, script_value>) {
                        // For script_value&, just return a reference to the dereferenced value
                        return deref();
                    } else {
                        // For user-defined types stored as objects
                        if (type_info_ && type_info_->is_object()) {
                            // Unregistered-class binding (opaque box): the pointer IS the object.
                            // No holder exists so there is no name check (matches the old behavior).
                            if (auto* box = bound_box()) {
                                return *static_cast<base_type*>(box->ptr);
                            }
                            // Non-owning C++ reference (registered class): verify the holder's
                            // registered type before reinterpreting.
                            if (auto* boundHolder = std::get_if<strong_ptr<object_holder>>(&storage_);
                                boundHolder && *boundHolder && (*boundHolder)->bound_ptr) {
                                if (!engine_holder_matches_type(engine_, (*boundHolder)->type_name, typeid(base_type))) {
                                    throw runtime_error("Object type mismatch");
                                }
                                return *static_cast<base_type*>((*boundHolder)->bound_ptr);
                            }
                            if (auto* holderPtr = std::get_if<strong_ptr<object_holder>>(&storage_); holderPtr && *holderPtr) {
                                if (!engine_holder_matches_type(engine_, (*holderPtr)->type_name, typeid(base_type))) {
                                    throw runtime_error("Object type mismatch");
                                }
                                auto objectPtr = (*holderPtr)->data;
                                // class_instance wrappers keep the C++ object in _cpp_object; a wrapper
                                // with no C++ payload (pure script object) must error here - it can
                                // never be reinterpreted as T
                                if ((*holderPtr)->is_class_instance_wrapper) {
                                    objectPtr = engine_extract_instance_cpp_object(engine_, objectPtr);
                                }
                                if (objectPtr) {
                                    if (auto typedPtr = std::static_pointer_cast<base_type>(objectPtr)) {
                                        return *typedPtr;
                                    }
                                }
                            }
                            throw runtime_error("Object type mismatch");
                        }
                        throw runtime_error("Unsupported type for non-const reference extraction");
                    }
                }
            }
            // For non-reference types, delegate to const version
            else {
                return const_cast<const script_value*>(this)->as<T>();
            }
        }

        // ============================================================================
        // CHECKED HELPER METHODS - Return checked_result instead of throwing
        // Using pointers to avoid reference_wrapper complexity and try/catch overhead
        // ============================================================================

        inline checked_result<const script_string*> checked_as_string() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_string_type) {
                return checked_result<const script_string*>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a string"
                );
            }
            if (auto* box = val.bound_box()) {
                return checked_result<const script_string*>(static_cast<const script_string*>(box->ptr));
            }
            return checked_result<const script_string*>(std::get<strong_ptr<script_string>>(val.storage_).get());
        }

        inline checked_result<const std::vector<script_value>*> checked_as_array() const {
            if (type() != script_value_type::jai_array_type) {
                return checked_result<const std::vector<script_value>*>(
                    make_error_code(runtime_error_code::not_an_array),
                    "script_value is not an array"
                );
            }
            assert(!std::get<strong_ptr<script_array>>(storage_)->is_typed() &&
                   "typed array reached a hetero-only path (missing kind dispatch)");
            return checked_result<const std::vector<script_value>*>(&std::get<strong_ptr<script_array>>(storage_)->values());
        }

        inline checked_result<const script_map*> checked_as_map() const {
            if (type() != script_value_type::jai_map_type) {
                return checked_result<const script_map*>(
                    make_error_code(runtime_error_code::not_a_map),
                    "script_value is not a map"
                );
            }
            return checked_result<const script_map*>(std::get<strong_ptr<script_map>>(storage_).get());
        }

        inline checked_result<script_int> checked_as_int() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                if (val.is_cpp_bound_primitive()) {
                    return checked_result<script_int>(val.unchecked_as_int());
                }
                return checked_result<script_int>(std::get<script_int>(val.storage_));
            } else if (val.type() == script_value_type::jai_float_type) {
                if (val.is_cpp_bound_primitive()) {
                    return checked_result<script_int>(float_to_script_int(val.unchecked_as_float()));
                }
                return checked_result<script_int>(float_to_script_int(std::get<script_float>(val.storage_)));
            }
            return checked_result<script_int>(
                make_error_code(runtime_error_code::type_mismatch),
                "script_value is not an integer or float"
            );
        }

        inline checked_result<script_float> checked_as_float() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                // unchecked_as_float() decodes the bound size (4-byte float vs
                // 8-byte double). Reading a bound float as a raw script_float
                // (double) reinterpreted 8 bytes from a 4-byte object -> garbage + OOB.
                if (val.is_cpp_bound_primitive()) {
                    return checked_result<script_float>(val.unchecked_as_float());
                }
                return checked_result<script_float>(std::get<script_float>(val.storage_));
            } else if (val.type() == script_value_type::jai_int_type) {
                // Int to float conversion. unchecked_as_int() decodes the bound
                // integer's true size/signedness; the old `const int*` cast read a
                // fixed 4-byte signed value, truncating int64 and misreading other widths.
                if (val.is_cpp_bound_primitive()) {
                    return checked_result<script_float>(static_cast<script_float>(val.unchecked_as_int()));
                }
                return checked_result<script_float>(static_cast<script_float>(std::get<script_int>(val.storage_)));
            }
            return checked_result<script_float>(
                make_error_code(runtime_error_code::type_mismatch),
                "script_value is not a float or integer"
            );
        }

        inline checked_result<script_bool> checked_as_bool() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_bool_type) {
                return checked_result<script_bool>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a boolean"
                );
            }
            if (auto* box = val.bound_box()) {
                return checked_result<script_bool>(*static_cast<const script_bool*>(box->ptr));
            }
            return checked_result<script_bool>(std::get<script_bool>(val.storage_));
        }

        inline checked_result<script_char> checked_as_char() const {
            const script_value& val = deref();
            if (val.type() != script_value_type::jai_char_type) {
                return checked_result<script_char>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a character"
                );
            }
            if (auto* box = val.bound_box()) {
                return checked_result<script_char>(*static_cast<const script_char*>(box->ptr));
            }
            return checked_result<script_char>(std::get<script_char>(val.storage_));
        }

        // ============================================================================
        // COMPREHENSIVE checked_as<T>() - Returns checked_result instead of throwing
        // ============================================================================
        template<typename T>
        checked_result<T> checked_as() const {
            // Try to unwrap transparent wrappers for basic types (int, float, bool, string, char)
            // This allows observable_property_ref<int> to be used as int in C++ code
            if constexpr (std::is_same_v<T, script_int> || std::is_same_v<T, int> || std::is_same_v<T, int64_t> ||
                          std::is_same_v<T, script_float> || std::is_same_v<T, double> || std::is_same_v<T, float> ||
                          std::is_same_v<T, script_bool> || std::is_same_v<T, script_char> ||
                          std::is_same_v<T, script_string> || std::is_same_v<T, std::string>) {
                const script_value& val = deref();
                if (val.is_object()) {
                    script_value unwrapped = val.try_unwrap_transparent_wrapper();
                    // If unwrapped to a different value (not an object anymore), use that
                    if (!unwrapped.is_object()) {
                        return unwrapped.checked_as<T>();
                    }
                }
            }

            // FAST PATH: Direct type specializations for hot types
            if constexpr (std::is_same_v<T, script_int>) {
                const script_value& val = deref();
                if (val.is_cpp_bound_primitive()) {
                    return checked_result<T>(val.unchecked_as_int());
                }
                if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(std::get<script_int>(val.storage_));
                } else if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(float_to_script_int(std::get<script_float>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not an integer or float. Actual type: {0}",
                    val.type_id()
                );
            }
            else if constexpr (std::is_same_v<T, script_float> || std::is_same_v<T, double>) {
                const script_value& val = deref();
                // Decode bound values via the size/sign-aware accessors. The old
                // blanket `*static_cast<const script_float*>(cpp_bound_ptr_)` read 8
                // bytes for ANY bound value, reinterpreting bound ints and 4-byte
                // floats as doubles (wrong value, and an out-of-bounds read for float).
                if (val.type() == script_value_type::jai_float_type) {
                    return checked_result<T>(val.is_cpp_bound_primitive() ? val.unchecked_as_float()
                                                                          : std::get<script_float>(val.storage_));
                } else if (val.type() == script_value_type::jai_int_type) {
                    return checked_result<T>(static_cast<script_float>(
                        val.is_cpp_bound_primitive() ? val.unchecked_as_int()
                                                     : std::get<script_int>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a float or integer. Actual type: {0}",
                    val.type_id()
                );
            }
            else if constexpr (std::is_same_v<T, script_bool>) {
                const script_value& val = deref();
                if (auto* box = val.bound_box()) {
                    return checked_result<T>(*static_cast<const script_bool*>(box->ptr));
                }
                if (val.type() == script_value_type::jai_bool_type) {
                    return checked_result<T>(std::get<script_bool>(val.storage_));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a boolean. Actual type: {0}",
                    val.type_id()
                );
            }
            else if constexpr (std::is_same_v<T, script_char>) {
                const script_value& val = deref();
                if (auto* box = val.bound_box()) {
                    return checked_result<T>(*static_cast<const script_char*>(box->ptr));
                }
                if (val.type() == script_value_type::jai_char_type) {
                    return checked_result<T>(std::get<script_char>(val.storage_));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "script_value is not a character. Actual type: {0}",
                    val.type_id()
                );
            }
            // MEDIUM PATH: Common integral conversions
            else if constexpr (std::is_same_v<T, int>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int>::min() || val > std::numeric_limits<int>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int"
                    );
                }
                return checked_result<T>(static_cast<int>(val));
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                return checked_result<T>(static_cast<int64_t>(val_result.value()));
            }
            else if constexpr (std::is_same_v<T, float>) {
                const script_value& val = deref();
                if (val.type() == script_value_type::jai_float_type) {
                    if (val.is_cpp_bound_primitive()) {
                        return checked_result<T>(static_cast<float>(val.unchecked_as_float()));
                    }
                    return checked_result<T>(static_cast<float>(std::get<script_float>(val.storage_)));
                } else if (val.type() == script_value_type::jai_int_type) {
                    if (val.is_cpp_bound_primitive()) {
                        return checked_result<T>(static_cast<float>(val.unchecked_as_int()));
                    }
                    return checked_result<T>(static_cast<float>(std::get<script_int>(val.storage_)));
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Cannot convert script_value to float"
                );
            }
            // String types
            else if constexpr (std::is_same_v<T, script_string> || std::is_same_v<T, std::string>) {
                auto str_result = checked_as_string();
                if (!str_result) {
                    return str_result.error_value();  // Preserves symbol IDs for formatting
                }
                return checked_result<T>(*str_result.value());
            }
            // Handle reference types (now supported via checked_result<T&> specialization)
            else if constexpr (std::is_reference_v<T>) {
                using base_type = std::remove_cv_t<std::remove_reference_t<T>>;

                if constexpr (std::is_const_v<std::remove_reference_t<T>>) {
                    if constexpr (std::is_same_v<base_type, script_int>) {
                        if (type() != script_value_type::jai_int_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not an integer"
                            );
                        }
                        return checked_result<T>(std::get<script_int>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_float>) {
                        if (type() != script_value_type::jai_float_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a float"
                            );
                        }
                        return checked_result<T>(std::get<script_float>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_bool>) {
                        if (type() != script_value_type::jai_bool_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a boolean"
                            );
                        }
                        return checked_result<T>(std::get<script_bool>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_char>) {
                        if (type() != script_value_type::jai_char_type) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "script_value is not a character"
                            );
                        }
                        return checked_result<T>(std::get<script_char>(storage_));
                    } else if constexpr (std::is_same_v<base_type, script_string> || std::is_same_v<base_type, std::string>) {
                        auto str_result = checked_as_string();
                        if (!str_result) {
                            return str_result.error_value();  // Preserves symbol IDs for formatting
                        }
                        return checked_result<T>(*str_result.value());
                    } else if constexpr (std::is_same_v<base_type, std::vector<script_value>>) {
                        auto arr_result = checked_as_array();
                        if (!arr_result) {
                            return arr_result.error_value();  // Preserves symbol IDs for formatting
                        }
                        return checked_result<T>(*arr_result.value());
                    } else if constexpr (std::is_same_v<base_type, script_map>) {
                        auto map_result = checked_as_map();
                        if (!map_result) {
                            return map_result.error_value();
                        }
                        return checked_result<T>(*map_result.value());
                    } else {
                        auto t = type();
                        if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                            auto ptr_result = checked_as<std::shared_ptr<base_type>>();
                            if (!ptr_result) {
                                return ptr_result.error_value();  // Preserves symbol IDs for formatting
                            }
                            return checked_result<T>(*ptr_result.value());
                        }
                        return checked_result<T>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Unsupported type for const reference extraction"
                        );
                    }
                } else {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot extract non-const reference from const script_value"
                    );
                }
            }
            // Signed integer types with bounds checking
            else if constexpr (std::is_same_v<T, int8_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int8_t>::min() || val > std::numeric_limits<int8_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int8_t"
                    );
                }
                return checked_result<T>(static_cast<int8_t>(val));
            }
            else if constexpr (std::is_same_v<T, int16_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int16_t>::min() || val > std::numeric_limits<int16_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int16_t"
                    );
                }
                return checked_result<T>(static_cast<int16_t>(val));
            }
            else if constexpr (std::is_same_v<T, int32_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < std::numeric_limits<int32_t>::min() || val > std::numeric_limits<int32_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for int32_t"
                    );
                }
                return checked_result<T>(static_cast<int32_t>(val));
            }
            // Unsigned integer types with bounds checking
            else if constexpr (std::is_same_v<T, uint8_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint8_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint8_t (must be 0-255)"
                    );
                }
                return checked_result<T>(static_cast<uint8_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint16_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint16_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint16_t (must be non-negative)"
                    );
                }
                return checked_result<T>(static_cast<uint16_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint32_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < 0 || val > std::numeric_limits<uint32_t>::max()) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value out of range for uint32_t (must be non-negative)"
                    );
                }
                return checked_result<T>(static_cast<uint32_t>(val));
            }
            else if constexpr (std::is_same_v<T, uint64_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < 0) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value must be non-negative for uint64_t"
                    );
                }
                return checked_result<T>(static_cast<uint64_t>(val));
            }
            else if constexpr (std::is_same_v<T, size_t>) {
                auto val_result = checked_as<script_int>();
                if (!val_result) {
                    return val_result.error_value();  // Preserves symbol IDs for formatting
                }
                script_int val = val_result.value();
                if (val < 0) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Integer value must be non-negative for size_t"
                    );
                }
                return checked_result<T>(static_cast<size_t>(val));
            }
            // Vector types
            else if constexpr (is_specialization_v<T, std::vector>) {
                // Use engine's conversion registry first
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Built-in vector handling
                if (type() != script_value_type::jai_array_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_array),
                        "Cannot convert non-array to vector"
                    );
                }

                using element_type = typename T::value_type;
                T result;
                auto arr_result = checked_as_array();
                if (!arr_result) {
                    return arr_result.error_value();  // Preserves symbol IDs for formatting
                }
                const auto& arr = *arr_result.value();
                result.reserve(arr.size());

                for (const auto& elem : arr) {
                    if constexpr (std::is_same_v<element_type, script_value>) {
                        result.push_back(elem);
                    } else {
                        auto elem_result = elem.checked_as<element_type>();
                        if (!elem_result) {
                            return elem_result.error_value();
                        }
                        result.push_back(elem_result.value());
                    }
                }
                return checked_result<T>(std::move(result));
            }
            // Map types
            else if constexpr (is_specialization_v<T, std::map>) {
                // Use engine's conversion registry first
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Built-in map handling
                if (type() != script_value_type::jai_map_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_a_map),
                        "Cannot convert non-map to std::map"
                    );
                }

                using key_type = typename T::key_type;
                using mapped_type = typename T::mapped_type;
                T result;
                auto map_result = checked_as_map();
                if (!map_result) {
                    return map_result.error_value();
                }
                const auto& m = *map_result.value();

                for (const auto& [k, v] : m) {
                    if constexpr (std::is_same_v<key_type, script_value> && std::is_same_v<mapped_type, script_value>) {
                        result.emplace(k, v);
                    } else if constexpr (std::is_same_v<key_type, script_value>) {
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return val_result.error_value();
                        }
                        result.emplace(k, val_result.value());
                    } else if constexpr (std::is_same_v<mapped_type, script_value>) {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return key_result.error_value();
                        }
                        result.emplace(key_result.value(), v);
                    } else {
                        auto key_result = k.checked_as<key_type>();
                        if (!key_result) {
                            return key_result.error_value();
                        }
                        auto val_result = v.checked_as<mapped_type>();
                        if (!val_result) {
                            return val_result.error_value();
                        }
                        result.emplace(key_result.value(), val_result.value());
                    }
                }
                return checked_result<T>(std::move(result));
            }
            // Support for shared_ptr<class_instance> extraction from objects
            else if constexpr (std::is_same_v<T, std::shared_ptr<jai::class_instance>>) {
                auto t = type();
                if (t != script_value_type::jai_object_type && t != script_value_type::jai_shared_ptr_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_object),
                        "script_value is not an object. Actual type: {0}",
                        type_id()
                    );
                }
                auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
                if (!objHolder->is_class_instance_wrapper) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_a_class),
                        "Object is not a class_instance. Actual type: {0}",
                        type_id()
                    );
                }
                return checked_result<T>(std::static_pointer_cast<class_instance>(objHolder->data));
            }
            // Support for shared_ptr<void> extraction
            else if constexpr (std::is_same_v<T, std::shared_ptr<void>>) {
                if (type() != script_value_type::jai_object_type) {
                    return checked_result<T>(
                        make_error_code(runtime_error_code::not_an_object),
                        "script_value is not an object"
                    );
                }
                auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
                return checked_result<T>(objHolder->data);
            }
            // Support for other shared_ptr types
            else if constexpr (is_specialization_v<T, std::shared_ptr>) {
                // First check if there's a registered conversion
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // Fall back to default shared_ptr extraction
                auto t = type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                    auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

                    // Check for non-owning C++ reference (holder bound_ptr set, data null)
                    // Cannot safely return shared_ptr from non-owning reference
                    if (objHolder->bound_ptr && !objHolder->data) {
                        return checked_result<T>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Cannot extract shared_ptr from non-owning C++ reference. "
                            "The object is bound by reference, not owned. Use T& or T* extraction."
                        );
                    }

                    // Check if we need to use the custom extractor
                    if (objHolder->is_class_instance_wrapper) {
                        if (engine_) {
                            auto registry = get_engine_conversion_registry(engine_);
                            if (registry) {
                                auto extracted = registry->extract_custom_object(objHolder->type_name, objHolder->data);
                                if (extracted) {
                                    return checked_result<T>(std::static_pointer_cast<typename T::element_type>(extracted));
                                }
                            }
                        }
                    }

                    // Otherwise use static cast
                    return checked_result<T>(std::static_pointer_cast<typename T::element_type>(objHolder->data));
                }

                return checked_result<T>(
                    make_error_code(runtime_error_code::not_an_object),
                    "script_value is not an object"
                );
            }
            // Support for extracting custom objects by value (dereference shared_ptr)
            else if constexpr (std::is_class_v<T> &&
                             !std::is_same_v<T, std::string> &&
                             !is_specialization_v<T, std::vector> &&
                             !is_specialization_v<T, std::map>) {
                // Check custom converter first
                if (engine_) {
                    auto registry = get_engine_conversion_registry(engine_);
                    if (registry && registry->template has_conversion<T>()) {
                        try {
                            return checked_result<T>(registry->template convert_from_script<T>(*this));
                        } catch (const runtime_error& e) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                e.what()
                            );
                        } catch (...) {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Custom conversion failed"
                            );
                        }
                    }
                }

                // For custom classes, try to extract shared_ptr and dereference
                auto t = type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                    auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

                    // Handle non-owning C++ reference via the holder's bound_ptr
                    if (objHolder->bound_ptr && !objHolder->data) {
                        // Copy from non-owning pointer (only if copyable)
                        if constexpr (std::is_copy_constructible_v<T>) {
                            return checked_result<T>(*static_cast<T*>(objHolder->bound_ptr));
                        } else {
                            return checked_result<T>(
                                make_error_code(runtime_error_code::type_mismatch),
                                "Cannot copy non-copyable type from non-owning C++ reference"
                            );
                        }
                    }

                    // Owning path: extract shared_ptr and dereference
                    auto ptr_result = checked_as<std::shared_ptr<T>>();
                    if (!ptr_result) {
                        return ptr_result.error_value();  // Preserves symbol IDs for formatting
                    }
                    return checked_result<T>(*ptr_result.value());
                }
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Cannot extract custom type by value from non-object"
                );
            }
            else {
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Unsupported type conversion"
                );
            }
        }

        // Conversion to string for debugging
        std::string to_string() const;

        // Try to unwrap a transparent wrapper type (like observable_property_ref)
        // Returns the unwrapped value if this is a transparent wrapper, otherwise returns copy of this
        // Used by checked_as<T>() to automatically unwrap wrapper types
        script_value try_unwrap_transparent_wrapper() const;

        // Dereference method - returns this for non-references, dereferences for references
        const script_value& deref() const;
        script_value& deref();
        
        // Assignment through reference - assigns to target if this is a reference
        void assign_through(const script_value& value);
        void assign_through(script_value&& value);
        
        // Operators
        bool operator==(const script_value& other) const;
        bool operator!=(const script_value& other) const { return !(*this == other); }
        
        // C++20 spaceship operator for complete ordering
        std::strong_ordering operator<=>(const script_value& other) const;
        
        // Custom conversion checker function type
        using conversion_func = std::function<script_value(const script_value&, const std::type_info&)>;
        
        // Get engine pointer for creating new script_values
        engine* get_engine() const { return engine_; }

        // Implicit conversion operators for common C++ types
        // These enable natural overload resolution in C++ function calls
        operator int() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                auto int_val = std::get<script_int>(val.storage_);
                if (int_val < std::numeric_limits<int>::min() || 
                    int_val > std::numeric_limits<int>::max()) {
                    throw runtime_error("script_int value out of range for int");
                }
                return static_cast<int>(int_val);
            }
            throw runtime_error("script_value is not an integer");
        }
        
        operator int64_t() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_int_type) {
                return std::get<script_int>(val.storage_);
            }
            throw runtime_error("script_value is not an integer");
        }
        
        operator double() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                return std::get<script_float>(val.storage_);
            } else if (val.type() == script_value_type::jai_int_type) {
                return static_cast<double>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        operator float() const {
            const script_value& val = deref();
            if (val.type() == script_value_type::jai_float_type) {
                return static_cast<float>(std::get<script_float>(val.storage_));
            } else if (val.type() == script_value_type::jai_int_type) {
                return static_cast<float>(std::get<script_int>(val.storage_));
            }
            throw runtime_error("script_value is not a numeric type");
        }
        
        
    private:
        type_info_ptr type_info_;  // Complete type information
        engine* engine_ = nullptr;  // Raw pointer to engine (no atomic ops on copy)
        
        // For object storage - external serialization will handle this
        struct object_holder {
            std::string type_name;           // Type identification for serialization
            uint64_t type_id = UINT64_MAX;  // Interned type name ID for fast comparison (UINT64_MAX = not set)
            std::shared_ptr<void> data;     // The actual object
            bool is_class_instance_wrapper = false;  // True if data is a class_instance object (both C++ and script classes), false for raw data
            // A coroutine handle IS a reference to a running computation: clone() shares
            // it (reference semantics, like shared_ptr) instead of deep-copying.
            bool is_coroutine_handle = false;

            // Registered-class T& binding: the live C++ object (data stays null). Non-owning!
            void* bound_ptr = nullptr;

            // Lifetime anchor for NON-OWNING references (data == nullptr, bound_ptr set).
            // When a C++ method returns a reference into its receiver (e.g. `return *this`
            // for chaining), the resulting non-owning value must keep the receiver's
            // underlying object alive even if the receiver was a temporary. This pins
            // that owner WITHOUT participating in extraction (bound_ptr remains the
            // authoritative target). Empty for owning objects.
            std::shared_ptr<void> keep_alive;

            // Note: Serialization functions will be managed externally
            // by ISerializer implementations to keep JaiScript dependency-free
        };

        // C++ primitive/string/opaque binding box (variant alternative 14).
        // IMMUTABLE after construction - copies of bound values stay aliases.
        struct cpp_bound_holder {
            void*   ptr = nullptr;        // address of the live C++ variable
            uint8_t size_and_sign = 0;    // low 7 bits: sizeof(T); bit 7: unsigned
            uint8_t semantic_index = 0;   // TYPEID_NULL/INT/FLOAT/STRING/CHAR/BOOL - the old shadow index
            cpp_bound_holder(void* p, uint8_t ss, uint8_t sem) noexcept
                : ptr(p), size_and_sign(ss), semantic_index(sem) {}
        };

        // Boxes the (never runtime-constructed) shared_ptr alternative 11 so the variant's
        // max payload stays one pointer. The atomic inner std::shared_ptr is preserved for
        // the original thread-safety intent; real cross-thread sharing is object_holder::data.
        struct shared_value_holder {
            std::shared_ptr<script_value> inner;
            explicit shared_value_holder(std::shared_ptr<script_value> v) noexcept : inner(std::move(v)) {}
        };

        // Reference wrapper for reference types. Four owner-pinned modes, mutually
        // exclusive - a reference NEVER holds a raw address + lifetime guess (the old
        // mode 1, raw script_value* + weak env anchor, is deleted; escape is legal):
        //
        // CELL (Lua-upvalue box): the holder OWNS the referenced value in inline
        // storage - one make_strong allocation is the whole box. Escape-marked
        // variables store a cell reference from declaration; unmarked storage boxes on
        // demand at the first ref bind. Binding/aliasing is a handle copy and an
        // escaped reference keeps its target alive.
        //
        // MAP-ENTRY: container_map pins the owning map (strong) and the inline storage
        // holds the KEY; deref/assign-through re-resolve via find(key) each time, so an
        // erased entry errors cleanly and a reference into a temporary map keeps the
        // map alive.
        struct reference_holder {
            type_info_ptr container_element_type = nullptr;  // For subscript/field refs: the element/field type constraint

            static constexpr size_t cell_storage_size = 32;   // == sizeof(script_value), gated in value.cpp
            alignas(8) unsigned char cell_storage[cell_storage_size];
            bool has_cell = false;      // inline storage holds the OWNED value
            bool has_map_key = false;   // inline storage holds the map KEY (container_map set)
            strong_ptr<script_map> container_map;
            script_value* cell() noexcept { return reinterpret_cast<script_value*>(cell_storage); }
            const script_value* cell() const noexcept { return reinterpret_cast<const script_value*>(cell_storage); }

            reference_holder() = default;
            reference_holder(const reference_holder&) = delete;
            reference_holder& operator=(const reference_holder&) = delete;
            ~reference_holder();   // destroys the inline value/key (value.cpp: script_value complete there)

            // ELEMENT mode: for references into a std::vector element (range-for
            // `auto&`, `arr[i]` lvalue): hold the OWNING container + index instead of a
            // raw element pointer. A raw pointer into a vector dangles when the vector
            // reallocates (e.g. a push inside the loop body) -> heap corruption on
            // write-through. When `container` is set, deref()/assign_through()
            // recompute the element address from container+index each time (surviving
            // reallocation) and bounds-check the index (a shrink throws instead of
            // reading freed memory). The strong_ptr also keeps the vector alive.
            strong_ptr<script_array> container;
            size_t container_index = SIZE_MAX;

            // TYPED element mode (raw-buffer nodes): there is no script_value element to
            // reference, so deref() materializes container[index] into cell_storage
            // (free in element mode) refreshed on EVERY touch — same re-resolve contract
            // as the other modes; callers already may not cache the address. Writers
            // never go through deref on typed elements: assign_through()/the typed
            // chokepoints store into the raw buffer (a scratch write-back would be
            // silent data loss — audited; see typed_array_design.md).
            bool has_elem_scratch = false;
            bool typed_element() const noexcept { return container && container->is_typed(); }
            const script_value& materialize_typed_element(engine* eng);   // value.cpp
            // In-place mutation sites (++/--) mutate the materialized scratch through
            // deref() and commit it back to the raw buffer with this (bounds re-checked:
            // no user code runs between materialize and commit, but stay defensive).
            void commit_typed_element_scratch() {
                if (has_elem_scratch && container_index < container->size()) {
                    container->set(container_index, *cell());
                }
            }

            // FIELD mode (ref bind of obj.field): pins the owning instance and
            // re-resolves the field node by id on every deref/assign-through, so it
            // survives hot-reload field migration (a removed field errors instead of
            // dangling).
            std::shared_ptr<class_instance> owner_instance;
            uint64_t field_id = UINT64_MAX;

            // Mode-based write-target resolution for the immediate-use assignment
            // paths: re-resolves every time (realloc/erase safe), single level - never
            // follows a chained reference (matching the old raw-target twins). Returns
            // null when the element/entry/field is gone. Defined in value.cpp
            // (class_instance complete there).
            script_value* resolve_target();
        };

        // Per-engine block pool for reference_holder mints (defined in value.cpp; state
        // lives behind engine::reference_holder_pool_slot). Elides the make_strong heap
        // allocation on the element/field/map-entry/cell mint paths. Single-threaded:
        // while a parallel region's workers run (they mint cell refs in their bodies),
        // acquire falls back to plain make_strong blocks that never touch the pool.
        struct reference_holder_pool;
        static strong_ptr<reference_holder> acquire_reference_holder(engine* eng);
        
        
        // Tag type for invalid values
        struct invalid_tag {};

        // Parallel captured-read borrow payload (variant alternative 15): ONE tagged raw
        // pointer to an engine-owned container that a parallel region proved read-only.
        // Trivially copyable ON PURPOSE - copying a borrow must never touch a refcount
        // (see make_parallel_borrow). Low bit = kind (0 array, 1 map); container
        // allocations are 8+ aligned so the bit is free.
        struct parallel_borrow_tag {
            uintptr_t bits = 0;
            static constexpr uintptr_t k_map_bit = 1;
            const void* pointer() const noexcept { return reinterpret_cast<const void*>(bits & ~k_map_bit); }
            bool is_map_kind() const noexcept { return (bits & k_map_bit) != 0; }
        };

        // Variant indices for ultra-fast unchecked access
        // IMPORTANT: Keep this enum in sync with the storage variant below!
        enum class storage_index : size_t {
            jai_null = 0,
            jai_int = 1,
            jai_float = 2,
            jai_string = 3,
            jai_char = 4,
            jai_bool = 5,
            jai_array = 6,
            jai_map = 7,
            jai_object = 8,
            jai_function = 9,
            jai_reference = 10,
            jai_shared_ptr = 11,
            jai_weak_ptr = 12,
            jai_invalid = 13,
            jai_cpp_bound = 14,
            jai_parallel_borrow = 15
        };

    public:
        // Constexpr type index constants for fast type checking in hot paths
        // Use with raw_storage_index() to avoid switch statements
        static constexpr size_t TYPEID_NULL = 0;
        static constexpr size_t TYPEID_INT = 1;
        static constexpr size_t TYPEID_FLOAT = 2;
        static constexpr size_t TYPEID_STRING = 3;
        static constexpr size_t TYPEID_CHAR = 4;
        static constexpr size_t TYPEID_BOOL = 5;
        static constexpr size_t TYPEID_ARRAY = 6;
        static constexpr size_t TYPEID_MAP = 7;
        static constexpr size_t TYPEID_OBJECT = 8;
        static constexpr size_t TYPEID_FUNCTION = 9;
        static constexpr size_t TYPEID_REFERENCE = 10;
        static constexpr size_t TYPEID_SHARED_PTR = 11;
        static constexpr size_t TYPEID_WEAK_PTR = 12;
        static constexpr size_t TYPEID_INVALID = 13;
        static constexpr size_t TYPEID_CPP_BOUND = 14;
        static constexpr size_t TYPEID_PARALLEL_BORROW = 15;
    private:

        // Type-erased storage using variant for efficiency
        // NOTE: string and function are wrapped in strong_ptr for cheap non-atomic copies
        // This shrinks the variant (all complex types are now 16-byte pointers)
        // and makes copies O(1) instead of O(n) for strings
        // strong_ptr uses non-atomic ref counting (fast!) until lock() is called
        using storage = std::variant<
            std::monostate,                               // 0 - Null
            script_int,                                   // 1 - script_int
            script_float,                                 // 2 - script_float
            strong_ptr<script_string>,                    // 3 - script_string (wrapped for cheap copies)
            script_char,                                  // 4 - script_char
            script_bool,                                  // 5 - script_bool
            strong_ptr<script_array>,                     // 6 - Array<T> (script_array node = identity)
            strong_ptr<script_map>, // 7 - Map<K,V>
            strong_ptr<object_holder>,                    // 8 - Object<T>
            strong_ptr<script_function>,                  // 9 - Function (wrapped for cheap copies)
            strong_ptr<reference_holder>,                 // 10 - T&
            strong_ptr<shared_value_holder>,              // 11 - shared_ptr<T> (boxed; never runtime-constructed - real shared_ptr values are alt 8 + type_info marker)
            jai::weaker_ptr<object_holder>,                 // 12 - weak_ptr<T>
            invalid_tag,                                  // 13 - Invalid value marker
            strong_ptr<cpp_bound_holder>,                 // 14 - C++ primitive/string/opaque binding box
            parallel_borrow_tag                           // 15 - parallel captured-read borrow (region-internal)
        >;
        static_assert(std::is_nothrow_move_constructible_v<storage> && std::is_nothrow_move_assignable_v<storage>,
                      "every alternative must stay nothrow-move (valueless_by_exception + noexcept move depend on it)");

        storage storage_;

    public:
        // Method to set engine pointer after construction
        void set_engine(engine* eng) { engine_ = eng; }

        // Clear engine pointer (called when engine is destroyed to prevent dangling pointers)
        void clear_engine() { engine_ = nullptr; }

        // Access raw storage for AST literals (bypasses type checking)
        const storage& get_storage() const { return storage_; }
        
        // Extract object_holder for class_instance operations
        // Returns nullptr if not actually storing an object (uses current_type, not defined_type)
        strong_ptr<object_holder> get_object_holder() {
            auto t = current_type();  // Use actual current type, not declared type
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<strong_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }

        const strong_ptr<object_holder> get_object_holder() const {
            auto t = current_type();  // Use actual current type, not declared type
            if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
                // After refactor: both object and shared_ptr store object_holder directly
                // shared_ptr<T> is just a type marker affecting clone behavior
                return std::get<strong_ptr<object_holder>>(storage_);
            }
            return nullptr;
        }

        // Set object holder directly (used when restoring from weak_ptr.lock())
        void set_object_holder(strong_ptr<object_holder> holder) {
            storage_ = holder;
        }

        // Get the array NODE handle for interpreter operations
        strong_ptr<script_array>& get_array_storage() {
            if (type() != script_value_type::jai_array_type) {
                throw runtime_error("script_value is not an array");
            }
            return std::get<strong_ptr<script_array>>(storage_);
        }

        // Get raw map storage for interpreter operations
        strong_ptr<script_map>& get_map_storage() {
            if (type() != script_value_type::jai_map_type) {
                throw runtime_error("script_value is not a map");
            }
            return std::get<strong_ptr<script_map>>(storage_);
        }

        // Get raw weak_ptr storage for interpreter operations
        jai::weaker_ptr<object_holder>& get_weak_ptr_storage() {
            if (type() != script_value_type::jai_weak_ptr_type) {
                throw runtime_error("script_value is not a weak_ptr");
            }
            return std::get<jai::weaker_ptr<object_holder>>(storage_);
        }
        
        // ===== Safe Public APIs for Interpreter Operations =====
        
        // Factory method for null values with engine pointer
        static script_value make_null(engine* eng) {
            return script_value(std::monostate{}, eng);
        }

        // Factory method for invalid values (used as sentinel for non-existent fields/methods)
        static script_value make_invalid(engine* eng);
        
        // Check if this value has a valid engine pointer
        bool has_valid_engine() const {
            return engine_ != nullptr;
        }

        // Safe access to reference holder for reference types
        // Use storage_type() because references may have type_info with different base_type
        reference_holder* get_reference_holder() {
            if (storage_type() == script_value_type::jai_reference_type) {
                return std::get<strong_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }

        const reference_holder* get_reference_holder() const {
            if (storage_type() == script_value_type::jai_reference_type) {
                return std::get<strong_ptr<reference_holder>>(storage_).get();
            }
            return nullptr;
        }

        // Non-null iff *this is a reference to a TYPED array element: deref() hands
        // back the holder's materialized scratch there, so in-place mutators must
        // commit_typed_element_scratch() after writing (typed_array_design.md).
        reference_holder* typed_element_holder() noexcept {
            if (raw_storage_index() != TYPEID_REFERENCE) { return nullptr; }
            auto& holder = *std::get_if<TYPEID_REFERENCE>(&storage_);
            return holder && holder->typed_element() ? holder.get() : nullptr;
        }
        
        
        // Check if array/map has unique ownership (for COW optimization)
        bool is_unique_reference() const {
            if (type() == script_value_type::jai_array_type) {
                const auto& ptr = std::get<strong_ptr<script_array>>(storage_);
                return ptr.use_count() == 1;
            } else if (type() == script_value_type::jai_map_type) {
                const auto& ptr = std::get<strong_ptr<script_map>>(storage_);
                return ptr.use_count() == 1;
            }
            return false;
        }

        // Safe weak_ptr access
        jai::weaker_ptr<object_holder> get_weak_ptr() const {
            if (type() == script_value_type::jai_weak_ptr_type) {
                return std::get<jai::weaker_ptr<object_holder>>(storage_);
            }
            return jai::weaker_ptr<object_holder>();
        }
        
        // Unchecked mutable object storage accessor - caller must verify raw object storage first
        // (count inspection without the handle copy get_object_holder() takes)
        inline strong_ptr<object_holder>& unchecked_get_object_storage() noexcept {
            return *std::get_if<TYPEID_OBJECT>(&storage_);
        }

        // Safe class_instance extraction from object_holder
        std::shared_ptr<class_instance> get_class_instance() {
            auto holder = get_object_holder();
            if (holder && holder->is_class_instance_wrapper) {
                return std::static_pointer_cast<class_instance>(holder->data);
            }
            return nullptr;
        }
        
        
        // Direct weak_ptr assignment for interpreter use
        void set_weak_ptr(const jai::weaker_ptr<object_holder>& weak) {
            if (type() == script_value_type::jai_weak_ptr_type) {
                storage_ = weak;
            } else {
                throw runtime_error("Cannot set weak_ptr on non-weak_ptr script_value");
            }
        }

        // Direct shared_ptr assignment for interpreter use
        void set_shared_ptr(const std::shared_ptr<script_value>& shared) {
            if (type() == script_value_type::jai_shared_ptr_type) {
                storage_ = make_strong<shared_value_holder>(shared);
            } else {
                throw runtime_error("Cannot set shared_ptr on non-shared_ptr script_value");
            }
        }

        // Get the wrapped value from a shared_ptr
        script_value get_shared_ptr_value() const {
            if (type() == script_value_type::jai_shared_ptr_type) {
                auto holder = std::get<strong_ptr<shared_value_holder>>(storage_);
                if (holder && holder->inner) {
                    return *holder->inner;
                }
            }
            throw runtime_error("Cannot get wrapped value from non-shared_ptr script_value");
        }
        
        // Create an empty weak_ptr value
        static script_value make_empty_weak_ptr(type_info_ptr weak_ptr_type, engine* eng);

        // Create a weak_ptr from an object
        // Create a weak_ptr from an object (implemented in value.cpp)
        // Returns checked_result to avoid throwing exceptions
        static checked_result<script_value> make_weak_ptr(const script_value& obj, engine* eng);
        
        // Set type info for special cases (like weak_ptr creation)
        void set_type_info(type_info_ptr type) {
            type_info_ = type;
            // Demote-on-stamp (typed_array_design.md): re-tagging an array value 'any'
            // (var binding) demotes a typed node in place — node kind must never outrun
            // the views' tags. The stamp sites operate on fresh clones/moved temps, so
            // the node is unique here; a shared demote would still be value-correct
            // (identity + contents preserved), merely losing the raw-buffer form.
            if (type && type->base_type == script_value_type::jai_any_type &&
                raw_storage_index() == TYPEID_ARRAY) {
                auto& node = *std::get_if<TYPEID_ARRAY>(&storage_);
                if (node && node->is_typed()) {
                    node->demote_to_hetero(engine_);
                }
            }
        }
        
    private:
        // Friends only for essential access patterns
        friend class interpreter;  // For coroutine handle creation (object_holder access)
        template<typename T> friend class dynamic_binder;  // For make_cpp_object
        friend class serialization::binary_archive_writer;  // For storage_ access
        friend class serialization::binary_archive_reader;  // For storage_ access
        friend class serialization::json_archive_writer;    // For storage_ access
        friend class serialization::json_archive_reader;    // For storage_ access
        
        // STL container friends for direct assignment support
        template<typename K, typename V, typename C, typename A> friend class std::map;
        template<typename T1, typename T2> friend struct std::pair;
        template<typename... Types> friend class std::tuple;
        template<typename K, typename C, typename A> friend class std::set;
    };

    // Permanent thin-value gates: 8B type_info_ + 8B engine_ + 16B variant (8B max payload + index).
    static_assert(sizeof(script_value) == 32 && alignof(script_value) == 8,
                  "script_value must stay 32 bytes / 8-aligned (thin-value fold)");
    static_assert(std::is_nothrow_move_constructible_v<script_value>);

    // ---- script_array kind-dispatched element access (script_value complete here) ----

    inline script_value script_array::get(size_t i, engine* eng) const {
        switch (kind_) {
            case kind_t::i64: return script_value(ints_[i], eng);
            case kind_t::f64: return script_value(floats_[i], eng);
            default: return values_[i];
        }
    }

    inline void script_array::set(size_t i, script_value v) {
        switch (kind_) {
            case kind_t::i64:
                ints_[i] = v.raw_storage_index() == script_value::TYPEID_FLOAT
                    ? static_cast<script_int>(v.unchecked_as_float()) : v.unchecked_as_int();
                break;
            case kind_t::f64:
                floats_[i] = v.raw_storage_index() == script_value::TYPEID_INT
                    ? static_cast<script_float>(v.unchecked_as_int()) : v.unchecked_as_float();
                break;
            default:
                values_[i] = std::move(v);
                break;
        }
    }

    inline void script_array::push(script_value pre_converted) {
        switch (kind_) {
            case kind_t::i64:
                ints_.push_back(pre_converted.raw_storage_index() == script_value::TYPEID_FLOAT
                    ? static_cast<script_int>(pre_converted.unchecked_as_float()) : pre_converted.unchecked_as_int());
                break;
            case kind_t::f64:
                floats_.push_back(pre_converted.raw_storage_index() == script_value::TYPEID_INT
                    ? static_cast<script_float>(pre_converted.unchecked_as_int()) : pre_converted.unchecked_as_float());
                break;
            default:
                values_.push_back(std::move(pre_converted));
                break;
        }
    }

    inline void script_array::pop_back() {
        switch (kind_) {
            case kind_t::i64: ints_.pop_back(); break;
            case kind_t::f64: floats_.pop_back(); break;
            default: values_.pop_back(); break;
        }
    }

    inline void script_array::erase_at(size_t i) {
        switch (kind_) {
            case kind_t::i64: ints_.erase(ints_.begin() + i); break;
            case kind_t::f64: floats_.erase(floats_.begin() + i); break;
            default: values_.erase(values_.begin() + i); break;
        }
    }

    inline void script_array::reverse() {
        switch (kind_) {
            case kind_t::i64: std::reverse(ints_.begin(), ints_.end()); break;
            case kind_t::f64: std::reverse(floats_.begin(), floats_.end()); break;
            default: std::reverse(values_.begin(), values_.end()); break;
        }
    }

    inline std::vector<script_value> script_array::materialize_values(engine* eng) const {
        std::vector<script_value> out;
        const size_t n = size();
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) { out.push_back(get(i, eng)); }
        return out;
    }

    inline void script_array::demote_to_hetero(engine* eng) {
        switch (kind_) {
            case kind_t::i64:
                values_.reserve(ints_.size());
                for (script_int n : ints_) { values_.emplace_back(n, eng); }
                ints_.clear();
                ints_.shrink_to_fit();
                break;
            case kind_t::f64:
                values_.reserve(floats_.size());
                for (script_float f : floats_) { values_.emplace_back(f, eng); }
                floats_.clear();
                floats_.shrink_to_fit();
                break;
            default:
                break;
        }
        kind_ = kind_t::hetero;
    }

} // namespace jai

#endif // __JAISCRIPT_CORE_VALUE_HPP__

