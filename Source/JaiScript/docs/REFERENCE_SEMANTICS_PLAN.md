# Non-Owning Reference Support in JaiScript

> **IMPLEMENTED — but via this plan's own REJECTED alternative. Do NOT implement from this doc.**
> Non-owning refs shipped in `object_holder::bound_ptr` (with the strong `keep_alive` anchor —
> stronger than the sketched weak `owner_hint_`), NOT the "recommended" `cpp_bound_ptr_`
> extension: that member no longer exists anywhere (folded into the boxed `cpp_bound_holder`
> variant alternative by the thin-value work). The converter shipped as
> `convert_reference_with_registry` (`engine_impl.hpp`), not a `value_converter<T&>` edit; no
> feature flag exists. What did ship as written: `has_registered_class<T>()`,
> `is_non_owning_object()`, and the extraction guard.
> The reference internals are being rewritten by the CELLS refactor — this doc is history only.
> The ChaiScript comparison and lifetime patterns below are still useful reading.

## Problem Statement

When C++ methods return `T&` or `const T&` for registered types, JaiScript's `value_converter<T&>::to()` currently attempts to copy the object. This fails for non-copyable types (types with deleted copy constructors, reference members, unique_ptr members, etc.).

## Leveraging Existing Infrastructure

JaiScript already has excellent infrastructure for reference semantics:

1. **`cpp_bound_ptr_`** - Used for non-owning pointers to C++ primitives (int, float, bool, string)
2. **`reference_holder`** - For script-level variable references (points to `script_value*`)
3. **`deref()`** - Transparently unwraps script references
4. **`is_cpp_bound()`** - Checks if value points to external C++ memory

The pattern for primitives:
```cpp
// In checked_as_int():
if (val.cpp_bound_ptr_) {
    return checked_result<script_int>(*static_cast<const int*>(val.cpp_bound_ptr_));
}
return checked_result<script_int>(std::get<script_int>(val.storage_));
```

**Key insight**: We can extend this same pattern to objects! The `cpp_bound_ptr_` field is already:
- Propagated through copy/move operations
- Checked via `is_cpp_bound()`
- Integrated with the variant storage system

### Current Failing Cases

```cpp
class Interface {
    InterfaceManager& manager;  // Reference member - makes class non-copyable
};

class InterfaceManager {
    std::vector<std::unique_ptr<Interface>> pages;  // Non-copyable member
    jai::engine& jaiEngine_;  // Reference member
};

// This fails:
builder.method("page", &InterfaceManager::page);  // Returns Interface&
```

The error occurs in `convert_custom_type_with_registry()` when it tries to create `std::make_shared<T>(t)` as a fallback, which requires copying.

## ChaiScript Approach

ChaiScript's `boxed_value` handles this with dual storage semantics:

```cpp
struct Data {
    chaiscript::detail::Any m_obj;      // Holds shared_ptr<T> OR reference_wrapper<T>
    void* m_data_ptr;                    // Raw pointer for fast access
    const void* m_const_data_ptr;        // Const raw pointer
    bool m_is_ref;                       // True = non-owning, False = owning
};

// For references (non-owning):
static auto get(std::reference_wrapper<T> obj, bool t_return_value) {
    return std::make_shared<Data>(
        detail::Get_Type_Info<T>::get(),
        chaiscript::detail::Any(std::move(obj)),  // Store reference_wrapper
        true,  // is_ref = true
        &obj.get(),
        t_return_value
    );
}

// For shared_ptr (owning):
static auto get(const std::shared_ptr<T>& obj, bool t_return_value) {
    return std::make_shared<Data>(
        detail::Get_Type_Info<T>::get(),
        chaiscript::detail::Any(obj),  // Store shared_ptr
        false,  // is_ref = false
        obj.get(),
        t_return_value
    );
}
```

## Proposed JaiScript Design

### Recommended: Extend cpp_bound_ptr_ Pattern to Objects

Use the existing `cpp_bound_ptr_` mechanism that already works for primitives:

