#pragma once

#ifndef __JAISCRIPT_CORE_BOUND_ARRAY_HPP__
#define __JAISCRIPT_CORE_BOUND_ARRAY_HPP__

#include "value.hpp"
#include "types.hpp"
#include <memory>
#include <iterator>
#include <algorithm>
#include <stdexcept>
#include <any>

namespace jai {

// Forward declarations
class engine;
template<typename K, typename V> class bound_map;

/**
 * @brief Zero-copy wrapper for JaiScript arrays with value semantics
 * 
 * When passed by value: Creates a deep copy of the array
 * When passed by reference: Zero-copy access to the underlying array
 * When passed by const reference: Zero-copy read-only access
 * 
 * @tparam T The element type of the array
 */
template<typename T>
class bound_array {
private:
    // Either owns the data (for by-value) or references it (for by-reference)
    mutable std::shared_ptr<script_value> owned_value_;
    script_array* arr_;  // script_array is std::vector<script_value>
    std::weak_ptr<engine> engine_ref_;  // Engine reference for creating script_values
    
    // Helper to get engine reference
    std::weak_ptr<engine> get_engine_ref() const {
        return engine_ref_;
    }
    
public:
    // Helper to create script_value from T (public for nested access)
    static script_value make_script_value(const T& value, std::weak_ptr<engine> eng) {
        if constexpr (std::is_same_v<T, int>) {
            return script_value(static_cast<script_int>(value), eng);
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, script_int>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<T, float>) {
            return script_value(static_cast<script_float>(value));
        } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, script_float>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<T, bool>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<T, char>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return script_value(value, eng);
        } else if constexpr (std::is_same_v<T, script_value>) {
            return value;
        } else if constexpr (is_bound_array_v<T>) {
            // For nested bound_array, return its underlying script_value
            if (value.is_owned()) {
                return value.as_script_value();
            } else {
                // If it's a reference wrapper, we need to create a deep copy
                script_value result = script_value::make_array(nullptr);
                auto& arr = result.as_array();
                arr.reserve(value.size());
                for (const auto& elem : value) {
                    // Recursively convert each element
                    arr.push_back(bound_array<typename T::value_type>::make_script_value(elem));
                }
                return result;
            }
        } else if constexpr (is_bound_map_v<T>) {
            // For nested bound_map, return its underlying script_value  
            if (value.is_owned()) {
                return value.as_script_value();
            } else {
                // If it's a reference wrapper, we need to create a deep copy
                using K = typename T::key_type;
                using V = typename T::mapped_type;
                script_value result = script_value::make_map(
                    type_info::make<K>(), 
                    type_info::make<V>()
                );
                auto& map = result.as_map();
                for (const auto& [k, v] : value) {
                    map[bound_array<K>::make_script_value(k)] = bound_array<V>::make_script_value(v);
                }
                return result;
            }
        } else {
            // Custom object type - wrap in shared_ptr
            auto type_name = typeid(T).name();
            return script_value::make_object(type_name, std::make_shared<T>(value));
        }
    }
    
    // Helper trait to detect bound_array types
    template<typename U>
    struct is_bound_array : std::false_type {};
    
    template<typename U>
    struct is_bound_array<bound_array<U>> : std::true_type {};
    
    template<typename U>
    static constexpr bool is_bound_array_v = is_bound_array<U>::value;
    
    // Helper trait to detect bound_map types
    template<typename U>
    struct is_bound_map : std::false_type {};
    
    template<typename K2, typename V2>
    struct is_bound_map<bound_map<K2, V2>> : std::true_type {};
    
    template<typename U>
    static constexpr bool is_bound_map_v = is_bound_map<U>::value;
    
    // Helper to extract T from script_value (public for nested access)
    static T extract_value(const script_value& val) {
        if constexpr (std::is_same_v<T, script_value>) {
            return val;
        } else if constexpr (is_bound_array_v<T>) {
            // For nested bound_array, create a zero-copy wrapper
            if (!val.is_array()) {
                throw runtime_error("Cannot extract bound_array from non-array script_value");
            }
            return T(const_cast<script_value&>(val).as_array(), val.get_engine_ref());
        } else if constexpr (is_bound_map_v<T>) {
            // For nested bound_map, create a zero-copy wrapper
            if (!val.is_map()) {
                throw runtime_error("Cannot extract bound_map from non-map script_value");
            }
            return T(const_cast<script_value&>(val).as_map(), val.get_engine_ref());
        } else {
            // For custom types, use the same logic as script_value::as<T>()
            // which will check conversion registry if engine reference is available
            return val.as<T>();
        }
    }
    
public:
    // Type aliases for STL compatibility
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = T&;
    using const_reference = const T&;
    
