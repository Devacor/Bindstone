# JaiScript Archive Devirtualization

> **DONE as of 2026-07 — fully shipped, including this doc's own "Remaining Work" items:**
> `write_unique_ptr`/`read_unique_ptr` are templated (`property_serialization.hpp`) and the
> legacy virtual `archive_writer`/`archive_reader` base classes are DELETED. CRTP bases live
> across the serialization headers; versioned save/load detection has since evolved via the
> `jai::access` wrappers. The "Current State (Problem)" section below describes a world that no
> longer exists — historical plan. The Cereal-coexistence section remains live guidance.

This document outlines the plan to eliminate virtual dispatch from JaiScript's serialization hot path, matching Cereal's performance characteristics.

## Current State (Problem)

JaiScript archives currently use virtual methods for I/O operations:

```cpp
// Current: Virtual dispatch on EVERY write
class archive_writer {
    virtual void write_int8(int8_t) = 0;      // Virtual call
    virtual void begin_object(...) = 0;        // Virtual call
    // ... 50+ virtual methods
};

class binary_archive_writer : public archive_writer {
    void write_int8(int8_t v) override { ... } // Override
};
```

**Problem**: Every `ar.write_int8()`, `ar.begin_object()`, etc. is a virtual call. With thousands of properties, this adds significant overhead.

## Target State (Cereal's Approach)

Cereal uses CRTP (Curiously Recurring Template Pattern) to eliminate ALL virtual dispatch in the serialization path:

```cpp
// Cereal's approach: ZERO virtual calls
template<class ArchiveType>
class OutputArchive {
    ArchiveType* self;  // Pointer to concrete type

    template<class... Types>
    ArchiveType& operator()(Types&&... args) {
        self->process(std::forward<Types>(args)...);  // Non-virtual!
        return *self;
    }
};

class BinaryOutputArchive : public OutputArchive<BinaryOutputArchive> {
public:
    BinaryOutputArchive() : OutputArchive(this) {}

    void saveBinary(const void* data, size_t size) { ... }  // Non-virtual!
};

// Free function takes concrete type - no polymorphism
template<class T>
void save(BinaryOutputArchive& ar, T const& t) {
    ar.saveBinary(&t, sizeof(t));  // Direct call, no virtual
}
```

**Key insight**: The concrete archive type flows through the entire call chain as a template parameter. No virtual dispatch anywhere in the hot path.

## Implementation Plan

### Phase 1: CRTP Base Classes

Convert archive base classes to CRTP templates:

```cpp
// archive.hpp
template<class Derived>
class archive_writer_impl : public archive_base {
protected:
    Derived* self() { return static_cast<Derived*>(this); }

public:
    template<class... Types>
    Derived& operator()(Types&&... args) {
        (self()->process(std::forward<Types>(args)), ...);
        return *self();
    }

    template<class T>
    void write_element(const T& value) {
        // Dispatch to appropriate serialization
        // All calls through self() are non-virtual
    }
};

template<class Derived>
class archive_reader_impl : public archive_base {
    // Similar pattern
};
```

### Phase 2: Concrete Archives

Update concrete archives to use CRTP:

```cpp
// binary_archive.hpp
class binary_archive_writer
    : public archive_writer_impl<binary_archive_writer> {
public:
    static constexpr bool is_text_format = false;
    static constexpr bool needs_property_keys = true;

    // Non-virtual I/O methods
    void write_int8(int8_t v) { write_raw(&v, sizeof(v)); }
    void write_int16(int16_t v) { write_raw(&v, sizeof(v)); }
    void write_string(const std::string& s) { ... }
    void begin_object(const std::string& type, uint32_t version) { ... }
    // ... all non-virtual
};

// json_archive.hpp
class json_archive_writer
    : public archive_writer_impl<json_archive_writer> {
public:
    static constexpr bool is_text_format = true;
    static constexpr bool needs_property_keys = false;

    // Non-virtual I/O methods with JSON implementation
    void write_int8(int8_t v) { write_json_value(v); }
    // ...
};
```