```cpp
// For non-owning object references:
// - object_holder stores type info (type_name, type_id) with data = nullptr
// - cpp_bound_ptr_ stores the raw pointer to the C++ object
// - Extraction checks cpp_bound_ptr_ first, just like primitives

template<typename T>
script_value script_value::make_cpp_bound(T* target, engine* eng) {
    // Existing primitive handling...

    // NEW: For registered class types
    if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string>) {
        if (eng->has_registered_class<T>()) {
            // Create object_holder with type info but no owning data
            auto holder = make_strong<object_holder>();
            holder->type_name = eng->get_registered_name<T>();
            holder->type_id = eng->intern_type_name(holder->type_name);
            holder->data = nullptr;  // Non-owning!
            holder->is_class_instance_wrapper = false;

            script_value val(std::monostate{}, eng);
            val.type_info_ = eng->get_type_info_object(holder->type_name);
            val.storage_ = std::move(holder);
            val.cpp_bound_ptr_ = static_cast<void*>(target);  // Non-owning pointer
            return val;
        }
    }
    // ... existing code for unregistered types ...
}
```

**Benefits of this approach:**
- Leverages existing `cpp_bound_ptr_` infrastructure
- No changes to `object_holder` structure
- `is_cpp_bound()` already works
- Copy/move already propagates `cpp_bound_ptr_`
- Consistent pattern with primitive bindings

### Alternative: Add non_owning_ptr_ to object_holder

```cpp
struct object_holder {
    std::shared_ptr<void> data;              // Owning storage (null for references)
    void* non_owning_ptr_ = nullptr;          // Alternative non-owning pointer
    // ...
};
```

**Rejected because:**
- Adds redundant pointer (cpp_bound_ptr_ already exists)
- Requires changing object_holder size
- Pattern already established with cpp_bound_ptr_

## Implementation Plan

### Phase 1: Extend make_cpp_bound for Objects (Low Risk)

1. **Add `engine::has_registered_class<T>()`** check:
   ```cpp
   template<typename T>
   bool engine::has_registered_class() const {
       return get_class_definition_by_type(std::type_index(typeid(T))) != nullptr;
   }
   ```

2. **Extend `script_value::make_cpp_bound<T>()`** for registered types:
   - Create `object_holder` with type info but `data = nullptr`
   - Set `cpp_bound_ptr_` to the raw pointer
   - Already works for primitives, just add object branch

### Phase 2: Update Extraction Paths (Medium Risk)

1. **Modify `checked_as<std::shared_ptr<T>>()`** to check `cpp_bound_ptr_`:
   ```cpp
   // In the shared_ptr extraction path:
   auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

   // Check for non-owning reference FIRST
   if (cpp_bound_ptr_ && !objHolder->data) {
       // Non-owning reference - cannot return shared_ptr safely
       return checked_result<T>(
           make_error_code(runtime_error_code::type_mismatch),
           "Cannot extract shared_ptr from non-owning C++ reference"
       );
   }
   // Existing owning path...
   ```

2. **Add `T*` extraction support** for non-owning objects:
   ```cpp
   // New specialization for T* extraction:
   if (cpp_bound_ptr_ && objHolder && !objHolder->data) {
       return static_cast<T*>(cpp_bound_ptr_);
   }
   ```

### Phase 3: Update value_converter (Medium Risk)

1. **Modify `value_converter<T&>::to()`**:
   ```cpp
   static script_value to(T& t, engine* eng) {
       if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string> &&
                    !is_specialization_v<T, std::vector> && !is_specialization_v<T, std::map>) {
           // For registered class types, use non-owning binding
           if (eng && eng->has_registered_class<T>()) {
               return script_value::make_cpp_bound(&t, eng);
           }
       }
       // Fall back to existing behavior
       return value_converter<const T&>::to(t, eng);
   }
   ```

### Phase 4: Testing & Validation

1. Run existing 900+ test suite (should pass unchanged)
2. Add specific tests for:
   - Methods returning `T&` for registered non-copyable types
   - `is_cpp_bound()` returns true for reference objects
   - `as<std::shared_ptr<T>>()` fails gracefully for non-owning
   - `as<T&>()` works for non-owning references

## Detailed Code Changes

### 1. Engine Type Registration Check (engine.hpp)

```cpp
// Check if type T has been registered via dynamic_binder
template<typename T>
bool has_registered_class() const {
    auto class_def = get_class_definition_by_type(std::type_index(typeid(T)));
    return class_def != nullptr;
}
```

### 2. Extend make_cpp_bound for Objects (value_impl.hpp)

