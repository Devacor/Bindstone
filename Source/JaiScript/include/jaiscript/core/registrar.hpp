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
        type_names_[ti] = std::string(name);
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
// At engine initialization:
//   jai::bind_registrar(engine);                     // No context
//   jai::bind_registrar<Services>(engine, services); // With context

// ============================================================================
// C++17-compatible registrar (runtime name)
// ============================================================================

// Primary template - with context
template<typename T, typename Context>
class registrar {
public:
    // Default: auto_bind + build
    explicit registrar(const char* name) {
        std::string name_str(name);
        type_name_registry::instance().register_type<T>(name);
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [name_str](engine& eng, const Context&) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (dynamic_binder<T>&, const Context&) -> void
    template<typename F>
    registrar(const char* name, F&& configure) {
        std::string name_str(name);
        type_name_registry::instance().register_type<T>(name);
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
};

// Specialization for no context
template<typename T>
class registrar<T, void> {
public:
    // Default: auto_bind + build
    explicit registrar(const char* name) {
        std::string name_str(name);
        type_name_registry::instance().register_type<T>(name);
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [name_str](engine& eng) {
                dynamic_binder<T> builder(eng, name_str);
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (dynamic_binder<T>&) -> void
    template<typename F>
    registrar(const char* name, F&& configure) {
        std::string name_str(name);
        type_name_registry::instance().register_type<T>(name);
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
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng, const Context&) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (dynamic_binder<T>&, const Context&) -> void
    template<typename F>
        requires std::invocable<F, dynamic_binder<T>&, const Context&>
    explicit nttp_registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
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
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng) {
                dynamic_binder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (dynamic_binder<T>&) -> void
    template<typename F>
        requires std::invocable<F, dynamic_binder<T>&>
    explicit nttp_registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
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

// Include dynamic_binder.hpp to make registrar lambdas work
#include <jaiscript/core/dynamic_binder.hpp>

#endif // __JAISCRIPT_CORE_REGISTRAR_HPP__