### Phase 3: Remove Virtual Keywords

Remove all `virtual` and `override` from I/O methods:

**Methods to devirtualize** (called in tight loops):
- `write_int8/16/32/64`, `write_uint8/16/32/64`
- `write_float32/64`, `write_bool`, `write_string`, `write_binary`
- `read_int8/16/32/64`, `read_uint8/16/32/64`
- `read_float32/64`, `read_bool`, `read_string`, `read_binary`
- `begin_object`, `end_object`, `begin_array`, `end_array`
- `write_property_name`, `read_property_name`
- `begin_map`, `end_map`, `write_map_key`, `read_map_key`
- `write_value`, `read_value`
- `has_property`, `seek_property`, `seek_property_by_index`
- `get_object_property_count`, `get_object_property_names`

**Methods that can stay virtual** (called once per serialization):
- `~archive_writer()` / `~archive_reader()` - destructor (rare, not in hot path)

**Replace with static constexpr**:
- `needs_property_keys()` -> `static constexpr bool needs_property_keys`

### Phase 4: Update All Usage Sites

Since all serialization functions are already templated on Archive (done previously), they'll automatically use non-virtual dispatch:

```cpp
// Already templated - will work with CRTP
template<typename Archive, typename T>
void save(Archive& ar, const std::vector<T>& vec) {
    ar.begin_array(vec.size());  // Non-virtual with CRTP!
    for (const auto& elem : vec) {
        ar.write_element(elem);   // Non-virtual with CRTP!
    }
    ar.end_array();               // Non-virtual with CRTP!
}
```

## Performance Impact