    /**
     * @brief Construct from array reference (zero-copy)
     * @param arr Reference to existing script array
     * @param eng Engine reference for creating new script_values
     */
    explicit bound_array(script_array& arr, std::weak_ptr<engine> eng) 
        : owned_value_(nullptr), arr_(&arr), engine_ref_(eng) {}
    
    /**
     * @brief Construct from script_value (creates deep copy for value semantics)
     * @param val Script value containing an array
     */
    explicit bound_array(const script_value& val) {
        if (!val.is_array()) {
            throw runtime_error("Cannot create bound_array from non-array script_value");
        }
        engine_ref_ = val.get_engine_ref();
        owned_value_ = std::make_shared<script_value>(val.clone());
        arr_ = &owned_value_->as_array();
    }
    
    /**
     * @brief Copy constructor
     * Always creates an owned deep copy (value semantics)
     */
    bound_array(const bound_array& other) {
        engine_ref_ = other.engine_ref_;  // Copy engine reference
        // Always create owned copy when copy constructing (value semantics)
        if (other.owned_value_) {
            owned_value_ = std::make_shared<script_value>(other.owned_value_->deref().clone());
        } else {
            // Create a new array and copy elements from the referenced array
            owned_value_ = std::make_shared<script_value>(script_value::make_array(nullptr, engine_ref_));
            auto& temp_arr = owned_value_->as_array();
            if (other.arr_) {
                for (const auto& elem : *other.arr_) {
                    temp_arr.push_back(elem.deref().clone());
                }
            }
        }
        arr_ = &owned_value_->as_array();
    }
    
    /**
     * @brief Move constructor
     */
    bound_array(bound_array&& other) noexcept
        : owned_value_(std::move(other.owned_value_)), arr_(other.arr_) {
        if (owned_value_) {
            arr_ = &owned_value_->as_array();
        }
        other.arr_ = nullptr;
    }
    
    /**
     * @brief Copy assignment
     */
    bound_array& operator=(const bound_array& other) {
        if (this != &other) {
            if (other.owned_value_) {
                owned_value_ = std::make_shared<script_value>(other.owned_value_->clone());
                arr_ = &owned_value_->as_array();
            } else {
                owned_value_ = nullptr;
                arr_ = other.arr_;
            }
        }
        return *this;
    }
    
    /**
     * @brief Move assignment
     */
    bound_array& operator=(bound_array&& other) noexcept {
        if (this != &other) {
            owned_value_ = std::move(other.owned_value_);
            arr_ = other.arr_;
            if (owned_value_) {
                arr_ = &owned_value_->as_array();
            }
            other.arr_ = nullptr;
        }
        return *this;
    }
    
    /**
     * @brief Check if this array owns its data (is a copy) or references external data
     */
    bool is_owned() const { return owned_value_ != nullptr; }
    
    // ===== Capacity =====
    
    size_type size() const { return arr_ ? arr_->size() : 0; }
    bool empty() const { return !arr_ || arr_->empty(); }
    
    void reserve(size_type new_cap) {
        if (arr_) arr_->reserve(new_cap);
    }
    
    size_type capacity() const { return arr_ ? arr_->capacity() : 0; }
    
    // ===== Modifiers =====
    
    void clear() {
        if (arr_) arr_->clear();
    }
    
    void push_back(const T& value) {
        if (!arr_) throw runtime_error("Cannot push_back to null bound_array");
        arr_->push_back(make_script_value(value, get_engine_ref()));
    }
    
    void push_back(T&& value) {
        if (!arr_) throw runtime_error("Cannot push_back to null bound_array");
        arr_->push_back(make_script_value(std::forward<T>(value), get_engine_ref()));
    }
    
    void pop_back() {
        if (arr_ && !arr_->empty()) {
            arr_->pop_back();
        }
    }
    
    void resize(size_type count) {
        if (!arr_) throw runtime_error("Cannot resize null bound_array");
        
        if (count > arr_->size()) {
            // Add default-constructed elements
            while (arr_->size() < count) {
                arr_->push_back(make_script_value(T{}, get_engine_ref()));
            }
        } else {
            arr_->resize(count);
        }
    }
    
    void resize(size_type count, const T& value) {
        if (!arr_) throw runtime_error("Cannot resize null bound_array");
        
        if (count > arr_->size()) {
            // Add copies of value
            while (arr_->size() < count) {
                arr_->push_back(make_script_value(value, get_engine_ref()));
            }
        } else {
            arr_->resize(count);
        }
    }
    
    // ===== Element access =====
    
    T operator[](size_type pos) const {
        return extract_value((*arr_)[pos]);
    }
    