```cpp
template<typename T>
script_value script_value::make_cpp_bound(T* target, engine* eng) {
    script_value val(std::monostate{}, eng);

    if (eng) {
        // Existing: Map C++ primitive types to script types
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            val.type_info_ = eng->get_type_info_int();
            val.storage_ = script_int{0};
        } else if constexpr (std::is_floating_point_v<T>) {
            val.type_info_ = eng->get_type_info_float();
            val.storage_ = script_float{0.0};
        } else if constexpr (std::is_same_v<T, bool>) {
            val.type_info_ = eng->get_type_info_bool();
            val.storage_ = script_bool{false};
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, script_string>) {
            val.type_info_ = eng->get_type_info_string();
            val.storage_ = make_strong<script_string>();
        } else if constexpr (std::is_same_v<T, char>) {
            val.type_info_ = eng->get_type_info_char();
            val.storage_ = script_char{'\0'};
        }
        // NEW: For registered class types, create non-owning object reference
        else if constexpr (std::is_class_v<T>) {
            if (eng->has_registered_class<T>()) {
                // Create object_holder with type info but NO owning data
                auto holder = make_strong<object_holder>();
                holder->type_name = eng->get_registered_name<T>();
                holder->type_id = eng->intern_type_name(holder->type_name);
                holder->data = nullptr;  // Non-owning!
                holder->is_class_instance_wrapper = false;

                val.type_info_ = eng->get_type_info_object(holder->type_name);
                val.storage_ = std::move(holder);
            } else {
                // Fallback for unregistered types
                val.type_info_ = eng->get_type_info_object(typeid(T).name());
                val.storage_ = std::monostate{};
            }
        } else {
            val.type_info_ = eng->get_type_info_object(typeid(T).name());
            val.storage_ = std::monostate{};
        }
    }

    val.cpp_bound_ptr_ = static_cast<void*>(target);  // Non-owning pointer
    return val;
}
```

### 3. Update checked_as<std::shared_ptr<T>>() (value.hpp)

```cpp
// In the shared_ptr extraction path:
else if constexpr (is_specialization_v<T, std::shared_ptr>) {
    auto t = type();
    if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
        auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

        // NEW: Check for non-owning C++ reference (cpp_bound_ptr_ set, data null)
        if (cpp_bound_ptr_ && !objHolder->data) {
            return checked_result<T>(
                make_error_code(runtime_error_code::type_mismatch),
                "Cannot extract shared_ptr from non-owning C++ reference. "
                "Use T& or T* extraction instead."
            );
        }

        // Existing owning path continues...
        return checked_result<T>(std::static_pointer_cast<typename T::element_type>(objHolder->data));
    }
}
```

### 4. Add T* and T& Extraction for Non-Owning (value.hpp)

```cpp
// In checked_as<T>() for custom class types:
else if constexpr (std::is_class_v<T> && !std::is_same_v<T, std::string> && ...) {
    auto t = type();
    if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) {
        auto objHolder = std::get<strong_ptr<object_holder>>(storage_);

        // NEW: Handle non-owning C++ reference
        if (cpp_bound_ptr_ && !objHolder->data) {
            // Return copy from non-owning pointer (only if copyable)
            if constexpr (std::is_copy_constructible_v<T>) {
                return checked_result<T>(*static_cast<T*>(cpp_bound_ptr_));
            } else {
                return checked_result<T>(
                    make_error_code(runtime_error_code::type_mismatch),
                    "Cannot copy non-copyable type from non-owning reference"
                );
            }
        }

        // Existing owning path...
        auto ptr_result = checked_as<std::shared_ptr<T>>();
        if (!ptr_result) return ptr_result.error_value();
        return checked_result<T>(*ptr_result.value());
    }
}
```

### 5. Update value_converter<T&>::to() (function_binder.hpp)

```cpp
template<typename T>
struct value_converter<T&> {
    // ... existing from() method unchanged ...

    static script_value to(T& t, engine* eng) {
        // For registered class types, use non-owning binding via make_cpp_bound
        if constexpr (std::is_class_v<T> &&
                     !std::is_same_v<T, std::string> &&
                     !is_specialization_v<T, std::vector> &&
                     !is_specialization_v<T, std::map>) {
            if (eng && eng->has_registered_class<T>()) {
                return script_value::make_cpp_bound(&t, eng);
            }
        }
        // Fall back to copy semantics for non-registered or basic types
        return value_converter<const T&>::to(t, eng);
    }
};
```

### 6. Helper: is_non_owning_object() (value.hpp)

```cpp
// Check if this is a non-owning C++ object reference
bool is_non_owning_object() const {
    if (!cpp_bound_ptr_) return false;
    auto idx = raw_storage_index();
    if (idx != TYPEID_OBJECT && idx != TYPEID_SHARED_PTR) return false;
    auto objHolder = std::get<strong_ptr<object_holder>>(storage_);
    return objHolder && !objHolder->data;
}
```

## Lifetime Considerations

### Safe Patterns

1. **Method return values used immediately:**
   ```javascript
   var page = manager.page("main");  // Returns Interface&, wrapped as non-owning
   page.show();  // Safe - manager still owns Interface
   ```

2. **Captured in closures bound to same owner:**
   ```javascript
   interface.onShow = fun(self) {
       self.root().visible = true;  // Safe if Interface owns the root
   };
   ```

### Dangerous Patterns

1. **Storing reference beyond owner lifetime:**
   ```javascript
   var page = manager.page("main");
   manager.removePage("main");  // Destroys the Interface
   page.show();  // DANGLING REFERENCE - undefined behavior
   ```

2. **Returning references from nested scopes:**
   ```javascript
   fun getPage() {
       var mgr = createManager();
       return mgr.page("main");  // Reference to destroyed object
   }
   var p = getPage();
   p.show();  // DANGLING REFERENCE
   ```

### Mitigation Strategies

1. **Documentation**: Document that reference-returning methods require the owner to outlive the reference.

2. **Debug Assertions**: In debug builds, add checks (if feasible) to detect dangling references.

3. **Optional Weak Reference Tracking**: Store `weak_ptr` to owner if available:
   ```cpp
   struct object_holder {
       // ... existing members ...
       std::weak_ptr<void> owner_hint_;  // Optional: for lifetime debugging
   };
   ```

## Test Cases

### Existing Test Compatibility

All existing tests should pass unchanged since:
- Owning semantics (`shared_ptr` storage) are unmodified
- New non-owning path only activates for `T&` returns of registered types

### New Test Cases

```cpp
// test_reference_semantics.cpp

TEST_CASE("Method returning T& creates non-owning reference") {
    auto eng = create_test_engine();

    class Container {
    public:
        Item& get_item() { return item_; }
    private:
        Item item_;
    };

    eng.add_class<Container>("Container")
       .method("get_item", &Container::get_item);

    auto container = std::make_shared<Container>();
    eng.add_global("container", container);

    // This should NOT copy Item, just create non-owning reference
    eng.eval("var item = container.get_item();");

    // Verify it's the same object (not a copy)
    eng.eval("item.value = 42;");
    REQUIRE(container->get_item().value == 42);
}

TEST_CASE("Non-owning reference query methods") {
    auto eng = create_test_engine();
    // ... setup ...

    auto val = eng.eval("container.get_item();");
    REQUIRE(val.is_object());

    auto holder = val.get_object_holder();
    REQUIRE(holder->is_reference());
    REQUIRE_FALSE(holder->is_owning());
}

TEST_CASE("Non-copyable type works with reference returns") {
    auto eng = create_test_engine();

    class NonCopyable {
    public:
        NonCopyable() = default;
        NonCopyable(const NonCopyable&) = delete;
        int value = 0;
    };

    class Owner {
    public:
        NonCopyable& get() { return nc_; }
    private:
        NonCopyable nc_;
    };

    eng.add_class<NonCopyable>("NonCopyable")
       .property("value", &NonCopyable::value);

    eng.add_class<Owner>("Owner")
       .constructor()
       .method("get", &Owner::get);

    // This should work now - no copy needed
    eng.eval(R"(
        var owner = Owner();
        var nc = owner.get();
        nc.value = 100;
    )");

    // Verify mutation worked through reference
    auto owner = eng.eval("owner").as<std::shared_ptr<Owner>>();
    REQUIRE(owner->get().value == 100);
}
```

## Migration Guide

### For Users

No changes required for existing code. New behavior is automatic for methods returning `T&`.

### For Type Authors

If you have non-copyable types that you want to return by reference:

1. Register the type with `dynamic_binder`
2. Bind methods that return `T&` normally
3. The value_converter will automatically use non-owning semantics

```cpp
// Before: This would fail at compile time or runtime
builder.method("page", &InterfaceManager::page);  // Returns Interface&

// After: Works automatically - creates non-owning reference
builder.method("page", &InterfaceManager::page);  // Returns Interface&
```

## Performance Impact

1. **No impact on owning values**: Existing `shared_ptr` path is unchanged
2. **Slight improvement for references**: Avoids copy, no ref-count increment
3. **Minimal memory overhead**: One extra pointer per object_holder (8 bytes on 64-bit)

## Rollout Strategy

1. **Phase 1**: Implement core infrastructure behind feature flag
2. **Phase 2**: Enable for internal testing, run full test suite
3. **Phase 3**: Enable by default, document in release notes
4. **Phase 4**: Remove feature flag after validation period
