#pragma once

// Helper macros for removing parentheses (matches MV_PROPERTY behavior)
#define JAI_EXPAND(x) x
#define JAI_REMOVE_PARENS_IMPL(...) __VA_ARGS__
#define JAI_REMOVE_PARENS(x) JAI_EXPAND(JAI_REMOVE_PARENS_IMPL x)

// Main property macros
// Note: Types MUST be wrapped in parentheses: JAI_PROPERTY((int), x) or JAI_PROPERTY((std::map<int,std::string>), mymap)
// This matches MV_PROPERTY behavior for easier migration
#define JAI_PROPERTY(type, name, ...) \
    jai::property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name, ##__VA_ARGS__ }

#define JAI_DELETED_PROPERTY(type, name) \
    jai::deleted_property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name }

// Named property macros (different property name vs variable name)
#define JAI_NAMED_PROPERTY(type, prop_name, var_name, ...) \
    jai::property<JAI_REMOVE_PARENS(type)> var_name{ property_mgr, prop_name, ##__VA_ARGS__ }

// Observable property macro (requires signals to be implemented first)
// This will be added after we migrate signals to JaiScript
// #define JAI_OBSERVABLE_PROPERTY(type, name, ...) \
//     jai::observable_property<JAI_REMOVE_PARENS(type)> name{ property_mgr, #name, ##__VA_ARGS__ }
