#pragma once

#ifndef JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP
#define JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP

#include <jaiscript/serialization/serialization_metadata.hpp>

#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <type_traits>

namespace jai {
namespace serialization {

class polymorphic_registry {
public:
    using save_fn_t = std::function<void(any_archive_writer&, const void*)>;
    // The uint32_t `id` is the shared-pointer id the object will be registered
    // under. The load_fn registers the object EAGERLY (immediately after
    // construction, before its own properties load) so that back-references
    // within the object's subtree — e.g. a child's weak_ptr to its parent, or
    // a Component's componentOwner pointing at the Node currently being loaded —
    // resolve correctly during load.
    using load_fn_t = std::function<std::shared_ptr<void>(any_archive_reader&, std::uint32_t id)>;

    struct type_entry {
        std::string name;
        save_fn_t save_fn;
        load_fn_t load_fn;
    };

    static polymorphic_registry& instance() {
        static polymorphic_registry reg;
        return reg;
    }

    void register_entry(std::type_index ti, type_entry entry) {
        auto [it, _] = by_rtti_.insert_or_assign(ti, std::move(entry));
        by_name_[it->second.name] = &it->second;
    }

    const type_entry* find(std::type_index ti) const {
        auto it = by_rtti_.find(ti);
        return it != by_rtti_.end() ? &it->second : nullptr;
    }

    const type_entry* find(const std::string& name) const {
        auto it = by_name_.find(name);
        return it != by_name_.end() ? it->second : nullptr;
    }

    // Auto-register a polymorphic type for serialization.
    // Detects save/load methods and load_and_construct via SFINAE.
    // Called from jai::registrar during static initialization.
    template<typename T>
    static void try_auto_register(const std::string& name) {
        if constexpr (std::is_polymorphic_v<T>) {
            type_entry entry;
            entry.name = name;
            entry.save_fn = make_save_fn<T>();
            entry.load_fn = make_load_fn<T>();
            instance().register_entry(std::type_index(typeid(T)), std::move(entry));
        }
    }

    // Manual registration for types that don't use the property system or
    // jai::registrar. Provide custom save/load lambdas that receive a
    // concrete archive reference (via dispatch).
    //
    // SaveFn: void(auto& archive, const T&)
    // LoadFn: std::shared_ptr<T>(auto& archive)
    //
    // Example:
    //   polymorphic_registry::register_manual<MyType>("MyType",
    //       [](auto& ar, const MyType& obj) { ar.serialize("x", obj.x); },
    //       [](auto& ar) {
    //           auto ptr = std::make_shared<MyType>();
    //           ar.serialize("x", ptr->x);
    //           return ptr;
    //       }
    //   );
    template<typename T, typename SaveFn, typename LoadFn>
    static void register_manual(const std::string& name, SaveFn&& save, LoadFn&& load) {
        type_entry entry;
        entry.name = name;
        entry.save_fn = [s = std::forward<SaveFn>(save)](any_archive_writer& ar, const void* ptr) {
            auto& obj = *static_cast<const T*>(ptr);
            ar.dispatch([&](auto& concrete_ar) { s(concrete_ar, obj); });
        };
        entry.load_fn = [l = std::forward<LoadFn>(load)](any_archive_reader& ar, std::uint32_t id) -> std::shared_ptr<void> {
            return ar.dispatch([&l, id](auto& concrete_ar) -> std::shared_ptr<void> {
                // Manual load lambdas own construction, so eager registration is
                // best-effort: register as soon as the object exists so that
                // anything loaded afterward can reference it. (A manual type that
                // needs to resolve references to *itself* mid-construction must
                // register the shared_ptr itself.)
                auto ptr = l(concrete_ar);
                if (ptr) {
                    concrete_ar.register_deserialized_shared(id, ptr);
                }
                return std::static_pointer_cast<void>(ptr);
            });
        };
        instance().register_entry(std::type_index(typeid(T)), std::move(entry));
    }

private:
    polymorphic_registry() = default;

    // --- Save function generation ---
    // Uses archive operator() via dispatch — handles save methods, property_mgr,
    // and access-controlled methods without requiring friendship

    template<typename T>
    static save_fn_t make_save_fn() {
        return [](any_archive_writer& ar, const void* ptr) {
            auto& obj = *static_cast<const T*>(ptr);
            ar.dispatch([&](auto& concrete_ar) {
                concrete_ar(obj);
            });
        };
    }

    // --- Load function generation ---
    // Priority: load_and_construct > default construct + member_load

    // Detection: T::load_and_construct(Archive&, construct<T>&) — JaiScript style
    struct detection_archive {
        static constexpr bool is_jai_archive = true;
        static constexpr bool is_text_format = false;
        template<typename... Args> void operator()(Args&&...) {}
        template<typename... Args> void serialize(Args&&...) {}
        template<typename U> U* get_user_context() const { return nullptr; }
    };

    template<typename T, typename = void>
    struct has_jai_load_and_construct : std::false_type {};
    template<typename T>
    struct has_jai_load_and_construct<T, std::void_t<decltype(
        ::jai::access::load_and_construct(std::declval<detection_archive&>(), std::declval<construct<T>&>())
    )>> : std::true_type {};

    template<typename T>
    static load_fn_t make_load_fn() {
        if constexpr (has_jai_load_and_construct<T>::value) {
            return [](any_archive_reader& ar, std::uint32_t id) -> std::shared_ptr<void> {
                return ar.dispatch([id](auto& concrete_ar) -> std::shared_ptr<void> {
                    std::shared_ptr<T> ptr;
                    construct<T> c(ptr);
                    // Register EAGERLY the moment the object is constructed, before
                    // load_and_construct loads its properties. This mirrors the
                    // non-polymorphic shared_ptr load path (archive_impl.hpp), which
                    // sets the same on_construct hook, and lets back-references
                    // inside this object's subtree resolve during load.
                    c.set_on_construct([&ptr, id, &concrete_ar]() {
                        concrete_ar.register_deserialized_shared(id, ptr);
                    });
                    ::jai::access::load_and_construct(concrete_ar, c);
                    return std::static_pointer_cast<void>(ptr);
                });
            };
        } else if constexpr (std::is_default_constructible_v<T>) {
            return [](any_archive_reader& ar, std::uint32_t id) -> std::shared_ptr<void> {
                return ar.dispatch([id](auto& concrete_ar) -> std::shared_ptr<void> {
                    auto ptr = std::make_shared<T>();
                    // Register before loading properties so subtree back-references
                    // (weak_ptr / shared_ptr pointing back into this object) resolve.
                    concrete_ar.register_deserialized_shared(id, ptr);
                    concrete_ar(*ptr);
                    if constexpr (requires { ptr->initialize(); }) {
                        ptr->initialize();
                    }
                    return std::static_pointer_cast<void>(ptr);
                });
            };
        } else {
            return nullptr;
        }
    }

    std::unordered_map<std::type_index, type_entry> by_rtti_;
    std::unordered_map<std::string, const type_entry*> by_name_;
};

// Free-function entry point named ahead of this header by jai::registrar (forward-declared
// there), so the registrar's class-template bodies can call it before this header is included.
template<typename T>
void try_auto_register(const std::string& name) {
    polymorphic_registry::try_auto_register<T>(name);
}

} // namespace serialization
} // namespace jai

#endif // JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP
