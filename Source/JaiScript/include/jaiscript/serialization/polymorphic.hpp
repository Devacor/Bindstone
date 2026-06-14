#pragma once

#ifndef JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP
#define JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP

#include <jaiscript/serialization/serialization_metadata.hpp>
#include <jaiscript/serialization/traits.hpp> //jai::access, named non-dependently by the load_and_construct probe below
#include <jaiscript/detail/static_type_name.hpp>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <type_traits>

// construct.hpp includes archive_impl.hpp (which includes this header), so only the
// declaration can appear here; the probe and factory bodies need no more until they
// instantiate, and any type declaring load_and_construct includes construct.hpp itself.
namespace jai {
namespace serialization {
template<typename T> class construct;
}
}

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
        std::type_index type = std::type_index(typeid(void));
        bool implicit = false; //registered via property_owner's derived name; any explicit registration wins
    };

    static polymorphic_registry& instance() {
        static polymorphic_registry reg;
        return reg;
    }

    // One registry, two doors: explicit (jai::registrar / register_manual — chosen name,
    // always wins) and implicit (property_owner — derived class name, never overrides).
    // Static-init order between the two is unspecified, so both orders must converge:
    // implicit-after-explicit no-ops; explicit-after-implicit replaces.
    void register_entry(std::type_index ti, type_entry entry, bool a_implicit = false) {
        entry.type = ti;
        entry.implicit = a_implicit;
        auto existing = by_rtti_.find(ti);
        if (existing != by_rtti_.end() && a_implicit) {
            return;
        }
        if (auto named = by_name_.find(entry.name); named != by_name_.end() && named->second->type != ti) {
            if (a_implicit) {
                std::fprintf(stderr, "[jai::serialization] implicit registration of '%s' skipped: the name already belongs to a different type. Register one of them with an explicit jai::registrar name.\n", entry.name.c_str());
                return;
            }
            // An explicit registration owns its name outright; the displaced type would
            // otherwise resolve to the wrong factory on load, so evict it entirely (its
            // next save fails loudly instead of corrupting data).
            std::fprintf(stderr, "[jai::serialization] explicit registration of '%s' evicts a previous%s registration of the same name for a different type.\n", entry.name.c_str(), named->second->implicit ? " implicit" : " explicit");
            auto victimType = named->second->type;
            by_name_.erase(named);
            by_rtti_.erase(victimType);
            existing = by_rtti_.find(ti);
        }
        if (existing != by_rtti_.end() && existing->second.name != entry.name) {
            by_name_.erase(existing->second.name);
        }
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

    // Implicit registration (property_owner): same entry machinery, weak precedence.
    // The name is `static constexpr const char* jai_type_name` if the class declares one,
    // else the bare class identifier (parsed from the compiler signature — never a mangled
    // typeid name, identical across MSVC/GCC/Clang; anonymous-namespace types fall out as
    // their bare name too). Template instantiations have no stable bare spelling, so
    // without jai_type_name they are skipped at compile time — pin a name in-class or
    // register externally (jai::registrar / try_auto_register, no type modification).
    template<typename T>
    static void try_auto_register_implicit() {
        if constexpr (std::is_polymorphic_v<T>) {
            if constexpr (requires { std::string_view{T::jai_type_name}; }) {
                register_implicit_entry<T>(std::string(T::jai_type_name));
            } else {
                constexpr auto derivedName = ::jai::detail::static_unqualified_type_name<T>();
                if constexpr (!derivedName.empty()) {
                    register_implicit_entry<T>(std::string(derivedName));
                }
            }
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

    template<typename T>
    static void register_implicit_entry(std::string name) {
        type_entry entry;
        entry.name = std::move(name);
        entry.save_fn = make_save_fn<T>();
        entry.load_fn = make_load_fn<T>();
        instance().register_entry(std::type_index(typeid(T)), std::move(entry), true);
    }

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

// Free-function entry points named ahead of this header by jai::registrar and
// jai::property_owner (forward-declared there), so their class-template bodies can call
// them before this header is included.
template<typename T>
void try_auto_register(const std::string& name) {
    polymorphic_registry::try_auto_register<T>(name);
}

template<typename T>
void try_auto_register_implicit() {
    polymorphic_registry::try_auto_register_implicit<T>();
}

} // namespace serialization
} // namespace jai

// NOTE: make_save_fn/make_load_fn instantiate any_archive dispatch(), defined in
// archive_dispatch.hpp. This header must stay light (archive_impl.hpp includes it before
// its own classes), so the registration ENTRY POINTS (registrar.hpp, property_manager.hpp)
// include archive_dispatch.hpp at their bottoms instead — otherwise a TU registering a
// type without the concrete archives fails with C3779 (decltype(auto) used before defined).

#endif // JAISCRIPT_SERIALIZATION_POLYMORPHIC_HPP
