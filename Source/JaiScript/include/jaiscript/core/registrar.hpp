#pragma once

#ifndef __JAISCRIPT_CORE_REGISTRAR_HPP__
#define __JAISCRIPT_CORE_REGISTRAR_HPP__

#include <functional>
#include <map>
#include <typeindex>
#include <string>
#include <string_view>
#include <optional>
#include <algorithm>
#include <type_traits>

#include <jaiscript/detail/static_type_name.hpp>

// C++20 features detection
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
#define JAI_HAS_CONCEPTS 1
#include <concepts>
#else
#define JAI_HAS_CONCEPTS 0
#endif

#if defined(__cpp_nontype_template_args) && __cpp_nontype_template_args >= 201911L
#define JAI_HAS_NTTP_STRING 1
#include "fixed_string.hpp"
#else
#define JAI_HAS_NTTP_STRING 0
#endif

namespace jai {

// Forward declarations
class engine;
template<typename T> class dynamic_binder;

// polymorphic.hpp is included at the bottom of this header (it depends on archive types
// defined elsewhere, and archive_impl.hpp closes the loop by including it). Forward-declare
// the auto-register entry point here so the registrar bodies below can name it as a template;
// its definition rides in with that bottom include.
namespace serialization {
	template<typename T> void try_auto_register(const std::string& name);
	template<typename Derived, typename... Bases> struct register_relation;
}

// Optional polymorphic base marker: registrar<T, Ctx, bases<Base...>> declares the serialization
// base edge(s) inline so a shared_ptr<Base> carrying this $type passes the load type check.
template<typename... Bases>
struct bases {};

template<typename T, typename... Bs>
inline void register_polymorphic_bases(bases<Bs...>) {
	if constexpr (sizeof...(Bs) > 0) { serialization::register_relation<T, Bs...>{}; }
}

// ============================================================================
// Type Name Registry - Global mapping from type_index to script name
// ============================================================================

class type_name_registry {
public:
    [[nodiscard]] static type_name_registry& instance() {
        static type_name_registry registry;
        return registry;
    }

    void register_type(std::type_index ti, std::string_view name) {
        auto& stored = type_names_[ti] = std::string(name);
        name_to_type_[stored] = ti;
    }

    template<typename T>
    void register_type(std::string_view name) {
        register_type(std::type_index(typeid(T)), name);
    }