    T at(size_type pos) const {
        if (!arr_ || pos >= arr_->size()) {
            throw std::out_of_range("bound_array::at: index out of range");
        }
        return extract_value((*arr_)[pos]);
    }
    
    T front() const {
        if (!arr_ || arr_->empty()) {
            throw runtime_error("bound_array::front: array is empty");
        }
        return extract_value(arr_->front());
    }
    
    T back() const {
        if (!arr_ || arr_->empty()) {
            throw runtime_error("bound_array::back: array is empty");
        }
        return extract_value(arr_->back());
    }
    
    // ===== Element modification proxy =====
    
    class element_proxy {
        script_array& arr_;
        size_type idx_;
        std::weak_ptr<engine> engine_ref_;
        // Storage for nested bound_map wrappers (no copying, just wrapper objects)
        mutable std::unique_ptr<std::any> nested_wrapper_storage_;
        
    public:
        element_proxy(script_array& arr, size_type idx, std::weak_ptr<engine> eng) 
            : arr_(arr), idx_(idx), engine_ref_(eng) {}
            
        // Copy constructor
        element_proxy(const element_proxy& other) 
            : arr_(other.arr_), idx_(other.idx_), engine_ref_(other.engine_ref_) {}
            
        // Move constructor - efficient for temporary proxies
        element_proxy(element_proxy&& other) noexcept
            : arr_(other.arr_), idx_(other.idx_), engine_ref_(std::move(other.engine_ref_)), 
              nested_wrapper_storage_(std::move(other.nested_wrapper_storage_)) {}
        
        element_proxy& operator=(const T& value) {
            arr_[idx_] = bound_array::make_script_value(value, engine_ref_);
            return *this;
        }
        
        element_proxy& operator=(T&& value) {
            arr_[idx_] = bound_array::make_script_value(std::forward<T>(value), engine_ref_);
            return *this;
        }
        
        // Copy assignment from other element_proxy
        element_proxy& operator=(const element_proxy& other) {
            arr_[idx_] = bound_array::make_script_value(other.get(), engine_ref_);
            return *this;
        }
        
        // Move assignment from other element_proxy - PERFORMANCE WIN!
        element_proxy& operator=(element_proxy&& other) {
            // Direct script_value move - no extraction/conversion needed!
            arr_[idx_] = std::move(other.arr_[other.idx_]);
            return *this;
        }
        
        operator T() const {
            return bound_array::extract_value(arr_[idx_]);
        }
        
        T get() const {
            return bound_array::extract_value(arr_[idx_]);
        }
        
        // Support for nested bound_array operations
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::element_proxy>::type
        operator[](size_type pos) {
            // Get the nested array and return its element_proxy
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return typename U::element_proxy(nested_arr, pos, engine_ref_);
        }
        
        // Const version for reading
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::value_type>::type
        operator[](size_type pos) const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return U::extract_value(nested_arr[pos]);
        }
        
