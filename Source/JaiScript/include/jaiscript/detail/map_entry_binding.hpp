#pragma once

// A range-for over a map, for both backends: the structured binding's recognition rule, the cursor its names read their halves
// from, and the pair a plain loop variable holds.

#include <jaiscript/core/value.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/environment.hpp>
#include <optional>

namespace jai::detail {

    // A structured binding's half (member_expr::entry_half): the key, the value's copy, or a reference to the entry
    inline constexpr uint8_t entry_half_key = 1, entry_half_value = 2, entry_half_reference = 3;

    // for (auto [k, v] : m): the parser's shape - the hidden entry# loop variable, a body block whose first two declarations
    // read entry#.first and entry#.second - marks the loop and each half's read. The parser and the .jaibite reader both run it.
    inline void stamp_map_entry_bindings(range_for_stmt& loop) {
        if (loop.variable_name != "entry#" || !loop.body || loop.body->get_type() != node_type::block_stmt) { return; }
        auto& decls = static_cast<block_stmt&>(*loop.body).declarations;
        if (decls.size() < 2) { return; }
        member_expr* halves[2] = {nullptr, nullptr};
        const char* names[2] = {"first", "second"};
        bool value_reference = false;
        for (size_t i = 0; i < 2; ++i) {
            if (!decls[i] || decls[i]->get_type() != node_type::variable_decl) { return; }
            const auto& decl = static_cast<variable_decl&>(*decls[i]);
            if (i == 1) { value_reference = decl.type && decl.type->base_type == script_value_type::jai_reference_type; }
            const auto& init = decl.initializer;
            if (!init || init->get_type() != node_type::member_expr) { return; }
            auto* m = static_cast<member_expr*>(init.get());
            if (m->is_static || m->member != names[i] || !m->object || m->object->get_type() != node_type::identifier_expr) { return; }
            if (static_cast<const identifier_expr&>(*m->object).name != loop.variable_name) { return; }
            halves[i] = m;
        }
        halves[0]->entry_half = entry_half_key;
        halves[1]->entry_half = value_reference ? entry_half_reference : entry_half_value;
        loop.binds_map_entry = true;
    }

    // The structured loop variable over a map holds the iteration's entry as a map-entry reference carrying the map's value
    // type. The holder the variable alone owns is re-aimed at the next entry (true); otherwise the caller mints one.
    inline bool reaim_map_entry_cursor(script_value* cell, const strong_ptr<script_map>& map, const script_value& key, const type_info_ptr& value_type) {
        auto* holder = cell ? cell->map_entry_cursor_holder() : nullptr;
        if (!holder || holder->container_map.get() != map.get()) { return false; }
        *holder->cell() = key;
        holder->container_element_type = value_type;
        return true;
    }

    // A half of the entry a cursor names, as the binding's declaration takes it: the key and, by value, the value (the
    // declaration copies them), or by reference a reference to the entry with the map's value type. Null when the loop
    // variable holds no cursor (the container was not a map): the general member read decides.
    inline std::optional<script_value> map_entry_half(const script_value& cell, uint8_t half, engine* eng) {
        const auto* holder = cell.raw_storage_index() == script_value::TYPEID_REFERENCE ? cell.get_reference_holder() : nullptr;
        if (!holder || !holder->has_map_key) { return std::nullopt; }
        const script_value& key = *holder->cell();
        if (half == entry_half_key) { return key; }
        if (half == entry_half_value) { return cell.deref(); }
        const auto* entry = holder->container_map->lookup(key);
        if (!entry) { return cell.deref(); }   // gone: deref raises the removed-entry text
        return script_value::make_map_entry_reference(holder->container_map, key, &entry->second, eng, holder->container_element_type);
    }

    // The halves of a plain loop variable's pair: by value the key (a string shares its node) and a copy of the value, by
    // reference the key and a reference to the entry with the map's value type
    inline void map_entry_pair_halves(const strong_ptr<script_map>& map, const script_map::value_type& entry, bool by_reference,
                                      const type_info_ptr& value_type, engine* eng, script_value& first, script_value& second) {
        if (by_reference) {
            first = entry.first;
            second = script_value::make_map_entry_reference(map, entry.first, &entry.second, eng, value_type);
        } else {
            first = entry.first.is_string() ? entry.first : entry.first.clone();
            second = clone_for_assignment(entry.second);
        }
    }

    // A plain loop variable's pair: the pair the variable alone owns takes the next entry's halves in place (true) (engine.cpp)
    bool reuse_map_entry_pair(script_value* cell, const class_definition* pair_class, script_value& first, script_value& second);
    // A new pair of the library's class, whatever the name `pair` means where the loop runs (engine.cpp)
    script_value mint_map_entry_pair(const std::shared_ptr<class_definition>& pair_class, script_value&& first, script_value&& second, engine* eng);
    // The library's pair class, or null when the library is not loaded (engine.cpp)
    std::shared_ptr<class_definition> map_entry_pair_class(engine& eng);

    JAI_NOINLINE inline checked_result<void> map_entry_pair_missing() {
        return checked_result<void>(make_error_code(runtime_error_code::stdlib_not_loaded), "'pair' type not registered - make sure stdlib is loaded");
    }

} // namespace jai::detail