    [[nodiscard]] std::optional<std::string_view> get_name(std::type_index ti) const {
        if (auto it = type_names_.find(ti); it != type_names_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    template<typename T>
    [[nodiscard]] std::optional<std::string_view> get_name() const {
        return get_name(std::type_index(typeid(T)));
    }

    [[nodiscard]] std::optional<std::type_index> get_type(const std::string& name) const {
        if (auto it = name_to_type_.find(name); it != name_to_type_.end()) {
            return *it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool has_name(std::type_index ti) const {
        return type_names_.contains(ti);
    }

    template<typename T>
    [[nodiscard]] bool has_name() const {
        return has_name(std::type_index(typeid(T)));
    }

private:
    type_name_registry() = default;
    std::map<std::type_index, std::string> type_names_;
    std::unordered_map<std::string, std::optional<std::type_index>> name_to_type_;
};

// ============================================================================
// Registrar Registry - Stores registration functions per context type
// ============================================================================

template<typename Context>
class registrar_registry {
public:
    using registration_fn = std::function<void(engine&, const Context&)>;

    [[nodiscard]] static registrar_registry& instance() {
        static registrar_registry registry;
        return registry;
    }

    void add(std::type_index ti, registration_fn fn) {
        registrations_[ti] = std::move(fn);
    }

    void register_all(engine& eng, const Context& ctx) {
        for (auto& [type, fn] : registrations_) {
            fn(eng, ctx);
        }
    }

    [[nodiscard]] bool has(std::type_index ti) const {
        return registrations_.contains(ti);
    }

private:
    registrar_registry() = default;
    std::map<std::type_index, registration_fn> registrations_;
};

template<>
class registrar_registry<void> {
public:
    using registration_fn = std::function<void(engine&)>;

    [[nodiscard]] static registrar_registry& instance() {
        static registrar_registry registry;
        return registry;
    }

    void add(std::type_index ti, registration_fn fn) {
        registrations_[ti] = std::move(fn);
    }

    void register_all(engine& eng) {
        for (auto& [type, fn] : registrations_) {
            fn(eng);
        }
    }

    [[nodiscard]] bool has(std::type_index ti) const {
        return registrations_.contains(ti);
    }

private:
    registrar_registry() = default;
    std::map<std::type_index, registration_fn> registrations_;
};

// ============================================================================
// jai::registrar - Type registration for JaiScript engine
// ============================================================================
//
// C++20 Usage (with NTTP string literals):
//   static jai::registrar<Player, "Player"> _player;
//   static jai::registrar<Player, "Player", Services> _player([](auto& builder, const Services& ctx) { ... });
//
// C++17 Usage (runtime name):
//   static jai::registrar<Player, MV::Services> _player("Player", [](auto& builder, const Services& ctx) { ... });
//
// Auto-named (the script name and serialization $type are derived from the C++ type, so
// you don't repeat the class name as a string — works for non-template classes):
//   static jai::registrar<Player, MV::Services> _player;                                  // name = "Player"
//   static jai::registrar<Player, MV::Services> _player([](auto& builder, const Services& ctx) { ... });
// Template instantiations have no portable derived name and must pass one explicitly
// (e.g. Point<int> -> "Pointi"); omitting it is a compile error directing you here.
//
// At engine initialization:
//   jai::bind_registrar(engine);                     // No context
//   jai::bind_registrar<Services>(engine, services); // With context

// Shared by the auto-named registrar constructors: the bare C++ type name (also used as
// the serialization $type, so the two stay in lockstep), or a compile error pointing at
// the explicit-name constructor when T has no portable name.
template<typename T>
inline std::string registrar_auto_name() {
    constexpr auto derived = ::jai::detail::static_unqualified_type_name<T>();
    static_assert(!derived.empty(),
        "jai::registrar<T, Context>: T has no portable auto-name (template instantiation, "
        "lambda, or local type). Pass an explicit name: jai::registrar<T, Context>(\"Name\").");
    return std::string(derived);
}

// ============================================================================
// C++17-compatible registrar (runtime name)
// ============================================================================

// Primary template - with context. Optional bases<...> tag declares polymorphic base relations
// inline: registrar<T, Ctx, bases<Base>>.
template<typename T, typename Context, typename BasesTag = bases<>>
class registrar {
    static void register_simple(std::string name_str) {
        type_name_registry::instance().register_type<T>(name_str);
        serialization::try_auto_register<T>(name_str);
        register_polymorphic_bases<T>(BasesTag{});
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [name_str](engine& eng, const Context&) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                builder.build();
            }
        );
    }

    template<typename F>
    static void register_configured(std::string name_str, F&& configure) {
        type_name_registry::instance().register_type<T>(name_str);
        serialization::try_auto_register<T>(name_str);
        register_polymorphic_bases<T>(BasesTag{});
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [name_str, configure = std::forward<F>(configure)](engine& eng, const Context& ctx) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                configure(builder, ctx);
                builder.build();
            }
        );
    }

public:
    explicit registrar(const char* name) { register_simple(std::string(name)); }

    registrar() { register_simple(registrar_auto_name<T>()); }

    template<typename F>
    registrar(const char* name, F&& configure) { register_configured(std::string(name), std::forward<F>(configure)); }