        // Support for size() on nested arrays
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, size_type>::type
        size() const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            return nested_val.as_array().size();
        }
        
        // Support for push_back on nested arrays
        template<typename U = T, typename V>
        typename std::enable_if<is_bound_array_v<U>, void>::type
        push_back(V&& value) {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            nested_arr.push_back(U::make_script_value(std::forward<V>(value)));
        }
        
        // Support for nested bound_map subscript operations
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::element_proxy>::type
        operator[](const typename U::key_type& key) {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            // Store the wrapper in our storage to keep it alive (zero-copy wrapper around existing script_value)
            nested_wrapper_storage_ = std::make_unique<std::any>(U(nested_map));
            U& bound_map_wrapper = std::any_cast<U&>(*nested_wrapper_storage_);
            return bound_map_wrapper[key];
        }
        
        // Const version for reading nested maps
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::mapped_type>::type
        operator[](const typename U::key_type& key) const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            U bound_map_wrapper(nested_map);
            return bound_map_wrapper[key];
        }
        
        // Support for iterating nested arrays
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::iterator>::type
        begin() {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return typename U::iterator(nested_arr.begin());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::iterator>::type
        end() {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return typename U::iterator(nested_arr.end());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::const_iterator>::type
        begin() const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return typename U::const_iterator(nested_arr.begin());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_array_v<U>, typename U::const_iterator>::type
        end() const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_array()) {
                throw runtime_error("Element is not an array");
            }
            auto& nested_arr = nested_val.as_array();
            return typename U::const_iterator(nested_arr.end());
        }
        
        // Support for iterating nested maps
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::iterator>::type
        begin() {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            return typename U::iterator(nested_map.begin());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::iterator>::type
        end() {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            return typename U::iterator(nested_map.end());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::const_iterator>::type
        begin() const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            return typename U::const_iterator(nested_map.begin());
        }
        
        template<typename U = T>
        typename std::enable_if<is_bound_map_v<U>, typename U::const_iterator>::type
        end() const {
            auto& nested_val = arr_[idx_];
            if (!nested_val.is_map()) {
                throw runtime_error("Element is not a map");
            }
            auto& nested_map = nested_val.as_map();
            return typename U::const_iterator(nested_map.end());
        }
    };
    
    element_proxy operator[](size_type pos) {
        return element_proxy(*arr_, pos, engine_ref_);
    }
    
    // ===== Iterators =====
    
    class iterator {
        script_array::iterator it_;
        
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T;
        
        iterator() = default;
        explicit iterator(script_array::iterator it) : it_(it) {}
        
        T operator*() const { return extract_value(*it_); }
        
        iterator& operator++() { ++it_; return *this; }
        iterator operator++(int) { iterator tmp = *this; ++it_; return tmp; }
        iterator& operator--() { --it_; return *this; }
        iterator operator--(int) { iterator tmp = *this; --it_; return tmp; }
        
        iterator& operator+=(difference_type n) { it_ += n; return *this; }
        iterator& operator-=(difference_type n) { it_ -= n; return *this; }
        
        iterator operator+(difference_type n) const { return iterator(it_ + n); }
        iterator operator-(difference_type n) const { return iterator(it_ - n); }
        
        difference_type operator-(const iterator& other) const { return it_ - other.it_; }
        
        bool operator==(const iterator& other) const { return it_ == other.it_; }
        bool operator!=(const iterator& other) const { return it_ != other.it_; }
        bool operator<(const iterator& other) const { return it_ < other.it_; }
        bool operator<=(const iterator& other) const { return it_ <= other.it_; }
        bool operator>(const iterator& other) const { return it_ > other.it_; }
        bool operator>=(const iterator& other) const { return it_ >= other.it_; }
        
        T operator[](difference_type n) const { return extract_value(*(it_ + n)); }
    };
    
    class const_iterator {
        script_array::const_iterator it_;
        
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T;
        
        const_iterator() = default;
        explicit const_iterator(script_array::const_iterator it) : it_(it) {}
        const_iterator(const iterator& other) : it_(other.it_) {}
        
        T operator*() const { return extract_value(*it_); }
        
        const_iterator& operator++() { ++it_; return *this; }
        const_iterator operator++(int) { const_iterator tmp = *this; ++it_; return tmp; }
        const_iterator& operator--() { --it_; return *this; }
        const_iterator operator--(int) { const_iterator tmp = *this; --it_; return tmp; }
        
        const_iterator& operator+=(difference_type n) { it_ += n; return *this; }
        const_iterator& operator-=(difference_type n) { it_ -= n; return *this; }
        
        const_iterator operator+(difference_type n) const { return const_iterator(it_ + n); }
        const_iterator operator-(difference_type n) const { return const_iterator(it_ - n); }
        
        difference_type operator-(const const_iterator& other) const { return it_ - other.it_; }
        
        bool operator==(const const_iterator& other) const { return it_ == other.it_; }
        bool operator!=(const const_iterator& other) const { return it_ != other.it_; }
        bool operator<(const const_iterator& other) const { return it_ < other.it_; }
        bool operator<=(const const_iterator& other) const { return it_ <= other.it_; }
        bool operator>(const const_iterator& other) const { return it_ > other.it_; }
        bool operator>=(const const_iterator& other) const { return it_ >= other.it_; }
        
        T operator[](difference_type n) const { return extract_value(*(it_ + n)); }
    };
    
    iterator begin() { return arr_ ? iterator(arr_->begin()) : iterator(); }
    iterator end() { return arr_ ? iterator(arr_->end()) : iterator(); }
    
    const_iterator begin() const { return arr_ ? const_iterator(arr_->begin()) : const_iterator(); }
    const_iterator end() const { return arr_ ? const_iterator(arr_->end()) : const_iterator(); }
    
    const_iterator cbegin() const { return begin(); }
    const_iterator cend() const { return end(); }
    
    // ===== Conversion =====
    
    /**
     * @brief Convert to std::vector<T> (always creates a copy)
     */
    std::vector<T> to_vector() const {
        std::vector<T> result;
        if (arr_) {
            result.reserve(arr_->size());
            for (const auto& val : *arr_) {
                result.push_back(extract_value(val));
            }
        }
        return result;
    }
    
    /**
     * @brief Get underlying script_value (for owned arrays only)
     */
    const script_value& as_script_value() const {
        if (!owned_value_) {
            throw runtime_error("Cannot get script_value from non-owned bound_array");
        }
        return *owned_value_;
    }
};

} // namespace jai

#endif // __JAISCRIPT_CORE_BOUND_ARRAY_HPP__