| Operation | Before (Virtual) | After (CRTP) |
|-----------|-----------------|--------------|
| Method call | vtable lookup + indirect call | Direct call (inlinable) |
| `ar(value)` | Multiple virtual dispatches | Zero virtual dispatches |
| Compiler optimization | Limited (can't see through virtual) | Full inlining possible |

For a serialization with 1000 properties, each with ~5 I/O calls = 5000 virtual calls eliminated.

## What We've Already Done

1. **Static format detection**: `static constexpr bool is_text_format` on all archives
2. **Templated STL functions**: All `save/load` for vector, map, variant, etc. templated on Archive
3. **Templated smart pointer functions**: `write_shared_ptr`, `read_shared_ptr`, etc. templated
4. **ADL wrappers templated**: All wrappers in `jai::serialization` namespace templated
5. **CRTP base templates created**: `archive_writer_impl<Derived>` and `archive_reader_impl<Derived>`
6. **Concrete archives converted to CRTP**:
   - `binary_archive_writer : public archive_writer_impl<binary_archive_writer>`
   - `binary_archive_reader : public archive_reader_impl<binary_archive_reader>`
   - `json_archive_writer : public archive_writer_impl<json_archive_writer>`
   - `json_archive_reader : public archive_reader_impl<json_archive_reader>`
7. **Removed all `virtual`/`override`** from I/O methods in concrete archives
8. **Replaced `needs_property_keys()` with `static constexpr bool needs_property_keys`**

## Remaining Work

1. **Template remaining functions** that take `archive_writer&` or `archive_reader&` by base reference:
   - `write_unique_ptr` / `read_unique_ptr` in property_serialization.hpp
   - Any other functions in serialize.h or MV code

2. **Delete legacy base classes** (`archive_writer` / `archive_reader`) once all references are templated

## Files Modified

1. **archive.hpp** - Added CRTP templates (`archive_writer_impl<Derived>`, `archive_reader_impl<Derived>`)
2. **binary_archive.hpp** - Removed all `override`, now inherits from CRTP base
3. **json_archive.hpp** - Removed all `override`, now inherits from CRTP base

## Files That May Need Updates

1. **property_serialization.hpp** - Template `write_unique_ptr`, `read_unique_ptr`
2. **serialize.h** - Check for any base class references
3. **MV layer code** - Check for any functions taking `archive_writer&` or `archive_reader&`

## Compatibility Notes

- User code with `template<class Archive> void serialize(Archive& ar)` continues to work unchanged
- Code taking `archive_writer&` by base class reference **WILL BREAK** until templated
- Static constexpr `is_text_format` and `needs_property_keys` already in place

## Cereal Coexistence: Versioned Function Detection

JaiScript archives now directly detect and call **versioned** `save(Archive&, uint32_t)` and `load(Archive&, uint32_t)` functions, passing version=0. This eliminates the need for one-parameter forwarders entirely.

### The Problem (Historical)

Previously, JaiScript detected one-parameter `save(Archive&)` functions while Cereal used two-parameter versioned `save(Archive&, uint32_t)` functions. This required MV types to have both:
- One-parameter forwarders for JaiScript detection
- Two-parameter versioned functions for Cereal

However, Cereal's trait detection (especially for polymorphic types via `CEREAL_REGISTER_TYPE`) would find both overloads and fail with: `"cereal found more than one compatible output serialization function"`.

Various SFINAE approaches were tried to hide the one-param forwarders from Cereal, but MSVC's aggressive trait detection (particularly in `bind_to_archives`) still found them.

### The Solution: Detect Versioned Functions Directly

JaiScript's `archive_writer_impl` and `archive_reader_impl` now detect and call the **same versioned functions** that Cereal uses:

```cpp
// In archive.hpp - trait detection
template<typename T, typename = void>
struct has_save_method : std::false_type {};
template<typename T>
struct has_save_method<T, std::void_t<decltype(
    std::declval<const T&>().save(std::declval<Derived&>(), std::uint32_t(0))
)>> : std::true_type {};

// When calling, pass version=0
template<typename T, std::enable_if_t<has_save_method<T>::value, int> = 0>
void operator()(const T& obj) {
    obj.save(*self(), 0);  // Version parameter is optional/ignored
}
```

### Pattern for MV Types with Dual Serialization

Types only need the versioned functions - no one-param forwarders required:

```cpp
class MyComponent : public Component {
protected:
    // Two-parameter versioned functions for BOTH Cereal and JaiScript
    template<class Archive>
    void save(Archive& archive, std::uint32_t const /*version*/) const {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            property_mgr.save(archive);
            Component::save(archive, 0);
        } else {
            archive(cereal::make_nvp("field", field.get()));
            archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
        }
    }

    template<class Archive>
    void load(Archive& archive, std::uint32_t const version) {
        if constexpr (jai::serialization::jai_archive<Archive>) {
            property_mgr.load(archive);
            Component::load(archive, 0);
        } else {
            archive(cereal::make_nvp("field", field.get()));
            archive(cereal::make_nvp("Component", cereal::base_class<Component>(this)));
        }
    }
};
```

### Benefits

1. **No SFINAE/requires complications** - No need to hide functions from Cereal
2. **Simpler code** - Just one set of save/load functions per type
3. **Full compatibility** - Works with both Cereal and JaiScript archives
4. **Version support** - JaiScript can leverage version info if needed in the future

### Migration

When updating MV types:
1. Remove all one-parameter forwarders (`save(Archive&)`, `load(Archive&)`)
2. Keep only the versioned functions (`save(Archive&, uint32_t)`, `load(Archive&, uint32_t)`)
3. Use `if constexpr (jai::serialization::jai_archive<Archive>)` to branch between serialization formats

## See Also

- [Cereal Serialization Library](https://github.com/USCiLab/cereal) - Design reference
- [SERIALIZATION.md](SERIALIZATION.md) - General serialization documentation
- Cereal source: `External/cereal/include/cereal/cereal.hpp` lines 318-690
