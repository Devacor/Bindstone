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
#include <concepts>

namespace jai {

// Forward declarations
class engine;
template<typename T> class class_builder;

// ============================================================================
// fixed_string - C++20 NTTP-compatible compile-time string
// ============================================================================

template<std::size_t N>
struct fixed_string {
    char value[N]{};

    constexpr fixed_string() = default;

    constexpr fixed_string(const char (&str)[N]) noexcept {
        std::copy_n(str, N, value);
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return {value, N - 1};
    }

    [[nodiscard]] constexpr operator std::string_view() const noexcept {
        return view();
    }

    [[nodiscard]] std::string str() const {
        return std::string(value, N - 1);
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return N - 1;
    }

    [[nodiscard]] constexpr const char* c_str() const noexcept {
        return value;
    }
};

template<std::size_t N>
fixed_string(const char (&)[N]) -> fixed_string<N>;

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
// jai::registrar - C++20 static type registration with compile-time name
// ============================================================================
//
// Usage:
//
//   // Simple - just auto_bind the type
//   static jai::registrar<Player, "Player"> _player;
//
//   // With customization
//   static jai::registrar<Player, "Player"> _player([](auto& builder) {
//       builder.method("heal", &Player::heal);
//   });
//
//   // With context
//   static jai::registrar<Player, "Player", Services> _player([](auto& builder, const Services& ctx) {
//       builder.method("getService", [&ctx](Player&) { return ctx.get(); });
//   });
//
//   // At engine initialization:
//   jai::bind_registrar(engine);                     // No context
//   jai::bind_registrar<Services>(engine, services); // With context

template<typename T, fixed_string Name, typename Context = void>
class registrar;

// Primary template - with context
template<typename T, fixed_string Name, typename Context>
class registrar {
public:
    // Default: auto_bind + build
    registrar() {
        type_name_registry::instance().register_type<T>(Name.view());
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng, const Context&) {
                class_builder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (class_builder<T>&, const Context&) -> void
    template<typename F>
        requires std::invocable<F, class_builder<T>&, const Context&>
    explicit registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
        registrar_registry<Context>::instance().add(
            std::type_index(typeid(T)),
            [configure = std::forward<F>(configure)](engine& eng, const Context& ctx) {
                class_builder<T> builder(eng, Name.str());
                builder.auto_bind();
                configure(builder, ctx);
                builder.build();
            }
        );
    }
};

// Specialization for no context
template<typename T, fixed_string Name>
class registrar<T, Name, void> {
public:
    // Default: auto_bind + build
    registrar() {
        type_name_registry::instance().register_type<T>(Name.view());
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [](engine& eng) {
                class_builder<T> builder(eng, Name.str());
                builder.auto_bind();
                builder.build();
            }
        );
    }

    // With configuration lambda: (class_builder<T>&) -> void
    template<typename F>
        requires std::invocable<F, class_builder<T>&>
    explicit registrar(F&& configure) {
        type_name_registry::instance().register_type<T>(Name.view());
        registrar_registry<void>::instance().add(
            std::type_index(typeid(T)),
            [configure = std::forward<F>(configure)](engine& eng) {
                class_builder<T> builder(eng, Name.str());
                builder.auto_bind();
                configure(builder);
                builder.build();
            }
        );
    }
};

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

// Include class_builder.hpp to make registrar lambdas work
#include <jaiscript/core/class_builder.hpp>

#endif // __JAISCRIPT_CORE_REGISTRAR_HPP__
