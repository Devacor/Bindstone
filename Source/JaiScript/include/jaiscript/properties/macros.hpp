#pragma once

#include <jaiscript/properties/property_schema.hpp>
#include <cstddef>

// Helper macros for removing parentheses (matches MV_PROPERTY behavior)
#define JAI_EXPAND(x) x
#define JAI_REMOVE_PARENS_IMPL(...) __VA_ARGS__
#define JAI_REMOVE_PARENS(x) JAI_EXPAND(JAI_REMOVE_PARENS_IMPL x)

// Helper to get offset of a member within a class
#define JAI_OFFSETOF(type, member) offsetof(type, member)

// ============================================================================
// Property Macros
// ============================================================================
//
// Usage (inside a class inheriting from property_owner<MyClass>):
//   JAI_PROPERTY((int), health, 100);
//   JAI_PROPERTY((std::string), name);
//   JAI_PROPERTY((std::vector<int>), scores);
//
// Note: Types MUST be wrapped in parentheses to handle commas in template types.
// This matches MV_PROPERTY behavior for easier migration.
//
// The macro automatically registers properties to:
// 1. Per-instance property_manager (for runtime access)
// 2. Type-level schema via type_registry (for static reflection)

// Schema registration helper - registers to type_registry using _jai_owner_type.
// Typed value access goes through property_base's erased bridge at runtime; the schema
// carries only identity + flags.
#define JAI_SCHEMA_REGISTER(prop_type, name, is_obs, allow_ser) \
    inline static const int _jai_schema_reg_##name = []{ \
        using OwnerT = _jai_owner_type; \
        using ValueT = prop_type; \
        jai::type_registry::instance().for_type<OwnerT>().add( \
            jai::property_meta{ \
                #name, \
                std::type_index(typeid(ValueT)), \
                allow_ser, \
                is_obs \
            } \
        ); \
        return 0; \
    }()

// Main property macro
#define JAI_PROPERTY(type, name, ...) \
    JAI_SCHEMA_REGISTER(JAI_REMOVE_PARENS(type), name, false, true); \
    jai::property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name, ##__VA_ARGS__ }

// Transient property — registered for runtime access but never serialized.
// Use for derived/reconstructed values (cached bounds, computed matrices, etc.)
// that are cheaper to recompute at load time than to store and parse.
// Optional default value: JAI_TRANSIENT_PROPERTY((int), myCache, 0);
#define JAI_TRANSIENT_PROPERTY(type, name, ...) \
    JAI_SCHEMA_REGISTER(JAI_REMOVE_PARENS(type), name, false, false); \
    jai::property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name, ##__VA_ARGS__, jai::serialize_mode::transient }

// Deleted property - placeholder for removed properties during deserialization
#define JAI_DELETED_PROPERTY(type, name) \
    JAI_SCHEMA_REGISTER(JAI_REMOVE_PARENS(type), name, false, false); \
    jai::deleted_property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name }

// Named property - different serialization name vs variable name
#define JAI_NAMED_PROPERTY(type, prop_name, var_name, ...) \
    inline static const int _jai_schema_reg_##var_name = []{ \
        using OwnerT = _jai_owner_type; \
        using ValueT = JAI_REMOVE_PARENS(type); \
        jai::type_registry::instance().for_type<OwnerT>().add( \
            jai::property_meta{ \
                prop_name, \
                std::type_index(typeid(ValueT)), \
                true, \
                false \
            } \
        ); \
        return 0; \
    }(); \
    jai::property<JAI_REMOVE_PARENS(type)> var_name{ property_mgr, prop_name, ##__VA_ARGS__ }

// ============================================================================
// Non-Macro Alternative (for reference)
// ============================================================================
//
// Unfortunately, C++ doesn't have compile-time reflection yet, so we can't
// automatically discover member names. The closest alternatives are:
//
// 1. Repeat the name (verbose):
//    jai::property<int> health{property_mgr, "health", 100};
//
// 2. Use a registration function (separates declaration from registration):
//    jai::property<int> health;
//    void register_props() { health.init(property_mgr, "health", 100); }
//
// The macro remains the most concise and least error-prone option.
//
// ============================================================================

// ============================================================================
// Observable property macros
// ============================================================================
//
// Observable properties emit a signal when their value changes.
// Requires: #include <jaiscript/properties/observable_property.hpp>
//
// Usage:
//   class Player : public jai::property_owner<Player> {
//   public:
//       JAI_OBSERVABLE_PROPERTY((int), health, 100);
//
//       Player() {
//           health.on_change.connect([](int old_val, int new_val) {
//               std::cout << "Health: " << old_val << " -> " << new_val << "\n";
//           });
//       }
//   };

#define JAI_OBSERVABLE_PROPERTY(type, name, ...) \
    JAI_SCHEMA_REGISTER(JAI_REMOVE_PARENS(type), name, true, true); \
    jai::observable_property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name, ##__VA_ARGS__ }