    // Auto-named + configuration lambda. enable_if keeps a string literal from binding
    // here instead of the explicit-name constructor above.
    template<typename F, std::enable_if_t<std::is_invocable_v<F&, dynamic_binder<T>&, const Context&>, int> = 0>
    explicit registrar(F&& configure) { register_configured(registrar_auto_name<T>(), std::forward<F>(configure)); }
};

// Specialization for no context
template<typename T, typename BasesTag>
class registrar<T, void, BasesTag> {
    static void register_simple(std::string name_str) {
        type_name_registry::instance().register_type<T>(name_str);
        serialization::try_auto_register<T>(name_str);
        register_polymorphic_bases<T>(BasesTag{});
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [name_str](engine& eng) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                builder.build();
            }
        );
    }

    template<typename F>
    static void register_configured(std::string name_str, F&& configure) {
        type_name_registry::instance().register_type<T>(name_str);
        serialization::try_auto_register<T>(name_str);
        register_polymorphic_bases<T>(BasesTag{});
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [name_str, configure = std::forward<F>(configure)](engine& eng) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                configure(builder);
                builder.build();
            }
        );
    }

public:
    explicit registrar(const char* name) { register_simple(std::string(name)); }

    registrar() { register_simple(registrar_auto_name<T>()); }

    template<typename F>
    registrar(const char* name, F&& configure) { register_configured(std::string(name), std::forward<F>(configure)); }

    // Auto-named + configuration lambda. enable_if keeps a string literal from binding
    // here instead of the explicit-name constructor above.
    template<typename F, std::enable_if_t<std::is_invocable_v<F&, dynamic_binder<T>&>, int> = 0>
    explicit registrar(F&& configure) { register_configured(registrar_auto_name<T>(), std::forward<F>(configure)); }
};

#if JAI_HAS_NTTP_STRING && JAI_HAS_CONCEPTS
// ============================================================================
// C++20 registrar with NTTP string (compile-time name)
// ============================================================================

template<typename T, fixed_string Name, typename Context = void>
class nttp_registrar;

// Primary template - with context
template<typename T, fixed_string Name, typename Context>
class nttp_registrar {
public:
    // Default: auto_bind + build
    nttp_registrar() {
        type_name_registry::instance().register_type<T>(Name.view());
        serialization::try_auto_register<T>(Name.str());
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng, const Context&) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    template<typename F>
        requires std::invocable<F, dynamic_binder<T>&, const Context&>
    explicit nttp_registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
        serialization::try_auto_register<T>(Name.str());
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [configure = std::forward<F>(configure)](engine& eng, const Context& ctx) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                configure(builder, ctx);
                builder.build();
            }
        );
    }
};

// Specialization for no context
template<typename T, fixed_string Name>
class nttp_registrar<T, Name, void> {
public:
    // Default: auto_bind + build
    nttp_registrar() {
        type_name_registry::instance().register_type<T>(Name.view());
        serialization::try_auto_register<T>(Name.str());
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    template<typename F>
        requires std::invocable<F, dynamic_binder<T>&>
    explicit nttp_registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
        serialization::try_auto_register<T>(Name.str());
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [configure = std::forward<F>(configure)](engine& eng) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                configure(builder);
                builder.build();
            }
        );
    }
};

#endif // JAI_HAS_NTTP_STRING && JAI_HAS_CONCEPTS

// ============================================================================
// bind_registrar - Trigger all registered bindings
// ============================================================================

template<typename Context>
void bind_registrar(engine& eng, const Context& ctx) {
    registrar_registry<Context>::instance().register_all(eng, ctx);
}

inline void bind_registrar(engine& eng) {
    registrar_registry<void>::instance().register_all(eng);
}

} // namespace jai

#include <jaiscript/core/dynamic_binder.hpp>
// Include polymorphic registry for auto-registration of polymorphic types, plus the
// any_archive dispatch() definitions its save/load factories instantiate (see the note
// at the bottom of polymorphic.hpp).
#include <jaiscript/serialization/polymorphic.hpp>
#include <jaiscript/serialization/archive_dispatch.hpp>

#endif // __JAISCRIPT_CORE_REGISTRAR_HPP__
