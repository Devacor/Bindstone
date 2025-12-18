#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/containers.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
#include <fstream>

namespace jai {

// Helper function to convert script_value_type to a human-readable string
static std::string get_type_name(script_value_type type) {
    switch (type) {
        case script_value_type::jai_null_type: return "null";
        case script_value_type::jai_int_type: return "int";
        case script_value_type::jai_float_type: return "float";
        case script_value_type::jai_string_type: return "string";
        case script_value_type::jai_char_type: return "char";
        case script_value_type::jai_bool_type: return "bool";
        case script_value_type::jai_array_type: return "array";
        case script_value_type::jai_map_type: return "map";
        case script_value_type::jai_object_type: return "object";
        case script_value_type::jai_function_type: return "function";
        case script_value_type::jai_reference_type: return "reference";
        case script_value_type::jai_shared_ptr_type: return "shared_ptr";
        case script_value_type::jai_weak_ptr_type: return "weak_ptr";
        case script_value_type::jai_any_type: return "any";
        case script_value_type::jai_invalid_type: return "invalid";
        default: return "unknown";
    }
}

// Initialize built-in method registries with interned method names for O(1) lookup
void interpreter::init_builtin_methods() {
    // Array methods
    array_methods_ = {
        {string_symbolizer_->intern("size"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_array().size()));
    }},

        {string_symbolizer_->intern("push"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "push() takes exactly one argument");
        }
        auto& arrayPtr = self.get_array_storage();
        arrayPtr->push_back(args[0].clone());  // Deep copy when pushing
        return interp->make_value();
    }},

        {string_symbolizer_->intern("pop"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "pop() takes no arguments");
        }
        auto& arrayPtr = self.get_array_storage();
        if (arrayPtr->empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot pop from empty array");
        }
        script_value last = arrayPtr->back();
        arrayPtr->pop_back();
        return last;
    }},

        {string_symbolizer_->intern("empty"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
        }
        return interp->make_value(self.as_array().empty());
    }},

        {string_symbolizer_->intern("clear"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "clear() takes no arguments");
        }
        auto& arrayPtr = self.get_array_storage();
        arrayPtr->clear();
        return interp->make_value();
    }},

        {string_symbolizer_->intern("front"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "front() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get front of empty array");
        }
        return arr.front();
    }},

        {string_symbolizer_->intern("back"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "back() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get back of empty array");
        }
        return arr.back();
    }},

        {string_symbolizer_->intern("index_of"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "index_of() takes exactly one argument");
        }
        const auto& arr = self.as_array();
        for (size_t i = 0; i < arr.size(); ++i) {
            if (arr[i] == args[0]) {
                return interp->make_value(static_cast<script_int>(i));
            }
        }
        return interp->make_value(static_cast<script_int>(-1));
    }},

        {string_symbolizer_->intern("has"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "has() takes exactly one argument");
        }
        const auto& arr = self.as_array();
        for (const auto& elem : arr) {
            if (elem == args[0]) {
                return interp->make_value(true);
            }
        }
        return interp->make_value(false);
    }},

        {string_symbolizer_->intern("contains"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        const auto& arr = self.as_array();
        for (const auto& elem : arr) {
            if (elem == args[0]) {
                return interp->make_value(true);
            }
        }
        return interp->make_value(false);
    }},

        {string_symbolizer_->intern("first"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "first() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get first of empty array");
        }
        return arr.front();
    }},

        {string_symbolizer_->intern("last"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "last() takes no arguments");
        }
        const auto& arr = self.as_array();
        if (arr.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::array_empty), "Cannot get last of empty array");
        }
        return arr.back();
    }},

        {string_symbolizer_->intern("length"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_array().size()));
    }},

        {string_symbolizer_->intern("slice"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "slice() takes exactly two arguments");
        }
        const auto& arr = self.as_array();
        script_int start = args[0].as<script_int>();
        script_int end = args[1].as<script_int>();

        // Handle negative indices
        if (start < 0) start = static_cast<script_int>(arr.size()) + start;
        if (end < 0) end = static_cast<script_int>(arr.size()) + end;

        // Clamp to valid range
        start = std::max<script_int>(0, std::min<script_int>(start, static_cast<script_int>(arr.size())));
        end = std::max<script_int>(0, std::min<script_int>(end, static_cast<script_int>(arr.size())));

        if (start > end) start = end;

        script_value result = script_value::make_array(nullptr, interp->get_engine_ref());
        auto& resultPtr = result.get_array_storage();
        for (script_int i = start; i < end; ++i) {
            resultPtr->push_back(arr[i].clone());
        }
        return result;
    }},

        {string_symbolizer_->intern("filter"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "filter() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "filter() requires a function argument");
        }

        const auto& arr = self.as_array();
        const auto& func = args[0].as_function();
        script_value result = script_value::make_array(nullptr, interp->get_engine_ref());
        auto& resultPtr = result.get_array_storage();

        for (const auto& elem : arr) {
            auto call_result = func({elem});
            if (!call_result) {
                return checked_result<script_value>(call_result.error(), call_result.message());
            }
            if (call_result.value().as<bool>()) {
                resultPtr->push_back(elem.clone());
            }
        }
        return result;
    }},

        {string_symbolizer_->intern("sort"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() > 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "sort() takes zero or one argument");
        }

        auto& arrPtr = self.get_array_storage();

        if (args.empty()) {
            // Default sort - numeric or lexicographic
            std::sort(arrPtr->begin(), arrPtr->end(), [](const script_value& a, const script_value& b) {
                if (a.is_int() && b.is_int()) {
                    return a.as<script_int>() < b.as<script_int>();
                } else if (a.is_float() && b.is_float()) {
                    return a.as<script_float>() < b.as<script_float>();
                } else if (a.is_string() && b.is_string()) {
                    return a.as<std::string>() < b.as<std::string>();
                }
                return false;
            });
        } else {
            // Custom comparator
            if (!args[0].is_function()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "sort() comparator must be a function");
            }
            const auto& comparator = args[0].as_function();
            std::sort(arrPtr->begin(), arrPtr->end(), [&comparator](const script_value& a, const script_value& b) {
                auto result = comparator({a, b});
                if (!result) return false;
                return result.value().as<bool>();
            });
        }
        return interp->make_value();
    }},

        {string_symbolizer_->intern("reverse"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reverse() takes no arguments");
        }
        auto& arrPtr = self.get_array_storage();
        std::reverse(arrPtr->begin(), arrPtr->end());
        return interp->make_value();
    }},

        {string_symbolizer_->intern("remove"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove() takes exactly one argument");
        }
        auto& arrPtr = self.get_array_storage();
        script_int index = args[0].as<script_int>();

        if (index < 0 || index >= static_cast<script_int>(arrPtr->size())) {
            return interp->make_value(false);
        }

        arrPtr->erase(arrPtr->begin() + index);
        return interp->make_value(true);
    }},

        {string_symbolizer_->intern("remove_if"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_if() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_if() requires a function argument");
        }

        auto& arrPtr = self.get_array_storage();
        const auto& predicate = args[0].as_function();
        script_int removed_count = 0;

        for (auto it = arrPtr->begin(); it != arrPtr->end(); ) {
            auto call_result = predicate({*it});
            if (!call_result) {
                return checked_result<script_value>(call_result.error(), call_result.message());
            }
            if (call_result.value().as<bool>()) {
                it = arrPtr->erase(it);
                ++removed_count;
            } else {
                ++it;
            }
        }
        return interp->make_value(removed_count);
    }}
    };

    // Map methods
    map_methods_ = {
        {string_symbolizer_->intern("size"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_map().size()));
    }},

        {string_symbolizer_->intern("empty"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
        }
        return interp->make_value(self.as_map().empty());
    }},

        {string_symbolizer_->intern("clear"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "clear() takes no arguments");
        }
        auto& mapPtr = self.get_map_storage();
        mapPtr->clear();
        return interp->make_value();
    }},

        {string_symbolizer_->intern("contains"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        const auto& map = self.as_map();
        return interp->make_value(map.find(args[0]) != map.end());
    }},

        {string_symbolizer_->intern("erase"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "erase() takes exactly one argument");
        }
        auto& mapPtr = self.get_map_storage();
        mapPtr->erase(args[0]);
        return interp->make_value();
    }},

        {string_symbolizer_->intern("keys"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "keys() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->get_engine_ref());
        auto& arrayPtr = result.get_array_storage();
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(key.clone());
        }
        return result;
    }},

        {string_symbolizer_->intern("values"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "values() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->get_engine_ref());
        auto& arrayPtr = result.get_array_storage();
        arrayPtr->reserve(map.size());
        for (const auto& [key, value] : map) {
            arrayPtr->push_back(value.clone());
        }
        return result;
    }},

        {string_symbolizer_->intern("has"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "has() takes exactly one argument");
        }
        const auto& map = self.as_map();
        return interp->make_value(map.find(args[0]) != map.end());
    }},

        {string_symbolizer_->intern("get"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "get() takes exactly two arguments (key, default)");
        }
        const auto& map = self.as_map();
        auto it = map.find(args[0]);
        if (it != map.end()) {
            return it->second;
        }
        return args[1];  // Return default value
    }},

        {string_symbolizer_->intern("length"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_map().size()));
    }},

        {string_symbolizer_->intern("remove"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove() takes exactly one argument");
        }
        auto& mapPtr = self.get_map_storage();
        auto it = mapPtr->find(args[0]);
        if (it != mapPtr->end()) {
            mapPtr->erase(it);
            return interp->make_value(true);
        }
        return interp->make_value(false);
    }},

        {string_symbolizer_->intern("remove_if"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "remove_if() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "remove_if() requires a function argument");
        }

        auto& mapPtr = self.get_map_storage();
        const auto& predicate = args[0].as_function();
        script_int removed_count = 0;

        for (auto it = mapPtr->begin(); it != mapPtr->end(); ) {
            auto call_result = predicate({it->first, it->second});
            if (!call_result) {
                return checked_result<script_value>(call_result.error(), call_result.message());
            }
            if (call_result.value().as<bool>()) {
                it = mapPtr->erase(it);
                ++removed_count;
            } else {
                ++it;
            }
        }
        return interp->make_value(removed_count);
    }},

        {string_symbolizer_->intern("filter"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "filter() takes exactly one argument");
        }
        if (!args[0].is_function()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "filter() requires a function argument");
        }

        const auto& map = self.as_map();
        const auto& predicate = args[0].as_function();
        script_value result = script_value::make_map(nullptr, nullptr, interp->get_engine_ref());
        auto& resultPtr = result.get_map_storage();

        for (const auto& [key, value] : map) {
            auto call_result = predicate({key, value});
            if (!call_result) {
                return checked_result<script_value>(call_result.error(), call_result.message());
            }
            if (call_result.value().as<bool>()) {
                (*resultPtr)[key.clone()] = value.clone();
            }
        }
        return result;
    }},

        {string_symbolizer_->intern("to_array"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "to_array() takes no arguments");
        }
        const auto& map = self.as_map();
        script_value result = script_value::make_array(nullptr, interp->get_engine_ref());
        auto& arrayPtr = result.get_array_storage();
        arrayPtr->reserve(map.size());

        // Return array of [key, value] pairs
        for (const auto& [key, value] : map) {
            script_value pair = script_value::make_array(nullptr, interp->get_engine_ref());
            auto& pairPtr = pair.get_array_storage();
            pairPtr->push_back(key.clone());
            pairPtr->push_back(value.clone());
            arrayPtr->push_back(std::move(pair));
        }
        return result;
    }}
    };

    // String methods - enable str.length(), str.substr(), etc.
    string_methods_ = {
        {string_symbolizer_->intern("length"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "length() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_string().size()));
    }},

        {string_symbolizer_->intern("size"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "size() takes no arguments");
        }
        return interp->make_value(static_cast<script_int>(self.as_string().size()));
    }},

        {string_symbolizer_->intern("empty"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "empty() takes no arguments");
        }
        return interp->make_value(self.as_string().empty());
    }},

        {string_symbolizer_->intern("substr"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() < 1 || args.size() > 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "substr() takes 1 or 2 arguments (start, [length])");
        }
        const auto& str = self.as_string();
        script_int start = args[0].as<script_int>();

        // Handle negative start index
        if (start < 0) start = static_cast<script_int>(str.size()) + start;
        if (start < 0) start = 0;
        if (start >= static_cast<script_int>(str.size())) {
            return interp->make_value(std::string(""));
        }

        if (args.size() == 2) {
            script_int len = args[1].as<script_int>();
            if (len < 0) len = 0;
            return interp->make_value(str.substr(static_cast<size_t>(start), static_cast<size_t>(len)));
        }
        return interp->make_value(str.substr(static_cast<size_t>(start)));
    }},

        {string_symbolizer_->intern("find"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "find() takes exactly one argument");
        }
        const auto& str = self.as_string();
        const auto& search = args[0].as_string();
        auto pos = str.find(search);
        if (pos == std::string::npos) {
            return interp->make_value(static_cast<script_int>(-1));
        }
        return interp->make_value(static_cast<script_int>(pos));
    }},

        {string_symbolizer_->intern("contains"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 1) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "contains() takes exactly one argument");
        }
        const auto& str = self.as_string();
        const auto& search = args[0].as_string();
        return interp->make_value(str.find(search) != std::string::npos);
    }}
    };

    // Weak pointer methods
    weak_ptr_methods_ = {
        {string_symbolizer_->intern("lock"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "lock() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "lock() can only be called on weak_ptr");
        }

        auto weak_ptr = self.get_weak_ptr();
        if (auto locked = weak_ptr.lock()) {
            // Reconstruct a script_value from the locked object_holder
            // IMPORTANT: Reuse the same std::shared_ptr<object_holder> to maintain reference semantics
            script_value result(std::monostate{}, interp->get_engine_ref());

            // Preserve the original type info (including shared_ptr marker if present)
            auto weak_type_info = self.get_type_info();
            if (weak_type_info && weak_type_info->element_type()) {
                result.set_type_info(weak_type_info->element_type());
            } else {
                // Fallback: use the object type
                if (auto eng = interp->get_engine_ref().lock()) {
                    result.set_type_info(eng->get_type_info_object(locked->type_name));
                }
            }

            // Directly assign the locked shared_ptr
            // Works for both regular objects and shared_ptr<T> since they use the same storage
            result.set_object_holder(locked);

            return result;
        } else {
            // weak_ptr is expired, return null
            return interp->make_value();
        }
    }},

        {string_symbolizer_->intern("expired"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "expired() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "expired() can only be called on weak_ptr");
        }

        auto weak_ptr = self.get_weak_ptr();
        return interp->make_value(weak_ptr.expired());
    }},

        {string_symbolizer_->intern("reset"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reset() takes no arguments");
        }

        if (!self.is_weak_ptr()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), "reset() can only be called on weak_ptr");
        }

        // Reset the weak_ptr to null
        auto& weak_storage = self.get_weak_ptr_storage();
        weak_storage.reset();

        return self; // Return the reset weak_ptr
    }}
    };

    // Shared pointer methods
    shared_ptr_methods_ = {
        {string_symbolizer_->intern("reset"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "reset() takes no arguments");
        }

        // In JaiScript, all objects are internally shared_ptr<object_holder>
        // Reset it to null while preserving the shared_ptr type
        auto current_type_info = self.get_type_info();
        self = interp->make_value();
        self.set_type_info(current_type_info); // Preserve the shared_ptr<T> type

        return self; // Return the reset shared_ptr
    }},

        {string_symbolizer_->intern("use_count"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "use_count() takes no arguments");
        }

        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                // Get the use count of the underlying shared_ptr
                long count = obj_holder->data.use_count();
                return interp->make_value(static_cast<script_int>(count));
            }
        }

        // Not a valid shared_ptr
        return interp->make_value(static_cast<script_int>(0));
    }},

        {string_symbolizer_->intern("unique"), [](interpreter* interp, script_value& self, const std::vector<script_value>& args) -> checked_result<script_value> {
        if (!args.empty()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "unique() takes no arguments");
        }

        if (self.is_object()) {
            auto obj_holder = self.get_object_holder();
            if (obj_holder && obj_holder->data) {
                // Check if use count is 1
                bool is_unique = (obj_holder->data.use_count() == 1);
                return interp->make_value(is_unique);
            }
        }

        // Not a valid shared_ptr
        return interp->make_value(false);
    }}
    };
}

void environment::define(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    define(id, value);
}

void environment::define(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    define(id, std::move(value));
}

void environment::define(uint64_t id, const script_value& value) {
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.count(id) > 0) {
        // Redefining local variable - update in place via flat_lookup_
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            *(it->second) = value;
            return;
        }
    }

    // New local variable - add to stable storage
    local_storage_.push_back(value);
    flat_lookup_[id] = &local_storage_.back();  // Update/shadow in flat lookup
    local_ids_.insert(id);
}

void environment::define(uint64_t id, script_value&& value) {
    // Check if we're redefining a local variable (shadowing parent is ok)
    if (local_ids_.count(id) > 0) {
        // Redefining local variable - update in place via flat_lookup_
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            *(it->second) = std::move(value);
            return;
        }
    }

    // New local variable - add to stable storage
    local_storage_.push_back(std::move(value));
    flat_lookup_[id] = &local_storage_.back();  // Update/shadow in flat lookup
    local_ids_.insert(id);
}

checked_result<script_value> environment::get(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return get(id);
}

checked_result<script_value> environment::get(uint64_t id) const {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return this_object_;
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return *(it->second);
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            // Cache for future O(1) access
            flat_lookup_[id] = ptr;
            return *ptr;
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get field first (non-throwing)
                const script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }

                // Try to get method (non-throwing)
                script_value method = instance->get_method(id, false);
                if (!method.is_invalid()) {
                    bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                    return bound_method_storage_;
                }

                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->has_static_field(id)) {
                    return class_def->get_static_field(id);
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        // Check static fields
        if (class_def_->has_static_field(id)) {
            return class_def_->get_static_field(id);
        }

        // Check static methods (for calling other static methods)
        if (class_def_->has_static_method(id)) {
            return class_def_->get_static_method(id);
        }
    }

    // Not found anywhere
    std::string name{symbolizer_->get_string(id)};
    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<void> environment::assign(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    return assign(id, value);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return get_ref(id);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(uint64_t id) const {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::cref(this_object_);
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return std::cref(*(it->second));
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::cref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field first (return const reference)
                const script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid()) {
                    return std::cref(inst_field_ref);
                }

                // Try to get static field from class definition (return const reference)
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    const script_value* field_ptr = class_def->get_static_field_ptr(id);
                    if (field_ptr) {
                        return std::cref(*field_ptr);
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        const script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return std::cref(*field_ptr);
        }
    }

    std::string name{symbolizer_->get_string(id)};
    return checked_result<std::reference_wrapper<const script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    return get_ref(id);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(uint64_t id) {
    // For method environments, handle 'this' specially (note: this is non-const so we return mutable ref)
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return std::ref(this_object_);
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return std::ref(*(it->second));
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return std::ref(*ptr);
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field first (return non-const reference)
                script_value& inst_field_ref = instance->get_field(id, false);
                if (!inst_field_ref.is_invalid()) {
                    return std::ref(inst_field_ref);
                }

                // Try to get static field from class definition (return non-const reference)
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    script_value* field_ptr = class_def->get_static_field_ptr(id);
                    if (field_ptr) {
                        return std::ref(*field_ptr);
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return std::ref(*field_ptr);
        }
    }

    std::string name{symbolizer_->get_string(id)};
    return checked_result<std::reference_wrapper<script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<void> environment::assign(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    return assign(id, std::move(value));
}

checked_result<void> environment::assign(uint64_t id, const script_value& value) {
    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        *(it->second) = value;
        return {};
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = value;
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    // Check if this is a C++ parent property that needs setter method
                    auto class_def = instance->get_class_definition();
                    if (class_def) {
                        auto cpp_base = class_def->get_cpp_base_class();
                        if (cpp_base) {
                            uint64_t setter_id = cpp_base->get_property_setter_id(id);
                            if (setter_id != 0) {
                                auto setter = cpp_base->get_method(setter_id, false);
                                if (setter.is_function()) {
                                    std::vector<script_value> args = {this_object_, value};
                                    auto result = setter.as_function()(args);
                                    if (!result) {
                                        return checked_result<void>(result.error(), result.message());
                                    }
                                    return {};
                                }
                            }
                        }
                    }
                    instance->set_field(id, value.clone());
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_def_->has_static_field(id)) {
            if (class_def_->set_static_field(id, value.clone())) {
                return {};
            }
            // Field existed but set failed - shouldn't happen, but handle gracefully
        }
    }

    std::string name{symbolizer_->get_string(id)};
    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<void> environment::assign(uint64_t id, script_value&& value) {
    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        *(it->second) = std::move(value);
        return {};
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            *ptr = std::move(value);
            return {};
        }
    }

    // Kind-specific fallback: assign to 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                if (instance && instance->has_field(id)) {
                    // Check if this is a C++ parent property that needs setter method
                    auto class_def = instance->get_class_definition();
                    if (class_def) {
                        auto cpp_base = class_def->get_cpp_base_class();
                        if (cpp_base) {
                            uint64_t setter_id = cpp_base->get_property_setter_id(id);
                            if (setter_id != 0) {
                                auto setter = cpp_base->get_method(setter_id, false);
                                if (setter.is_function()) {
                                    std::vector<script_value> args = {this_object_, std::move(value)};
                                    auto result = setter.as_function()(args);
                                    if (!result) {
                                        return checked_result<void>(result.error(), result.message());
                                    }
                                    return {};
                                }
                            }
                        }
                    }
                    instance->set_field(id, std::move(value));
                    return {};
                }
            }
        }
    }

    // Kind-specific fallback: assign to static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        if (class_def_->has_static_field(id)) {
            if (class_def_->set_static_field(id, std::move(value))) {
                return {};
            }
            // Field existed but set failed - shouldn't happen, but handle gracefully
        }
    }

    std::string name{symbolizer_->get_string(id)};
    return checked_result<void>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

bool environment::contains(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    return contains(id);
}

bool environment::contains(uint64_t id) const {
    // Check cache first (O(1))
    if (flat_lookup_.find(id) != flat_lookup_.end()) {
        return true;
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return true;
        }
    }
    return false;
}

std::unordered_map<std::string_view, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string_view, script_value> result;
    // Iterate over local_ids_ set and look up values via flat_lookup_
    for (uint64_t id : local_ids_) {
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            result[symbolizer_->get_string(id)] = *(it->second);
        }
    }
    return result;
}

void environment::clear_values() {
    // Remove local IDs from flat_lookup_
    for (uint64_t id : local_ids_) {
        flat_lookup_.erase(id);
    }

    // Clear local storage (destroys script_values)
    local_storage_.clear();
    local_ids_.clear();
}

void environment::clear_parent_cache() {
    // Remove non-local entries (parent/field pointers that may be stale)
    // Keep local variable entries (which have stable pointers into local_storage_)
    for (auto it = flat_lookup_.begin(); it != flat_lookup_.end(); ) {
        if (local_ids_.find(it->first) == local_ids_.end()) {
            it = flat_lookup_.erase(it);
        } else {
            ++it;
        }
    }
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(new_parent);

    parent_ = new_parent;

    // Reset to standard kind and clear kind-specific fields
    kind_ = env_kind::standard;
    this_object_ = script_value::make_null(std::weak_ptr<engine>{});
    class_def_.reset();
    bound_method_storage_ = script_value::make_null(std::weak_ptr<engine>{});

    // With lazy caching, we start with empty flat_lookup_ (no copy needed)
    // Variables will be cached on first access
    flat_lookup_.clear();
}

void environment::reset_as_method(std::shared_ptr<environment> parent, script_value this_obj) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(parent);

    parent_ = parent;

    // Set to method kind with this object
    kind_ = env_kind::method;
    this_object_ = std::move(this_obj);
    class_def_.reset();
    bound_method_storage_ = script_value::make_null(std::weak_ptr<engine>{});

    // With lazy caching, we start with empty flat_lookup_
    flat_lookup_.clear();
}

void environment::reset_as_static_method(std::shared_ptr<environment> parent, std::shared_ptr<class_definition> class_def) {
    // Clear all local values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(parent);

    parent_ = parent;

    // Set to static_method kind with class definition
    kind_ = env_kind::static_method;
    this_object_ = script_value::make_null(std::weak_ptr<engine>{});
    class_def_ = class_def;
    bound_method_storage_ = script_value::make_null(std::weak_ptr<engine>{});

    // With lazy caching, we start with empty flat_lookup_
    flat_lookup_.clear();
}

std::unordered_map<std::string_view, script_value> environment::get_all_variables() const {
    // With lazy caching, we need to walk the parent chain to get all variables
    std::unordered_map<std::string_view, script_value> allVars;

    // First, get all from parent (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }

    // Then add/override with our local variables
    for (uint64_t id : local_ids_) {
        auto it = flat_lookup_.find(id);
        if (it != flat_lookup_.end()) {
            allVars[symbolizer_->get_string(id)] = *(it->second);
        }
    }
    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    // For method environments, handle 'this' specially
    if (kind_ == env_kind::method && id == symbolizer_->get_this_id()) {
        return &this_object_;
    }

    // Check cache first (O(1))
    auto it = flat_lookup_.find(id);
    if (it != flat_lookup_.end()) {
        return it->second;
    }

    // Cache miss - walk parent chain
    if (parent_) {
        script_value* ptr = parent_->get_value_ptr(id);
        if (ptr) {
            flat_lookup_[id] = ptr;
            return ptr;
        }
    }

    // Kind-specific fallback: check 'this' object fields for method environments
    if (kind_ == env_kind::method) {
        auto this_type = this_object_.type();
        if (id != symbolizer_->get_this_id() &&
            (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) &&
            !this_object_.is_null()) {
            auto obj_holder = this_object_.get_object_holder();
            if (obj_holder && obj_holder->data) {
                auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                // Try to get instance field - get_field returns a reference
                script_value& field_ref = instance->get_field(id, false);
                if (!field_ref.is_invalid()) {
                    return &field_ref;
                }

                // Try to get static field from class definition
                auto class_def = instance->get_class_definition();
                if (class_def) {
                    script_value* static_ptr = class_def->get_static_field_ptr(id);
                    if (static_ptr) {
                        return static_ptr;
                    }
                }
            }
        }
    }

    // Kind-specific fallback: check static fields for static_method environments
    if (kind_ == env_kind::static_method && class_def_) {
        script_value* field_ptr = class_def_->get_static_field_ptr(id);
        if (field_ptr) {
            return field_ptr;
        }
    }

    return nullptr;
}

// interpreter implementation

// Helper to resolve include/import paths
std::string resolve_include_path(const std::string& path, std::shared_ptr<engine> engine_ptr) {
    // First, try the path as-is (for absolute paths)
    if (std::filesystem::exists(path)) {
        return std::filesystem::canonical(path).string();
    }
    
    // Get the include paths from the engine
    auto include_paths = engine_ptr->get_include_paths();
    
    // Try each include path
    for (const auto& include_path : include_paths) {
        auto full_path = std::filesystem::path(include_path) / path;
        if (std::filesystem::exists(full_path)) {
            return std::filesystem::canonical(full_path).string();
        }
    }
    
    // Path not found
    throw runtime_error("Could not find include/import file: " + path);
}

// Note: Direct engine implementation access removed
// Import tracking will be handled by the engine's public API

// Helper to create a bound method - binds 'this' as the first argument
script_value interpreter::create_bound_method(const script_value& this_obj, const script_value& method) {
    auto engine_weak = this_obj.get_engine_ref();
    return script_value::make_function([this_obj, method](const std::vector<script_value>& args) -> checked_result<script_value> {
        // Create a new argument list with 'this' as the first argument
        std::vector<script_value> method_args;
        method_args.reserve(args.size() + 1);
        method_args.push_back(this_obj);
        method_args.insert(method_args.end(), args.begin(), args.end());

        // Call the method with 'this' included
        const auto& method_func = method.as_function();
        return method_func(method_args);
    }, engine_weak);
}

// Helper to check if an expression is an lvalue (existing object that should be cloned)
// Lvalues: identifiers, member access, subscript access
// Non-lvalues (temporaries): function calls, constructors, literals, operators
bool interpreter::is_lvalue_expression(expression* e) const {
    if (!e) return false;

    // Direct identifier - always an lvalue
    if (e->get_type() == node_type::identifier_expr) {
        return true;
    }

    // Member access - always an lvalue
    if (e->get_type() == node_type::member_expr) {
        return true;
    }

    // Array/map subscript (binary expr with left_bracket) - lvalue
    if (e->get_type() == node_type::binary_expr) {
        auto* bin = static_cast<binary_expr*>(e);
        if (bin->op.type == token_type::left_bracket) {
            return true;
        }
    }

    // Everything else (function calls, constructors, literals, etc.) is a temporary
    return false;
}

// Helper for is_truthy() to check for to_bool() method on objects
bool interpreter::object_to_bool_via_method(const script_value& value) {
    // Use get_class_instance() which safely returns nullptr if not a class instance
    auto instance = const_cast<script_value&>(value).get_class_instance();
    if (!instance) {
        return true;  // Not a class instance - treat as truthy
    }

    auto method_id = string_symbolizer_->intern("to_bool");
    auto method_val = instance->get_method(method_id, false);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        return true;  // No to_bool() method - objects are truthy by default
    }

    script_value bound = create_bound_method(value, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> no_args;
    auto result = method(no_args);
    if (result.has_value() && result.value().is_bool()) {
        return result.value().as_bool();
    }
    return true;  // Method didn't return a valid bool - treat as truthy
}

// Helper for handle_equal() to check for operator== or equals() method on objects
// Returns: nullopt if no custom equality method, true/false if method found and returned valid result
std::optional<bool> interpreter::object_equality_via_method(const script_value& left, const script_value& right) {
    // Get class instance from the left operand
    auto instance = const_cast<script_value&>(left).get_class_instance();
    if (!instance) {
        return std::nullopt;  // Not a class instance - no custom equality
    }

    // Look for "==" method - used by both script classes (operator==) and class_builder
    auto eq_id = string_symbolizer_->intern("==");
    auto method_val = instance->get_method(eq_id, false);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        return std::nullopt;  // No == method - use default reference comparison
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);  // Push by copy (const ref)

    auto result = method(args);
    if (result.has_value() && result.value().is_bool()) {
        return result.value().as_bool();
    }

    // Method didn't return a valid bool - fall back to reference comparison
    return std::nullopt;
}

// Generic helper for comparison operators (<, <=, >, >=) via custom methods
std::optional<bool> interpreter::object_comparison_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Get class instance from the left operand
    auto instance = const_cast<script_value&>(left).get_class_instance();
    if (!instance) {
        return std::nullopt;  // Not a class instance - no custom comparison
    }

    // Look for the operator method by symbol ID
    auto method_val = instance->get_method(op_symbol_id, false);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        return std::nullopt;  // No custom method - use default comparison
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);

    auto result = method(args);
    if (result.has_value() && result.value().is_bool()) {
        return result.value().as_bool();
    }

    // Method didn't return a valid bool
    return std::nullopt;
}

// Generic helper for arithmetic operators (+, -, *, /, %) via custom methods
std::optional<script_value> interpreter::object_arithmetic_via_method(const script_value& left, const script_value& right, uint64_t op_symbol_id) {
    // Get class instance from the left operand
    auto instance = const_cast<script_value&>(left).get_class_instance();
    if (!instance) {
        return std::nullopt;  // Not a class instance - no custom arithmetic
    }

    // Look for the operator method by symbol ID
    auto method_val = instance->get_method(op_symbol_id, false);
    if (method_val.is_null() || method_val.is_invalid() || !method_val.is_function()) {
        return std::nullopt;  // No custom method - use default arithmetic
    }

    // Create a bound method and call it with the right operand
    script_value bound = create_bound_method(left, method_val);
    const script_function& method = bound.as_function();
    std::vector<script_value> args;
    args.push_back(right);

    auto result = method(args);
    if (result.has_value()) {
        return result.value();
    }

    // Method call failed
    return std::nullopt;
}

interpreter::interpreter()
    : ownedSymbolizer_(std::make_unique<string_symbolizer>()),
      string_symbolizer_(ownedSymbolizer_.get()),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls
    
    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

	// Initialize cached keyword symbol IDs for fast keyword checks
	this_id_ = string_symbolizer_->intern("this");
	super_id_ = string_symbolizer_->intern("super");

	// Verify this_id_ matches symbolizer's cached ID
	if(this_id_ != string_symbolizer_->get_this_id()){
        throw std::runtime_error("this_id_ mismatch with symbolizer");
    }
	getValue_id_ = string_symbolizer_->intern("getValue");
	cpp_object_field_id_ = string_symbolizer_->intern(class_constants::CPP_OBJECT_FIELD);

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(std::make_shared<environment>(string_symbolizer_)),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls

    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

	// Initialize cached keyword symbol IDs for fast keyword checks
	this_id_ = string_symbolizer_->intern("this");
	super_id_ = string_symbolizer_->intern("super");

	// Verify this_id_ matches symbolizer's cached ID
	if (this_id_ != string_symbolizer_->get_this_id()) {
		throw std::runtime_error("this_id_ mismatch with symbolizer");
	}
	getValue_id_ = string_symbolizer_->intern("getValue");
	cpp_object_field_id_ = string_symbolizer_->intern(class_constants::CPP_OBJECT_FIELD);

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

interpreter::interpreter(string_symbolizer* external_symbolizer, std::shared_ptr<environment> global_env)
    : ownedSymbolizer_(nullptr),
      string_symbolizer_(external_symbolizer),
      environment_(global_env),
      hasReturnValue_(false),
      current_method_this_(std::monostate{}, std::weak_ptr<engine>{}) {
    // Initialize optimization pools
    argument_pool_.reserve(16);  // Reasonable default for most function calls
    environment_pool_.reserve(8);  // For nested function calls

    // Pre-populate environment pool
    for (size_t i = 0; i < 8; ++i) {
        environment_pool_.emplace_back(std::make_shared<environment>(nullptr, string_symbolizer_));
    }

    // Initialize cached type IDs for fast object type comparison
    class_definition_type_id_ = string_symbolizer_->intern("class_definition");
    weak_ptr_holder_type_id_ = string_symbolizer_->intern("weak_ptr_holder");
    shared_ptr_holder_type_id_ = string_symbolizer_->intern("shared_ptr_holder");
    weak_from_this_id_ = string_symbolizer_->intern("weak_from_this");
    shared_from_this_id_ = string_symbolizer_->intern("shared_from_this");

    // Initialize cached operator symbol IDs for fast operator overload lookup
    op_plus_id_ = string_symbolizer_->intern("+");
    op_minus_id_ = string_symbolizer_->intern("-");
    op_star_id_ = string_symbolizer_->intern("*");
    op_slash_id_ = string_symbolizer_->intern("/");
    op_percent_id_ = string_symbolizer_->intern("%");
    op_less_id_ = string_symbolizer_->intern("<");
    op_less_equal_id_ = string_symbolizer_->intern("<=");
    op_greater_id_ = string_symbolizer_->intern(">");
    op_greater_equal_id_ = string_symbolizer_->intern(">=");
    op_equal_equal_id_ = string_symbolizer_->intern("==");
    op_bang_equal_id_ = string_symbolizer_->intern("!=");
    op_spaceship_id_ = string_symbolizer_->intern("<=>");
    op_ampersand_id_ = string_symbolizer_->intern("&");
    op_pipe_id_ = string_symbolizer_->intern("|");
    op_caret_id_ = string_symbolizer_->intern("^");
    op_left_shift_id_ = string_symbolizer_->intern("<<");
    op_right_shift_id_ = string_symbolizer_->intern(">>");
    subscript_op_id_ = string_symbolizer_->intern("[]");
    assign_operator_id_ = string_symbolizer_->intern("=");

    // Initialize built-in method registries with interned method names
    init_builtin_methods();

    // Initialize binary operator dispatch table
    init_dispatch_table();
}

void interpreter::add_globals(const std::unordered_map<std::string, script_value>& globals) {
    for (const auto& [name, value] : globals) {
        environment_->define(name, value);
    }
}

void interpreter::add_global(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

std::shared_ptr<environment> interpreter::get_global_environment() const {
    // Get the global environment directly from the engine
    // This avoids issues with closures/methods capturing stale environment references
    // from different execute() calls that don't chain to the same root
    if (auto eng = engine_ref_.lock()) {
        return eng->get_global_environment();
    }
    // Fallback: walk up the parent chain (shouldn't happen if engine is alive)
    auto global_env = environment_;
    while (global_env && global_env->get_parent()) {
        global_env = global_env->get_parent();
    }
    return global_env ? global_env : environment_;
}

void interpreter::prepare_for_execution() {
    // Clear execution state
    valueStack_.clear();
    returnValue_.reset();  // No need to create a value - value_or() will create it if needed
    hasReturnValue_ = false;

    // Clear exception state
    current_exception_.reset();
    is_unwinding_ = false;
    active_exception_value_.reset();  // No need to create a value here either
    current_catch_var_id_ = 0;

    // Reset to global scope but keep all variables defined at global scope
    // Only pop scopes if we're in a nested scope
    while (environment_->parent_) {
        environment_ = environment_->parent_;
    }
    // Note: We don't clear the global environment, so variables persist between executions
}

void interpreter::push_scope() {
    environment_ = get_pooled_environment(environment_);  // Use pool instead of make_shared!
}

void interpreter::pop_scope() {
    if (environment_->parent_) {
        // Clear local values to trigger destructors before popping scope
        // This is crucial for script class destructors to run at scope exit
        auto current_env = environment_;
        environment_ = environment_->parent_;
        release_environment(current_env);
    }
}

void interpreter::define_variable(const std::string& name, const script_value& value) {
    environment_->define(name, value);
}

script_value interpreter::execute(const std::vector<declaration_ptr>& declarations) {
    // std::cerr << "DEBUG: interpreter::execute called with " << declarations.size() << " declarations\n";
    script_value last_script_value = make_value();
    hasReturnValue_ = false;  // Reset return value state

    for (size_t i = 0; i < declarations.size(); i++) {
        const auto& decl = declarations[i];
        // std::cerr << "  Declaration " << i << " type: " << typeid(*decl).name() << "\n";

        // Execute declaration with exception handling
        try {
            // std::cerr << "  About to visit declaration " << i << "\n";
            auto result = dispatch_decl(decl.get());
            if (!result) [[unlikely]] {
                // Convert error code to exception at boundary
                // Include the custom error message if available
                if (!result.message().empty()) {
                    throw std::system_error(result.error(), result.message());
                } else {
                    throw std::system_error(result.error());
                }
            }
            // std::cerr << "  Finished visiting declaration " << i << "\n";
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
        }

        // Check if we're unwinding due to an uncaught exception
        if (is_unwinding_) {
            // Stop executing further declarations
            break;
        }
        
        // Check if this is an implicit return expression
        if (decl->get_type() == node_type::expression_decl) {
            auto* expr_decl = static_cast<expression_decl*>(decl.get());
            if (expr_decl->implicit_return && !valueStack_.empty()) {
                last_script_value = pop_value();
                // Dereference in case it's a reference (for expressions like m["key"] that return references)
                last_script_value = last_script_value.deref();
            }
        }
        
        // Clear any remaining values on the stack (from non-implicit expressions)
        while (!valueStack_.empty()) {
            pop_value();
        }
        
        // If we hit a return statement, break out of execution
        if (hasReturnValue_) {
            reset_environment_pool();  // Reset pool for next execution
            return returnValue_.value();
        }
    }
    


    reset_environment_pool();  // Reset pool for next execution
    return last_script_value;
}

script_value interpreter::evaluate(expression_ptr expr) {
    auto result = dispatch_expr(expr.get());
    if (!result) {
        // Return null on error
        return make_value();
    }
    return pop_value();
}

// Variable access methods
script_value interpreter::get_variable(const std::string& name) const {
    auto result = environment_->get(name);
    if (!result) {
        throw runtime_error(result.message());
    }
    return result.value().deref();
}

bool interpreter::has_variable(const std::string& name) const {
    return environment_->contains(name);
}

std::unordered_map<std::string_view, script_value> interpreter::get_all_variables() const {
    // Since we should be at root scope after execution, just return local variables
    return environment_->get_local_variables();
}


// expression visitors
checked_result<void> interpreter::visit_literal_expr(literal_expr* expr) {
    // Literals are created at parse time without engine references and type_info set to nullptr
    // Extract raw values from storage and recreate with proper type_info
    // We can't use as_int()/as_string() etc because they check type() which returns jai_null_type for AST literals

    // Access the raw storage variant directly
    const auto& storage = expr->value.get_storage();

    // Determine type from variant index and extract + recreate value
    switch (storage.index()) {
        case 1:  // script_int
            push_value(make_value(std::get<script_int>(storage)));
            break;
        case 2:  // script_float
            push_value(make_value(std::get<script_float>(storage)));
            break;
        case 3:  // script_string
            push_value(make_value(std::get<script_string>(storage)));
            break;
        case 4:  // script_char
            push_value(make_value(std::get<script_char>(storage)));
            break;
        case 5:  // script_bool
            push_value(make_value(std::get<script_bool>(storage)));
            break;
        case 0:  // std::monostate (null)
            push_value(make_value());
            break;
        default:
            // For other types, try to set engine ref (though this shouldn't happen with literals)
            expr->value.set_engine_ref(engine_ref_);
            push_value(expr->value);
            break;
    }
    return checked_result<void>();
}

checked_result<void> interpreter::visit_identifier_expr(identifier_expr* expr) {
    if (expr->symbol_id == getValue_id_) {
    }
    // Check if this identifier is the current catch variable (fast symbol_id comparison)
    if (current_catch_var_id_ != 0 && expr->symbol_id == current_catch_var_id_) {
        push_value(active_exception_value_.value());
        return checked_result<void>();
    }
    
    // Special handling for type constructors like weak_ptr<T>, shared_ptr<T>
    if (expr->name.find("weak_ptr<") == 0 || expr->name.find("shared_ptr<") == 0) {
        // This is a type constructor being used as a function
        // Extract the base type name (weak_ptr or shared_ptr)
        size_t pos = expr->name.find('<');
        std::string base_type = expr->name.substr(0, pos);
        
        // Look up the constructor function for this type
        auto ctor_result = environment_->get(base_type);
        if (ctor_result && ctor_result.value().is_function()) {
            push_value(std::move(ctor_result.value()));
            return checked_result<void>();
        }
        // Fall through to normal error handling if not found
    }
    
    // Use parser's pre-computed symbol ID (always set by parser)
    // With lazy caching, environment_ will cache lookups automatically
    auto ref_result = environment_->get_ref(expr->symbol_id);
    if (ref_result) {
        const script_value& val = ref_result.value().get();
        push_value(val.deref());  // Automatically handles references
    } else {
        // If we're in a class method context, collect unresolved identifier
        if (current_class_context_ && current_class_context_->in_method) {
            // Add to unresolved identifiers for later validation (using pre-computed ID)
            current_class_context_->unresolved_identifiers.insert(expr->symbol_id);
            // Push a placeholder value to continue parsing
            push_value(make_value());
            return checked_result<void>();
        }


        // Variable not found - check if it's a member of 'this'
        auto this_result = environment_->get(string_symbolizer_->get_this_id());
        if (this_result) {
            script_value this_val = std::move(this_result.value());
            if (this_val.is_object()) {
                // Try to access as a member of 'this'
                auto obj_holder = this_val.get_object_holder();

                // Both C++ and script classes wrap data in class_instance (script_class_instance inherits from class_instance)
                // is_class_instance_wrapper should be true for both
                std::shared_ptr<class_instance> instance = obj_holder->is_class_instance_wrapper
                    ? std::static_pointer_cast<class_instance>(obj_holder->data)
                    : nullptr;

                if (instance) {
                    // Intern the name to ID
                    uint64_t name_id = string_symbolizer_->intern(expr->name);

                    // Check instance fields first
                    if (instance->has_field(name_id)) {
                        push_value(instance->get_field(name_id));
                        return checked_result<void>();
                    }

                    // Check for methods (returns bound method)
                    script_value method = instance->get_method(name_id, false);
                    if (!method.is_invalid()) {
                        script_value bound_method = create_bound_method(this_val, method);
                        push_value(std::move(bound_method));
                        return checked_result<void>();
                    }

                    // Check for static fields of the class
                    auto class_def = instance->get_class_definition();
                    if (class_def && class_def->has_static_field(expr->symbol_id)) {
                        push_value(class_def->get_static_field(expr->symbol_id));
                        return checked_result<void>();
                    }
                }
            }
        }

        // Use error code instead of exception state - include variable name for debugging
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
            "Undefined variable '" + expr->name + "'");
    }
    return checked_result<void>();
}

checked_result<void> interpreter::visit_binary_expr(binary_expr* expr) {
    // Track if we've pre-fetched values to avoid duplicate lookups
    bool already_have_values = false;
    std::optional<script_value> pre_fetched_left, pre_fetched_right;

	// FAST PATH: identifier + identifier (e.g., "a + b", "sum + i") - eliminates 2 virtual calls
	// Skip logical operators - they need short-circuit evaluation
	// Skip if we're in a catch block - catch variables need special handling
	if (expr->op.type != token_type::ampersand_ampersand && expr->op.type != token_type::pipe_pipe && current_catch_var_id_ == 0) {
		if (expr->left->get_type() == node_type::identifier_expr) {
			auto* leftId = static_cast<identifier_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				// Both operands are simple identifiers - direct variable lookup without AST traversal

				// Get both values directly from environment
				auto leftResult = environment_->get(leftId->symbol_id);
				if (!leftResult) {
					return checked_result<void>(leftResult.error(), leftResult.message());
				}
				script_value leftVal = std::move(leftResult.value()).deref();

				auto rightResult = environment_->get(rightId->symbol_id);
				if (!rightResult) {
					return checked_result<void>(rightResult.error(), rightResult.message());
				}
				script_value rightVal = std::move(rightResult.value()).deref();

				// Fast path for integer arithmetic (most common in loops)
				if (can_use_fast_path(expr->op.type)) {
					auto leftType = leftVal.type();
					auto rightType = rightVal.type();

					if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::plus:
							push_value(make_value(leftInt + rightInt));
							return {};
						case token_type::minus:
							push_value(make_value(leftInt - rightInt));
							return {};
						case token_type::star:
							push_value(make_value(leftInt * rightInt));
							return {};
						case token_type::slash:
							if (rightInt == 0) {
								return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
							}
							push_value(make_value(leftInt / rightInt));
							return {};
						case token_type::percent:
							if (rightInt == 0) {
								return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));
							}
							push_value(make_value(leftInt % rightInt));
							return {};
						case token_type::less:
							push_value(make_value(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_value(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_value(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_value(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_value(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_value(leftInt != rightInt));
							return {};
						default:
							break; // Fall through to normal path
						}
					}
					// Fast path for float/mixed arithmetic
					else if ((leftType == script_value_type::jai_int_type || leftType == script_value_type::jai_float_type) &&
						(rightType == script_value_type::jai_int_type || rightType == script_value_type::jai_float_type)) {
						script_float leftFloat = leftType == script_value_type::jai_int_type ?
							static_cast<script_float>(leftVal.unchecked_as_int()) : leftVal.unchecked_as_float();
						script_float rightFloat = rightType == script_value_type::jai_int_type ?
							static_cast<script_float>(rightVal.unchecked_as_int()) : rightVal.unchecked_as_float();

						switch (expr->op.type) {
						case token_type::plus:
							push_value(make_value(leftFloat + rightFloat));
							return {};
						case token_type::minus:
							push_value(make_value(leftFloat - rightFloat));
							return {};
						case token_type::star:
							push_value(make_value(leftFloat * rightFloat));
							return {};
						case token_type::slash:
							if (rightFloat == 0.0) {
								return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
							}
							push_value(make_value(leftFloat / rightFloat));
							return {};
						case token_type::percent:
							if (rightFloat == 0.0) {
								return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));
							}
							push_value(make_value(std::fmod(leftFloat, rightFloat)));
							return {};
						case token_type::less:
							push_value(make_value(leftFloat < rightFloat));
							return {};
						case token_type::less_equal:
							push_value(make_value(leftFloat <= rightFloat));
							return {};
						case token_type::greater:
							push_value(make_value(leftFloat > rightFloat));
							return {};
						case token_type::greater_equal:
							push_value(make_value(leftFloat >= rightFloat));
							return {};
						case token_type::equal_equal:
							push_value(make_value(leftFloat == rightFloat));
							return {};
						case token_type::bang_equal:
							push_value(make_value(leftFloat != rightFloat));
							return {};
						default:
							break;
						}
					}
					// Fast path for string concatenation (common operation)
					else if (expr->op.type == token_type::plus &&
						leftType == script_value_type::jai_string_type &&
						rightType == script_value_type::jai_string_type) {
						const script_string& leftStr = leftVal.unchecked_as_string();
						const script_string& rightStr = rightVal.unchecked_as_string();
						push_value(make_value(leftStr + rightStr));
						return {};
					}
				}

				// Fast path didn't handle it - save values to avoid re-fetching
				pre_fetched_left = std::move(leftVal);
				pre_fetched_right = std::move(rightVal);
				already_have_values = true;
			}
			// FAST PATH 2: identifier + literal (e.g., "i < 100", "x + 5") - most common loop condition!
			else if (expr->right->get_type() == node_type::literal_expr) {
				auto* rightLit = static_cast<literal_expr*>(expr->right.get());
				// Get left value from environment, right value is already in AST
				auto leftResult = environment_->get(leftId->symbol_id);
				if (!leftResult) {
					return checked_result<void>(leftResult.error(), leftResult.message());
				}
				script_value leftVal = std::move(leftResult.value()).deref();
				const script_value& rightVal = rightLit->value;  // Direct access - no lookup!

				// Ultra-fast integer path (most common for loop conditions)
				if (can_use_fast_path(expr->op.type)) {
					// Use raw_storage_index for fastest type check
					size_t leftIdx = leftVal.raw_storage_index();
					size_t rightIdx = rightVal.raw_storage_index();

					if (leftIdx == 1 && rightIdx == 1) {  // Both ints
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_value(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_value(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_value(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_value(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_value(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_value(leftInt != rightInt));
							return {};
						case token_type::plus:
							push_value(make_value(leftInt + rightInt));
							return {};
						case token_type::minus:
							push_value(make_value(leftInt - rightInt));
							return {};
						case token_type::star:
							push_value(make_value(leftInt * rightInt));
							return {};
						case token_type::slash:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
							push_value(make_value(leftInt / rightInt));
							return {};
						case token_type::percent:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));
							push_value(make_value(leftInt % rightInt));
							return {};
						default:
							break;
						}
					}
					// Float/mixed numeric path
					else if ((leftIdx == 1 || leftIdx == 2) && (rightIdx == 1 || rightIdx == 2)) {
						script_float leftFloat = leftIdx == 1 ?
							static_cast<script_float>(leftVal.unchecked_as_int()) : leftVal.unchecked_as_float();
						script_float rightFloat = rightIdx == 1 ?
							static_cast<script_float>(rightVal.unchecked_as_int()) : rightVal.unchecked_as_float();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_value(leftFloat < rightFloat));
							return {};
						case token_type::less_equal:
							push_value(make_value(leftFloat <= rightFloat));
							return {};
						case token_type::greater:
							push_value(make_value(leftFloat > rightFloat));
							return {};
						case token_type::greater_equal:
							push_value(make_value(leftFloat >= rightFloat));
							return {};
						case token_type::plus:
							push_value(make_value(leftFloat + rightFloat));
							return {};
						case token_type::minus:
							push_value(make_value(leftFloat - rightFloat));
							return {};
						case token_type::star:
							push_value(make_value(leftFloat * rightFloat));
							return {};
						case token_type::slash:
							if (rightFloat == 0.0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
							push_value(make_value(leftFloat / rightFloat));
							return {};
						default:
							break;
						}
					}
				}
				// Fast path didn't fully handle - save for slow path
				pre_fetched_left = std::move(leftVal);
				pre_fetched_right = rightVal;
				already_have_values = true;
			}
		}
		// FAST PATH 3: literal + identifier (e.g., "100 > i", "5 + x")
		else if (expr->left->get_type() == node_type::literal_expr) {
			auto* leftLit = static_cast<literal_expr*>(expr->left.get());
			if (expr->right->get_type() == node_type::identifier_expr) {
				auto* rightId = static_cast<identifier_expr*>(expr->right.get());
				const script_value& leftVal = leftLit->value;  // Direct access!
				auto rightResult = environment_->get(rightId->symbol_id);
				if (!rightResult) {
					return checked_result<void>(rightResult.error(), rightResult.message());
				}
				script_value rightVal = std::move(rightResult.value()).deref();

				// Ultra-fast integer path
				if (can_use_fast_path(expr->op.type)) {
					size_t leftIdx = leftVal.raw_storage_index();
					size_t rightIdx = rightVal.raw_storage_index();

					if (leftIdx == 1 && rightIdx == 1) {  // Both ints
						script_int leftInt = leftVal.unchecked_as_int();
						script_int rightInt = rightVal.unchecked_as_int();

						switch (expr->op.type) {
						case token_type::less:
							push_value(make_value(leftInt < rightInt));
							return {};
						case token_type::less_equal:
							push_value(make_value(leftInt <= rightInt));
							return {};
						case token_type::greater:
							push_value(make_value(leftInt > rightInt));
							return {};
						case token_type::greater_equal:
							push_value(make_value(leftInt >= rightInt));
							return {};
						case token_type::equal_equal:
							push_value(make_value(leftInt == rightInt));
							return {};
						case token_type::bang_equal:
							push_value(make_value(leftInt != rightInt));
							return {};
						case token_type::plus:
							push_value(make_value(leftInt + rightInt));
							return {};
						case token_type::minus:
							push_value(make_value(leftInt - rightInt));
							return {};
						case token_type::star:
							push_value(make_value(leftInt * rightInt));
							return {};
						case token_type::slash:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
							push_value(make_value(leftInt / rightInt));
							return {};
						case token_type::percent:
							if (rightInt == 0) return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));
							push_value(make_value(leftInt % rightInt));
							return {};
						default:
							break;
						}
					}
				}
				// Fast path didn't fully handle - save for slow path
				pre_fetched_left = leftVal;
				pre_fetched_right = std::move(rightVal);
				already_have_values = true;
			}
		}
	}

    // Handle logical operators specially for short-circuit evaluation
    // Return proper boolean values (not JavaScript-style operand values)
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        JAISCRIPT_TRY(dispatch_expr(expr->left.get()));
        script_value left = pop_value();

        bool leftTruthy = is_truthy(left);

        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(make_value(false));  // Short-circuit: left is falsy, return false
                return {};
            }
            // Left is truthy, evaluate right and return its boolean value
            JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(make_value(true));  // Short-circuit: left is truthy, return true
                return {};
            }
            // Left is falsy, evaluate right and return its boolean value
            JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
            script_value right = pop_value();
            push_value(make_value(is_truthy(right)));
            return {};
        }
    }

    // Evaluate operands once and use them throughout (or use pre-fetched values)
    std::optional<script_value> left_raw_opt, left_opt, right_opt;

    if (already_have_values) {
        // Use pre-fetched values from fast path (already dereferenced)
        left_opt = std::move(*pre_fetched_left);
        left_raw_opt = *left_opt;  // Already dereferenced
        right_opt = std::move(*pre_fetched_right);
    } else {
        // Normal path: evaluate via AST traversal
        JAISCRIPT_TRY(dispatch_expr(expr->left.get()));
        left_raw_opt = pop_value();  // Keep raw value for subscript handling
        left_opt = left_raw_opt->deref();  // Dereferenced version for most operations

        JAISCRIPT_TRY(dispatch_expr(expr->right.get()));
        // Check if we're unwinding due to an exception in the right expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return {};
        }
        right_opt = pop_value().deref();  // Handle references safely
    }

    // Extract values from optional (guaranteed to have values at this point)
    script_value& left_raw = *left_raw_opt;
    script_value& left = *left_opt;
    script_value& right = *right_opt;
    
    // Check for custom operator functions first using cached symbol IDs (eliminates string construction)
    uint64_t op_symbol_id = 0;
    switch (expr->op.type) {
        case token_type::plus: op_symbol_id = op_plus_id_; break;
        case token_type::minus: op_symbol_id = op_minus_id_; break;
        case token_type::star: op_symbol_id = op_star_id_; break;
        case token_type::slash: op_symbol_id = op_slash_id_; break;
        case token_type::percent: op_symbol_id = op_percent_id_; break;
        case token_type::less: op_symbol_id = op_less_id_; break;
        case token_type::less_equal: op_symbol_id = op_less_equal_id_; break;
        case token_type::greater: op_symbol_id = op_greater_id_; break;
        case token_type::greater_equal: op_symbol_id = op_greater_equal_id_; break;
        case token_type::equal_equal: op_symbol_id = op_equal_equal_id_; break;
        case token_type::bang_equal: op_symbol_id = op_bang_equal_id_; break;
        case token_type::spaceship: op_symbol_id = op_spaceship_id_; break;
        case token_type::ampersand: op_symbol_id = op_ampersand_id_; break;
        case token_type::pipe: op_symbol_id = op_pipe_id_; break;
        case token_type::caret: op_symbol_id = op_caret_id_; break;
        case token_type::left_shift: op_symbol_id = op_left_shift_id_; break;
        case token_type::right_shift: op_symbol_id = op_right_shift_id_; break;
        default: break;
    }

    // Check for custom operator function (excluding subscript)
    if (op_symbol_id != 0 && environment_ && environment_->contains(op_symbol_id)) {
        auto op_result = environment_->get(op_symbol_id);
        if (op_result && op_result.value().is_function()) {
            script_value opFunc = std::move(op_result.value());
            const script_function& func = opFunc.as_function();
            std::vector<script_value> args = {left, right};
            auto result = func(args);
            if (!result) {
                // Function returned error - propagate it up
                return checked_result<void>(result.error(), result.message());
            }
            push_value(std::move(result.value()));
            return {};
        }
    }

    // Handle subscript operation specially
    if (expr->op.type == token_type::left_bracket) {
        if (left.is_array()) {
            if (!right.is_int()) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_index_type));  // [ErrorText] Invalid index type
            }
            script_int index = right.as_int();
            const auto& array = left.as_array();

            if (index < 0 || index >= static_cast<script_int>(array.size())) {
                return checked_result<void>(make_error_code(runtime_error_code::index_out_of_bounds));  // [ErrorText] Index out of bounds
            }

            // Check if the left side is an lvalue (variable, member access, or subscript)
            // These should allow modification, even if use_count == 1
            bool is_lvalue = expr->left->get_type() == node_type::identifier_expr ||
                            expr->left->get_type() == node_type::member_expr ||
                            (expr->left->get_type() == node_type::binary_expr &&
                             static_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

            if (is_lvalue) {
                // This is an lvalue expression, return a reference to allow modification
                auto& mut_array = const_cast<std::vector<script_value>&>(array);
                script_value* element_ptr = &mut_array[index];
                script_value ref_value = script_value::make_reference(element_ptr, environment_);
                push_value(ref_value);
            } else {
                // True temporary (e.g., function return), read-only access
                push_value(array[index]);
            }
        } else if (left.is_map()) {
            // For maps, we need to handle both assignment and read access
            // Try to get a mutable reference if possible
            try {
                auto& map = const_cast<std::map<script_value, script_value>&>(left.as_map());

                // Check if the left side is an lvalue (variable, member access, or subscript)
                bool is_lvalue = expr->left->get_type() == node_type::identifier_expr ||
                                expr->left->get_type() == node_type::member_expr ||
                                (expr->left->get_type() == node_type::binary_expr &&
                                 static_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

                if (is_lvalue) {
                    // This is an lvalue expression, return a reference to allow modification
                    script_value& value_ref = map[right];

                    // If this created a new entry with default constructor, it has invalid engine reference
                    if (!value_ref.has_valid_engine_ref()) {
                        if (!left.has_valid_engine_ref()) {
                            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                                "Invalid script_value: both map and new entry missing engine reference");
                        }
                        value_ref.set_engine_ref(left.get_engine_ref());
                    }

                    script_value* element_ptr = &value_ref;
                    script_value ref_value = script_value::make_reference(element_ptr, environment_);
                    push_value(ref_value);
                } else {
                    // This is a true temporary (e.g., function return), read-only access
                    auto it = map.find(right);
                    if (it != map.end()) {
                        // Ensure the value has an engine ref before pushing
                        script_value val = it->second;
                        if (!val.has_valid_engine_ref()) {
                            val.set_engine_ref(engine_ref_);
                        }
                        push_value(val);
                    } else {
                        push_value(script_value(std::monostate{}, engine_ref_));
                    }
                }
            } catch (...) {
                // Fallback for any edge cases
                const auto& map = left.as_map();
                auto it = map.find(right);
                if (it != map.end()) {
                    // Ensure the value has an engine ref before pushing
                    script_value val = it->second;
                    if (!val.has_valid_engine_ref()) {
                        val.set_engine_ref(engine_ref_);
                    }
                    push_value(val);
                } else {
                    push_value(script_value(std::monostate{}, engine_ref_));
                }
            }
        } else {
            if (left.is_object()) {
                // First, try to find operator[] as a method on the object (for class instances)
                auto instance_result = left.checked_as<std::shared_ptr<class_instance>>();
                if (instance_result) {
                    auto instance = instance_result.value();
                    // Use cached subscript operator ID
                    script_value method = instance->get_method(subscript_op_id_, false);
                    if (method.is_function()) {
                        const script_function& func = method.as_function();
                        std::vector<script_value> args = {left, right};
                        auto result = func(args);
                        if (!result) {
                            return checked_result<void>(result.error(), result.message());
                        }
                        push_value(std::move(result.value()));
                        return {};
                    }
                }

                // Fall back to global [] operator function
                auto method_result = environment_->get("[]");
                if (method_result && method_result.value().is_function()) {
                    script_value getMethod = std::move(method_result.value());
                    const script_function& func = getMethod.as_function();
                    std::vector<script_value> args = {left, right};
                    auto result = func(args);
                    if (!result) {
                        // Function returned error - propagate it up
                        return checked_result<void>(result.error(), result.message());
                    }
                    push_value(std::move(result.value()));
                    return {};
                }
            }
            return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                "Subscript can only be used on arrays, maps, or types with [] operator");
        }
        return {};
    }

    // Use dispatch table for built-in operators with already-evaluated operands
    auto handler = binary_dispatch_table_.find(expr->op.type);
    if (handler != binary_dispatch_table_.end()) {
        script_value result = (this->*handler->second)(left, right);
        push_value(result);
    } else {
        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));  // [ErrorText] Unknown operator
    }
    return {};
}
checked_result<void> interpreter::visit_unary_expr(unary_expr* expr) {
    // Fast path for literal unary operations
    if (expr->operand->get_type() == node_type::literal_expr) {
        auto* literal = static_cast<literal_expr*>(expr->operand.get());
        const script_value& val = literal->value;

        switch (expr->op.type) {
            case token_type::minus:
                if (val.is_int()) {
                    push_value(make_value(-val.as_int()));
                    return {};
                } else if (val.is_float()) {
                    push_value(make_value(-val.as_float()));
                    return {};
                }
                break;
            case token_type::bang:
                push_value(make_value(!is_truthy(val)));
                return {};
            case token_type::tilde:
                if (val.is_int()) {
                    push_value(make_value(~val.as_int()));
                    return {};
                }
                break;
            default:
                break; // Fall through to generic path for increment/decrement
        }
    }

    // Generic path - evaluate operand and use existing logic
    JAISCRIPT_TRY(dispatch_expr(expr->operand.get()));
    script_value operand = pop_value();
    
    switch (expr->op.type) {
        case token_type::minus: {
            // Use single type() call + switch for faster type checking
            switch (operand.type()) {
                case script_value_type::jai_int_type:
                    push_value(make_value(-operand.as_int()));
                    break;
                case script_value_type::jai_float_type:
                    push_value(make_value(-operand.as_float()));
                    break;
                default:
                    return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));  // [ErrorText] Unary minus requires numeric operand
            }
            break;
        }

        case token_type::bang:
            push_value(make_value(!is_truthy(operand)));
            break;

        case token_type::tilde:
            // Bitwise NOT
            if (operand.type() != script_value_type::jai_int_type) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));  // [ErrorText] Bitwise NOT requires integer operand
            }
            push_value(make_value(~operand.as_int()));
            break;
            
        case token_type::plus_plus:
        case token_type::minus_minus: {
            // Handle increment/decrement with in-place mutation (ChaiScript-style)
            if (expr->operand->get_type() == node_type::identifier_expr) {
                auto* identifier = static_cast<identifier_expr*>(expr->operand.get());
                // Cache symbol ID if not already cached
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }

                // Try fast path: direct variable lookup
                script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
                if (varPtr) {
                    // Fast path: direct in-place mutation for local variables
                    script_value& target = varPtr->deref();

                    const bool isIncrement = (expr->op.type == token_type::plus_plus);
                    switch (target.type()) {
                        case script_value_type::jai_int_type: {
                            if (expr->is_postfix) {
                                push_value(make_value(target.unchecked_as_int()));
                                if (isIncrement) ++target.unchecked_as_int_ref();
                                else --target.unchecked_as_int_ref();
                            } else {
                                if (isIncrement) ++target.unchecked_as_int_ref();
                                else --target.unchecked_as_int_ref();
                                push_value(make_value(target.unchecked_as_int()));
                            }
                            return {};
                        }
                        case script_value_type::jai_float_type: {
                            if (expr->is_postfix) {
                                push_value(make_value(target.unchecked_as_float()));
                                if (isIncrement) target.unchecked_as_float_ref() += 1.0;
                                else target.unchecked_as_float_ref() -= 1.0;
                            } else {
                                if (isIncrement) target.unchecked_as_float_ref() += 1.0;
                                else target.unchecked_as_float_ref() -= 1.0;
                                push_value(make_value(target.unchecked_as_float()));
                            }
                            return {};
                        }
                        default:
                            return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
                    }
                } else {
                    // Fallback: identifier is an implicit this.member access
                    // Check if 'this' exists and has this field
                    auto this_result = environment_->get(string_symbolizer_->get_this_id());
                    if (this_result && this_result.value().is_object()) {
                        script_value this_val = std::move(this_result.value());
                        auto obj_holder = this_val.get_object_holder();

                        std::shared_ptr<class_instance> instance = obj_holder->is_class_instance_wrapper
                            ? std::static_pointer_cast<class_instance>(obj_holder->data)
                            : nullptr;

                        if (instance && instance->has_field(identifier->symbol_id)) {
                            script_value currentVal = instance->get_field(identifier->symbol_id);
                            const bool isIncrement = (expr->op.type == token_type::plus_plus);

                            if (currentVal.is_int()) {
                                script_int oldVal = currentVal.as_int();
                                script_int newVal = isIncrement ? oldVal + 1 : oldVal - 1;
                                instance->set_field(identifier->symbol_id, make_value(newVal));
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else if (currentVal.is_float()) {
                                script_float oldVal = currentVal.as_float();
                                script_float newVal = isIncrement ? oldVal + 1.0 : oldVal - 1.0;
                                instance->set_field(identifier->symbol_id, make_value(newVal));
                                push_value(make_value(expr->is_postfix ? oldVal : newVal));
                                return {};
                            } else {
                                return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));
                            }
                        }
                    }
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
                }
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target));
            }
            break;
        }

        default:
            return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));  // [ErrorText] Unknown operator
    }
    return {};
}

checked_result<void> interpreter::visit_assignment_expr(assignment_expr* expr) {

    // For compound assignment operators, we need the current value
    if (expr->op.type != token_type::equal) {
        // Get current value of the target
        if (expr->target->get_type() == node_type::identifier_expr) {
            auto* identifier = static_cast<identifier_expr*>(expr->target.get());
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }

            // Try fast path: direct variable lookup for in-place mutation
            script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
            if (varPtr) {
                // Fast path: direct in-place mutation for local variables
                script_value& target = varPtr->deref();
                auto leftType = target.type();

                // === ULTRA FAST PATH: int += int literal (e.g., sum += 1) ===
                // Skip dispatch_expr entirely for int literal RHS
                if (leftType == script_value_type::jai_int_type && !has_custom_numeric_ops_) [[likely]] {
                    if (expr->value->get_type() == node_type::literal_expr) {
                        auto* rhs_lit = static_cast<literal_expr*>(expr->value.get());
                        if (rhs_lit->value.raw_storage_index() == 1) {  // int literal
                            script_int rhs_val = rhs_lit->value.unchecked_as_int();
                            switch (expr->op.type) {
                                case token_type::plus_equal:
                                    target.unchecked_as_int_ref() += rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::minus_equal:
                                    target.unchecked_as_int_ref() -= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::star_equal:
                                    target.unchecked_as_int_ref() *= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::slash_equal:
                                    if (rhs_val == 0) {
                                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                                    }
                                    target.unchecked_as_int_ref() /= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                default:
                                    break;  // Fall through to general path
                            }
                        }
                    }
                    // === ULTRA FAST PATH: int += int variable (e.g., sum += i) ===
                    // Skip dispatch_expr for simple identifier RHS
                    else if (expr->value->get_type() == node_type::identifier_expr) {
                        auto* rhs_id = static_cast<identifier_expr*>(expr->value.get());
                        if (rhs_id->symbol_id == UINT64_MAX) {
                            rhs_id->symbol_id = string_symbolizer_->intern(rhs_id->name);
                        }
                        script_value* rhs_ptr = environment_->get_value_ptr(rhs_id->symbol_id);
                        if (rhs_ptr && rhs_ptr->raw_storage_index() == 1) {  // int value
                            script_int rhs_val = rhs_ptr->unchecked_as_int();
                            switch (expr->op.type) {
                                case token_type::plus_equal:
                                    target.unchecked_as_int_ref() += rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::minus_equal:
                                    target.unchecked_as_int_ref() -= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::star_equal:
                                    target.unchecked_as_int_ref() *= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                case token_type::slash_equal:
                                    if (rhs_val == 0) {
                                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                                    }
                                    target.unchecked_as_int_ref() /= rhs_val;
                                    push_value(target.clone());
                                    return {};
                                default:
                                    break;  // Fall through to general path
                            }
                        }
                    }
                    // === FAST PATH: int += simple binary expr (e.g., sum += i * 2) ===
                    else if (expr->value->get_type() == node_type::binary_expr) {
                        auto* rhs_binary = static_cast<binary_expr*>(expr->value.get());
                        // Handle identifier * literal pattern (most common in loops)
                        if (rhs_binary->left->get_type() == node_type::identifier_expr) {
                            auto* left_id = static_cast<identifier_expr*>(rhs_binary->left.get());
                            if (rhs_binary->right->get_type() == node_type::literal_expr) {
                                auto* right_lit = static_cast<literal_expr*>(rhs_binary->right.get());
                                if (right_lit->value.raw_storage_index() == 1) {  // int literal
                                    if (left_id->symbol_id == UINT64_MAX) {
                                        left_id->symbol_id = string_symbolizer_->intern(left_id->name);
                                    }
                                    script_value* left_ptr = environment_->get_value_ptr(left_id->symbol_id);
                                    if (left_ptr && left_ptr->raw_storage_index() == 1) {
                                        script_int left_val = left_ptr->unchecked_as_int();
                                        script_int right_val = right_lit->value.unchecked_as_int();
                                        script_int binary_result = 0;
                                        bool handled = true;
                                        switch (rhs_binary->op.type) {
                                            case token_type::star: binary_result = left_val * right_val; break;
                                            case token_type::plus: binary_result = left_val + right_val; break;
                                            case token_type::minus: binary_result = left_val - right_val; break;
                                            default: handled = false; break;
                                        }
                                        if (handled && expr->op.type == token_type::plus_equal) {
                                            target.unchecked_as_int_ref() += binary_result;
                                            push_value(target.clone());
                                            return {};
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Evaluate the right-hand side (general path)
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                script_value rightValue = pop_value();
                auto& derefRight = rightValue.deref();

                // Get type for RHS
                auto rightType = derefRight.type();

                // Check for custom operators first (rare path)
                if (has_custom_numeric_ops_) [[unlikely]] {
                    const char* opName = nullptr;
                    switch (expr->op.type) {
                        case token_type::plus_equal: opName = "+"; break;
                        case token_type::minus_equal: opName = "-"; break;
                        case token_type::star_equal: opName = "*"; break;
                        default: break;
                    }
                    if (opName && environment_->contains(opName)) {
                        auto op_result = environment_->get(opName);
                        if (op_result && op_result.value().is_function()) {
                            script_value opFunc = std::move(op_result.value());
                            const script_function& func = opFunc.as_function();
                            std::vector<script_value> args = {target.clone(), rightValue};
                            auto result = func(args);
                            if (!result) {
                                return checked_result<void>(result.error(), result.message());
                            }
                            target = std::move(result.value());
                            push_value(target.clone());
                            return {};
                        }
                    }
                }

                // Check for custom arithmetic operators on class instances
                if (leftType == script_value_type::jai_object_type) {
                    uint64_t op_symbol_id = 0;
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                        case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                        case token_type::star_equal: op_symbol_id = op_star_id_; break;
                        case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                        case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                        default: break;
                    }
                    if (op_symbol_id != 0) {
                        auto custom_result = object_arithmetic_via_method(target, rightValue, op_symbol_id);
                        if (custom_result.has_value()) {
                            target = std::move(custom_result.value());
                            push_value(target.clone());
                            return {};
                        }
                    }
                }

                // === IN-PLACE MUTATION (ChaiScript-style) ===
                // Modify the value directly in storage, avoiding make_value() allocations
                switch (expr->op.type) {
                    case token_type::plus_equal: {
                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            target.unchecked_as_int_ref() += derefRight.unchecked_as_int();
                        } else if (leftType == script_value_type::jai_float_type) {
                            target.unchecked_as_float_ref() += derefRight.as_float();
                        } else if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_float_type) {
                            target = make_value(target.unchecked_as_int() + derefRight.unchecked_as_float());
                        } else if (leftType == script_value_type::jai_string_type && rightType == script_value_type::jai_string_type) {
                            target.unchecked_as_string_ref() += derefRight.unchecked_as_string();
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    }

                    case token_type::minus_equal: {
                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            target.unchecked_as_int_ref() -= derefRight.unchecked_as_int();
                        } else if (leftType == script_value_type::jai_float_type) {
                            target.unchecked_as_float_ref() -= derefRight.as_float();
                        } else if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_float_type) {
                            target = make_value(target.unchecked_as_int() - derefRight.unchecked_as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    }

                    case token_type::star_equal: {
                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            target.unchecked_as_int_ref() *= derefRight.unchecked_as_int();
                        } else if (leftType == script_value_type::jai_float_type) {
                            target.unchecked_as_float_ref() *= derefRight.as_float();
                        } else if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_float_type) {
                            target = make_value(target.unchecked_as_int() * derefRight.unchecked_as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    }

                    case token_type::slash_equal: {
                        if (rightType == script_value_type::jai_int_type && derefRight.unchecked_as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }
                        if (rightType == script_value_type::jai_float_type && derefRight.unchecked_as_float() == 0.0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }

                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            target.unchecked_as_int_ref() /= derefRight.unchecked_as_int();
                        } else if (leftType == script_value_type::jai_float_type) {
                            target.unchecked_as_float_ref() /= derefRight.as_float();
                        } else if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_float_type) {
                            target = make_value(target.unchecked_as_int() / derefRight.unchecked_as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    }

                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                }

                push_value(target.clone());
            } else {
                // Fallback: identifier is an implicit this.member access
                // Check if 'this' exists and has this field
                auto this_result = environment_->get(string_symbolizer_->get_this_id());
                if (!this_result || !this_result.value().is_object()) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
                }

                script_value this_val = std::move(this_result.value());
                auto obj_holder = this_val.get_object_holder();
                std::shared_ptr<class_instance> instance = obj_holder->is_class_instance_wrapper
                    ? std::static_pointer_cast<class_instance>(obj_holder->data)
                    : nullptr;

                if (!instance || !instance->has_field(identifier->symbol_id)) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
                }

                // Get current field value
                script_value currentValue = instance->get_field(identifier->symbol_id);

                // Evaluate the right-hand side
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                script_value rightValue = pop_value();

                // Check for custom arithmetic operators on class instances
                if (currentValue.is_object()) {
                    uint64_t op_symbol_id = 0;
                    switch (expr->op.type) {
                        case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                        case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                        case token_type::star_equal: op_symbol_id = op_star_id_; break;
                        case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                        case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                        default: break;
                    }
                    if (op_symbol_id != 0) {
                        auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                        if (custom_result.has_value()) {
                            instance->set_field(identifier->symbol_id, custom_result.value().clone());
                            push_value(std::move(custom_result.value()));
                            return {};
                        }
                    }
                }

                // Perform the compound operation (using standard path, no in-place mutation for fields)
                script_value resultValue = make_value();
                switch (expr->op.type) {
                    case token_type::plus_equal:
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                        } else if (currentValue.is_string() && rightValue.is_string()) {
                            resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    case token_type::minus_equal:
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    case token_type::star_equal:
                        if (currentValue.is_int() && rightValue.is_int()) {
                            resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                        } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    case token_type::slash_equal:
                        if (rightValue.is_int() && rightValue.as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));
                        }
                        if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                            resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
                        }
                        break;
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));
                }

                // Set the field and push result
                instance->set_field(identifier->symbol_id, resultValue.clone());
                push_value(std::move(resultValue));
            }
        } else if (expr->target->get_type() == node_type::member_expr) {
            auto* memberExpr = static_cast<member_expr*>(expr->target.get());
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // First, get the current value of the property
            JAISCRIPT_TRY(dispatch_expr(memberExpr));
            script_value currentValue = pop_value().deref();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
            script_value rightValue = pop_value();

            // Check for custom arithmetic operators on class instances
            if (currentValue.is_object()) {
                uint64_t op_symbol_id = 0;
                switch (expr->op.type) {
                    case token_type::plus_equal: op_symbol_id = op_plus_id_; break;
                    case token_type::minus_equal: op_symbol_id = op_minus_id_; break;
                    case token_type::star_equal: op_symbol_id = op_star_id_; break;
                    case token_type::slash_equal: op_symbol_id = op_slash_id_; break;
                    case token_type::percent_equal: op_symbol_id = op_percent_id_; break;
                    default: break;
                }
                if (op_symbol_id != 0) {
                    auto custom_result = object_arithmetic_via_method(currentValue, rightValue, op_symbol_id);
                    if (custom_result.has_value()) {
                        // Assign the result back to the property
                        JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
                        script_value objectValue = pop_value();
                        if (objectValue.is_object()) {
                            auto objHolder = objectValue.get_object_holder();
                            if (objHolder) {
                                auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
                                uint64_t member_id = string_symbolizer_->intern(memberExpr->member);
                                instance->set_field(member_id, custom_result.value().clone());
                                push_value(std::move(custom_result.value()));
                                return {};
                            }
                        }
                    }
                }
            }

            // Perform the compound operation
            script_value resultValue = make_value();

            switch (expr->op.type) {
                case token_type::plus_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() + rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() + rightValue.as_float());
                    } else if (currentValue.is_string() && rightValue.is_string()) {
                        resultValue = make_value(currentValue.as_string() + rightValue.as_string());
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for +=");
                    }
                    break;
                }
                case token_type::minus_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() - rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() - rightValue.as_float());
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for -=");
                    }
                    break;
                }
                case token_type::star_equal: {
                    if (currentValue.is_int() && rightValue.is_int()) {
                        resultValue = make_value(currentValue.as_int() * rightValue.as_int());
                    } else if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() * rightValue.as_float());
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for *=");
                    }
                    break;
                }
                case token_type::slash_equal: {
                    if (rightValue.is_int() && rightValue.as_int() == 0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                            "Division by zero");
                    } else if (rightValue.is_float() && rightValue.as_float() == 0.0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                            "Division by zero");
                    }
                    if ((currentValue.is_int() || currentValue.is_float()) && (rightValue.is_int() || rightValue.is_float())) {
                        resultValue = make_value(currentValue.as_float() / rightValue.as_float());
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Invalid operands for /=");
                    }
                    break;
                }
                default:
                    return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                        "Unsupported compound assignment operator");
            }
            
            // Now assign the result back to the property
            // We need to evaluate the object again to get a fresh reference
            JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
            script_value objectValue = pop_value();

            // After refactor: shared_ptr<T> uses same storage, no unwrapping needed

            // Check if it's an object
            if (!objectValue.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            // Extract the class_instance
            auto objHolder = objectValue.get_object_holder();
            if (!objHolder) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot assign property to non-object value");
            }
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);

            // Intern the member name to ID
            uint64_t member_id = string_symbolizer_->intern(memberExpr->member);

            // Check if there's a property setter
            uint64_t setter_id = string_symbolizer_->intern("_set_" + memberExpr->member);
            script_value setter = instance->get_method(setter_id, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, std::move(resultValue.clone())};
                auto result = func(args);
                if (!result) {
                    // Setter failed - propagate error
                    return checked_result<void>(result.error(), result.message());
                }
            } else if (instance->has_field(member_id)) {
                // Direct field assignment (deep copy)
                instance->set_field(member_id, std::move(resultValue.clone()));
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }
            
            push_value(std::move(resultValue));
        } else {
            // General compound assignment for any expression
            // This handles subscripts, function calls that return references, etc.

            // First, evaluate the target expression to get current value
            JAISCRIPT_TRY(dispatch_expr(expr->target.get()));
            script_value currentValue = pop_value();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue = make_value();
            
            // Try custom operators first
            auto op_result = environment_->get(std::string(1, expr->op.lexeme[0]));
            if (op_result && op_result.value().is_function()) {
                script_value opFunc = std::move(op_result.value());
                const script_function& func = opFunc.as_function();
                std::vector<script_value> args = {currentValue, rightValue};
                auto result = func(args);
                if (!result) {
                    // Function returned error - propagate it up
                    return checked_result<void>(result.error(), result.message());
                }
                resultValue = std::move(result.value());
            } else {
                // Fall back to built-in operators
                switch (expr->op.type) {
                    case token_type::plus_equal: {
                        if (currentValue.is_string() || rightValue.is_string()) {
                            resultValue = make_value(currentValue.to_string() + rightValue.to_string());
                        } else {
                            JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::plus, rightValue));
                        }
                        break;
                    }
                    case token_type::minus_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::minus, rightValue));
                        break;
                    }
                    case token_type::star_equal: {
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::star, rightValue));
                        break;
                    }
                    case token_type::slash_equal: {
                        if ((rightValue.is_int() && rightValue.as_int() == 0) ||
                            (rightValue.is_float() && rightValue.as_float() == 0.0)) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Division by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::slash, rightValue));
                        break;
                    }
                    case token_type::percent_equal: {
                        if (rightValue.is_int() && rightValue.as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Modulo by zero");
                        }
                        JAISCRIPT_TRY_ASSIGN(resultValue, evaluate_arithmetic(currentValue, token_type::percent, rightValue));
                        break;
                    }
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                            "Unknown compound assignment operator");
                }
            }
            
            // Directly assign the result without creating new AST nodes (optimization)
            if (expr->target->get_type() == node_type::identifier_expr) {
                auto* identifier = static_cast<identifier_expr*>(expr->target.get());
                // Fast path for simple identifier assignment
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }

                // FIX #5: Enforce type compatibility for compound assignments
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
                if (currentVal) {
                    type_info_ptr target_type = currentVal->get_type_info();
                    if (target_type && target_type->base_type != script_value_type::jai_any_type) {
                        auto enforced = enforce_type_compatibility(std::move(resultValue), target_type, identifier->name);
                        if (!enforced) {
                            return checked_result<void>(enforced.error(), enforced.message());
                        }
                        resultValue = std::move(enforced.value());
                    }
                }

                // FIX #3/#4: Save result before moving to avoid returning moved-from value
                script_value returnValue = resultValue.clone();
                JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(resultValue)));
                push_value(std::move(returnValue));
            } else {
                // Fall back to AST creation for complex lvalues
                auto regularAssignment = std::make_shared<assignment_expr>(
                    expr->location,
                    expr->target,
                    token(token_type::equal, "=", expr->op.location),
                    std::make_shared<literal_expr>(expr->location, resultValue)
                );
                JAISCRIPT_TRY(dispatch_expr(regularAssignment.get()));
            }
        }
    } else {
        // Regular assignment

        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
        // Check if we're unwinding due to an exception in the value expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return {};
        }
        script_value value = pop_value();
        
        
        // Check if target is an identifier
        if (expr->target->get_type() == node_type::identifier_expr) {
            auto* identifier = static_cast<identifier_expr*>(expr->target.get());
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            // Get the current value to check if it's a reference
            if (environment_->contains(identifier->symbol_id)) {
                script_value* currentVal = environment_->get_value_ptr(identifier->symbol_id);
                if (currentVal && currentVal->is_reference()) {
                    // This is a reference - assign through it (deep copy the value)
                    currentVal->deref() = std::move(value.deref().clone());
                } else if (currentVal && currentVal->is_cpp_bound()) {
                    // This is a C++ bound value - use assign_through
                    currentVal->assign_through(value);
                } else if (currentVal && currentVal->is_weak_ptr()) {
                    // Special handling for weak_ptr assignment
                    if (value.is_null()) {
                        // Assign null - create empty weak_ptr
                        auto type_info = currentVal->get_type_info();
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, script_value::make_empty_weak_ptr(type_info, engine_ref_)));
                    } else if (value.is_weak_ptr()) {
                        // Assign another weak_ptr
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(value)));
                    } else if (value.type() == script_value_type::jai_shared_ptr_type) {
                        // Convert shared_ptr to weak_ptr
                        auto weak_result = script_value::make_weak_ptr(value, engine_ref_);
                        if (!weak_result) {
                            return checked_result<void>(weak_result.error(), weak_result.message());
                        }
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(weak_result.value())));
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Helpful error for value-semantic objects
                        auto type_info = currentVal->get_type_info();
                        std::string weak_type = type_info && !type_info->type_params.empty() ?
                            "weak_ptr<" + type_info->type_params[0]->type_name + ">" : "weak_ptr";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign value-semantic object to " + weak_type + ". Use shared_ptr<T> to enable reference semantics.");
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        auto weak_type_info = currentVal->get_type_info();
                        std::string weak_type = weak_type_info && !weak_type_info->type_params.empty() ?
                            "weak_ptr<" + weak_type_info->type_params[0]->type_name + ">" : "weak_ptr";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign " + type_name + " to " + weak_type + ". Use shared_ptr<T> to enable reference semantics.");
                    }
                } else if (currentVal && currentVal->get_type_info() &&
                          currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                    // Special handling for shared_ptr assignment
                    if (value.is_null()) {
                        // Assign null - that's fine
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(value)));
                    } else if (value.is_weak_ptr()) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // FIX #6: Check type parameter compatibility for shared_ptr<T>
                        auto ptr_type_info = currentVal->get_type_info();
                        if (!ptr_type_info->type_params.empty()) {
                            auto expected_type = ptr_type_info->type_params[0];
                            auto actual_type = value.get_type_info();
                            std::string actual_type_name = actual_type ? actual_type->type_name : "unknown";

                            // Check if types are compatible (same type or inheritance)
                            bool compatible = (actual_type_name == expected_type->type_name);

                            // TODO #7: Check inheritance hierarchy here
                            // For now, just check exact type match
                            if (!compatible && actual_type) {
                                // Try to get class definition and check inheritance
                                if (auto eng = engine_ref_.lock()) {
                                    auto actual_class = eng->get_class_definition(actual_type_name);
                                    if (actual_class) {
                                        auto parent = actual_class->get_parent();
                                        while (parent && !compatible) {
                                            if (parent->get_name() == expected_type->type_name) {
                                                compatible = true;
                                            }
                                            parent = parent->get_parent();
                                        }
                                    }
                                }
                            }

                            if (!compatible) {
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                                    "Cannot assign " + actual_type_name + " to shared_ptr<" + expected_type->type_name + ">");
                            }
                        }

                        // Assign object to shared_ptr - update the value but keep the shared_ptr type info
                        auto type_info = currentVal->get_type_info();
                        value.set_type_info(type_info);
                        JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(value)));
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign " + type_name + " to shared_ptr");
                    }
                } else {
                    // Regular variable assignment with STRONG TYPES enforcement
                    type_info_ptr target_type = currentVal->get_type_info();

                    // Try assignment operator lookup for class types before type enforcement
                    if (target_type && target_type->base_type == script_value_type::jai_object_type) {
                        // Check if currentVal has an operator= method for the source type
                        auto source_type_info = value.get_type_info();
                        std::string source_type_name = source_type_info ? source_type_info->type_name : "unknown";

                        // Only try operator= if types are different (same type just uses regular assignment)
                        if (source_type_name != target_type->type_name) {
                            auto instance_result = currentVal->checked_as<std::shared_ptr<class_instance>>();
                            if (instance_result) {
                                auto instance = instance_result.value();
                                // Look for operator= method using cached symbol ID
                                script_value method = instance->get_method(assign_operator_id_, false);
                                if (method.is_function()) {
                                    // Found operator= method - call it with the source value
                                    // Script methods expect 'this' as the first argument
                                    const script_function& func = method.as_function();
                                    std::vector<script_value> args;
                                    args.push_back(*currentVal);  // 'this' - the instance being assigned to
                                    args.push_back(std::move(value));  // the value being assigned
                                    auto result = func(args);
                                    if (result) {
                                        // Operator= succeeded - return without replacing the variable
                                        // The operator= method should have modified the instance in place
                                        return checked_result<void>();
                                    }
                                    // FIX #10: Propagate operator= errors instead of silent fallthrough
                                    return checked_result<void>(result.error(),
                                        "operator= failed for " + target_type->type_name + ": " + result.message());
                                }
                            }
                        }
                    }

                    // Enforce type compatibility
                    auto enforced = enforce_type_compatibility(std::move(value), target_type, identifier->name);
                    if (!enforced) {
                        return checked_result<void>(enforced.error(), enforced.message());
                    }
                    value = std::move(enforced.value());

                    // Handle first assignment for uninitialized auto variables
                    if (!target_type) {
                        // Lock the variable's type to the assigned value's type
                        // The value keeps its own type_info
                    }
                    // For any_type (var), keep the any_type on the variable
                    else if (target_type->base_type == script_value_type::jai_any_type) {
                        value.set_type_info(target_type);  // Keep any_type marker
                    }
                    // For locked types, the value already has correct type from enforce_type_compatibility

                    // FIX #1: Always clone for value semantics (objects too!)
                    // shared_ptr types already handled above with reference semantics
                    // Regular objects should deep copy like all other value types
                    script_value assignValue = value.clone();
                    JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, std::move(assignValue)));
                }
            } else {
                // Variable doesn't exist in environment
                // Try static_method_environment's assign (which handles static fields)
                // or instance method's 'this' field assignment
                bool assigned_to_member = false;
                auto this_result = environment_->get(string_symbolizer_->get_this_id());
                if (this_result) {
                    script_value this_val = std::move(this_result.value());
                    if (this_val.is_object()) {
                        auto obj_holder = this_val.get_object_holder();
                        if (obj_holder->is_class_instance_wrapper) {
                            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                            // First try instance fields
                            if (instance->has_field(identifier->symbol_id)) {
                                // Check if this is a C++ parent property that needs setter method
                                auto class_def = instance->get_class_definition();
                                if (class_def) {
                                    auto cpp_base = class_def->get_cpp_base_class();
                                    if (cpp_base) {
                                        // Check if field has a pre-registered setter ID (fast path)
                                        uint64_t setter_id = cpp_base->get_property_setter_id(identifier->symbol_id);
                                        if (setter_id != 0) {
                                            // This is a C++ property - call the setter method
                                            auto setter = cpp_base->get_method(setter_id, false);
                                            if (setter.is_function()) {
                                                // Call setter with this and value
                                                std::vector<script_value> args = {this_val, value};
                                                auto result = setter.as_function()(args);
                                                if (!result) {
                                                    return checked_result<void>(result.error(), result.message());
                                                }
                                                assigned_to_member = true;
                                            }
                                        }
                                    }
                                }
                                // Not a C++ property or no setter - use regular field assignment
                                if (!assigned_to_member) {
                                    instance->set_field(identifier->symbol_id, value.clone());
                                    assigned_to_member = true;
                                }
                            }
                            // Then try static fields
                            else {
                                auto class_def = instance->get_class_definition();
                                if (class_def && class_def->set_static_field(identifier->symbol_id, value.clone())) {
                                    assigned_to_member = true;
                                }
                            }
                        }
                    }
                }

                if (!assigned_to_member) {
                    // Use environment->assign() which will:
                    // - For static_method_environment: check static fields, then return error if not found
                    // - For method_environment: check 'this' fields, then define locally if not found
                    // - For regular environment: return error if variable doesn't exist
                    JAISCRIPT_TRY(environment_->assign(identifier->symbol_id, value.clone()));
                }
            }

            // FIX #3: Return a valid value, not moved-from
            // value is still valid here since we only cloned it above
            push_value(std::move(value));
        }
        // Check if target is a member expression (property assignment)
        else if (expr->target->get_type() == node_type::member_expr) {
            auto* memberExpr = static_cast<member_expr*>(expr->target.get());
            // Check if this is a static member assignment
            if (memberExpr->is_static) {
                // For static assignment, get the class definition
                if (memberExpr->object->get_type() != node_type::identifier_expr) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "Static member assignment requires a class name");
                }
                auto* ident_expr = static_cast<identifier_expr*>(memberExpr->object.get());
                
                std::string class_name = ident_expr->name;

                auto class_result = environment_->get("__class_" + class_name);
                if (!class_result) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Class '" + class_name + "' not found");
                }
                script_value class_var = std::move(class_result.value());

                if (!class_var.is_object()) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "'" + class_name + "' is not a class");
                }

                auto objHolder = class_var.get_object_holder();
                if (!objHolder || objHolder->type_name != "class_definition") {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "'" + class_name + "' is not a valid class");
                }
                
                auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

                // Evaluate the value
                JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
                script_value value = pop_value();

                // Set the static field
                if (!class_def->set_static_field(memberExpr->member_id, value.clone())) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Cannot assign to static member: field '" + memberExpr->member + "' not found");
                }


                push_value(value);
                return {};
            }

            // Regular member assignment - evaluate the object
            JAISCRIPT_TRY(dispatch_expr(memberExpr->object.get()));
            script_value objectValue = pop_value();

            // Dereference if it's a reference (e.g., from array[index])
            script_value dereferenced = objectValue.deref();

            // Unwrap shared_ptr if needed
            // After refactor: shared_ptr<T> uses same storage, no unwrapping needed

            // Check if it's an object
            if (!dereferenced.is_object()) {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to member of non-object type");
                current_exception_ = script_exception("Cannot assign to member of non-object type", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }

            // Extract the class_instance
            auto objHolder = dereferenced.get_object_holder();
            if (!objHolder) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot assign property to non-object value");
            }
            auto instance = std::static_pointer_cast<class_instance>(objHolder->data);

            // Intern the member name to ID
            uint64_t member_id = string_symbolizer_->intern(memberExpr->member);

            // Check if there's a property setter first (for C++ properties)
            uint64_t setter_id = string_symbolizer_->intern("_set_" + memberExpr->member);
            script_value setter = instance->get_method(setter_id, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {dereferenced, std::move(value.clone())};
                auto result = func(args);
                if (!result) {
                    // Setter failed - propagate error
                    return checked_result<void>(result.error(), result.message());
                }
            } else if (instance->has_field(member_id)) {
                // Direct field assignment (deep copy)
                instance->set_field(member_id, std::move(value.clone()));
            } else {
                // Set exception state instead of throwing
                active_exception_value_ = make_value("Cannot assign to non-existent member '" + memberExpr->member + "'");
                current_exception_ = script_exception("Cannot assign to non-existent member '" + memberExpr->member + "'", memberExpr->location);
                is_unwinding_ = true;
                push_value(make_value());
                return {};
            }
            
            push_value(std::move(value));  // Assignment expressions return the assigned value
        }
        // Check if target is a subscript expression (array[index] or map[key])
        else if (expr->target->get_type() == node_type::binary_expr) {
            auto* binaryExpr = static_cast<binary_expr*>(expr->target.get());
            if (binaryExpr->op.type == token_type::left_bracket) {
                // Evaluate the entire target expression (e.g., nested["nums"][1])
                // This should return a reference if it's a valid lvalue
                JAISCRIPT_TRY(dispatch_expr(expr->target.get()));
                script_value target_ref = pop_value();
                
                // Check if we got a reference
                if (target_ref.is_reference()) {
                    // Get the actual target through the reference
                    auto refHolder = target_ref.get_reference_holder();
                    script_value* target_ptr = refHolder->target;
                    if (!target_ptr) {
                        throw runtime_error("Invalid reference in assignment");
                    }
                    
                    // Assign the value  
                    *target_ptr = std::move(value.clone());
                    push_value(std::move(value));  // Assignment expressions return the assigned value
                } else {
                    // Not a reference - this means the subscript expression didn't
                    // return an lvalue (e.g., trying to assign to a function call result)
                    throw runtime_error("Cannot assign to rvalue expression");
                }
            } else {
                throw runtime_error("Complex assignment targets not yet implemented");
            }
        } else {
            throw runtime_error("Complex assignment targets not yet implemented");
        }
    }
    return {};
}

// statement visitors
checked_result<void> interpreter::visit_expression_stmt(expression_stmt* stmt) {
    JAISCRIPT_TRY(dispatch_expr(stmt->expression.get()));

    // Early exit if exception is propagating
    if (is_unwinding_) return {};

    // Pop the result - expression statements don't produce values
    // (except for top-level expressions in global scope, which are handled by expression_decl)
    pop_value();
    return {};
}

checked_result<void> interpreter::visit_block_stmt(block_stmt* stmt) {
    // Create a new child scope for the block
    // With lazy caching, this is O(1) - no flat_lookup_ copy needed
    auto previous = environment_;
    environment_ = get_pooled_environment(environment_);

    try {
        for (const auto& decl : stmt->declarations) {
            auto result = dispatch_decl(decl.get());

            // IMPORTANT: Clear value stack after each declaration to prevent accumulation
            // This ensures objects are destroyed at statement boundaries, not just at block exit
            // Variable declarations pop their values, but some other declarations (like expression_decl)
            // may leave values on the stack
            if (valueStack_.size() > 0) {
                valueStack_.clear();
            }

            if (!result) {
                // Restore scope before returning
                release_environment(environment_);
                environment_ = previous;
                return result;
            }

            // Check for control flow: break, continue, return, or exceptions
            if (is_unwinding_ || hasBreakRequest_ || hasContinueRequest_ || hasReturnValue_) {
                break;
            }
        }
    } catch (...) {
        // Restore scope even if an error occurs
        release_environment(environment_);
        environment_ = previous;
        throw;
    }

    // Clear the value stack before releasing scope
    // This ensures any lingering object references on the stack are released
    // Block statements don't produce a value, so the stack should be cleared
    valueStack_.clear();

    // Restore the previous scope
    release_environment(environment_);
    environment_ = previous;
    return {};
}

checked_result<void> interpreter::visit_variable_decl(variable_decl* decl) {
    // Check if this is a reference variable declaration
    bool is_reference = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_reference_type) {
        is_reference = true;
    }
    
    // Check if this is a weak_ptr declaration
    bool is_weak_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_weak_ptr_type) {
        is_weak_ptr = true;
    }
    
    // Check if this is a shared_ptr declaration
    bool is_shared_ptr = false;
    if (decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type) {
        is_shared_ptr = true;
    }
    
    if (is_weak_ptr) {
        // weak_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create empty weak_ptr
            script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_ref_);
            environment_->define(decl->name_id, std::move(weak));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value value = pop_value();
            
            // Handle different initialization cases
            if (value.is_null()) {
                // Initialize with null - create empty weak_ptr
                script_value weak = script_value::make_empty_weak_ptr(decl->type, engine_ref_);
                environment_->define(decl->name_id, std::move(weak));
            } else if (value.is_weak_ptr()) {
                // Initialize with another weak_ptr - copy it
                environment_->define(decl->name_id, std::move(value));
            } else if (value.type() == script_value_type::jai_shared_ptr_type) {
                // Initialize with shared_ptr - create weak_ptr from it
                auto weak_result = script_value::make_weak_ptr(value, engine_ref_);
                if (!weak_result) {
                    return checked_result<void>(weak_result.error(), weak_result.message());
                }
                environment_->define(decl->name_id, std::move(weak_result.value()));
            } else if (value.type() == script_value_type::jai_object_type) {
                // Helpful error for value-semantic objects
                auto type_info = decl->type;
                std::string weak_type = type_info && !type_info->type_params.empty() ?
                    "weak_ptr<" + type_info->type_params[0]->type_name + ">" : "weak_ptr";
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot initialize " + weak_type + " from a value-semantic object. Use shared_ptr<T> to enable reference semantics: auto obj = shared_ptr<T>(...); auto weak = weak_ptr<T>(obj);");
            } else {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                auto weak_type_info = decl->type;
                std::string weak_type = weak_type_info && !weak_type_info->type_params.empty() ?
                    "weak_ptr<" + weak_type_info->type_params[0]->type_name + ">" : "weak_ptr";
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                    "Cannot initialize " + weak_type + " with " + type_name + ". Use shared_ptr<T> to enable reference semantics.");
            }
        }
    } else if (is_shared_ptr) {
        // shared_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create null shared_ptr
            script_value null_ptr = make_value();
            null_ptr.set_type_info(decl->type);  // Mark as shared_ptr type
            environment_->define(decl->name_id, std::move(null_ptr));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value value = pop_value();
            
            // Handle different initialization cases
            if (value.is_null()) {
                // Initialize with null - that's fine
                value.set_type_info(decl->type);  // Mark as shared_ptr type
                environment_->define(decl->name_id, std::move(value));
            } else if (value.is_weak_ptr()) {
                throw runtime_error("Cannot initialize shared_ptr directly from weak_ptr - use weak.lock() instead");
            } else if (value.type() == script_value_type::jai_object_type ||
                      value.type() == script_value_type::jai_shared_ptr_type) {
                // Initialize with object/shared_ptr - that's fine, objects are already shared_ptr
                // Mark the type as shared_ptr to ensure reference semantics
                value.set_type_info(decl->type);
                environment_->define(decl->name_id, std::move(value));
            } else {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("Cannot initialize shared_ptr with " + type_name);
            }
        }
    } else if (is_reference) {
        // Reference variable - must have initializer
        if (!decl->initializer) {
            throw runtime_error("Reference variable '" + decl->name + "' must be initialized");
        }
        
        // Check if initializer is an identifier (can take reference)
        if (decl->initializer->get_type() == node_type::identifier_expr) {
            auto* identExpr = static_cast<identifier_expr*>(decl->initializer.get());
            // Get the target variable's address
            uint64_t targetSymbolId = string_symbolizer_->intern(identExpr->name);
            
            // Get a pointer to the target value in the environment
            // This is safe because environment uses unordered_map which doesn't invalidate pointers
            script_value* targetPtr = environment_->get_value_ptr(targetSymbolId);
            if (!targetPtr) {
                throw runtime_error("Cannot take reference of undefined variable '" + identExpr->name + "'");
            }
            
            // Check if the target is itself a reference
            if (targetPtr->is_reference()) {
                // Reference to reference - get the final target and its environment
                auto refHolder = targetPtr->get_reference_holder();
                targetPtr = refHolder->target;
                // Use the original reference's environment
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name_id, std::move(refValue));
            } else {
                // Regular reference - use current environment
                script_value refValue = script_value::make_reference(targetPtr, environment_);
                environment_->define(decl->name_id, std::move(refValue));
            }
        } else {
            // For other expressions, evaluate them and check if they return a reference
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            script_value result = pop_value();
            
            // If the result is a reference, we can create a reference to its target
            if (result.is_reference()) {
                auto refHolder = result.get_reference_holder();
                script_value* targetPtr = refHolder->target;
                auto target_env = refHolder->sourceEnv.lock();
                if (!target_env) {
                    throw runtime_error("Reference target environment has been destroyed");
                }
                // Create a new reference to the same target
                script_value refValue = script_value::make_reference(targetPtr, target_env);
                environment_->define(decl->name_id, std::move(refValue));
            } else {
                throw runtime_error("Cannot take reference of non-lvalue expression");
            }
        }
    } else {
        // Regular variable declaration
        script_value value = make_value();
        if (decl->initializer) {
            JAISCRIPT_TRY(dispatch_expr(decl->initializer.get()));
            value = std::move(pop_value());

            // Only clone if initializing from an lvalue (existing object)
            // Temporaries (constructor calls, expressions) should use move semantics
            if (is_lvalue_expression(decl->initializer.get())) {
                // Initializing from an existing object - deep copy
                value = value.clone();
            }
            // else: Initializing from a temporary - use move semantics (no clone)
        }
        // If no initializer, value remains null

        // === STRONG TYPES: Set type_info based on declaration ===
        // - Explicit type (int, float, var, etc.): Use declared type (locks the variable)
        // - auto with initializer: Infer type from value (locks to inferred type)
        // - auto without initializer: Keep nullptr (first assignment will lock type)
        if (decl->type) {
            // Explicit type declaration (int x, float y, var z, etc.)
            // Set declared type - this locks the variable's type
            value.set_type_info(decl->type);
        } else if (decl->initializer && value.get_type_info()) {
            // auto with initializer - type is inferred from value and locked
            // Value already has type_info from make_value() or evaluation
            // Keep the value's type_info (already set)
        }
        // else: auto without initializer - type_info remains nullptr (uninitialized)
        // First assignment will lock the type

        // Define in the current environment
        environment_->define(decl->name_id, std::move(value));
        // After move, value is in moved-from state, so don't access it
    }
    return {};
}

// === STRONG TYPES: Type enforcement for assignment ===
checked_result<script_value> interpreter::enforce_type_compatibility(
    script_value value,
    type_info_ptr target_type,
    const std::string& var_name
) {
    // Case 1: Uninitialized variable (auto x;) - lock to source type
    if (!target_type) {
        // First assignment locks the type - value already has its own type
        return std::move(value);
    }

    // Case 2: Any type (var x) - allow anything
    if (target_type->base_type == script_value_type::jai_any_type) {
        // Keep any_type on the variable, but store the value
        // The value's internal type is preserved for operations
        return std::move(value);
    }

    // Case 3: Locked type - enforce compatibility
    auto source_type = value.type();
    auto target = target_type->base_type;

    // Fast path: same type (but NOT for object types - need to compare class names)
    if (source_type == target && target != script_value_type::jai_object_type) {
        return std::move(value);
    }

    // Numeric conversions (int <-> float)
    if (target == script_value_type::jai_int_type) {
        if (source_type == script_value_type::jai_float_type) {
            // float -> int (truncate)
            return make_value(static_cast<script_int>(value.as_float()));
        }
        if (source_type == script_value_type::jai_bool_type) {
            // bool -> int
            return make_value(static_cast<script_int>(value.as_bool() ? 1 : 0));
        }
    }

    if (target == script_value_type::jai_float_type) {
        if (source_type == script_value_type::jai_int_type) {
            // int -> float (widening)
            return make_value(static_cast<script_float>(value.as_int()));
        }
        if (source_type == script_value_type::jai_bool_type) {
            // bool -> float
            return make_value(static_cast<script_float>(value.as_bool() ? 1.0 : 0.0));
        }
    }

    if (target == script_value_type::jai_bool_type) {
        // Truthy conversion to bool
        return make_value(is_truthy(value));
    }

    if (target == script_value_type::jai_string_type) {
        // to_string conversion
        return make_value(value.to_string());
    }

    // Null can be assigned to object/shared_ptr/weak_ptr types
    // Keep the target type_info so the variable remains typed
    if (source_type == script_value_type::jai_null_type) {
        if (target == script_value_type::jai_object_type ||
            target == script_value_type::jai_shared_ptr_type ||
            target == script_value_type::jai_weak_ptr_type) {
            // Return null but preserve the target type for future assignments
            script_value null_val = make_value();
            null_val.set_type_info(target_type);
            return null_val;
        }
    }

    // Class type compatibility for object types (nominal typing)
    if (target == script_value_type::jai_object_type &&
        source_type == script_value_type::jai_object_type) {

        auto source_type_info = value.get_type_info();
        if (source_type_info && target_type) {
            // Compare class names - must match exactly or source must be subclass of target
            const std::string& source_class_name = source_type_info->type_name;
            const std::string& target_class_name = target_type->type_name;

            // Same class name - compatible
            if (source_class_name == target_class_name) {
                return std::move(value);
            }

            // Check inheritance - source must be derived from target (child -> parent is OK)
            // We need to get the actual class instance to check the inheritance chain
            try {
                auto instance = value.as<std::shared_ptr<class_instance>>();
                if (instance) {
                    auto class_def = instance->get_class_definition();
                    if (class_def) {
                        // Walk up the inheritance chain looking for target class
                        auto current = class_def;
                        while (current) {
                            for (const auto& parent : current->get_parent_classes()) {
                                if (parent && parent->get_name() == target_class_name) {
                                    // Source is derived from target - compatible
                                    return std::move(value);
                                }
                            }
                            // Move up to first parent for next iteration (single inheritance path)
                            current = current->get_parent();
                        }
                    }
                }
            } catch (...) {
                // Not a class_instance or extraction failed - fall through to error
            }

            // Classes are incompatible - try constructor-based conversion
            // Look for a constructor Target(Source) in the target class
            auto ctor_result = environment_->get(target_class_name);
            if (ctor_result && ctor_result.value().is_function()) {
                const script_function& ctor = ctor_result.value().as_function();
                std::vector<script_value> ctor_args;
                ctor_args.push_back(value);

                try {
                    auto result = ctor(ctor_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                } catch (const runtime_error& e) {
                    std::string error_msg = e.what();
                    if (error_msg.find("No constructor found") == std::string::npos) {
                        // Constructor exists but failed - propagate the error
                        throw runtime_error("Error converting " + source_class_name +
                                          " to " + target_class_name + ": " + error_msg);
                    }
                    // No matching constructor - fall through to type mismatch error
                }
            }

            // No suitable constructor found - incompatible types
            std::string msg = "Cannot assign " + source_class_name + " to ";
            if (!var_name.empty()) {
                msg += "variable '" + var_name + "' of type " + target_class_name;
            } else {
                msg += "type " + target_class_name;
            }
            msg += " (incompatible class types)";
            return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), msg);
        }
    }

    // Primitive-to-object conversion via constructor
    // Try to convert primitives (int, float, string, bool, char) to object types via constructors
    if (target == script_value_type::jai_object_type && target_type && !target_type->type_name.empty()) {
        bool is_primitive = (source_type == script_value_type::jai_int_type ||
                            source_type == script_value_type::jai_float_type ||
                            source_type == script_value_type::jai_string_type ||
                            source_type == script_value_type::jai_bool_type ||
                            source_type == script_value_type::jai_char_type);

        if (is_primitive) {
            const std::string& target_class_name = target_type->type_name;

            // Try to find and call the constructor with the primitive value
            auto ctor_result = environment_->get(target_class_name);
            if (ctor_result && ctor_result.value().is_function()) {
                const script_function& ctor = ctor_result.value().as_function();
                std::vector<script_value> ctor_args;
                ctor_args.push_back(value);

                try {
                    auto result = ctor(ctor_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                } catch (const runtime_error& e) {
                    std::string error_msg = e.what();
                    if (error_msg.find("No constructor found") == std::string::npos) {
                        // Constructor exists but failed - propagate the error
                        throw runtime_error("Error converting " + get_type_name(source_type) +
                                          " to " + target_class_name + ": " + error_msg);
                    }
                    // No matching constructor - fall through to type mismatch error
                }
            }
        }
    }

    // Incompatible types - error
    std::string source_name = value.get_type_info() ? value.get_type_info()->type_name : "unknown";
    if (source_name.empty() || source_name == "unknown") {
        source_name = get_type_name(source_type);
    }
    std::string target_name = target_type->type_name;
    std::string msg = "Cannot assign " + source_name + " to ";
    if (!var_name.empty()) {
        msg += "variable '" + var_name + "' of type " + target_name;
    } else {
        msg += "type " + target_name;
    }
    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch), msg);
}

// Helper to convert value to string, checking for to_string() method on objects
std::string interpreter::value_to_string_with_method(const script_value& val) {
    if (val.type() == script_value_type::jai_object_type) {
        // Use get_class_instance() which safely returns nullptr if not a class instance
        auto instance = const_cast<script_value&>(val).get_class_instance();
        if (instance) {
            auto method_id = string_symbolizer_->intern("to_string");
            auto method_val = instance->get_method(method_id, false);
            if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
                script_value bound = create_bound_method(val, method_val);
                const script_function& method = bound.as_function();
                std::vector<script_value> no_args;
                auto result = method(no_args);
                if (result.has_value() && result.value().is_string()) {
                    return result.value().as_string();
                }
            }
        }
    }
    return val.to_string();
}

// Binary operation helpers
checked_result<script_value> interpreter::evaluate_arithmetic(const script_value& left, token_type op, const script_value& right) {
    // Special case for string concatenation (use move to avoid copying the temporary)
    // Check for to_string() method on objects before falling back to default
    if (op == token_type::plus && (left.is_string() || right.is_string())) {
        return make_value(value_to_string_with_method(left) + value_to_string_with_method(right));
    }

    // Fast path for pure integer arithmetic (avoid float conversion)
    if (left.is_int() && right.is_int()) {
        script_int leftInt = left.as_int();
        script_int rightInt = right.as_int();

        switch (op) {
            case token_type::plus:
                return make_value(leftInt + rightInt);
            case token_type::minus:
                return make_value(leftInt - rightInt);
            case token_type::star:
                return make_value(leftInt * rightInt);
            case token_type::slash:
                if (rightInt == 0) {
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::division_by_zero),
                        "Division by zero");
                }
                // Integer division returns integer (C++ semantics)
                return make_value(leftInt / rightInt);
            case token_type::percent:
                if (rightInt == 0) {
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::modulo_by_zero),
                        "Division by zero");
                }
                return make_value(leftInt % rightInt);
            default:
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::unknown_operator),
                    "Unknown arithmetic operator");
        }
    }

    // Mixed or floating point arithmetic path
    script_float leftNum, rightNum;

    if (left.is_int()) {
        leftNum = static_cast<script_float>(left.as_int());
    } else if (left.is_float()) {
        leftNum = left.as_float();
    } else {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Left operand must be numeric");
    }

    if (right.is_int()) {
        rightNum = static_cast<script_float>(right.as_int());
    } else if (right.is_float()) {
        rightNum = right.as_float();
    } else {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Right operand must be numeric");
    }

    switch (op) {
        case token_type::plus:
            return make_value(leftNum + rightNum);
        case token_type::minus:
            return make_value(leftNum - rightNum);
        case token_type::star:
            return make_value(leftNum * rightNum);
        case token_type::slash:
            if (rightNum == 0.0) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::division_by_zero),
                    "Division by zero");
            }
            return make_value(leftNum / rightNum);
        case token_type::percent:
            if (rightNum == 0.0) {
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::modulo_by_zero),
                    "Division by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown arithmetic operator");
    }
}

checked_result<script_value> interpreter::evaluate_comparison(const script_value& left, token_type op, const script_value& right) {
    // Handle weak_ptr comparisons with null
    if ((left.is_weak_ptr() && right.is_null()) || (left.is_null() && right.is_weak_ptr())) {
        if (op == token_type::equal_equal || op == token_type::bang_equal) {
            // For weak_ptr, null comparison checks if expired
            bool is_expired = false;
            if (left.is_weak_ptr()) {
                if (left.is_weak_ptr()) {
                    auto weak_ptr = left.get_weak_ptr();
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (left.get_object_holder() != nullptr) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = left.get_object_holder();
                    is_expired = (holder->type_id == weak_ptr_holder_type_id_ && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            } else {
                // right is weak_ptr
                if (right.is_weak_ptr()) {
                    auto weak_ptr = right.get_weak_ptr();
                    // Check if weak_ptr is expired (includes default-constructed)
                    is_expired = weak_ptr.expired();
                } else if (right.get_object_holder() != nullptr) {
                    // weak_ptr_holder type - check if it contains an actual value
                    auto holder = right.get_object_holder();
                    is_expired = (holder->type_id == weak_ptr_holder_type_id_ && !holder->data);
                } else {
                    // Other cases - consider expired
                    is_expired = true;
                }
            }

            if (op == token_type::equal_equal) {
                return make_value(is_expired);  // weak == null is true if expired
            } else {
                return make_value(!is_expired); // weak != null is true if not expired
            }
        }
    }

    // Handle null comparisons
    if (left.is_null() || right.is_null()) {
        switch (op) {
            case token_type::equal_equal:
                return make_value(left.is_null() && right.is_null());
            case token_type::bang_equal:
                return make_value(!(left.is_null() && right.is_null()));
            default:
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::invalid_operation),
                    "Cannot compare null values with relational operators");
        }
    }

    // For now, only support numeric and string comparisons
    if (left.is_string() && right.is_string()) {
        const auto& leftStr = left.as_string();
        const auto& rightStr = right.as_string();

        switch (op) {
            case token_type::less:
                return make_value(leftStr < rightStr);
            case token_type::less_equal:
                return make_value(leftStr <= rightStr);
            case token_type::greater:
                return make_value(leftStr > rightStr);
            case token_type::greater_equal:
                return make_value(leftStr >= rightStr);
            case token_type::equal_equal:
                return make_value(leftStr == rightStr);
            case token_type::bang_equal:
                return make_value(leftStr != rightStr);
            case token_type::spaceship: {
                // Three-way comparison for strings
                int cmp = leftStr.compare(rightStr);
                return make_value(cmp < 0 ? script_int(-1) : (cmp > 0 ? script_int(1) : script_int(0)));
            }
            default:
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::unknown_operator),
                    "Unknown comparison operator");
        }
    }

    // Numeric comparison
    script_float leftNum = to_numeric(left).as_float();
    script_float rightNum = to_numeric(right).as_float();

    switch (op) {
        case token_type::less:
            return make_value(leftNum < rightNum);
        case token_type::less_equal:
            return make_value(leftNum <= rightNum);
        case token_type::greater:
            return make_value(leftNum > rightNum);
        case token_type::greater_equal:
            return make_value(leftNum >= rightNum);
        case token_type::equal_equal:
            return make_value(leftNum == rightNum);
        case token_type::bang_equal:
            return make_value(leftNum != rightNum);
        case token_type::spaceship: {
            // Three-way comparison for numbers
            // Return -1 if less, 0 if equal, 1 if greater
            if (leftNum < rightNum) return make_value(script_int(-1));
            else if (leftNum > rightNum) return make_value(script_int(1));
            else return make_value(script_int(0));
        }
        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown comparison operator");
    }
}

checked_result<script_value> interpreter::evaluate_logical(const script_value& left, token_type op, const script_value& right) {
    bool leftTruthy = is_truthy(left);

    switch (op) {
        case token_type::ampersand_ampersand:
            // Return boolean result (not JavaScript-style operand values)
            if (!leftTruthy) {
                return make_value(false);  // Left is falsy -> result is false
            }
            return make_value(is_truthy(right));  // Return truthiness of right

        case token_type::pipe_pipe:
            // Return boolean result (not JavaScript-style operand values)
            if (leftTruthy) {
                return make_value(true);  // Left is truthy -> result is true
            }
            return make_value(is_truthy(right));  // Return truthiness of right

        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown logical operator");
    }
}

checked_result<script_value> interpreter::evaluate_bitwise(const script_value& left, token_type op, const script_value& right) {
    // Bitwise operations only work on integers
    if (!left.is_int() || !right.is_int()) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::invalid_numeric_operand),
            "Bitwise operations require integer operands");
    }

    script_int leftInt = left.as_int();
    script_int rightInt = right.as_int();

    switch (op) {
        case token_type::ampersand:
            return make_value(leftInt & rightInt);
        case token_type::pipe:
            return make_value(leftInt | rightInt);
        case token_type::caret:
            return make_value(leftInt ^ rightInt);
        case token_type::left_shift:
            return make_value(leftInt << rightInt);
        case token_type::right_shift:
            return make_value(leftInt >> rightInt);
        default:
            return checked_result<script_value>(
                make_error_code(runtime_error_code::unknown_operator),
                "Unknown bitwise operator");
    }
}


// Placeholder implementations for remaining visitors
checked_result<void> interpreter::visit_call_expr(call_expr* expr) {
    // Special handling for weak_from_this() and shared_from_this()
    if (expr->callee->get_type() == node_type::identifier_expr) {
        auto* ident_expr = static_cast<identifier_expr*>(expr->callee.get());
        // Use interned symbol IDs for fast comparison
        if (ident_expr->symbol_id == weak_from_this_id_ || ident_expr->symbol_id == shared_from_this_id_) {
            // These functions take no arguments
            if (!expr->arguments.empty()) {
                return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch),
                    ident_expr->name + "() takes no arguments");
            }

            // Get 'this' from the current environment
            auto this_result = environment_->get(string_symbolizer_->get_this_id());
            if (!this_result) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                    ident_expr->name + "() can only be called from within a method");
            }
            script_value this_val = std::move(this_result.value());
            if (!this_val.is_object()) {
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                    ident_expr->name + "() can only be called from within a method");
            }

            if (ident_expr->symbol_id == weak_from_this_id_) {
                // Create a weak_ptr from the 'this' object
                auto weak_result = script_value::make_weak_ptr(this_val, engine_ref_);
                if (!weak_result) {
                    return checked_result<void>(weak_result.error(), weak_result.message());
                }
                push_value(std::move(weak_result.value()));
                return {};
            } else {  // shared_from_this
                // Just return the shared_ptr (which is already 'this')
                push_value(this_val);
                return {};
            }
        }
    }

    // Evaluate the callee expression
    JAISCRIPT_TRY(dispatch_expr(expr->callee.get()));
    script_value callee = pop_value();

    // Check if the callee is a function
    if (!callee.is_function()) {
        return checked_result<void>(make_error_code(runtime_error_code::not_a_function));  // [ErrorText] Not a function
    }
    
    // Use a local vector for arguments to avoid issues with nested calls
    std::vector<script_value> arguments;
    arguments.reserve(expr->arguments.size());
    
    // Also track argument metadata for reference parameters
    std::vector<std::pair<uint64_t, std::shared_ptr<environment>>> argMetadata;
    argMetadata.reserve(expr->arguments.size());
    
    for (const auto& argExpr : expr->arguments) {
        // Check if this is a simple identifier (needed for references)
        if (argExpr->get_type() == node_type::identifier_expr) {
            auto* identExpr = static_cast<identifier_expr*>(argExpr.get());
            // Get the symbol ID for this variable
            uint64_t symbol_id = string_symbolizer_->intern(identExpr->name);
            argMetadata.emplace_back(symbol_id, environment_);
        } else {
            // Not an identifier - can't take reference
            argMetadata.emplace_back(UINT64_MAX, nullptr);
        }
        
        // Evaluate argument with exception handling
        try {
            JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
            arguments.emplace_back(std::move(pop_value()));
        } catch (const script_exception& e) {
            // Convert to interpreter exception state
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = e;
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return {};
        } catch (const std::runtime_error& e) {
            // Convert runtime errors to script exceptions
            active_exception_value_ = make_value(std::string(e.what()));
            current_exception_ = script_exception(e.what());
            is_unwinding_ = true;
            push_value(make_value());  // Push null for the failed call
            return {};
        }
    }
    
    // Store argument metadata in a member variable so call_function can access it
    current_arg_metadata_ = std::move(argMetadata);
    
    // Call the function - now returns checked_result instead of throwing
    const script_function& func = callee.as_function();
    auto result_checked = func(arguments);

    // Clear argument metadata
    current_arg_metadata_.clear();

    // Check if function call succeeded
    if (!result_checked) {
        // Function returned an error via checked_result - propagate it
        return checked_result<void>(result_checked.error(), result_checked.message());
    }

    // Push successful result onto the stack
    push_value(std::move(result_checked.value()));
    return {};
}

checked_result<void> interpreter::visit_member_expr(member_expr* expr) {
    // Check if this is a static member access (::)
    if (expr->is_static) {
        // For static access, the object should be an identifier (class or namespace name)
        identifier_expr* ident_expr = nullptr;
        if (expr->object->get_type() == node_type::identifier_expr) {
            ident_expr = static_cast<identifier_expr*>(expr->object.get());
        }
        // Handle nested namespace access: outer::inner::getValue()
        // Build the full namespace path
        std::string name;
        uint64_t name_id;

        if (ident_expr) {
            // Simple case: just an identifier
            name = ident_expr->name;
            name_id = ident_expr->symbol_id;

            // Cache symbol ID if not already done
            if (name_id == UINT64_MAX) {
                name_id = string_symbolizer_->intern(name);
                ident_expr->symbol_id = name_id;
            }
        } else if (expr->object->get_type() == node_type::member_expr) {
            auto* member_expr_obj = static_cast<member_expr*>(expr->object.get());
            // Nested namespace: outer::inner where the object is "outer::inner" (a member_expr)
            // Recursively build the full path
            std::function<std::string(expression*)> build_namespace_path = [&](expression* e) -> std::string {
                if (e->get_type() == node_type::identifier_expr) {
                    return static_cast<identifier_expr*>(e)->name;
                } else if (e->get_type() == node_type::member_expr) {
                    auto* member = static_cast<member_expr*>(e);
                    if (member->is_static) {
                        // This is a :: access, continue building the path
                        return build_namespace_path(member->object.get()) + "::" + member->member;
                    }
                }
                return "";
            };

            name = build_namespace_path(expr->object.get());
            if (name.empty()) {
                return checked_result<void>(make_error_code(jai::runtime_error_code::type_mismatch));  // [ErrorText] Invalid namespace path
            }
            name_id = string_symbolizer_->intern(name);
        } else {
            // Static member access requires an identifier or namespace path
            return checked_result<void>(make_error_code(jai::runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // PRIORITY 1: Check for namespace with this name FIRST
        // Namespaces can override class static methods
        auto ns_it = namespaces_.find(name_id);
        bool is_namespace = (ns_it != namespaces_.end());

        // PRIORITY 1.5: Special case for namespace::class::static_member
        // ONLY if name is NOT a namespace itself
        // For "my::nested::cat::meow()", name would be "my::nested::cat" which is NOT a namespace
        // We need to detect this pattern and split it into namespace="my::nested" and class="cat"
        if (!is_namespace && name.find("::") != std::string::npos) {
            size_t last_colon = name.rfind("::");
            std::string potential_ns = name.substr(0, last_colon);
            std::string potential_class = name.substr(last_colon + 2);

            uint64_t ns_id = string_symbolizer_->intern(potential_ns);
            uint64_t class_id = string_symbolizer_->intern(potential_class);

            auto ns_check = namespaces_.find(ns_id);
            if (ns_check != namespaces_.end()) {
                auto class_check = ns_check->second->classes.find(class_id);
                if (class_check != ns_check->second->classes.end()) {
                    // Found! This is namespace::class, and we're accessing a static member
                    auto class_def = class_check->second;

                    // Try to get the static method (use pre-computed member_id from parser)
                    script_value static_method = class_def->get_static_method(expr->member_id, false);
                    if (!static_method.is_null()) {
                        push_value(static_method);
                        return {};
                    }

                    // Method not found - might be trying to access the class constructor
                    // Fall through to handle namespace::class access
                }
            }
        }

        // PRIORITY 2: Handle namespace members
        if (is_namespace) {
            auto& ns_data = ns_it->second;

            // Use parser's pre-computed member ID (always set by parser)
            // Look for function in namespace (handles overloads by arity)
            // This will be called later with arguments, so we need to return a callable
            auto func_it = ns_data->functions.find(expr->member_id);
            if (func_it != ns_data->functions.end()) {
                // Create a namespace function wrapper
                // Store all overloads for arity-based dispatch
                auto overloads = func_it->second;

                // Check if there's also a class with the same name (for fallback)
                std::shared_ptr<class_definition> fallback_class;
                auto class_var_result = environment_->get("__class_" + name);
                if (class_var_result) {
                    script_value class_var = std::move(class_var_result.value());
                    if (class_var.is_object()) {
                        auto objHolder = class_var.get_object_holder();
                        if (objHolder && objHolder->type_name == "class_definition") {
                            fallback_class = std::static_pointer_cast<class_definition>(objHolder->data);
                        }
                    }
                }
                // No class with this name - that's fine, fallback_class remains null

                // Create a script_function that dispatches based on arity
                // Capture namespace_id to provide access to namespace variables
                uint64_t namespace_id = name_id;
                uint64_t member_id = expr->member_id;
                script_function namespace_func = [this, overloads, name, namespace_id, fallback_class, member_id](const std::vector<script_value>& args) -> checked_result<script_value> {
                    // Find matching overload by arity in namespace
                    for (const auto& func_decl : overloads) {
                        if (func_decl->parameters.size() == args.size()) {
                            // Create an environment with namespace variables accessible
                            auto ns_env = std::make_shared<environment>(environment_, string_symbolizer_);

                            // Add all namespace variables to this environment
                            auto ns_it = namespaces_.find(namespace_id);
                            if (ns_it != namespaces_.end()) {
                                for (const auto& [var_id, var_value] : ns_it->second->variables) {
                                    ns_env->define(var_id, var_value);
                                }
                            }

                            // Found matching arity - create script_defined_function with namespace environment
                            auto script_func = std::make_shared<script_defined_function>(
                                func_decl->name,
                                func_decl->parameters,
                                func_decl->return_type,
                                func_decl->body,
                                ns_env  // Environment with namespace variables
                            );
                            return call_function(*script_func, args);
                        }
                    }

                    // No matching overload found in namespace - try fallback to class static method
                    if (fallback_class) {
                        // get_static_method with false doesn't throw - returns null if not found
                        script_value static_method = fallback_class->get_static_method(member_id, false);
                        if (!static_method.is_null() && static_method.is_function()) {
                            // Call the class static method
                            auto func = static_method.as_function();
                            return func(args);
                        }
                    }

                    // Neither namespace nor class has matching method
                    return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                        "No matching function overload in namespace '" + name + "' for " + std::to_string(args.size()) + " arguments");
                };

                push_value(script_value::make_function(namespace_func, engine_ref_));
                return {};
            }

            // Look for variable in namespace
            auto var_it = ns_data->variables.find(expr->member_id);
            if (var_it != ns_data->variables.end()) {
                push_value(var_it->second);
                return {};
            }

            // Look for class in namespace
            // Note: namespace::class::static_member is handled earlier (before namespace lookup)
            auto class_it = ns_data->classes.find(expr->member_id);
            if (class_it != ns_data->classes.end()) {
                // Found a class in the namespace - return the constructor
                auto ctor_result = environment_->get(expr->member_id);
                if (!ctor_result) {
                    return checked_result<void>(ctor_result.error(), ctor_result.message());
                }
                push_value(std::move(ctor_result.value()));
                return {};
            }

            // Member not found in namespace - fall through to check class static methods
        }

        // PRIORITY 2: Look up the class definition
        auto class_var_result = environment_->get("__class_" + name);
        if (!class_var_result) {
            // Class not found
            return checked_result<void>(class_var_result.error(), class_var_result.message());
        }
        script_value class_var = std::move(class_var_result.value());

        if (!class_var.is_object()) {
            // Not a class
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Extract the class definition
        auto objHolder = class_var.get_object_holder();
        if (!objHolder || objHolder->type_name != "class_definition") {
            // Not a valid class
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        auto class_def = std::static_pointer_cast<class_definition>(objHolder->data);

        // Try static method first (most common case for :: access)
        // get_static_method with false doesn't throw - returns null if not found
        script_value static_method = class_def->get_static_method(expr->member_id, false);
        if (!static_method.is_null()) {
            push_value(static_method);
            return {};
        }

        // Try static field access
        // get_static_field returns null if not found
        script_value static_value = class_def->get_static_field(expr->member_id);
        if (!static_value.is_null()) {
            push_value(static_value);
            return {};
        }

        // Try getter method as fallback (for C++ bound properties)
        std::string getter_name = "_get_" + expr->member;
        uint64_t getter_id = string_symbolizer_->intern(getter_name);
        // get_static_method with false doesn't throw - returns null if not found
        script_value getter_method = class_def->get_static_method(getter_id, false);
        if (!getter_method.is_null() && getter_method.is_function()) {
            // Call the getter with no arguments to get the live C++ value
            auto func = getter_method.as_function();
            std::vector<script_value> no_args;
            auto result = func(no_args);
            if (!result) {
                // Function returned error - propagate it up
                return checked_result<void>(result.error(), result.message());
            }
            script_value static_value = std::move(result.value());
            push_value(static_value);
            return {};
        }

        // Class has no static member
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
    }

    // Check if this is a super:: member access
    bool is_super_access = expr->object->get_type() == node_type::super_expr;

    // Evaluate the object expression
    JAISCRIPT_TRY(dispatch_expr(expr->object.get()));
    script_value objectValue = pop_value();

    // Dereference if needed - subscript access returns references
    objectValue = objectValue.deref();

    // Handle super:: member access specially
    if (is_super_access) {
        // objectValue is 'this' from visit_super_expr
        if (!objectValue.is_object()) {
            // super:: used on non-object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Get the class instance
        auto objHolder = objectValue.get_object_holder();
        if (!objHolder || !objHolder->data) {
            // super:: used on non-class object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Both script and C++ classes store class_instance in data
        auto instance = std::static_pointer_cast<class_instance>(objHolder->data);
        if (!instance) {
            // super:: used on non-class object
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Get the class definition and its parent
        auto class_def = instance->get_class_definition();
        if (!class_def) {
            // Class definition not found for super:: access
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        auto parent_def = class_def->get_parent();
        if (!parent_def) {
            // super:: used in class with no parent
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
        }

        // Look for the method in the parent class (use pre-computed member_id from parser)
        script_value method = parent_def->get_method(expr->member_id);
        if (method.is_null()) {
            // Parent class has no method
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
        }

        // Return a bound method that calls the parent's implementation
        push_value(create_bound_method(objectValue, method));
        return {};
    }

    // Handle string methods
    if (objectValue.is_string()) {
        auto methodIt = string_methods_.find(expr->member_id);
        if (methodIt != string_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the string value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                return method(this, capturedValue, args);
            };

            push_value(script_value::make_function(boundMethod, engine_ref_));
            return {};
        }
        else {
            // Set exception state instead of throwing
            active_exception_value_ = make_value("String has no method '" + expr->member + "'");
            current_exception_ = script_exception("String has no method '" + expr->member + "'", expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return {};
        }
    }

    // Handle array methods
    if (objectValue.is_array()) {
        auto methodIt = array_methods_.find(expr->member_id);
        if (methodIt != array_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the array value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                return method(this, capturedValue, args);
            };

            push_value(script_value::make_function(boundMethod, engine_ref_));
            return {};
        }
        else {
            // Set exception state instead of throwing
            active_exception_value_ = make_value("Array has no method '" + expr->member + "'");
            current_exception_ = script_exception("Array has no method '" + expr->member + "'", expr->location);
            is_unwinding_ = true;
            push_value(make_value());
            return {};
        }
    }

    // Handle map methods
    if (objectValue.is_map()) {
        auto methodIt = map_methods_.find(expr->member_id);
        if (methodIt != map_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the map value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                return method(this, capturedValue, args);
            };

            push_value(script_value::make_function(boundMethod, engine_ref_));
            return {};
        }
        else {
            // Map has no method
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
        }
    }

    // Handle weak_ptr methods
    if (objectValue.is_weak_ptr()) {
        auto methodIt = weak_ptr_methods_.find(expr->member_id);
        if (methodIt != weak_ptr_methods_.end()) {
            // Found the method in the registry
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the weak_ptr value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                return method(this, capturedValue, args);
            };

            push_value(script_value::make_function(boundMethod, engine_ref_));
            return {};
        }
        else {
            // weak_ptr has no method
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
        }
    }
    
    // Handle shared_ptr methods (shared_ptr<T> explicitly marked objects)
    // After refactor: check type_info marker instead of storage type
    if (objectValue.get_type_info() &&
        objectValue.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
        // This is an explicitly marked shared_ptr<T> object
        auto methodIt = shared_ptr_methods_.find(expr->member_id);
        if (methodIt != shared_ptr_methods_.end()) {
            // Found the method in the registry (reset, use_count, unique)
            const builtin_method& method = methodIt->second;

            // Create a wrapper function that captures the shared_ptr value by moving it
            script_function boundMethod = [this, capturedValue = std::move(objectValue), method](const std::vector<script_value>& args) mutable -> checked_result<script_value> {
                return method(this, capturedValue, args);
            };

            push_value(script_value::make_function(boundMethod, engine_ref_));
            return {};
        }
        // Method not found in shared_ptr built-ins - forward to the underlying object
    }

    // After refactor: shared_ptr<T> uses same storage as regular objects
    // No unwrapping needed - just access the object_holder directly

    // Check if it's an object (only objects have members/methods)
    if (!objectValue.is_object()) {
        // Cannot access member on non-object type
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Extract the class_instance from the object
    auto objHolder = objectValue.get_object_holder();

    // Get the class_instance - both C++ and script classes use class_instance wrapper
    // (script_class_instance inherits from class_instance)
    if (!objHolder->is_class_instance_wrapper) {
        // Cannot access member on non-class object
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    std::shared_ptr<class_instance> instance = std::static_pointer_cast<class_instance>(objHolder->data);
    if (!instance) {
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Use pre-computed member_id from parser
    uint64_t member_id = expr->member_id;

    // Check for property getter method (C++ properties and inherited properties)
    // OPTIMIZATION: Cache getter_id on AST node to avoid repeated string allocation + interning
    try {
        uint64_t getter_id = expr->getter_id;
        if (getter_id == UINT64_MAX) {
            getter_id = string_symbolizer_->intern("_get_" + expr->member);
            expr->getter_id = getter_id;
        }
        script_value getter = instance->get_method(getter_id, false);
        if (!getter.is_null() && !getter.is_invalid() && getter.is_function()) {
            // Call the getter with 'this' as argument
            const script_function& func = getter.as_function();
            std::vector<script_value> args = {objectValue};
            auto result = func(args);
            if (!result) {
                return checked_result<void>(result.error(), result.message());
            }
            push_value(std::move(result.value()));
            return {};
        }
    } catch (const std::exception&) {
        // If get_method fails (e.g., class definition expired), fall back to field access
    }

    // Check if it's a field (script class fields without getters, or C++ fields)
    if (instance->has_field(member_id)) {
        push_value(instance->get_field(member_id));
        return {};
    }

    // Look for a method (pass false to avoid throwing)
    script_value method = instance->get_method(member_id, false);
    if (!method.is_null() && !method.is_invalid()) {
        // Return a bound method (function that has 'this' pre-bound)
        push_value(create_bound_method(objectValue, method));
        return {};
    }
    // Set exception state instead of throwing
    active_exception_value_ = make_value("Object has no member '" + expr->member + "'");
    current_exception_ = script_exception("Object has no member '" + expr->member + "'", expr->location);
    is_unwinding_ = true;
    push_value(make_value());  // Push null for failed member access
    return {};
}

checked_result<void> interpreter::visit_lambda_expr(lambda_expr* expr) {

    // Capture current environment for closure
    auto closure_env = environment_;

    // Check if we need a capture environment
    bool has_explicit_captures = !expr->captures.empty();
    bool has_default_capture = (expr->default_capture != lambda_expr::capture_default::none);
    
    
    // For default captures, analyze the lambda body to find which variables are actually used
    std::unordered_set<std::string> used_variables;
    if (has_default_capture) {
        // Helper to recursively find all identifiers in an expression
        std::function<void(expression*)> find_identifiers;
        find_identifiers = [&](expression* e) {
            if (e->get_type() == node_type::identifier_expr) {
                auto* ident = static_cast<identifier_expr*>(e);
                // Skip parameter names
                bool is_param = false;
                for (const auto& param : expr->parameters) {
                    if (param.name == ident->name) {
                        is_param = true;
                        break;
                    }
                }
                if (!is_param) {
                    used_variables.insert(ident->name);
                }
            } else if (e->get_type() == node_type::binary_expr) {
                auto* binary = static_cast<binary_expr*>(e);
                find_identifiers(binary->left.get());
                find_identifiers(binary->right.get());
            } else if (e->get_type() == node_type::unary_expr) {
                auto* unary = static_cast<unary_expr*>(e);
                find_identifiers(unary->operand.get());
            } else if (e->get_type() == node_type::call_expr) {
                auto* call = static_cast<call_expr*>(e);
                find_identifiers(call->callee.get());
                for (const auto& arg : call->arguments) {
                    find_identifiers(arg.get());
                }
            } else if (e->get_type() == node_type::member_expr) {
                auto* member = static_cast<member_expr*>(e);
                find_identifiers(member->object.get());
            } else if (e->get_type() == node_type::assignment_expr) {
                auto* assign = static_cast<assignment_expr*>(e);
                find_identifiers(assign->target.get());
                find_identifiers(assign->value.get());
            } else if (e->get_type() == node_type::ternary_expr) {
                auto* ternary = static_cast<ternary_expr*>(e);
                find_identifiers(ternary->condition.get());
                find_identifiers(ternary->then_expression.get());
                find_identifiers(ternary->else_expression.get());
            }
            // Add more expression types as needed
        };
        
        // Helper to find identifiers in statements
        std::function<void(statement*)> find_in_statement;
        find_in_statement = [&](statement* s) {
            if (s->get_type() == node_type::expression_stmt) {
                auto* expr_stmt = static_cast<expression_stmt*>(s);
                find_identifiers(expr_stmt->expression.get());
            } else if (s->get_type() == node_type::block_stmt) {
                auto* block = static_cast<block_stmt*>(s);
                for (const auto& decl : block->declarations) {
                    if (decl->get_type() == node_type::expression_decl) {
                        auto* expr_decl = static_cast<expression_decl*>(decl.get());
                        find_identifiers(expr_decl->expression.get());
                    } else if (decl->get_type() == node_type::statement_decl) {
                        auto* stmt_decl = static_cast<statement_decl*>(decl.get());
                        find_in_statement(stmt_decl->statement.get());
                    }
                }
            } else if (s->get_type() == node_type::if_stmt) {
                auto* if_s = static_cast<if_stmt*>(s);
                find_identifiers(if_s->condition.get());
                find_in_statement(if_s->then_statement.get());
                if (if_s->else_statement) {
                    find_in_statement(if_s->else_statement.get());
                }
            } else if (s->get_type() == node_type::while_stmt) {
                auto* while_s = static_cast<while_stmt*>(s);
                find_identifiers(while_s->condition.get());
                find_in_statement(while_s->body.get());
            } else if (s->get_type() == node_type::return_stmt) {
                auto* return_s = static_cast<return_stmt*>(s);
                if (return_s->value) {
                    find_identifiers(return_s->value.get());
                }
            }
            // Add more statement types as needed
        };
        
        // Analyze the lambda body
        find_in_statement(expr->body.get());
        
    }
    
    // Determine if we actually need a capture environment
    bool needs_capture_env = has_explicit_captures || (has_default_capture && !used_variables.empty());

    // Pre-cache capture symbol IDs for optimization
    uint64_t this_id = string_symbolizer_->get_this_id();
    for (auto& capture : expr->captures) {
        if (capture.symbol_id == UINT64_MAX) {
            capture.symbol_id = string_symbolizer_->intern(capture.name);
        }
    }

    // Check if [this] is captured - we'll need special handling
    bool captures_this = false;
    for (const auto& capture : expr->captures) {
        if (capture.symbol_id == this_id) {
            captures_this = true;
            break;
        }
    }

    std::shared_ptr<environment> final_closure_env;

    if (needs_capture_env) {
        // Create captured variables in the closure environment
        std::shared_ptr<environment> captureEnv = std::make_shared<environment>(closure_env, string_symbolizer_);
        
        // Process default captures first ([=] or [&])
        if (has_default_capture && !used_variables.empty()) {
            bool capture_by_ref = (expr->default_capture == lambda_expr::capture_default::by_reference);
            
            for (const auto& varName : used_variables) {
                // Check if this variable is explicitly overridden in the capture list
                bool is_overridden = false;
                uint64_t var_id = string_symbolizer_->intern(varName);
                for (const auto& capture : expr->captures) {
                    if (capture.symbol_id == var_id) {
                        is_overridden = true;
                        break;
                    }
                }

                if (!is_overridden && environment_->contains(var_id)) {
                    if (capture_by_ref) {
                        // Capture by reference - create reference to original variable
                        script_value* targetPtr = environment_->get_value_ptr(var_id);
                        if (targetPtr) {
                            script_value refValue = script_value::make_reference(targetPtr, environment_);
                            captureEnv->define(var_id, std::move(refValue));
                        }
                    } else {
                        // Capture by value - deep copy at capture time
                        auto capture_result = environment_->get(var_id);
                        if (capture_result) {
                            captureEnv->define(var_id, capture_result.value().clone());
                        }
                    }
                }
            }
        }
        
        // Process explicit captures
        for (const auto& capture : expr->captures) {
            // Special handling for 'this' - method_environment provides it via get() override
            // even though contains() might return false
            bool can_capture = environment_->contains(capture.symbol_id);
            if (!can_capture && capture.symbol_id == this_id) {
                // Try to get 'this' - method_environment will provide it
                auto this_test_result = environment_->get(this_id);
                if (this_test_result) {
                    can_capture = true;
                }
            }

            if (can_capture) {
                if (capture.by_reference) {
                    // Capture by reference - create reference to original variable
                    script_value* targetPtr = environment_->get_value_ptr(capture.symbol_id);
                    if (targetPtr) {
                        script_value refValue = script_value::make_reference(targetPtr, environment_);
                        captureEnv->define(capture.symbol_id, std::move(refValue));
                    } else {
                        // Cannot capture variable by reference
                        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
                    }
                } else {
                    // Capture by value - deep copy at capture time
                    auto capture_result = environment_->get(capture.symbol_id);
                    if (!capture_result) {
                        return checked_result<void>(capture_result.error(), capture_result.message());
                    }
                    captureEnv->define(capture.symbol_id, capture_result.value().clone());
                }
            } else {
                // Cannot capture undefined variable
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
            }
        }

        // If [this] was captured, we need to use a method_environment instead
        // so that member variables can be accessed without "this."
        if (captures_this) {
            // Get the 'this' object that was captured
            auto this_result = captureEnv->get(this_id);
            if (!this_result) {
                throw runtime_error(this_result.message());
            }
            script_value this_obj = std::move(this_result.value());

            // Create a method_environment with the captured 'this' object
            // The parent is closure_env (the environment where the lambda was defined)
            // Use pooled environment to avoid creating infinite parent chains
            auto method_env = get_pooled_method_environment(
                closure_env,
                this_obj
            );
            method_env->define(this_id, this_obj);

            // Copy all captured variables (except 'this') into the method_environment
            for (const auto& capture : expr->captures) {
                if (capture.symbol_id != this_id && captureEnv->contains(capture.symbol_id)) {
                    auto capture_result = captureEnv->get(capture.symbol_id);
                    if (capture_result) {
                        method_env->define(capture.symbol_id, std::move(capture_result.value()));
                    }
                }
            }

            // Also copy default-captured variables
            if (has_default_capture) {
                for (const auto& varName : used_variables) {
                    uint64_t var_id = string_symbolizer_->intern(varName);
                    if (var_id != this_id && captureEnv->contains(var_id)) {
                        auto var_result = captureEnv->get(var_id);
                        if (var_result) {
                            method_env->define(var_id, std::move(var_result.value()));
                        }
                    }
                }
            }

            final_closure_env = method_env;
        } else {
            final_closure_env = captureEnv;
        }
    } else {
        // No captures needed - use current environment directly (fast path)
        final_closure_env = closure_env;
        
    }
    
    // Convert the lambda body to a block_stmt if it's not already
    std::shared_ptr<block_stmt> lambdaBody;
    if (auto blockStmt = std::dynamic_pointer_cast<block_stmt>(expr->body)) {
        lambdaBody = blockStmt;
    } else {
        // Wrap single statement in a block
        std::vector<declaration_ptr> stmts;
        if (auto stmt = std::dynamic_pointer_cast<statement>(expr->body)) {
            auto stmtDecl = std::make_shared<statement_decl>(expr->location, stmt);
            stmts.push_back(stmtDecl);
        }
        lambdaBody = std::make_shared<block_stmt>(expr->location, std::move(stmts));
    }
    
    // Pre-cache parameter symbol IDs for optimization
    for (auto& param : expr->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }
    
    // Create the script function
    // Use final_closure_env which is either the capture environment or current environment
    // This ensures lambdas can access variables from their creation context
    // IMPORTANT: If needs_capture_env is false, we pass nullptr as closure_env
    // This makes the lambda behave exactly like a regular function
    
    
    auto lambdaFunc = std::make_shared<script_defined_function>(
        "<lambda>",  // Anonymous function name
        expr->parameters,
        expr->return_type,
        lambdaBody,
        needs_capture_env ? final_closure_env : nullptr  // Only use closure env if we have captures
    );
    
    // Create a script_function wrapper
    // capture lambdaFunc by value to ensure it stays alive
    script_function funcWrapper = [this, lambdaFunc](const std::vector<script_value>& args) -> checked_result<script_value> {
        return call_function(*lambdaFunc, args);
    };

    // Push the lambda as a function value
    push_value(script_value::make_function(funcWrapper, engine_ref_));
    return {};
}

checked_result<void> interpreter::visit_new_expr(new_expr* expr) {
    // This handles expressions like: new Point(), new Point(3.0, 4.0), etc.
    // The new_expr contains a type and arguments

    // std::cerr << "DEBUG: visit_new_expr called for type: " << (expr->type ? expr->type->type_name : "NULL") << std::endl;

    if (!expr->type) {
        // New expression missing type information
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Handle built-in types specially
    if (expr->type->base_type == script_value_type::jai_array_type) {
        // array<T>{} constructor
        if (!expr->arguments.empty()) {
            // array{} constructor does not take arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }

        // Create empty array with the specified element type
        auto element_type = expr->type->element_type();
        if (!element_type) {
            if (auto eng = engine_ref_.lock()) {
                element_type = eng->get_type_info_int(); // Default to int if no type specified
            }
        }
        push_value(script_value::make_array(element_type, engine_ref_));
        return {};
    }

    if (expr->type->base_type == script_value_type::jai_map_type) {
        // map<K,V>{} constructor
        if (!expr->arguments.empty()) {
            // map{} constructor does not take arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }

        // Create empty map with the specified key/value types
        auto key_type = expr->type->key_type();
        auto value_type = expr->type->value_type();
        if (auto eng = engine_ref_.lock()) {
            if (!key_type) key_type = eng->get_type_info_string();
            if (!value_type) value_type = eng->get_type_info_int();
        }
        push_value(script_value::make_map(key_type, value_type, engine_ref_));
        return {};
    }
    
    if (expr->type->base_type == script_value_type::jai_weak_ptr_type) {
        // weak_ptr<T>() or weak_ptr<T>(obj) constructor
        if (expr->arguments.empty()) {
            // No arguments - create empty weak_ptr
            push_value(script_value::make_empty_weak_ptr(expr->type, engine_ref_));
        } else if (expr->arguments.size() == 1) {
            // One argument - create weak_ptr from object
            JAISCRIPT_TRY(dispatch_expr(expr->arguments[0].get()));
            script_value obj = pop_value();

            // Handle null objects
            if (obj.is_null()) {
                push_value(script_value::make_empty_weak_ptr(expr->type, engine_ref_));
                return {};
            }

            // Allow creating weak_ptr from:
            // 1. Another weak_ptr (copy constructor)
            // 2. shared_ptr<T> (jai_shared_ptr_type)
            // NOTE: Regular objects have value semantics and cannot be used with weak_ptr

            if (obj.is_weak_ptr()) {
                // Copy constructor - just return the weak_ptr as-is
                push_value(obj);
                return {};
            }

            // Check if obj is a shared_ptr (required for weak_ptr)
            if (obj.type() != script_value_type::jai_shared_ptr_type) {
                if (obj.type() == script_value_type::jai_object_type) {
                    // Helpful error for value-semantic objects
                    return checked_result<void>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot create weak_ptr from a value-semantic object. Use shared_ptr<T>: auto obj = shared_ptr<" + expr->type->type_params[0]->type_name + ">(...); auto weak = weak_ptr<" + expr->type->type_params[0]->type_name + ">(obj);");
                } else {
                    auto type_info = obj.get_type_info();
                    std::string type_name = type_info ? type_info->type_name : "unknown";
                    return checked_result<void>(
                        make_error_code(runtime_error_code::type_mismatch),
                        "Cannot create weak_ptr from " + type_name + ". Use shared_ptr<T> to enable reference semantics: auto obj = shared_ptr<" + expr->type->type_params[0]->type_name + ">(...); auto weak = weak_ptr<" + expr->type->type_params[0]->type_name + ">(obj);");
                }
            }

            // Create weak_ptr from the shared_ptr
            auto weak_result = script_value::make_weak_ptr(obj, engine_ref_);
            if (!weak_result) {
                return checked_result<void>(weak_result.error(), weak_result.message());
            }
            push_value(std::move(weak_result.value()));
        } else {
            // weak_ptr() expects 0 or 1 arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }
        return {};
    }
    
    if (expr->type->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T>() or shared_ptr<T>(obj) constructor
        // shared_ptr is now a TYPE MARKER only - it affects cloning behavior, not storage

        if (expr->arguments.empty()) {
            // No arguments - create empty shared_ptr (null)
            push_value(make_value());
        } else if (expr->arguments.size() == 1) {
            // One argument - mark it as shared_ptr type
            JAISCRIPT_TRY(dispatch_expr(expr->arguments[0].get()));
            script_value value = pop_value();

            // Handle null
            if (value.is_null()) {
                push_value(value);
                return {};
            }

            // Cannot create shared_ptr from weak_ptr directly
            if (value.is_weak_ptr()) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
            }

            // If it's already a shared_ptr, just return it
            if (value.get_type_info() && value.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                push_value(std::move(value));
                return {};
            }

            // For objects, just mark as shared_ptr type (no wrapping needed)
            // Objects are already stored as shared_ptr<object_holder>
            if (value.type() == script_value_type::jai_object_type) {
                value.set_type_info(expr->type);  // Mark as shared_ptr<T>
                push_value(std::move(value));
                return {};
            }

            // Primitives, arrays, maps, and functions not supported yet
            // TODO: Add primitive wrapping support later if needed
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] shared_ptr only supports objects currently
        } else {
            // shared_ptr() expects 0 or 1 arguments
            return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch));  // [ErrorText] Invalid argument count
        }
        return {};
    }
    
    std::string className = expr->type->type_name;

    // Evaluate all arguments
    std::vector<script_value> args;
    for (const auto& argExpr : expr->arguments) {
        JAISCRIPT_TRY(dispatch_expr(argExpr.get()));
        args.push_back(std::move(pop_value()));
    }

    // Look for a constructor function registered with this class name
    // The class builder registers constructors as overloaded functions
    auto ctor_result = environment_->get(className);
    if (ctor_result && ctor_result.value().is_function()) {
        script_value constructorFunc = std::move(ctor_result.value());
        const script_function& func = constructorFunc.as_function();
        auto result = func(args);
        if (!result) {
            // Function returned error - propagate it up
            return checked_result<void>(result.error(), result.message());
        }
        push_value(std::move(result.value()));
        return {};
    }

    // No constructor found for class
    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));
}

checked_result<void> interpreter::visit_ternary_expr(ternary_expr* expr) {
    // Evaluate the condition
    JAISCRIPT_TRY(dispatch_expr(expr->condition.get()));
    script_value conditionValue = pop_value();

    // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
    bool conditionIsTruthy;
    if (expression_returns_bool(expr->condition.get())) {
        conditionIsTruthy = conditionValue.unchecked_as_bool();
    } else {
        conditionIsTruthy = is_truthy(conditionValue);
    }

    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        JAISCRIPT_TRY(dispatch_expr(expr->then_expression.get()));
    } else {
        JAISCRIPT_TRY(dispatch_expr(expr->else_expression.get()));
    }
    return {};
}

checked_result<void> interpreter::visit_array_literal_expr(array_literal_expr* expr) {
    // Create array script_value with mixed element type (for now)
    type_info_ptr element_type = nullptr;
    if (auto eng = engine_ref_.lock()) {
        element_type = eng->get_type_info_int(); // TODO: Better type inference
    }
    script_value arrayValue = script_value::make_array(element_type, engine_ref_);

    // Get the internal vector to populate
    auto& array = const_cast<std::vector<script_value>&>(arrayValue.as_array());

    // Reserve capacity to avoid reallocations (optimization)
    array.reserve(expr->elements.size());

    // Evaluate each element and add to array
    for (const auto& element : expr->elements) {
        JAISCRIPT_TRY(dispatch_expr(element.get()));
        array.push_back(pop_value());
    }

    push_value(std::move(arrayValue));
    return {};
}

checked_result<void> interpreter::visit_map_literal_expr(map_literal_expr* expr) {
    // Create map script_value with mixed key/value types (for now)
    type_info_ptr keyType = nullptr;
    type_info_ptr valueType = nullptr;
    if (auto eng = engine_ref_.lock()) {
        keyType = eng->get_type_info_string(); // TODO: Better type inference
        valueType = eng->get_type_info_int(); // TODO: Better type inference
    }
    script_value mapValue = script_value::make_map(keyType, valueType, engine_ref_);

    // Get the internal map to populate
    auto& map = const_cast<std::map<script_value, script_value>&>(mapValue.as_map());

    // Evaluate each key-value pair and add to map
    for (const auto& entry : expr->entries) {
        // Evaluate key
        JAISCRIPT_TRY(dispatch_expr(entry.first.get()));
        script_value key = pop_value();

        // Evaluate value
        JAISCRIPT_TRY(dispatch_expr(entry.second.get()));
        script_value value = pop_value();

        // Insert into map
        map.insert_or_assign(std::move(key), std::move(value));
    }

    push_value(std::move(mapValue));
    return {};
}

checked_result<void> interpreter::visit_this_expr(this_expr* expr) {
    // If we're in a class method context during parsing, allow 'this'
    if (current_class_context_ && current_class_context_->in_method) {
        // Push a placeholder value to continue parsing
        push_value(make_value());
        return {};
    }

    // Try to get 'this' from the current environment
    // Use the symbolizer's cached ID (not interpreter's this_id_ which may be stale)
    auto this_result = environment_->get(string_symbolizer_->get_this_id());
    if (!this_result) {
        // 'this' can only be used inside methods
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
    }
    push_value(std::move(this_result.value()));
    return {};
}

checked_result<void> interpreter::visit_super_expr(super_expr* expr) {
    // Super expression is used for accessing parent class methods: super::method()
    // Constructor delegation (Enemy() : super()) is handled in the parser/constructor

    // Get 'this' from the environment - super only makes sense in instance methods
    // Use the symbolizer's cached ID (not interpreter's this_id_ which may be stale)
    auto this_result = environment_->get(string_symbolizer_->get_this_id());
    if (!this_result) {
        // 'this' not found - super used outside of class method
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
    }
    script_value this_value = std::move(this_result.value());
    if (this_value.is_null()) {
        // 'this' is null - super used outside of class method
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
    }

    // Push 'this' onto the stack - visit_member_expr will handle the parent lookup
    // when it detects that the object expression is a super_expr
    push_value(std::move(this_value));
    return {};
}

checked_result<void> interpreter::visit_throw_expr(throw_expr* expr) {
    if (expr->value) {
        // Evaluate the expression to throw
        JAISCRIPT_TRY(dispatch_expr(expr->value.get()));
        script_value val = pop_value();

        // Store the exception value and convert to string for exception message
        active_exception_value_ = val;
        std::string message = val.to_string();
        current_exception_ = script_exception(message, expr->location);
    } else {
        // Re-throw current exception
        if (!current_exception_) {
            // No exception to re-throw (note: this uses exception unwinding, not error codes)
            throw script_exception("No exception to re-throw", expr->location);
        }
        // Keep the existing active_exception_value_
    }

    is_unwinding_ = true;
    return {};
}

checked_result<void> interpreter::visit_if_stmt(if_stmt* stmt) {
    // Evaluate the condition
    JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));
    script_value conditionValue = pop_value();

    // OPTIMIZATION: If condition is guaranteed to return bool, skip type dispatch
    bool is_true;
    if (expression_returns_bool(stmt->condition.get())) {
        is_true = conditionValue.unchecked_as_bool();
    } else {
        is_true = is_truthy(conditionValue);
    }

    // Execute appropriate branch based on truthiness
    if (is_true) {
        JAISCRIPT_TRY(dispatch_stmt(stmt->then_statement.get()));
    } else if (stmt->else_statement) {
        JAISCRIPT_TRY(dispatch_stmt(stmt->else_statement.get()));
    }
    return {};
}

checked_result<void> interpreter::visit_while_stmt(while_stmt* stmt) {
    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    const bool condition_returns_bool = expression_returns_bool(stmt->condition.get());

    while (true) {
        // Evaluate the condition
        JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));

        const auto& val = valueStack_.top();
        bool is_true;

        // FAST PATH: If we KNOW the condition returns bool, skip type dispatch
        if (condition_returns_bool) {
            is_true = val.unchecked_as_bool();
        } else {
            is_true = is_truthy(val);
        }

        valueStack_.discard();

        if (!is_true) {
            break;
        }

        // Execute the loop body
        auto result = dispatch_stmt(stmt->body.get());
        if (!result) return result;

        // Check for control flow changes (break/continue/return)
        if (hasBreakRequest_) {
            hasBreakRequest_ = false;  // Clear the flag
            break;
        }

        if (hasContinueRequest_) {
            hasContinueRequest_ = false;  // Clear the flag
            continue;
        }

        if (hasReturnValue_) {
            break;
        }
    }
    return {};
}

checked_result<void> interpreter::visit_for_stmt(for_stmt* stmt) {
    // FAST PATHS: Detect and optimize integer counting loops
    // Pattern: for (int/auto/var i = START; i < END; ++i) { body }
    //
    // Two fast paths based on type declaration:
    // 1. ULTRA FAST (locked types): int i, auto i = 0 - direct int pointer, no type checks
    //    Safe because strong types prevent type changes after locking
    // 2. VAR FAST (dynamic type): var i = 0 - native loop with per-iteration type validation
    //    Validates type is still int before each iteration (handles rare type changes)
    //
    if (stmt->initializer && stmt->condition && stmt->update) {
        auto* init_var = stmt->initializer->get_type() == node_type::variable_decl
            ? static_cast<variable_decl*>(stmt->initializer.get()) : nullptr;
        auto* cond_binary = stmt->condition->get_type() == node_type::binary_expr
            ? static_cast<binary_expr*>(stmt->condition.get()) : nullptr;

        // Try to match update patterns: ++i, i++, i += step, i = i + step
        auto* update_unary = stmt->update->get_type() == node_type::unary_expr
            ? static_cast<unary_expr*>(stmt->update.get()) : nullptr;
        auto* update_assign = stmt->update->get_type() == node_type::assignment_expr
            ? static_cast<assignment_expr*>(stmt->update.get()) : nullptr;

        // Extract update info: which variable, what step
        uint64_t update_var_id = UINT64_MAX;
        script_int step_value = 1;  // Default for ++i
        uint64_t step_var_id = UINT64_MAX;  // For dynamic step (i += j)
        bool valid_update = false;

        if (update_unary && update_unary->op.type == token_type::plus_plus) {
            // Pattern: ++i or i++
            if (update_unary->operand->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_unary->operand.get());
                update_var_id = update_id->symbol_id;
                step_value = 1;
                valid_update = true;
            }
        } else if (update_unary && update_unary->op.type == token_type::minus_minus) {
            // Pattern: --i or i-- (step = -1, but this is unusual for counting up)
            if (update_unary->operand->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_unary->operand.get());
                update_var_id = update_id->symbol_id;
                step_value = -1;
                valid_update = true;
            }
        } else if (update_assign && update_assign->op.type == token_type::plus_equal) {
            // Pattern: i += step (literal or variable)
            if (update_assign->target->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_assign->target.get());
                update_var_id = update_id->symbol_id;
                // Check if step is an int literal
                if (update_assign->value->get_type() == node_type::literal_expr) {
                    auto* step_lit = static_cast<literal_expr*>(update_assign->value.get());
                    if (step_lit->value.raw_storage_index() == 1) {  // int literal
                        step_value = step_lit->value.unchecked_as_int();
                        valid_update = true;
                    }
                }
                // Check if step is an identifier (i += j where j is int)
                else if (update_assign->value->get_type() == node_type::identifier_expr) {
                    auto* step_id = static_cast<identifier_expr*>(update_assign->value.get());
                    // Look up step variable in current environment
                    script_value* step_ptr = environment_->get_value_ptr(step_id->symbol_id);
                    if (step_ptr && step_ptr->raw_storage_index() == 1) {  // int value
                        step_var_id = step_id->symbol_id;
                        step_value = step_ptr->unchecked_as_int();  // Initial value
                        valid_update = true;
                    }
                }
            }
        } else if (update_assign && update_assign->op.type == token_type::minus_equal) {
            // Pattern: i -= step (literal or variable)
            if (update_assign->target->get_type() == node_type::identifier_expr) {
                auto* update_id = static_cast<identifier_expr*>(update_assign->target.get());
                update_var_id = update_id->symbol_id;
                if (update_assign->value->get_type() == node_type::literal_expr) {
                    auto* step_lit = static_cast<literal_expr*>(update_assign->value.get());
                    if (step_lit->value.raw_storage_index() == 1) {
                        step_value = -step_lit->value.unchecked_as_int();
                        valid_update = true;
                    }
                }
                // Check if step is an identifier (i -= j where j is int)
                else if (update_assign->value->get_type() == node_type::identifier_expr) {
                    auto* step_id = static_cast<identifier_expr*>(update_assign->value.get());
                    script_value* step_ptr = environment_->get_value_ptr(step_id->symbol_id);
                    if (step_ptr && step_ptr->raw_storage_index() == 1) {  // int value
                        step_var_id = step_id->symbol_id;
                        step_value = step_ptr->unchecked_as_int();  // Initial value
                        valid_update = true;
                    }
                }
            }
        }

        if (init_var && cond_binary && valid_update) {
            // Select comparison function based on operator type
            // Supports: <, <=, >, >=, ==, !=
            using compare_fn = bool(*)(script_int, script_int);
            compare_fn cmp = nullptr;
            switch (cond_binary->op.type) {
                case token_type::less:          cmp = [](script_int a, script_int b) { return a < b; }; break;
                case token_type::less_equal:    cmp = [](script_int a, script_int b) { return a <= b; }; break;
                case token_type::greater:       cmp = [](script_int a, script_int b) { return a > b; }; break;
                case token_type::greater_equal: cmp = [](script_int a, script_int b) { return a >= b; }; break;
                case token_type::equal_equal:   cmp = [](script_int a, script_int b) { return a == b; }; break;
                case token_type::bang_equal:    cmp = [](script_int a, script_int b) { return a != b; }; break;
                default: break;
            }

            if (cmp) {
                auto* cond_id = cond_binary->left->get_type() == node_type::identifier_expr
                    ? static_cast<identifier_expr*>(cond_binary->left.get()) : nullptr;

                // End value can be literal OR variable
                script_int end_literal = 0;
                uint64_t end_var_id = UINT64_MAX;
                bool has_end = false;

                if (cond_binary->right->get_type() == node_type::literal_expr) {
                    auto* cond_lit = static_cast<literal_expr*>(cond_binary->right.get());
                    if (cond_lit->value.raw_storage_index() == 1) {  // int literal
                        end_literal = cond_lit->value.unchecked_as_int();
                        has_end = true;
                    }
                } else if (cond_binary->right->get_type() == node_type::identifier_expr) {
                    auto* cond_end_id = static_cast<identifier_expr*>(cond_binary->right.get());
                    // End is a variable - we'll bind to its pointer
                    end_var_id = cond_end_id->symbol_id;
                    has_end = true;
                }

                if (cond_id && has_end) {
                    // Check: update variable matches condition variable
                    if (update_var_id == cond_id->symbol_id) {
                        // Check: init is variable i = literal (same identifier, int literal)
                        auto* init_lit = init_var->initializer && init_var->initializer->get_type() == node_type::literal_expr
                            ? static_cast<literal_expr*>(init_var->initializer.get()) : nullptr;

                        if (init_lit && init_lit->value.raw_storage_index() == 1 &&
                            init_var->name_id == cond_id->symbol_id) {

                            // === PATTERN MATCHED! Run optimized native loop ===
                            // Unified fast path for all integer counting loops (auto/int/var)
                            // Type validation per-iteration is negligible (~0.1ns) vs body dispatch
                            script_int i = init_lit->value.unchecked_as_int();
                            uint64_t var_id = init_var->name_id;

                            // Setup environment once (from pool)
                            auto previous = environment_;
                            auto loop_env = get_pooled_environment(previous);
                            environment_ = loop_env;

                            // Create value preserving declared type (var = any_type, auto = nullptr, int = int_type)
                            script_value init_val = make_value(i);
                            if (init_var->type) {
                                init_val.set_type_info(init_var->type);
                            }
                            environment_->define(var_id, std::move(init_val));

                            script_value* var_ptr = environment_->get_value_ptr(var_id);

                            // Bind end value pointer (if variable) AFTER environment setup
                            script_int* end_ptr = nullptr;
                            script_int end_val = end_literal;
                            if (end_var_id != UINT64_MAX) {
                                script_value* end_sv = environment_->get_value_ptr(end_var_id);
                                if (end_sv && end_sv->raw_storage_index() == 1) {
                                    end_ptr = &end_sv->unchecked_as_int_ref();
                                } else {
                                    // End variable not int - fall back to slow path
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    goto slow_path;
                                }
                            }

                            // Bind step value pointer (if variable)
                            script_int* step_ptr = nullptr;
                            if (step_var_id != UINT64_MAX) {
                                script_value* step_sv = environment_->get_value_ptr(step_var_id);
                                if (step_sv && step_sv->raw_storage_index() == 1) {
                                    step_ptr = &step_sv->unchecked_as_int_ref();
                                } else {
                                    // Step variable not int - fall back to slow path
                                    release_environment(loop_env);
                                    environment_ = previous;
                                    goto slow_path;
                                }
                            }

                            // Determine step operation: += or -=
                            const bool step_subtract = update_assign &&
                                update_assign->op.type == token_type::minus_equal;

                            // === UNIFIED FAST PATH ===
                            // Native C++ loop with cached pointers for end and step
                            // Per-iteration type check is cheap (~0.1ns) and handles edge cases
                            bool fell_through = false;

                            // Optimization: If body is a block, pre-allocate its environment
                            // and reuse across iterations (define() overwrites existing vars in place)
                            auto* body_block = stmt->body->get_type() == node_type::block_stmt
                                ? static_cast<block_stmt*>(stmt->body.get()) : nullptr;
                            std::shared_ptr<environment> body_env = nullptr;
                            if (body_block) {
                                body_env = get_pooled_environment(loop_env);
                            }

                            while (true) {
                                script_int current_end = end_ptr ? *end_ptr : end_val;
                                if (!cmp(i, current_end)) break;

                                // Type validation - if type changed, fall back to slow path
                                if (var_ptr->raw_storage_index() != 1) [[unlikely]] {
                                    fell_through = true;
                                    break;
                                }
                                var_ptr->unchecked_as_int_ref() = i;

                                // Execute body - use pre-allocated environment for blocks
                                if (body_block) {
                                    // Reuse body_env - it's already a child of loop_env
                                    environment_ = body_env;
                                    for (const auto& decl : body_block->declarations) {
                                        auto result = dispatch_decl(decl.get());
                                        if (valueStack_.size() > 0) {
                                            valueStack_.clear();
                                        }
                                        if (!result) {
                                            release_environment(body_env);
                                            release_environment(loop_env);
                                            environment_ = previous;
                                            return result;
                                        }
                                        if (is_unwinding_ || hasBreakRequest_ || hasContinueRequest_ || hasReturnValue_) {
                                            break;
                                        }
                                    }
                                    valueStack_.clear();
                                    // No need to clear locals - define() overwrites existing vars in place
                                    environment_ = loop_env;
                                } else {
                                    auto result = dispatch_stmt(stmt->body.get());
                                    if (!result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return result;
                                    }
                                }

                                if (hasBreakRequest_) { hasBreakRequest_ = false; break; }
                                if (hasReturnValue_) break;
                                if (hasContinueRequest_) hasContinueRequest_ = false;

                                // Update step - read from pointer if dynamic
                                script_int step = step_ptr ? *step_ptr : step_value;
                                if (step_subtract) {
                                    i -= step;
                                } else {
                                    i += step;
                                }
                            }

                            // Release body environment if we allocated it
                            if (body_env) {
                                release_environment(body_env);
                            }

                            // If type changed mid-loop, continue with slow path dispatch
                            if (fell_through) [[unlikely]] {
                                while (true) {
                                    auto cond_result = dispatch_expr(stmt->condition.get());
                                    if (!cond_result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return cond_result;
                                    }
                                    if (!is_truthy(pop_value())) break;

                                    auto body_result = dispatch_stmt(stmt->body.get());
                                    if (!body_result) {
                                        release_environment(loop_env);
                                        environment_ = previous;
                                        return body_result;
                                    }

                                    if (hasBreakRequest_) { hasBreakRequest_ = false; break; }
                                    if (hasReturnValue_) break;
                                    if (hasContinueRequest_) hasContinueRequest_ = false;

                                    if (stmt->update) {
                                        auto update_result = dispatch_expr(stmt->update.get());
                                        if (!update_result) {
                                            release_environment(loop_env);
                                            environment_ = previous;
                                            return update_result;
                                        }
                                        pop_value();
                                    }
                                }
                            }

                            release_environment(loop_env);
                            environment_ = previous;
                            return {};
                        }
                    }
                }
            }
        }
    slow_path: ;  // Empty statement for goto target
    }

    // === GENERAL PATH: Standard for-loop handling ===
    // Create new scope for the for loop (initialization variables should be scoped)
    auto previous = environment_;
    auto loop_env = get_pooled_environment(environment_);
    environment_ = loop_env;

    // Error capture for lambdas (avoid throwing from hot path)
    std::optional<checked_result<void>> error;

    // OPTIMIZATION: Pre-check if condition is guaranteed to return bool
    // This allows us to skip is_truthy() type dispatch entirely
    const bool condition_returns_bool = stmt->condition && expression_returns_bool(stmt->condition.get());

    // Lambda helpers (ChaiScript-style) for cleaner, more optimizable code
    auto eval_condition = [&]() -> bool {
        if (!stmt->condition) return true;  // No condition = infinite loop

        auto result = dispatch_expr(stmt->condition.get());
        if (!result) {
            error = result;  // Capture error, signal loop termination
            return false;
        }

        const auto& val = valueStack_.top();
        bool is_true;

        // FAST PATH: If we KNOW the condition returns bool (comparisons, logical ops),
        // use unchecked direct access - no type dispatch needed!
        if (condition_returns_bool) {
            is_true = val.unchecked_as_bool();
        } else {
            // SLOW PATH: General case - need to check type and convert to bool
            is_true = is_truthy(val);
        }

        valueStack_.discard();
        return is_true;
    };

    auto eval_update = [&]() {
        if (!stmt->update) return;

        auto result = dispatch_expr(stmt->update.get());
        if (!result) {
            error = result;  // Capture error, loop will terminate on next condition check
            return;
        }

        // Discard the update result if it leaves a value on the stack (optimization)
        if (!valueStack_.empty()) {
            valueStack_.discard();
        }
    };

    // Execute initialization (if present)
    if (stmt->initializer) {
        auto result = dispatch_decl(stmt->initializer.get());
        if (!result) {
            release_environment(loop_env);
            environment_ = previous;
            return result;
        }
    }

    // Native C++ for-loop structure (more recognizable to compiler optimizer)
    // Pattern: for(init; condition; update) { body; }
    for (; eval_condition(); eval_update()) {
        // Check if lambda captured an error
        if (error) break;

        // Execute the loop body
        auto result = dispatch_stmt(stmt->body.get());
        if (!result) {
            release_environment(loop_env);
            environment_ = previous;
            return result;
        }

        // Check for control flow changes (break/continue/return)
        // These need to happen BEFORE update (continue) or skip update (break/return)
        if (hasBreakRequest_) {
            hasBreakRequest_ = false;
            break;
        }

        if (hasReturnValue_) {
            break;
        }

        if (hasContinueRequest_) {
            hasContinueRequest_ = false;
            continue;  // Will execute update in for-loop increment
        }
    }

    // Release the loop environment to destroy all loop variables
    release_environment(loop_env);

    // Restore previous environment
    environment_ = previous;

    // If lambda captured an error, propagate it
    if (error) {
        return *error;
    }

    return {};
}

checked_result<void> interpreter::visit_range_for_stmt(range_for_stmt* stmt) {
    // Evaluate the container expression
    JAISCRIPT_TRY(dispatch_expr(stmt->container.get()));
    script_value container = pop_value();

    // Create a new scope for the loop variable
    push_scope();

    if (container.is_array()) {
        // Iterate over array
        auto& array_storage = container.get_array_storage();
        const size_t array_size = array_storage->size();

        if (array_size > 0) {
            // OPTIMIZATION: Define loop variable ONCE, then use pointer for direct assignment
            // Use pre-interned symbol ID from parser - no runtime string interning needed
            environment_->define(stmt->variable_name_id, make_value());
            script_value* loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);

            for (size_t i = 0; i < array_size; ++i) {
                if (stmt->is_reference) {
                    // Create a reference to the actual array element
                    *loop_var_ptr = script_value::make_reference(&(*array_storage)[i], environment_, engine_ref_);
                } else {
                    // Make a copy of the element - assign directly to pointer
                    *loop_var_ptr = (*array_storage)[i].clone();
                }

                // Execute loop body
                auto body_result = dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                // Check for control flow changes (break/continue/return) - mirror while/for loops
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;  // Clear the flag
                    break;
                }

                if (hasContinueRequest_) {
                    hasContinueRequest_ = false;  // Clear the flag
                    continue;  // Skip to next iteration
                }

                if (hasReturnValue_) {
                    break;
                }
            }
        }

    } else if (container.is_map()) {
        // Iterate over map - return key-value pairs with first/second access
        auto& map_storage = container.get_map_storage();

        if (!map_storage->empty()) {
            // OPTIMIZATION: Look up pair constructor ONCE before the loop
            uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
            auto pair_result = environment_->get_ref(pair_symbol_id);
            if (!pair_result) {
                pop_scope();
                return checked_result<void>(pair_result.error(), pair_result.message());
            }
            const script_value& pairConstructor = pair_result.value().get();
            if (!pairConstructor.is_function()) {
                pop_scope();
                return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] pair type not registered - make sure stdlib is loaded
            }
            const script_function& pair_func = pairConstructor.as_function();

            // OPTIMIZATION: Define loop variable ONCE, then use pointer for direct assignment
            // Use pre-interned symbol ID from parser - no runtime string interning needed
            environment_->define(stmt->variable_name_id, make_value());
            script_value* loop_var_ptr = environment_->get_value_ptr(stmt->variable_name_id);

            for (auto it = map_storage->begin(); it != map_storage->end(); ++it) {
                // Create pair args
                std::vector<script_value> args;

                if (stmt->is_reference) {
                    // For references, create a pair with a reference to the map value
                    script_value* value_ptr = const_cast<script_value*>(&it->second);
                    args.push_back(it->first);  // Don't clone - just pass the key
                    args.push_back(script_value::make_reference(value_ptr, environment_, engine_ref_));
                } else {
                    // For copies, clone key and value
                    args.push_back(it->first.clone());
                    args.push_back(it->second.clone());
                }

                auto result = pair_func(args);
                if (!result) {
                    pop_scope();
                    return checked_result<void>(result.error(), result.message());
                }
                *loop_var_ptr = std::move(result.value());

                // Execute loop body
                auto body_result = dispatch_stmt(stmt->body.get());
                if (!body_result) {
                    pop_scope();
                    return body_result;
                }

                // Check for control flow changes (break/continue/return) - mirror while/for loops
                if (hasBreakRequest_) {
                    hasBreakRequest_ = false;  // Clear the flag
                    break;
                }

                if (hasContinueRequest_) {
                    hasContinueRequest_ = false;  // Clear the flag
                    continue;  // Skip to next iteration
                }

                if (hasReturnValue_) {
                    break;
                }
            }
        }

    } else {
        pop_scope();
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Range-based for loop requires an array or map
    }

    // Pop the loop scope
    pop_scope();
    return {};
}

checked_result<void> interpreter::visit_return_stmt(return_stmt* stmt) {
    if (stmt->value) {
        // Evaluate the return expression
        JAISCRIPT_TRY(dispatch_expr(stmt->value.get()));
        returnValue_ = std::move(pop_value());
    } else {
        // Return null if no expression
        returnValue_ = make_value();
    }

    hasReturnValue_ = true;
    return {};
}

checked_result<void> interpreter::visit_break_stmt(break_stmt* stmt) {
    hasBreakRequest_ = true;
    return {};
}

checked_result<void> interpreter::visit_continue_stmt(continue_stmt* stmt) {
    hasContinueRequest_ = true;
    return {};
}

checked_result<void> interpreter::visit_try_stmt(try_stmt* stmt) {
    // Save exception state
    auto saved_exception = current_exception_;
    auto saved_unwinding = is_unwinding_;
    auto saved_exception_value = active_exception_value_;
    auto saved_catch_var_id = current_catch_var_id_;

    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw)
    if (current_catch_var_id_ == 0) {
        current_exception_.reset();
        active_exception_value_ = make_value();
    }
    is_unwinding_ = false;
    current_catch_var_id_ = 0;

    // Execute try block
    auto try_result = dispatch_stmt(stmt->try_block.get());

    // Check if an error occurred (either checked_result error or exception unwinding)
    bool caught_error = false;

    if (!try_result) {
        // Checked_result error - treat as catchable exception
        active_exception_value_ = make_value(try_result.message());
        current_exception_ = script_exception(try_result.message());
        caught_error = true;
    } else if (is_unwinding_ && current_exception_) {
        // Old-style exception unwinding (for script throw statements)
        caught_error = true;
    }

    if (caught_error) {
        // Reset unwinding flag
        is_unwinding_ = false;

        // Set the current catch variable ID so identifier lookup can find it (symbolize once here)
        if (auto eng = engine_ref_.lock()) {
            current_catch_var_id_ = eng->symbolize(stmt->catch_var);
        }

        // Execute catch block
        auto catch_result = dispatch_stmt(stmt->catch_block.get());
        // Propagate errors from catch block
        if (!catch_result) {
            current_catch_var_id_ = saved_catch_var_id;
            return catch_result;
        }

        // Clear catch variable
        current_catch_var_id_ = 0;

        // Only clear exception if it wasn't re-thrown
        if (!is_unwinding_) {
            current_exception_.reset();
            active_exception_value_ = make_value();
        }
    }

    // If still unwinding after catch, we need to be careful about state restoration
    // Don't restore if a new exception was thrown in the catch block
    if (is_unwinding_ && saved_unwinding) {
        // We were already unwinding before this try/catch, restore that state
        current_exception_ = saved_exception;
        active_exception_value_ = saved_exception_value;
    }
    // If is_unwinding_ is true but saved_unwinding was false,
    // it means a new exception was thrown in the catch block - keep it

    // Always restore the catch variable state
    current_catch_var_id_ = saved_catch_var_id;
    return {};
}

checked_result<void> interpreter::visit_switch_stmt(switch_stmt* stmt) {
    // Evaluate the switch condition
    JAISCRIPT_TRY(dispatch_expr(stmt->condition.get()));
    script_value switch_value = pop_value();

    // Save and set switch state
    bool old_in_switch = in_switch_;
    bool old_should_fallthrough = should_fallthrough_;
    in_switch_ = true;
    should_fallthrough_ = false;

    try {
        bool matched = false;
        bool executed_case = false;

        // Check each case
        for (const auto& case_stmt : stmt->cases) {
            // Evaluate case value
            auto result = dispatch_expr(case_stmt->value.get());
            if (!result) {
                in_switch_ = old_in_switch;
                should_fallthrough_ = old_should_fallthrough;
                return result;
            }
            script_value case_value = pop_value();

            // Check if values match using operator==
            bool case_matches = false;
            try {
                case_matches = (switch_value == case_value);
            } catch (const std::exception&) {
                // If comparison fails, treat as non-match
                case_matches = false;
            }

            // Execute case if it matches OR if we're falling through from a previous case
            if (case_matches || (executed_case && should_fallthrough_)) {
                matched = true;
                executed_case = true;

                // Reset fallthrough flag for this case (will be set again if case contains fallthrough statement)
                should_fallthrough_ = false;

                // Create a new scope for the case body (like an if statement)
                auto previous = environment_;
                environment_ = get_pooled_environment(environment_);  // Use pool!

                try {
                    // Execute case body
                    auto case_result = dispatch_stmt(case_stmt.get());
                    if (!case_result) {
                        environment_ = previous;
                        in_switch_ = old_in_switch;
                        should_fallthrough_ = old_should_fallthrough;
                        return case_result;
                    }

                    // Restore the previous environment
                    environment_ = previous;

                    // Check if we should continue to next case (implicit break by default)
                    if (!should_fallthrough_) {
                        break;  // Stop executing further cases
                    }
                    // If should_fallthrough_ is true, continue to next iteration
                } catch (...) {
                    // Restore environment before re-throwing
                    environment_ = previous;
                    throw;
                }
            }
        }

        // Execute default if no case matched OR if we're falling through from the last case
        if ((!matched || (executed_case && should_fallthrough_)) && stmt->default_case) {
            // Create a new scope for the default body
            auto previous = environment_;
            environment_ = get_pooled_environment(environment_);  // Use pool!

            try {
                auto default_result = dispatch_stmt(stmt->default_case.get());
                if (!default_result) {
                    environment_ = previous;
                    in_switch_ = old_in_switch;
                    should_fallthrough_ = old_should_fallthrough;
                    return default_result;
                }

                // Restore the previous environment
                environment_ = previous;
            } catch (...) {
                // Restore environment before re-throwing
                environment_ = previous;
                throw;
            }
        }
    } catch (const break_exception&) {
        // Break out of switch - this is expected behavior
    }

    // Restore switch state
    in_switch_ = old_in_switch;
    should_fallthrough_ = old_should_fallthrough;
    return {};
}

checked_result<void> interpreter::visit_case_stmt(case_stmt* stmt) {
    // Execute all statements in the case body
    // Note: The scope is created by visit_switch_stmt when it decides to execute this case
    for (const auto& s : stmt->body) {
        auto result = dispatch_stmt(s.get());
        if (!result) return result;

        // Check for break or return
        if (hasReturnValue_ || is_unwinding_) {
            break;
        }
    }
    return {};
}

checked_result<void> interpreter::visit_default_stmt(default_stmt* stmt) {
    // Execute all statements in the default body
    // Note: The scope is created by visit_switch_stmt when it decides to execute the default
    for (const auto& s : stmt->body) {
        auto result = dispatch_stmt(s.get());
        if (!result) return result;

        // Check for break or return
        if (hasReturnValue_ || is_unwinding_) {
            break;
        }
    }
    return {};
}

checked_result<void> interpreter::visit_fallthrough_stmt(fallthrough_stmt* stmt) {
    // Set flag to continue to next case
    should_fallthrough_ = true;
    return {};
}

checked_result<void> interpreter::visit_function_decl(function_decl* decl) {
    // Pre-cache symbol IDs for all parameters (parameter binding optimization)
    for (auto& param : decl->parameters) {
        if (param.symbol_id == UINT64_MAX) {
            param.symbol_id = string_symbolizer_->intern(param.name);
        }
    }

    // Don't capture any environment in the closure - just use nullptr
    // The environment stack will handle variable lookup naturally
    auto scriptFunc = std::make_shared<script_defined_function>(
        decl->name,
        decl->parameters,
        decl->return_type,
        decl->body,
        nullptr  // No closure needed - environment stack handles everything
    );

    // Create wrapper function
    script_value functionValue = script_value::make_function([this, scriptFunc](const std::vector<script_value>& args) -> checked_result<script_value> {
        return call_function(*scriptFunc, args);
    }, engine_ref_);

    // Define the function in current environment
    environment_->define(decl->name_id, functionValue);
    return {};
}

checked_result<void> interpreter::visit_class_decl(class_decl* decl) {
    // Set up class parsing context
    class_context prev_context;
    bool had_context = false;
    if (current_class_context_) {
        prev_context = *current_class_context_;
        had_context = true;
    }
    
    // Create new context for this class
    current_class_context_ = class_context{decl->name, {}, false};
    
    // Restore previous context on exit
    auto context_guard = std::shared_ptr<void>(nullptr, [this, prev_context, had_context](void*) {
        if (had_context) {
            current_class_context_ = prev_context;
        } else {
            current_class_context_.reset();
        }
    });
    
    // Check if class already exists (for hot reloading)
    std::shared_ptr<script_class_definition> class_def = nullptr;
    bool is_redefinition = false;

    // Use a static prefix to avoid repeated allocations
    static const std::string CLASS_PREFIX = "__class_";
    std::string class_var_name = CLASS_PREFIX + decl->name;

    // Look for existing class in GLOBAL environment (hot reload support)
    // get_global_environment() now uses engine's global directly (not parent chain walking)
    auto global_env = get_global_environment();
    if (!global_env) {
        return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
    }
    auto existing_result = global_env->get(class_var_name);
    if (existing_result) {
        script_value existing = std::move(existing_result.value());
        if (!existing.is_null() && existing.is_object()) {
            // Class already exists - extract from object holder
            auto objHolder = existing.get_object_holder();
            if (objHolder && objHolder->type_id == class_definition_type_id_) {
                class_def = std::static_pointer_cast<script_class_definition>(objHolder->data);
                is_redefinition = true;
            }
        }
    }
    // If result failed, class doesn't exist yet - that's fine
    
    if (!class_def) {
        // Create a new script class definition
        // Use cached name_id if available, otherwise intern the name
        uint64_t type_id = (decl->name_id != UINT64_MAX) ? decl->name_id : string_symbolizer_->intern(decl->name);
        class_def = std::make_shared<script_class_definition>(decl->name, type_id, engine_ref_);
    } else if (is_redefinition) {
        // Clear old ASTs for hot reload
        class_def->clear_asts();
    }
    
    // Collect new field defaults and methods (using uint64_t IDs for performance)
    std::unordered_map<uint64_t, script_value> new_field_defaults;
    std::unordered_map<uint64_t, script_value> new_methods;
    std::unordered_map<uint64_t, script_value> new_static_methods;
    
    // Reserve capacity based on member count for efficiency
    if (!decl->members.empty()) {
        new_field_defaults.reserve(decl->members.size());
        new_methods.reserve(decl->members.size());
        new_static_methods.reserve(decl->members.size());
    }
    
    // Debug output
    // std::cerr << "DEBUG: Processing class declaration: " << decl->name << std::endl;
    
    // Handle base classes (now supports multiple inheritance)
    if (!decl->base_classes.empty()) {
        std::vector<std::shared_ptr<class_definition>> parent_defs;
        parent_defs.reserve(decl->base_classes.size());

        // Look up each base class definition
        for (const std::string& base_name : decl->base_classes) {
            // First try to find a script class
            script_value base_class_var = make_value();
            auto base_result = environment_->get("__class_" + base_name);
            if (base_result) {
                base_class_var = std::move(base_result.value());
            }

            std::shared_ptr<class_definition> base_class_def;

            if (!base_class_var.is_null() && base_class_var.is_object()) {
                // Found a class definition in __class_<name> - extract from object holder
                // This could be either a C++ class or a script class
                auto objHolder = base_class_var.get_object_holder();
                if (objHolder && objHolder->type_id == class_definition_type_id_) {
                    base_class_def = std::static_pointer_cast<class_definition>(objHolder->data);
                } else {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Base class is not a valid class definition
                }
            } else {
                // Try to find a C++ class using the class lookup callback
                if (class_lookup_callback_) {
                    auto cpp_class_def = class_lookup_callback_(base_name);
                    if (cpp_class_def) {
                        // Found a C++ class!
                        base_class_def = cpp_class_def;
                    } else if (environment_->contains(base_name)) {
                        // Constructor exists but no class definition found
                        // This shouldn't happen with proper engine integration
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Constructor found but no class definition available
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Base class not found
                    }
                } else {
                    // No class lookup callback set - check if constructor exists
                    if (environment_->contains(base_name)) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Script class inheriting from C++ class requires engine integration
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Base class not found
                    }
                }
            }

            if (base_class_def) {
                parent_defs.push_back(base_class_def);

                // Check if this is a C++ class (not a script_class_definition)
                // C++ classes don't have script_class_definition type, so dynamic_pointer_cast fails
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(base_class_def);
                if (!script_class && parent_defs.size() == 1) {
                    // This is a C++ class and it's the first parent - set as cpp_base_class
                    class_def->set_cpp_base_class(base_class_def);
                }
            }
        }

        // Set all parent classes at once
        if (!parent_defs.empty()) {
            // set_parents() now checks for diamond inheritance internally
            if (!class_def->set_parents(parent_defs)) {
                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Diamond inheritance not supported
            }
        }
    }

    // Collect field IDs defined in this class (before evaluating them)
    // This is needed for multiple inheritance conflict detection
    std::unordered_set<uint64_t> derived_field_names;
    for (const auto& member : decl->members) {
        if (member.declaration->get_type() == node_type::variable_decl) {
            auto* var_decl = static_cast<variable_decl*>(member.declaration.get());
            if (!var_decl->is_static) {
                derived_field_names.insert(var_decl->name_id);
            }
        }
    }

    // Check for field name conflicts in multiple inheritance (only for fields NOT redefined in derived class)
    // C++ doesn't allow ambiguous field access - we follow the same semantics
    // But if the derived class defines its own version, it shadows the parent fields (allowed)
    if (!decl->base_classes.empty() && decl->base_classes.size() > 1) {
        const auto& parent_classes = class_def->get_parent_classes();
        std::unordered_map<uint64_t, std::vector<std::string>> field_sources;

        // Collect all fields from each parent (including inherited ones)
        for (const auto& parent : parent_classes) {
            const auto& parent_fields = parent->get_all_field_defaults();
            for (const auto& [field_id, _] : parent_fields) {
                // Skip fields that are redefined in the derived class (shadowing is allowed)
                if (derived_field_names.find(field_id) == derived_field_names.end()) {
                    field_sources[field_id].push_back(parent->get_name());
                }
            }
        }

        // Check for conflicts (field appears in multiple parents and NOT redefined in derived)
        for (const auto& [field_id, sources] : field_sources) {
            if (sources.size() > 1) {
                // Convert ID back to string for error message
                std::string field_name = std::string(string_symbolizer_->get_string(field_id));

                // Build error message with all conflicting parents
                std::string parent_list;
                for (size_t i = 0; i < sources.size(); ++i) {
                    if (i > 0) parent_list += ", ";
                    parent_list += sources[i];
                }

                throw runtime_error("Field '" + field_name + "' inherited from multiple parents (" +
                                  parent_list + ") in class '" + decl->name +
                                  "'. Ambiguous member access - rename field in one of the parent classes, " +
                                  "define it in the derived class to shadow the parent fields, " +
                                  "or use explicit qualification.");
            }
        }
    }

    // Track whether we found an explicit constructor
    bool found_constructor = false;
    
    // Process class members
    for (const auto& member : decl->members) {
        // Extract the actual declaration from the member
        auto* var_decl = member.declaration->get_type() == node_type::variable_decl
            ? static_cast<variable_decl*>(member.declaration.get()) : nullptr;
        auto* func_decl = member.declaration->get_type() == node_type::function_decl
            ? static_cast<function_decl*>(member.declaration.get()) : nullptr;

        if (var_decl) {
            // Field declaration
            script_value default_val(std::monostate{}, engine_ref_);  // Ensure engine reference
            std::string field_name = var_decl->name;
            // Use pre-computed ID from parser (already interned during parsing)
            uint64_t field_id = var_decl->name_id;
            expression_ptr initializer_ast = nullptr;

            if (var_decl->initializer) {
                // Check if the initializer is an assignment expression
                // This happens when the parser sees "x = 0" and creates assignment_expr
                auto* assign_expr = var_decl->initializer->get_type() == node_type::assignment_expr
                    ? static_cast<assignment_expr*>(var_decl->initializer.get()) : nullptr;
                if (assign_expr) {
                    // For field declarations like "x = 0", we need to get the field name from the assignment
                    if (assign_expr->target->get_type() == node_type::identifier_expr) {
                        auto* ident_expr = static_cast<identifier_expr*>(assign_expr->target.get());
                        field_name = ident_expr->name;
                        field_id = ident_expr->symbol_id;  // FIX: Also update the ID to match the field name
                    }
                    // Store the RHS AST for later evaluation during instance construction
                    initializer_ast = assign_expr->value;
                } else {
                    // Normal initializer expression - store the AST
                    initializer_ast = var_decl->initializer;
                }
            }

            // Check if field is static
            if (var_decl->is_static) {
                // Static fields must be evaluated immediately (they're shared across all instances)
                if (initializer_ast) {
                    JAISCRIPT_TRY(dispatch_expr(initializer_ast.get()));
                    default_val = pop_value();

                    // Ensure the default value has an engine reference
                    if (default_val.get_engine_ref().expired() && !engine_ref_.expired()) {
                        default_val.set_engine_ref(engine_ref_);
                    }
                }

                // Add static field directly to the class
                if (field_id != 0) {
                    class_def->add_static_field(field_id, default_val);
                }
            } else {
                // Instance field - store initializer AST for evaluation at construction time
                if (!field_name.empty()) {
                    if (initializer_ast) {
                        // Store the initializer AST in the script class definition
                        class_def->add_field_initializer_ast(field_name, initializer_ast);
                    }
                    // Also add a null default value to the field_defaults map (using ID for performance)
                    // This ensures the field exists but will be properly initialized later
                    new_field_defaults[field_id] = default_val;
                }
            }

        } else if (func_decl) {
            // Method declaration
            auto method_name = func_decl->name;
            // Use pre-computed ID from parser (already interned during parsing)
            uint64_t method_id = func_decl->name_id;

            // Check for constructor
            if (method_name == decl->name) {
                // Constructor
                found_constructor = true;

                // Pre-cache symbol IDs for constructor parameters
                for (auto& param : func_decl->parameters) {
                    if (param.symbol_id == UINT64_MAX) {
                        param.symbol_id = string_symbolizer_->intern(param.name);
                    }
                }

                // Set in_method flag while processing constructor body (for static field access)
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }

                try {
                    class_def->add_constructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration),
                        this
                    );
                } catch (const runtime_error&) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
                // Constructor will be registered after all members are processed
                
            } else if (method_name.size() > 0 && method_name[0] == '~') {
                // Destructor
                // Set in_method flag while processing destructor body
                if (current_class_context_) {
                    current_class_context_->in_method = true;
                }
                
                try {
                    class_def->add_destructor_from_ast(
                        std::static_pointer_cast<function_decl>(member.declaration),
                        this
                    );
                } catch (const runtime_error&) {
                    // Reset in_method flag on error
                    if (current_class_context_) {
                        current_class_context_->in_method = false;
                    }
                    throw;
                }
                
                // Reset in_method flag
                if (current_class_context_) {
                    current_class_context_->in_method = false;
                }
                
            } else {
                // Regular method or static method
                auto method_ast = std::static_pointer_cast<function_decl>(member.declaration);

                // Capture the current definition environment (namespace or global) for method environments
                // This is the stable environment where the class is defined, not an execution environment
                // Methods need access to this scope for static members, namespace variables, etc.
                auto definition_env = environment_;

                if (is_redefinition) {
                    // For redefinition, just collect the method function
                    // We'll add it to the class via redefine_class later

                    if (method_ast->is_static) {
                        // Static method - no 'this' parameter
                        auto static_method_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()),
                                                  method_ast,
                                                  class_def,
                                                  definition_env,
                                                  class_name = decl->name](const std::vector<script_value>& args) -> script_value {
                            auto self = weak_self.lock();
                            if (!self) {
                                throw runtime_error("Interpreter was destroyed before static method call");
                            }

                            // Create a static method environment (C++ scope rules for static members)
                            // This environment automatically resolves unqualified static member access
                            // Use definition_env (namespace/global) as parent
                            auto static_env = std::make_shared<environment>(
                                definition_env,
                                self->string_symbolizer_,
                                class_def
                            );

                            // Call the interpreter method directly without 'this'
                            return self->execute_method_ast(method_ast, static_env, args);
                        };

                        new_static_methods[method_id] = script_value::make_function(static_method_func, engine_ref_);
                    } else {
                        // Instance method - has 'this' parameter
                        auto method_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()),
                                           method_ast,
                                           class_def,
                                           definition_env,
                                           class_name = decl->name](const std::vector<script_value>& args) -> script_value {
                            auto self = weak_self.lock();
                            if (!self) {
                                throw runtime_error("Interpreter was destroyed before method call");
                            }

                            // First argument should be 'this' object
                            if (args.empty()) {
                                throw runtime_error("Method called without 'this' object");
                            }

                            // Extract 'this' from first argument
                            script_value this_obj = args[0];

                            // Create remaining arguments (excluding 'this')
                            std::vector<script_value> method_args(args.begin() + 1, args.end());

                            // Create a method environment that provides implicit 'this' field access
                            // Use definition_env (namespace/global) as parent
                            scoped_method_environment method_env(
                                self.get(),
                                definition_env,
                                this_obj
                            );

                            // Call the interpreter method directly
                            auto result = self->execute_method_ast(method_ast, method_env.get(), method_args);

                            return result;
                        };

                        new_methods[method_id] = script_value::make_function(method_func, engine_ref_);
                    }
                } else {
                    // For new classes, add method normally
                    try {
                        // Set in_method flag while processing the method
                        if (current_class_context_) {
                            current_class_context_->in_method = true;
                        }

                        if (method_ast->is_static) {
                            // Add static method - pass current environment as definition environment
                            class_def->add_static_script_method(method_name, method_ast, this, environment_);
                        } else {
                            // Add instance method
                            class_def->add_method_from_ast(method_name, method_ast, this, is_redefinition);
                        }
                        
                        // Reset in_method flag
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                    } catch (const runtime_error& e) {
                        // Reset in_method flag on error
                        if (current_class_context_) {
                            current_class_context_->in_method = false;
                        }
                        // Don't re-throw "Undefined variable" errors - they'll be validated later
                        std::string error_msg = e.what();
                        if (error_msg.find("Undefined variable") == std::string::npos) {
                            // Re-throw other errors
                            throw;
                        }
                        // For undefined variable errors, we've already collected them in unresolved_identifiers
                    }
                }
            }
        }
    }
    
    // After processing all members, create a dispatcher for constructors if any were found
    if (found_constructor) {
        // Capture the global environment for constructor execution
        // This ensures constructor body has access to global definitions (classes, functions)
        auto definition_env = get_global_environment();

        // Create a constructor dispatcher that selects based on argument count
        auto ctor_dispatcher = [weak_self = std::weak_ptr<interpreter>(shared_from_this()),
                               class_def,
                               definition_env,
                               class_name = decl->name,
                               cpp_object_field_id = cpp_object_field_id_](const std::vector<script_value>& args) -> script_value {
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Get all constructor ASTs
            const auto& ctor_asts = class_def->get_constructor_asts();

            // Find constructor with matching parameter count AND types
            // Priority: 1) exact type match, 2) numeric conversion match, 3) untyped fallback
            std::shared_ptr<function_decl> exact_match_ctor;
            std::shared_ptr<function_decl> convertible_match_ctor;
            std::shared_ptr<function_decl> arity_match_ctor;  // Fallback for untyped params

            for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast->parameters.size() != args.size()) {
                    continue;
                }

                // Remember first arity match as fallback
                if (!arity_match_ctor) {
                    arity_match_ctor = ctor_ast;
                }

                // Check if all parameter types match exactly
                bool exact_match = true;
                bool convertible_match = true;
                for (size_t i = 0; i < args.size() && (exact_match || convertible_match); ++i) {
                    const auto& param = ctor_ast->parameters[i];
                    if (param.type && !param.type->type_name.empty()) {
                        // Parameter has explicit type - check if arg is compatible
                        auto arg_type = args[i].type();
                        if (arg_type == script_value_type::jai_object_type) {
                            // For objects, check class name
                            auto instance = const_cast<script_value&>(args[i]).get_class_instance();
                            if (instance) {
                                if (instance->get_class_name() != param.type->type_name) {
                                    exact_match = false;
                                    convertible_match = false;
                                }
                            } else {
                                exact_match = false;
                                convertible_match = false;
                            }
                        } else {
                            // For primitives, check base type
                            if (arg_type != param.type->base_type) {
                                exact_match = false;
                                // Check if numeric conversion is allowed
                                bool is_numeric_conversion =
                                    (arg_type == script_value_type::jai_int_type &&
                                     param.type->base_type == script_value_type::jai_float_type) ||
                                    (arg_type == script_value_type::jai_float_type &&
                                     param.type->base_type == script_value_type::jai_int_type);
                                if (!is_numeric_conversion) {
                                    convertible_match = false;
                                }
                            }
                        }
                    }
                    // If param has no type, it accepts anything
                }

                if (exact_match && !exact_match_ctor) {
                    exact_match_ctor = ctor_ast;
                }
                if (convertible_match && !convertible_match_ctor) {
                    convertible_match_ctor = ctor_ast;
                }
            }

            // Select best matching constructor: exact > convertible > arity fallback
            std::shared_ptr<function_decl> matching_ctor = exact_match_ctor;
            if (!matching_ctor) {
                matching_ctor = convertible_match_ctor;
            }
            if (!matching_ctor) {
                matching_ctor = arity_match_ctor;
            }

            if (!matching_ctor) {
                throw runtime_error("No constructor found for " + class_name +
                                  " with " + std::to_string(args.size()) + " arguments");
            }
            
            // Create instance
            auto instance = class_def->create_instance();
            // Instance created

            // Create 'this' value using the class_def's registered name and type_id
            // This ensures we use the exact name/id that was registered (e.g., with namespace)
            // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
            auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, self->engine_ref_, true);

            // Create a regular environment for field initializers and constructor initializer arguments
            // Use the captured definition environment as the parent
            auto init_env = std::make_shared<environment>(definition_env, self->string_symbolizer_);
            init_env->define("this", this_value);

            // Bind constructor parameters so they're available in initializer expressions
            // NOTE: Do NOT clone here - these params are just for field initializer evaluation
            // The actual parameter binding with proper value/reference semantics happens in call_function
            if (matching_ctor->parameters.size() != args.size()) {
                throw runtime_error("Constructor parameter count mismatch");
            }
            for (size_t i = 0; i < matching_ctor->parameters.size(); ++i) {
                init_env->define(matching_ctor->parameters[i].name, args[i]);
            }

            // Track whether the iterative multi-level loop handled parent field initializers
            bool handled_parent_init = false;

            // Process constructor initializers (: super(args), : this(args))
            for (const auto& initializer : matching_ctor->initializers) {
                if (initializer.target == "super") {
                    // Call base class constructor
                    if (class_def->get_parent()) {
                        // Evaluate initializer arguments in init environment
                        std::vector<script_value> init_args;
                        init_args.reserve(initializer.arguments.size());
                        
                        // Temporarily switch to init environment for argument evaluation
                        auto old_env = self->environment_;
                        self->environment_ = init_env;

                        for (const auto& arg_expr : initializer.arguments) {
                            auto result = self->dispatch_expr(arg_expr.get());
                            if (!result) {
                                // Restore environment before throwing
                                self->environment_ = old_env;
                                throw runtime_error("Failed to evaluate constructor initializer argument");
                            }
                            init_args.push_back(self->pop_value());
                        }

                        // Restore environment
                        self->environment_ = old_env;
                        
                        // Call parent constructor to initialize parent fields
                        auto parent_class = class_def->get_parent();
                        if (parent_class) {
                            // Check if parent is a script class
                            auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent_class);
                            if (parent_script_class) {
                                // Find matching parent constructor
                                const auto& parent_ctor_asts = parent_script_class->get_constructor_asts();
                                std::shared_ptr<function_decl> parent_ctor;
                                for (const auto& ctor_ast : parent_ctor_asts) {
                                    if (ctor_ast->parameters.size() == init_args.size()) {
                                        parent_ctor = ctor_ast;
                                        break;
                                    }
                                }

                                if (parent_ctor) {
                                    // === Multi-level inheritance support ===
                                    // We need to process the entire super() chain to find and call the C++ base constructor.
                                    // Constructor bodies are executed AFTER field initializers by the outer flow.
                                    //
                                    // Algorithm (iterative):
                                    // 1. Walk up super() calls, evaluating arguments at each level
                                    // 2. When we hit a C++ class, call its constructor to get _cpp_object
                                    // 3. Store constructor info for later body execution
                                    // 4. Execute bodies from root to leaf, interleaved with field initializers

                                    struct CtorChainEntry {
                                        std::shared_ptr<script_class_definition> script_class;
                                        std::shared_ptr<function_decl> ctor;
                                        std::vector<script_value> args;
                                    };

                                    std::vector<CtorChainEntry> ctor_chain;
                                    ctor_chain.push_back({parent_script_class, parent_ctor, init_args});

                                    // Walk up the inheritance chain, collecting constructors and evaluating args
                                    size_t chain_idx = 0;
                                    while (chain_idx < ctor_chain.size()) {
                                        auto& entry = ctor_chain[chain_idx];

                                        // Create environment for this level's argument evaluation
                                        auto level_env = std::make_shared<environment>(definition_env, self->string_symbolizer_);
                                        level_env->define("this", this_value);
                                        for (size_t pi = 0; pi < entry.ctor->parameters.size() && pi < entry.args.size(); ++pi) {
                                            level_env->define(entry.ctor->parameters[pi].name, entry.args[pi]);
                                        }

                                        // Look for super() in this constructor's initializers
                                        for (const auto& init : entry.ctor->initializers) {
                                            if (init.target == "super") {
                                                auto ancestor = entry.script_class->get_parent();
                                                if (ancestor) {
                                                    // Evaluate super() arguments in this level's environment
                                                    std::vector<script_value> ancestor_args;
                                                    auto old_env = self->environment_;
                                                    self->environment_ = level_env;
                                                    for (const auto& arg_expr : init.arguments) {
                                                        auto r = self->dispatch_expr(arg_expr.get());
                                                        if (!r) {
                                                            self->environment_ = old_env;
                                                            throw runtime_error("Failed to evaluate super() argument at inheritance level " + std::to_string(chain_idx));
                                                        }
                                                        ancestor_args.push_back(self->pop_value());
                                                    }
                                                    self->environment_ = old_env;

                                                    auto ancestor_script = std::dynamic_pointer_cast<script_class_definition>(ancestor);
                                                    if (ancestor_script) {
                                                        // Ancestor is a script class - find matching constructor and add to chain
                                                        const auto& ancestor_ctors = ancestor_script->get_constructor_asts();
                                                        std::shared_ptr<function_decl> ancestor_ctor;
                                                        for (const auto& ac : ancestor_ctors) {
                                                            if (ac->parameters.size() == ancestor_args.size()) {
                                                                ancestor_ctor = ac;
                                                                break;
                                                            }
                                                        }
                                                        if (ancestor_ctor) {
                                                            ctor_chain.push_back({ancestor_script, ancestor_ctor, std::move(ancestor_args)});
                                                        } else if (!ancestor_ctors.empty()) {
                                                            throw runtime_error("No matching constructor for ancestor class " + ancestor->get_name());
                                                        }
                                                    } else {
                                                        // Ancestor is a C++ class - call its constructor NOW
                                                        auto cpp_name = ancestor->get_name();
                                                        auto cpp_ctor_result = self->environment_->get(cpp_name);
                                                        if (cpp_ctor_result && cpp_ctor_result.value().is_function()) {
                                                            auto cpp_result = cpp_ctor_result.value().as_function()(ancestor_args);
                                                            if (!cpp_result) {
                                                                throw runtime_error("Failed to call C++ ancestor constructor: " + cpp_name);
                                                            }
                                                            script_value cpp_obj = std::move(cpp_result.value());

                                                            // Copy _cpp_object from C++ instance to our instance
                                                            if (cpp_obj.is_object()) {
                                                                auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
                                                                if (cpp_instance) {
                                                                    uint64_t src_id = cpp_instance->get_cpp_object_field_id();
                                                                    uint64_t dst_id = instance->get_cpp_object_field_id();
                                                                    if (cpp_instance->has_field(src_id)) {
                                                                        instance->set_field(dst_id, cpp_instance->get_field(src_id));
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                                break; // Only one super() per constructor
                                            }
                                        }
                                        chain_idx++;
                                    }

                                    // Execute field initializers and constructor bodies from root to leaf
                                    // This ensures proper initialization order:
                                    // 1. Grandparent field defaults, then grandparent body
                                    // 2. Parent field defaults, then parent body
                                    // 3. (Current class handled by outer code after this block)
                                    for (auto it = ctor_chain.rbegin(); it != ctor_chain.rend(); ++it) {
                                        // Create init environment for field initializers
                                        auto level_init_env = std::make_shared<environment>(definition_env, self->string_symbolizer_);
                                        level_init_env->define("this", this_value);
                                        for (size_t pi = 0; pi < it->ctor->parameters.size() && pi < it->args.size(); ++pi) {
                                            level_init_env->define(it->ctor->parameters[pi].name, it->args[pi]);
                                        }

                                        // Evaluate THIS class's field initializers only (not parents - they're handled by their own iteration)
                                        const auto& field_initializers = it->script_class->get_field_initializer_asts();
                                        auto old_env = self->environment_;
                                        self->environment_ = level_init_env;
                                        for (const auto& [field_name, initializer_ast] : field_initializers) {
                                            if (initializer_ast) {
                                                auto r = self->dispatch_expr(initializer_ast.get());
                                                if (r) {
                                                    script_value field_value = self->pop_value();
                                                    uint64_t field_name_id = self->string_symbolizer_->intern(field_name);
                                                    instance->set_field(field_name_id, std::move(field_value));
                                                }
                                            }
                                        }
                                        self->environment_ = old_env;

                                        // Execute constructor body
                                        scoped_method_environment method_env(
                                            self.get(),
                                            definition_env,
                                            this_value
                                        );
                                        self->execute_method_ast(it->ctor, method_env.get(), it->args);
                                    }
                                    // Mark that we handled all parent field initializers
                                    handled_parent_init = true;
                                } else if (parent_ctor_asts.empty() && init_args.empty()) {
                                    // Parent has no explicit constructors (only default constructor)
                                    // and super() called with no arguments - this is valid, nothing to do
                                    // The parent fields will be initialized with their default values
                                } else {
                                    throw runtime_error("No matching parent constructor found for super(" +
                                                      std::to_string(init_args.size()) + " arguments)");
                                }
                            } else {
                                // Parent is a C++ class - call its constructor
                                try {
                                    // Get the C++ class constructor function
                                    auto parent_name = parent_class->get_name();
                                    auto ctor_result = self->environment_->get(parent_name);
                                    if (ctor_result && ctor_result.value().is_function()) {
                                        script_value cpp_ctor = std::move(ctor_result.value());
                                        // Call C++ constructor with init_args
                                        auto result = cpp_ctor.as_function()(init_args);
                                        if (!result) {
                                            // Constructor failed - throw exception
                                            throw runtime_error(result.message());
                                        }
                                        script_value cpp_obj = std::move(result.value());

                                        // Extract the C++ object and store it in _cpp_object field
                                        if (cpp_obj.is_object()) {
                                            auto cpp_instance = cpp_obj.as<std::shared_ptr<class_instance>>();
                                            if (cpp_instance) {
                                                // Use each instance's own field ID getter to ensure consistency
                                                uint64_t src_field_id = cpp_instance->get_cpp_object_field_id();
                                                uint64_t dst_field_id = instance->get_cpp_object_field_id();
                                                if (cpp_instance->has_field(src_field_id)) {
                                                    // Copy _cpp_object from parent to derived instance
                                                    auto src_value = cpp_instance->get_field(src_field_id);
                                                    instance->set_field(dst_field_id, src_value);
                                                }
                                            }
                                        }
                                    }
                                } catch (const runtime_error& e) {
                                    throw runtime_error("Failed to call C++ parent constructor: " + std::string(e.what()));
                                }
                            }
                        }
                    } else {
                        throw runtime_error("Cannot call super() - class has no base class");
                    }
                } else if (initializer.target == "this") {
                    // Delegate to another constructor in the same class
                    // Evaluate initializer arguments in constructor environment
                    std::vector<script_value> init_args;
                    init_args.reserve(initializer.arguments.size());
                    
                    // Temporarily switch to init environment for argument evaluation
                    auto old_env = self->environment_;
                    self->environment_ = init_env;

                    for (const auto& arg_expr : initializer.arguments) {
                        auto result = self->dispatch_expr(arg_expr.get());
                        if (!result) {
                            // Restore environment before throwing
                            self->environment_ = old_env;
                            throw runtime_error("Failed to evaluate constructor initializer argument");
                        }
                        init_args.push_back(self->pop_value());
                    }

                    // Restore environment
                    self->environment_ = old_env;
                    
                    // Find matching constructor in same class
                    const auto& ctor_asts = class_def->get_constructor_asts();
                    std::shared_ptr<function_decl> target_ctor;
                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() == init_args.size() && ctor_ast != matching_ctor) {
                            target_ctor = ctor_ast;
                            break;
                        }
                    }
                    
                    if (!target_ctor) {
                        throw runtime_error("No matching constructor found for this(" + 
                                          std::to_string(init_args.size()) + " arguments)");
                    }
                    
                    // Call the target constructor on this instance with method environment
                    // Use definition_env as parent
                    scoped_method_environment target_method_env(
                        self.get(),
                        definition_env,
                        this_value
                    );

                    self->execute_method_ast(target_ctor, target_method_env.get(), init_args);
                }
            }

            // Evaluate field initializers BEFORE executing the constructor body
            // Field initializers can access constructor parameters via init_env
            // If the iterative multi-level loop already handled parent field initializers,
            // skip recursive parent processing to avoid re-evaluating defaults
            self->evaluate_field_initializers(instance, class_def, init_env, handled_parent_init);

            // Execute the matching constructor with method environment
            // Create a method environment that provides implicit 'this' field access
            // Use definition_env as parent
            scoped_method_environment method_env(
                self.get(),
                definition_env,
                this_value
            );

            // Execute constructor as a method so it has access to 'this' and fields
            // The constructor implicitly returns 'this', so use that return value
            // instead of creating a new script_value (which would be a duplicate reference)
            auto result = self->execute_method_ast(matching_ctor, method_env.get(), args);

            // Constructor executed and returned 'this'
            return result;
        };

        // Register the dispatcher in global environment (constructors are always global)
        get_global_environment()->define(decl->name_id, script_value::make_function(ctor_dispatcher, engine_ref_));
    }

    // If no constructor was found, create a default constructor
    else {
        // Create a default constructor that just initializes the instance
        auto default_ctor_func = [weak_self = std::weak_ptr<interpreter>(shared_from_this()), class_def, class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            // Get strong reference from weak_ptr
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Default constructor shouldn't have arguments
            if (!args.empty()) {
                throw runtime_error("Default constructor for class " + class_name + " takes no arguments");
            }
            
            // Create instance using inherited create_instance()!
            auto instance = class_def->create_instance();
            // Default constructor instance created

            // Create 'this' value for field initializer evaluation
            // Use class_def's registered name and type_id to handle namespaces correctly
            // Note: is_class_instance_wrapper=true because instance is a class_instance object (script_class_instance inherits from class_instance)
            auto this_value = script_value::make_object(class_def->get_name(), class_def->get_type_id(), instance, self->engine_ref_, true);

            // Find the root (global) environment with cycle detection
            std::unordered_set<environment*> visited;
            auto current_env = self->environment_;
            while (current_env && current_env->get_parent()) {
                // Cycle detection
                if (visited.count(current_env.get()) > 0) {
                    // Cycle detected! Log and break
                    std::cerr << "WARNING: Environment cycle detected at " << current_env.get()
                              << " (type: " << typeid(*current_env).name() << ")\n";
                    std::cerr << "  Visited " << visited.size() << " environments before cycle\n";
                    break;
                }
                visited.insert(current_env.get());
                current_env = current_env->get_parent();
            }
            auto global_env = current_env ? current_env : self->environment_;

            // Evaluate field initializers with a regular environment that has 'this'
            auto init_env = std::make_shared<environment>(global_env, self->string_symbolizer_);
            init_env->define("this", this_value);
            self->evaluate_field_initializers(instance, class_def, init_env);

            // Default constructor object wrapped
            return this_value;
        };

        // Register default constructor in global environment (constructors are always global)
        get_global_environment()->define(decl->name_id, script_value::make_function(default_ctor_func, engine_ref_));
        // std::cerr << "DEBUG: Registered default constructor for class: " << decl->name << std::endl;
    }
    
    // If this is a redefinition, we need to call redefine_class to update all instances
    if (is_redefinition) {
        // Evaluate field initializer ASTs to get actual default values for hot reload
        std::unordered_map<uint64_t, script_value> field_defaults_with_engine;
        field_defaults_with_engine.reserve(new_field_defaults.size());

        for (const auto& [field_id, value] : new_field_defaults) {
            // Convert ID back to string for get_field_initializer_ast (which still uses strings)
            std::string field_name = std::string(string_symbolizer_->get_string(field_id));

            // Get the field initializer AST from the class definition
            auto initializer_ast = class_def->get_field_initializer_ast(field_name);
            script_value evaluated_value = value;

            if (initializer_ast) {
                // Evaluate the initializer AST in the current (definition) environment
                // to get the actual default value
                JAISCRIPT_TRY(dispatch_expr(initializer_ast.get()));
                evaluated_value = pop_value();
            }

            // Ensure the value has an engine reference
            if (evaluated_value.get_engine_ref().expired() && !engine_ref_.expired()) {
                evaluated_value.set_engine_ref(engine_ref_);
            }

            field_defaults_with_engine[field_id] = evaluated_value;
        }

        // Generate getter and setter methods for all fields (including new ones)
        // This is needed for hot reload to work properly with property access
        for (const auto& [field_id, default_val] : field_defaults_with_engine) {
            // Get field name for getter/setter method names
            std::string field_name = std::string(string_symbolizer_->get_string(field_id));

            // Add getter method
            auto getter = [field_id, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Get the field value using the cached ID
                return instance->get_field(field_id);
            };
            uint64_t getter_id = string_symbolizer_->intern("_get_" + field_name);
            new_methods[getter_id] = script_value::make_function(getter, engine_ref_);

            // Add setter method
            auto setter = [field_id, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Set the field value using the cached ID
                instance->set_field(field_id, args[1]);

                // Return the value that was set
                return args[1];
            };
            uint64_t setter_id = string_symbolizer_->intern("_set_" + field_name);
            new_methods[setter_id] = script_value::make_function(setter, engine_ref_);
        }
        
        
        // Call redefine_class with the new field defaults and methods
        // Call redefine_class to migrate existing instances
        class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_ref_);

        // Invalidate cached field pointers - they may point to stale storage after migration
        environment_->clear_all_parent_caches();
    } else {
        // For new classes, add the fields normally
        for (const auto& [field_id, default_val] : new_field_defaults) {
            // Convert ID back to string for add_field (legacy API)
            std::string field_name = std::string(string_symbolizer_->get_string(field_id));
            class_def->add_field(field_name, default_val);

            // Generate getter and setter methods for script class fields
            // This enables property-style access (obj.field) to work properly

            // Add getter method - capture field_id for performance
            auto getter = [field_id, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Get the field value using ID
                return instance->get_field(field_id);
            };
            class_def->add_method("_get_" + field_name, getter);

            // Add setter method - capture field_id for performance
            auto setter = [field_id, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }

                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();

                // Set the field value using ID
                instance->set_field(field_id, args[1]);

                // Return the value that was set
                return args[1];
            };
            class_def->add_method("_set_" + field_name, setter);
        }
        // Initialize fingerprint for future comparisons
        class_def->initialize_fingerprint();
    }
    
    // Validate unresolved identifiers before finalizing the class
    if (current_class_context_ && !current_class_context_->unresolved_identifiers.empty()) {
        // Get all fields including inherited ones (already efficient)
        auto all_fields = class_def->get_all_field_defaults();
        
        // Check each unresolved identifier (already stored as IDs)
        std::vector<std::string> undefined_identifiers;
        for (const auto& identifier_id : current_class_context_->unresolved_identifiers) {
            // Skip special keywords that are always valid in methods
            if (identifier_id == this_id_ || identifier_id == super_id_) continue;

            // Check if it's a field (all_fields uses ID-based keys)
            if (all_fields.find(identifier_id) != all_fields.end()) continue;

            // Convert ID back to string for find_method (which still uses strings)
            std::string identifier = std::string(string_symbolizer_->get_string(identifier_id));

            // Check if it's a method (including inherited)
            if (class_def->find_method(identifier).owner_class != nullptr) continue;

            // Not found as field or method
            undefined_identifiers.push_back(identifier);
        }
        
        // If there are still undefined identifiers, throw an error
        if (!undefined_identifiers.empty()) {
            std::string error_msg = "Undefined identifiers in class '" + decl->name + "': ";
            for (size_t i = 0; i < undefined_identifiers.size(); ++i) {
                if (i > 0) error_msg += ", ";
                error_msg += "'" + undefined_identifiers[i] + "'";
            }
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined identifiers in class
        }
    }

    // CRITICAL: Register the script class in the class registry
    // This makes it available for type lookups and prevents "unregistered class" errors
    auto eng = engine_ref_.lock();
    if (!eng) {
        return checked_result<void>(make_error_code(runtime_error_code::engine_destroyed));
    }
    auto register_result = eng->get_class_registry().register_script_class(class_def);
    if (!register_result) {
        return register_result;
    }

    // Store the class definition in a special variable for later retrieval
    // This allows inheritance and other features to work
    // IMPORTANT: Define in GLOBAL environment so it's visible across execute() calls (hot reload)
    global_env->define(class_var_name, script_value::make_object("class_definition", class_definition_type_id_, class_def, engine_ref_, false));

    // The constructor function is already registered in the environment
    // which allows "new ClassName()" syntax to work
    return {};
}

checked_result<void> interpreter::visit_namespace_decl(namespace_decl* decl) {
    // Namespaces are FLAT - "my::nested::deep" is a single namespace name
    // Members are stored in a registry and accessed via qualified names (ns::member)

    // Intern the namespace name if not already done
    if (decl->name_id == UINT64_MAX) {
        decl->name_id = string_symbolizer_->intern(decl->name);
    }

    // Get or create namespace data
    auto& ns_data = namespaces_[decl->name_id];
    if (!ns_data) {
        ns_data = std::make_shared<namespace_data>();
    }

    // Process all declarations within the namespace
    // Functions, variables, and classes are stored in the namespace_data registry
    for (const auto& member_decl : decl->declarations) {
        // Check what kind of declaration this is
        if (member_decl->get_type() == node_type::function_decl) {
            auto* func_decl = static_cast<function_decl*>(member_decl.get());
            // Intern the function name if not already done
            if (func_decl->name_id == UINT64_MAX) {
                func_decl->name_id = string_symbolizer_->intern(func_decl->name);
            }

            // Check for collisions: same name AND same arity in this namespace
            auto& overloads = ns_data->functions[func_decl->name_id];
            for (auto it = overloads.begin(); it != overloads.end(); ++it) {
                if ((*it)->parameters.size() == func_decl->parameters.size()) {
                    // Collision detected!
                    if (!func_decl->is_override) {
                        std::string error_msg = "Function '" + func_decl->name + "' with " +
                                              std::to_string(func_decl->parameters.size()) +
                                              " parameters already exists in namespace '" + decl->name +
                                              "'. Use 'override' keyword to replace it.";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch), error_msg);
                    }
                    // Override is specified - remove old definition
                    overloads.erase(it);
                    break;
                }
            }

            // Check if this namespace name matches a class name
            // If so, check for collision with class static methods
            if (!func_decl->is_override) {
                uint64_t class_var_id = string_symbolizer_->intern("__class_" + decl->name);
                auto class_result = environment_->get(class_var_id);
                if (class_result && class_result.value().is_object()) {
                    script_value class_var = std::move(class_result.value());
                    {
                        auto obj_holder = class_var.get_object_holder();
                        if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
                            auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);

                            // Arity-aware collision check
                            if (class_def->has_static_method_with_arity(func_decl->name_id, func_decl->parameters.size())) {
                                std::string error_msg = "Function '" + func_decl->name + "' with " +
                                                      std::to_string(func_decl->parameters.size()) +
                                                      " parameters in namespace '" + decl->name +
                                                      "' collides with static method in class '" + decl->name +
                                                      "'. Use 'override' keyword to override the class static method.";
                                return checked_result<void>(make_error_code(runtime_error_code::type_mismatch), error_msg);
                            }
                        }
                    }
                }
            }

            // Store function declaration
            overloads.emplace_back(std::make_shared<function_decl>(*func_decl));

        } else if (member_decl->get_type() == node_type::variable_decl) {
            auto* var_decl = static_cast<variable_decl*>(member_decl.get());
            // Intern the variable name if not already done
            if (var_decl->name_id == UINT64_MAX) {
                var_decl->name_id = string_symbolizer_->intern(var_decl->name);
            }

            // Evaluate variable initializer and store value
            if (var_decl->initializer) {
                JAISCRIPT_TRY(dispatch_expr(var_decl->initializer.get()));
                script_value value = pop_value();
                ns_data->variables[var_decl->name_id] = value;
            } else {
                // No initializer - store null
                ns_data->variables[var_decl->name_id] = make_value();
            }

        } else if (member_decl->get_type() == node_type::class_decl) {
            auto* class_decl_ptr = static_cast<class_decl*>(member_decl.get());
            // Intern the class name if not already done
            if (class_decl_ptr->name_id == UINT64_MAX) {
                class_decl_ptr->name_id = string_symbolizer_->intern(class_decl_ptr->name);
            }

            // Process class declaration normally to register it globally
            // Then also store reference in namespace
            JAISCRIPT_TRY(dispatch_decl(class_decl_ptr));

            // Look up the registered class definition using __class_ prefix
            std::string class_var_name = "__class_" + class_decl_ptr->name;
            uint64_t class_var_id = string_symbolizer_->intern(class_var_name);
            if (auto* class_def_var = environment_->get_value_ptr(class_var_id)) {
                if (class_def_var->is_object()) {
                    auto obj_holder = class_def_var->get_object_holder();
                    if (obj_holder && obj_holder->type_id == class_definition_type_id_) {
                        auto class_def = std::static_pointer_cast<class_definition>(obj_holder->data);
                        ns_data->classes[class_decl_ptr->name_id] = class_def;
                    }
                }
            }

        } else {
            // Other declaration types (expressions, includes, etc.) - execute normally
            JAISCRIPT_TRY(dispatch_decl(member_decl.get()));
        }
    }

    return {};
}

checked_result<void> interpreter::visit_expression_decl(expression_decl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    return dispatch_expr(decl->expression.get());
}

checked_result<void> interpreter::visit_include_decl(include_decl* decl) {
    // Get the engine reference
    auto engine_ptr = engine_ref_.lock();
    if (!engine_ptr) {
        // [ErrorText] Engine reference expired during include processing
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Get the path either from literal or expression
    std::string path;
    if (decl->path_expr) {
        // Evaluate the expression to get the path
        JAISCRIPT_TRY(dispatch_expr(decl->path_expr.get()));
        script_value path_value = pop_value();

        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            // [ErrorText] Include path expression must evaluate to a string
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = decl->path;
    }

    // Resolve the file path
    std::string resolved_path = resolve_include_path(path, engine_ptr);

    // Read the file contents
    std::ifstream file(resolved_path);
    if (!file.is_open()) {
        // [ErrorText] Failed to open include file
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Execute the included file
    // Note: include always parses and executes the file
    auto result = engine_ptr->execute(content);

    // Push the result onto the value stack
    push_value(result);
    return {};
}

checked_result<void> interpreter::visit_import_decl(import_decl* decl) {
    // Get the engine reference
    auto engine_ptr = engine_ref_.lock();
    if (!engine_ptr) {
        // [ErrorText] Engine reference expired during import processing
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
    }

    // Get the path either from literal or expression
    std::string path;
    if (decl->path_expr) {
        // Evaluate the expression to get the path
        JAISCRIPT_TRY(dispatch_expr(decl->path_expr.get()));
        script_value path_value = pop_value();

        // Convert to string
        if (path_value.type() != script_value_type::jai_string_type) {
            // [ErrorText] Import path expression must evaluate to a string
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));
        }
        path = path_value.as<std::string>();
    } else {
        // Use the literal path
        path = decl->path;
    }

    // Resolve the file path
    std::string resolved_path = resolve_include_path(path, engine_ptr);

    // Use the engine's public API to handle import with tracking
    auto result = engine_ptr->execute_import(resolved_path);

    // Push the result onto the value stack
    push_value(result);
    return {};
}

// Execute a method AST with a given environment
script_value interpreter::execute_method_ast(std::shared_ptr<function_decl> ast,
                                           std::shared_ptr<environment> method_env,
                                           const std::vector<script_value>& args) {
    // Create a script_defined_function with the method environment
    script_defined_function script_func(
        ast->name,
        ast->parameters,
        ast->return_type,
        ast->body,
        method_env  // Method environment with 'this'
    );

    // Execute method with the arguments
    auto result = call_function(script_func, args);

    // Unwrap checked_result - throw on error (execute_method_ast callers expect script_value)
    if (!result) {
        throw runtime_error(result.message().empty() ? result.error().message() : result.message());
    }
    return std::move(result.value());
}

// Evaluate field initializers for a script class instance at construction time
// If skip_parent_recursion is true, only evaluate THIS class's field initializers (parents already handled)
void interpreter::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env,
                                             bool skip_parent_recursion) {
    // First, evaluate parent class field initializers (if any)
    // Support multiple inheritance by iterating over all parent classes
    // Skip this if parents were already processed (e.g., by multi-level super() chain handling)
    if (!skip_parent_recursion) {
        for (const auto& parent : class_def->get_parent_classes()) {
            auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
            if (parent_script_class) {
                // Recursively evaluate parent field initializers
                evaluate_field_initializers(instance, parent_script_class, init_env, false);
            }
        }
    }

    // Now evaluate this class's field initializers
    const auto& field_initializers = class_def->get_field_initializer_asts();

    // Temporarily switch to the init environment for evaluation
    auto old_env = environment_;
    environment_ = init_env;

    for (const auto& [field_name, initializer_ast] : field_initializers) {
        if (initializer_ast) {
            // Evaluate the initializer expression
            auto result = dispatch_expr(initializer_ast.get());
            if (!result) {
                // Restore environment before throwing
                environment_ = old_env;
                throw runtime_error("Failed to evaluate field initializer for '" + field_name + "'");
            }
            script_value field_value = pop_value();

            // Ensure the field value has an engine reference
            if (field_value.get_engine_ref().expired() && !engine_ref_.expired()) {
                field_value.set_engine_ref(engine_ref_);
            }

            // Set the field on the instance (intern the name to ID)
            uint64_t field_id = string_symbolizer_->intern(field_name);
            instance->set_field(field_id, field_value);
        }
    }

    // Restore environment
    environment_ = old_env;
}

// RAII wrapper for method environments
scoped_method_environment::scoped_method_environment(
    interpreter* interp,
    std::shared_ptr<environment> parent,
    const script_value& this_obj)
    : interp_(interp)
    , env_(interp->get_pooled_method_environment(parent, this_obj))
{
    // Define 'this' as a regular variable for compatibility
    // method_environment::get() also has special handling for 'this'
    env_->define("this", this_obj);
}

scoped_method_environment::~scoped_method_environment() {
    // Always clear immediately to release 'this' reference
    interp_->release_environment(env_, true);
}

// Determine parameter binding semantics based on C++ rules
interpreter::parameter_semantics interpreter::get_parameter_semantics(
    const parameter& param,
    const script_value& arg) const
{
    // Reference parameters always share
    if (param.is_reference) {
        return parameter_semantics::reference;
    }

    // shared_ptr<T> parameter type means reference semantics
    if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
        return parameter_semantics::reference;
    }

    // shared_ptr<T> argument means preserve reference semantics
    if (arg.is_shared_ptr_type()) {
        return parameter_semantics::reference;
    }

    // Default: value semantics (clone)
    return parameter_semantics::value;
}

// Bind parameter with proper value/reference semantics
script_value interpreter::bind_parameter(
    const parameter& param,
    const script_value& arg) const
{
    auto semantics = get_parameter_semantics(param, arg);
    return (semantics == parameter_semantics::value) ? arg.clone() : arg;
}

// Function call implementation - returns checked_result for consistent error handling
checked_result<script_value> interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args) {
    // Check recursion depth limit FIRST
    if (current_call_depth_ >= JAI_MAX_CALL_DEPTH) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::max_recursion_depth),
            "Maximum recursion depth (" + std::to_string(JAI_MAX_CALL_DEPTH) + ") exceeded - possible infinite recursion"
        );
    }

    // RAII guard for call depth tracking - ensures decrement even on early return
    struct call_depth_guard {
        int& depth;
        call_depth_guard(int& d) : depth(d) { ++depth; }
        ~call_depth_guard() { --depth; }
    } depth_guard(current_call_depth_);

    // Validate argument count
    if (function.parameters.size() != args.size()) {
        return checked_result<script_value>(
            make_error_code(runtime_error_code::argument_count_mismatch),
            "Function expected " + std::to_string(function.parameters.size()) +
            " arguments but got " + std::to_string(args.size())
        );
    }

    // Create new environment for function execution using pool optimization
    // Both lambdas and functions need a fresh environment for their parameters
    auto previousEnv = environment_;

    // For lambdas with closures, the execution environment needs to chain:
    // [parameter env] -> [closure env] -> [global env]
    // For regular functions:
    // [parameter env] -> [current env]
    if (function.closure_env) {
        // Check if the closure is a method environment
        if (function.closure_env->is_method_env()) {
            auto this_obj = function.closure_env->get_this_object();
            // Create a new method environment that preserves implicit 'this' lookups
            environment_ = get_pooled_method_environment(function.closure_env->get_parent(), this_obj);
        } else if (function.closure_env->is_static_method_env()) {
            // Static method - create new static method environment that preserves static field access
            // Use the closure_env itself as parent to maintain access to class static fields
            environment_ = std::make_shared<environment>(
                function.closure_env->get_parent(),
                string_symbolizer_,
                function.closure_env->get_class_definition()
            );
        } else {
            // Regular closure - create new environment for parameters
            environment_ = get_pooled_environment(function.closure_env);
        }
    } else {
        // Regular function: create fresh environment with current as parent
        environment_ = get_pooled_environment(previousEnv);
    }

    // Store previous return state
    bool previousHasReturn = hasReturnValue_;
    std::optional<script_value> previousReturn = returnValue_;
    hasReturnValue_ = false;

    // Helper lambda to cleanup and restore state
    auto cleanup = [&](bool clear_this = true) {
        auto function_env = environment_;
        if (clear_this) {
            if (function_env->is_method_env()) {
                function_env->clear_this_reference();
            } else if (function_env->get_parent() && function_env->get_parent()->is_method_env()) {
                function_env->get_parent()->clear_this_reference();
            }
        }
        environment_ = previousEnv;
        release_environment(function_env, false);
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
    };

    // Bind parameters to arguments
    for (size_t i = 0; i < function.parameters.size(); ++i) {
        const auto& param = function.parameters[i];
        const auto& arg = args[i];

        // Use pre-cached symbol ID (parameter binding optimization)
        // Symbol IDs are cached at function definition time in visit_function_decl
        if (param.is_reference) {
            // For reference parameters, create a reference value
            if (!current_arg_metadata_.empty() && i < current_arg_metadata_.size()) {
                auto symbol_id = current_arg_metadata_[i].first;
                auto env = current_arg_metadata_[i].second;

                if (symbol_id != UINT64_MAX && env != nullptr) {
                    // Get pointer to the argument
                    script_value* argPtr = env->get_value_ptr(symbol_id);
                    if (!argPtr) {
                        cleanup();
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::undefined_variable),
                            "Cannot take reference of undefined variable"
                        );
                    }

                    // If the argument is itself a reference, get the final target
                    if (argPtr->is_reference()) {
                        auto refHolder = argPtr->get_reference_holder();
                        if (!refHolder || !refHolder->target) {
                            cleanup();
                            return checked_result<script_value>(
                                make_error_code(runtime_error_code::invalid_reference),
                                "Reference target is null"
                            );
                        }
                        // Create reference to the final target
                        script_value refValue = script_value::make_reference(refHolder->target, refHolder->sourceEnv.lock());
                        if (param.symbol_id != UINT64_MAX) {
                            environment_->define(param.symbol_id, std::move(refValue));
                        } else {
                            environment_->define(param.name, std::move(refValue));
                        }
                    } else {
                        // Create reference to the argument
                        script_value refValue = script_value::make_reference(argPtr, env);
                        if (param.symbol_id != UINT64_MAX) {
                            environment_->define(param.symbol_id, std::move(refValue));
                        } else {
                            environment_->define(param.name, std::move(refValue));
                        }
                    }
                } else {
                    // No metadata - can't create reference
                    cleanup();
                    return checked_result<script_value>(
                        make_error_code(runtime_error_code::invalid_reference),
                        "Cannot pass non-lvalue to reference parameter"
                    );
                }
            } else {
                // No metadata - can't create reference
                cleanup();
                return checked_result<script_value>(
                    make_error_code(runtime_error_code::invalid_reference),
                    "Cannot pass non-lvalue to reference parameter"
                );
            }
        } else {
            // Non-reference parameter - try to convert the argument to the parameter type if needed
            script_value converted_arg = make_value();
            JAISCRIPT_TRY_ASSIGN(converted_arg, try_convert_for_parameter(arg, param.type));

            // Decide between value semantics (clone) or reference semantics (share)
            // Use reference semantics (no clone) if:
            // 1. Parameter type is declared as shared_ptr<T>, OR
            // 2. Argument is already shared_ptr<T> (preserve reference semantics)
            // Otherwise use value semantics (clone for C++-like behavior)

            bool should_share = false;

            // Check if parameter type is shared_ptr<T>
            if (param.type && param.type->base_type == script_value_type::jai_shared_ptr_type) {
                should_share = true;
            }

            // Check if argument is shared_ptr<T> (preserve reference semantics)
            if (converted_arg.get_type_info() && converted_arg.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                should_share = true;
            }

            if (param.symbol_id != UINT64_MAX) {
                if (should_share) {
                    // Shallow copy - share ownership (reference semantics)
                    environment_->define(param.symbol_id, converted_arg);
                } else {
                    // Deep copy - value semantics (C++-like default)
                    auto cloned = converted_arg.clone();
                    environment_->define(param.symbol_id, std::move(cloned));
                }
            } else {
                // Fallback to parameter name if symbol_id not set
                if (should_share) {
                    environment_->define(param.name, converted_arg);
                } else {
                    environment_->define(param.name, converted_arg.clone());
                }
            }
        }
    }

    // Execute function body without creating another environment
    // (since we already created one for the function call)
    for (const auto& decl : function.body->declarations) {
        auto result = dispatch_decl(decl.get());

        // Check for error codes - propagate errors
        if (!result && result.error() != std::error_code()) {
            cleanup();
            std::string error_msg = result.message();
            if (error_msg.empty()) {
                error_msg = "Error in function body: " + result.error().message();
            }
            return checked_result<script_value>(result.error(), error_msg);
        }

        // Check if we hit a return statement and break early
        if (hasReturnValue_) {
            break;
        }
    }

    // Get return value
    script_value result = make_value();

    if (hasReturnValue_) {
        result = std::move(returnValue_.value());

        // Apply return type conversion if needed
        // Skip conversion for void, auto, or any type (implicit return type accepts any value)
        if (function.return_type && !function.return_type->type_name.empty() &&
            function.return_type->type_name != "void" &&
            function.return_type->type_name != "auto" &&
            function.return_type->base_type != script_value_type::jai_any_type) {
            JAISCRIPT_TRY_ASSIGN(result, try_convert_for_parameter(result, function.return_type));
        }
    } else {
        // Check if this is a constructor (method environment with no explicit return)
        // Constructors implicitly return 'this'
        // Note: The environment_ at this point is a pooled environment for parameters,
        // so we need to check the PARENT which could be the method environment
        auto function_env = environment_;
        if (function_env->is_method_env()) {
            result = function_env->get_this_object();
        } else if (function_env->get_parent()) {
            // Check parent - the closure_env passed to call_function might be a method environment
            if (function_env->get_parent()->is_method_env()) {
                result = function_env->get_parent()->get_this_object();
            } else {
                // Regular function with no return statement returns null
                result = make_value();
            }
        } else {
            // Regular function with no return statement returns null
            result = make_value();
        }
    }

    // Cleanup and return success
    cleanup();
    return result;
}

void interpreter::validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args) {
    if (params.size() != args.size()) {
        throw runtime_error("Function expected " + std::to_string(params.size()) +
                         " arguments but got " + std::to_string(args.size()));
    }

    // Type checking is now done in call_function via try_convert_for_parameter
}

bool interpreter::can_convert_to_type(const script_value& source, type_info_ptr target_type) const {
    if (!target_type) return true;  // No type specified = any type accepted

    auto source_type = source.type();
    auto target_base_type = target_type->base_type;

    // Any type accepts anything
    if (target_base_type == script_value_type::jai_any_type) return true;

    // Exact match - no conversion needed
    if (source_type == target_base_type) {
        // For objects, also check the type name
        if (source_type == script_value_type::jai_object_type) {
            // First, try to get the type name from type_info
            auto source_type_info = source.get_type_info();
            if (source_type_info && !source_type_info->type_name.empty() &&
                source_type_info->type_name == target_type->type_name) {
                return true;
            }
            // Also check the class_instance's class name (for script-defined classes)
            // Use get_class_instance() which safely returns nullptr if not a class instance
            auto instance = const_cast<script_value&>(source).get_class_instance();
            if (instance && instance->get_class_name() == target_type->type_name) {
                return true;
            }
            // Different object types - might still be convertible via constructor
        } else {
            return true;  // Non-object types match
        }
    }

    // shared_ptr<T> handling - storage is object_holder but type_info marks as shared_ptr
    auto source_type_info = source.get_type_info();
    if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T> -> shared_ptr<T>
        if (target_base_type == script_value_type::jai_shared_ptr_type) {
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return true;  // Inner types match
            }
            auto instance = const_cast<script_value&>(source).get_class_instance();
            if (instance && instance->get_class_name() == target_type->type_name) {
                return true;
            }
        }
        // shared_ptr<T> -> T
        if (target_base_type == script_value_type::jai_object_type) {
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return true;
            }
            auto instance = const_cast<script_value&>(source).get_class_instance();
            if (instance && instance->get_class_name() == target_type->type_name) {
                return true;
            }
        }
    }

    // For object target types, check if constructor conversion is available
    if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty()) {
        // Look up the class definition
        auto eng = engine_ref_.lock();
        if (eng) {
            auto class_def = eng->get_class_definition(target_type->type_name);
            if (class_def) {
                // Check if there's a constructor that takes 1 argument
                // For script classes, check constructor ASTs
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(class_def);
                if (script_class) {
                    const auto& ctor_asts = script_class->get_constructor_asts();
                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() == 1) {
                            // Has a single-argument constructor - conversion might be possible
                            // Further type checking could be done here if parameters have types
                            return true;
                        }
                    }
                }
                // For C++ classes, the constructor would be in the environment
                // Try looking it up
                auto ctor_result = environment_->get(target_type->type_name);
                if (ctor_result && ctor_result.value().is_function()) {
                    return true;  // Constructor exists
                }
            }
        }
    }

    return false;
}

checked_result<script_value> interpreter::try_convert_for_parameter(const script_value& arg, type_info_ptr target_type) {
    if (!target_type) {
        return arg;  // No type specified = any type accepted
    }

    // Dereference if the argument is a reference to get the actual value type
    const script_value& derefed_arg = arg.deref();
    // Get source type - prefer storage_type for accuracy
    auto source_type = derefed_arg.storage_type();
    // If still showing as reference after deref, get the actual target and check its type
    if (source_type == script_value_type::jai_reference_type) {
        // The deref'd value is still showing as reference - manually get the target
        auto ref_holder = derefed_arg.get_reference_holder();
        if (ref_holder && ref_holder->target) {
            source_type = ref_holder->target->storage_type();
        }
    }
    auto target_base_type = target_type->base_type;

    // Any type accepts anything - no conversion needed
    if (target_base_type == script_value_type::jai_any_type) return arg;

    // Exact match - no conversion needed
    if (source_type == target_base_type) {
        // For objects, also check the type name (with inheritance support)
        if (source_type == script_value_type::jai_object_type) {
            // First, try to get the type name from type_info
            auto source_type_info = derefed_arg.get_type_info();
            if (source_type_info && !source_type_info->type_name.empty() &&
                source_type_info->type_name == target_type->type_name) {
                return derefed_arg;  // Same object type - no conversion needed
            }
            // Also check the class_instance's class name (for script-defined classes)
            // Use get_class_instance() which safely returns nullptr if not a class instance
            auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
            if (instance) {
                if (instance->get_class_name() == target_type->type_name) {
                    return arg;  // Same object type - no conversion needed
                }
                // Check inheritance - derived types are compatible with base types
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type - compatible without conversion
                }
            }
            // Different object types - fall through to try constructor conversion
        } else {
            return arg;  // Non-object types match - no conversion needed
        }
    }

    // Null can be assigned to object types without conversion
    if (source_type == script_value_type::jai_null_type &&
        target_base_type == script_value_type::jai_object_type) {
        return arg;
    }

    // shared_ptr<T> handling
    auto source_type_info = derefed_arg.get_type_info();
    if (source_type_info && source_type_info->base_type == script_value_type::jai_shared_ptr_type) {
        // shared_ptr<T> -> shared_ptr<T> - same shared_ptr type
        if (target_base_type == script_value_type::jai_shared_ptr_type) {
            // Check if inner types match
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return arg;  // shared_ptr<T> -> shared_ptr<T> with matching inner type
            }
            // Also check via class instance (with inheritance support)
            auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
            if (instance) {
                if (instance->get_class_name() == target_type->type_name) {
                    return arg;
                }
                // Check inheritance
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type compatible with base
                }
            }
        }
        // shared_ptr<T> -> T - unwrap to regular object
        if (target_base_type == script_value_type::jai_object_type) {
            // Check if the inner type matches the target type (with inheritance)
            auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
            if (instance) {
                if (instance->get_class_name() == target_type->type_name) {
                    return arg;  // shared_ptr<T> -> T is allowed (preserves reference semantics)
                }
                // Check inheritance
                auto class_def = instance->get_class_definition();
                if (class_def && class_def->is_subtype_of(target_type->type_name)) {
                    return arg;  // Derived type compatible with base
                }
            }
            // Check type_info's inner type name
            if (!source_type_info->type_name.empty() && source_type_info->type_name == target_type->type_name) {
                return arg;
            }
        }
    }

    // For object target types, try constructor-based conversion
    if (target_base_type == script_value_type::jai_object_type && !target_type->type_name.empty()) {
        const std::string& target_class_name = target_type->type_name;

        // First, check if there's a constructor that directly accepts the source type
        // This prevents chained conversions (A->B->C)
        auto eng = engine_ref_.lock();
        if (eng) {
            auto class_def = eng->get_class_definition(target_class_name);
            if (class_def) {
                auto script_class = std::dynamic_pointer_cast<script_class_definition>(class_def);
                if (script_class) {
                    // Check constructor ASTs for one that accepts the source type
                    const auto& ctor_asts = script_class->get_constructor_asts();
                    bool has_matching_ctor = false;

                    // Get source type info (name and class_def for inheritance)
                    std::string source_type_name;
                    std::shared_ptr<class_definition> source_class_def;
                    if (source_type == script_value_type::jai_object_type) {
                        auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
                        if (instance) {
                            source_type_name = instance->get_class_name();
                            source_class_def = instance->get_class_definition();
                        }
                    }

                    for (const auto& ctor_ast : ctor_asts) {
                        if (ctor_ast->parameters.size() != 1) continue;

                        const auto& param = ctor_ast->parameters[0];
                        if (!param.type || param.type->type_name.empty()) {
                            // Untyped parameter - accepts anything
                            has_matching_ctor = true;
                            break;
                        }

                        // Check if parameter type matches source type (with inheritance)
                        if (source_type == script_value_type::jai_object_type) {
                            if (param.type->type_name == source_type_name) {
                                has_matching_ctor = true;
                                break;
                            }
                            // Check inheritance - derived types are accepted
                            if (source_class_def && source_class_def->is_subtype_of(param.type->type_name)) {
                                has_matching_ctor = true;
                                break;
                            }
                        } else {
                            // For primitives, check base_type match or numeric conversion
                            if (param.type->base_type == source_type ||
                                (source_type == script_value_type::jai_int_type &&
                                 param.type->base_type == script_value_type::jai_float_type) ||
                                (source_type == script_value_type::jai_float_type &&
                                 param.type->base_type == script_value_type::jai_int_type)) {
                                has_matching_ctor = true;
                                break;
                            }
                        }
                    }

                    if (!has_matching_ctor) {
                        // No constructor directly accepts the source type - don't allow chained conversion
                        return checked_result<script_value>(
                            make_error_code(runtime_error_code::type_mismatch),
                            "Cannot convert " + get_type_name(source_type) +
                            " to " + target_class_name +
                            " (no suitable single-argument constructor)");
                    }
                }
            }
        }

        // Try to find and call the constructor with the source value
        // Use global environment since constructors are always registered at global scope
        auto ctor_result = get_global_environment()->get(target_class_name);
        if (ctor_result && ctor_result.value().is_function()) {
            const script_function& ctor = ctor_result.value().as_function();

            // Call constructor with the source value as argument
            std::vector<script_value> ctor_args;
            ctor_args.push_back(arg);

            auto result = ctor(ctor_args);
            if (result.has_value()) {
                return std::move(result.value());
            }
            // Constructor returned invalid result - fall through to error
        }

        // No suitable constructor found
        return checked_result<script_value>(
            make_error_code(runtime_error_code::type_mismatch),
            "Cannot convert " + get_type_name(source_type) +
            " to " + target_class_name +
            " (no suitable single-argument constructor)");
    }

    // Built-in type conversions
    // Int <-> Float implicit conversions
    if (source_type == script_value_type::jai_int_type &&
        target_base_type == script_value_type::jai_float_type) {
        return script_value(static_cast<script_float>(derefed_arg.as_int()), engine_ref_);
    }
    if (source_type == script_value_type::jai_float_type &&
        target_base_type == script_value_type::jai_int_type) {
        return script_value(static_cast<script_int>(derefed_arg.as_float()), engine_ref_);
    }

    // Object -> primitive via to_X() methods
    if (source_type == script_value_type::jai_object_type) {
        std::string method_name;
        if (target_base_type == script_value_type::jai_int_type) {
            method_name = "to_int";
        } else if (target_base_type == script_value_type::jai_float_type) {
            method_name = "to_float";
        } else if (target_base_type == script_value_type::jai_string_type) {
            method_name = "to_string";
        } else if (target_base_type == script_value_type::jai_bool_type) {
            method_name = "to_bool";
        } else if (target_base_type == script_value_type::jai_char_type) {
            method_name = "to_char";
        }

        if (!method_name.empty()) {
            // Use get_class_instance() which safely returns nullptr if not a class instance
            auto instance = const_cast<script_value&>(derefed_arg).get_class_instance();
            if (instance) {
                auto method_id = string_symbolizer_->intern(method_name);
                auto method_val = instance->get_method(method_id, false);
                if (!method_val.is_null() && !method_val.is_invalid() && method_val.is_function()) {
                    // Create a bound method with the object as 'this'
                    script_value bound = create_bound_method(arg, method_val);
                    const script_function& method = bound.as_function();
                    std::vector<script_value> no_args;
                    auto result = method(no_args);
                    if (result.has_value()) {
                        return std::move(result.value());
                    }
                }
            }
        }
    }

    // Type mismatch - return error
    return checked_result<script_value>(
        make_error_code(runtime_error_code::type_mismatch),
        "Type mismatch: expected " + target_type->type_name +
        " but got " + get_type_name(source_type));
}

script_value interpreter::make_function(std::shared_ptr<script_defined_function> func) {
    // Create a wrapper that handles reference parameters properly
    script_function wrapper = [this, func](const std::vector<script_value>& args) -> checked_result<script_value> {
        // For functions with reference parameters, we need special handling
        bool hasRefParams = false;
        for (const auto& param : func->parameters) {
            if (param.is_reference) {
                hasRefParams = true;
                break;
            }
        }

        if (!hasRefParams) {
            // No reference parameters - use normal call
            return call_function(*func, args);
        }

        // Has reference parameters - we need to handle them specially
        // For now, just call normally - we'll implement proper reference handling later
        return call_function(*func, args);
    };
    return script_value::make_function(wrapper, engine_ref_);
}

// Function call optimization helpers
std::shared_ptr<environment> interpreter::get_pooled_environment(std::shared_ptr<environment> parent) {
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        env->reset(parent);
        return env;
    } else {
        // Pool is exhausted, create new environment and add to pool
        auto newEnv = std::make_shared<environment>(parent, string_symbolizer_);
        environment_pool_.emplace_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

// Release an environment back to the pool
// For block scopes: clears values immediately (safe because blocks don't return values)
// For function scopes: just returns to pool, will be cleared on next reuse
void interpreter::release_environment(std::shared_ptr<environment> env, bool clear_now) {
    if (!env) return;

    // Clear environment if requested (safe for blocks, but not for functions with return values)
    if (clear_now) {
        env->reset(nullptr);
    }

    // All environments use the unified pool now
    if (environment_pool_index_ > 0) {
        --environment_pool_index_;
    }
}

std::shared_ptr<environment> interpreter::get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj) {
    // Use unified pool - get an environment and reset it as a method environment
    if (environment_pool_index_ < environment_pool_.size()) {
        // Reuse existing environment from pool
        auto env = environment_pool_[environment_pool_index_++];
        // Reset as method environment with this object
        env->reset_as_method(parent, std::move(this_obj));
        return env;
    } else {
        // Pool is exhausted, create new method environment and add to pool
        auto newEnv = std::make_shared<environment>(parent, string_symbolizer_, std::move(this_obj));
        environment_pool_.emplace_back(newEnv);
        ++environment_pool_index_;
        return newEnv;
    }
}

void interpreter::reset_environment_pool() {
    environment_pool_index_ = 0;

    // Clear all environments in the unified pool to release references
    for (auto& env : environment_pool_) {
        // Reset the environment by clearing its parent, values, and kind-specific fields
        env->reset(nullptr);
    }
}

// ============================================================
// SWITCH-BASED DISPATCH (faster than virtual calls)
// These functions use a switch on node_type enum instead of
// virtual method dispatch, eliminating vtable lookup overhead.
// ============================================================

checked_result<void> interpreter::dispatch_expr(expression* expr) {
    switch (expr->get_type()) {
        case node_type::literal_expr:
            return visit_literal_expr(static_cast<literal_expr*>(expr));
        case node_type::identifier_expr:
            return visit_identifier_expr(static_cast<identifier_expr*>(expr));
        case node_type::binary_expr:
            return visit_binary_expr(static_cast<binary_expr*>(expr));
        case node_type::unary_expr:
            return visit_unary_expr(static_cast<unary_expr*>(expr));
        case node_type::assignment_expr:
            return visit_assignment_expr(static_cast<assignment_expr*>(expr));
        case node_type::call_expr:
            return visit_call_expr(static_cast<call_expr*>(expr));
        case node_type::member_expr:
            return visit_member_expr(static_cast<member_expr*>(expr));
        case node_type::lambda_expr:
            return visit_lambda_expr(static_cast<lambda_expr*>(expr));
        case node_type::new_expr:
            return visit_new_expr(static_cast<new_expr*>(expr));
        case node_type::ternary_expr:
            return visit_ternary_expr(static_cast<ternary_expr*>(expr));
        case node_type::array_literal_expr:
            return visit_array_literal_expr(static_cast<array_literal_expr*>(expr));
        case node_type::map_literal_expr:
            return visit_map_literal_expr(static_cast<map_literal_expr*>(expr));
        case node_type::this_expr:
            return visit_this_expr(static_cast<this_expr*>(expr));
        case node_type::super_expr:
            return visit_super_expr(static_cast<super_expr*>(expr));
        case node_type::throw_expr:
            return visit_throw_expr(static_cast<throw_expr*>(expr));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown expression type in dispatch_expr");
    }
}

checked_result<void> interpreter::dispatch_stmt(statement* stmt) {
    switch (stmt->get_type()) {
        case node_type::expression_stmt:
            return visit_expression_stmt(static_cast<expression_stmt*>(stmt));
        case node_type::block_stmt:
            return visit_block_stmt(static_cast<block_stmt*>(stmt));
        case node_type::if_stmt:
            return visit_if_stmt(static_cast<if_stmt*>(stmt));
        case node_type::while_stmt:
            return visit_while_stmt(static_cast<while_stmt*>(stmt));
        case node_type::for_stmt:
            return visit_for_stmt(static_cast<for_stmt*>(stmt));
        case node_type::range_for_stmt:
            return visit_range_for_stmt(static_cast<range_for_stmt*>(stmt));
        case node_type::return_stmt:
            return visit_return_stmt(static_cast<return_stmt*>(stmt));
        case node_type::break_stmt:
            return visit_break_stmt(static_cast<break_stmt*>(stmt));
        case node_type::continue_stmt:
            return visit_continue_stmt(static_cast<continue_stmt*>(stmt));
        case node_type::try_stmt:
            return visit_try_stmt(static_cast<try_stmt*>(stmt));
        case node_type::switch_stmt:
            return visit_switch_stmt(static_cast<switch_stmt*>(stmt));
        case node_type::case_stmt:
            return visit_case_stmt(static_cast<case_stmt*>(stmt));
        case node_type::default_stmt:
            return visit_default_stmt(static_cast<default_stmt*>(stmt));
        case node_type::fallthrough_stmt:
            return visit_fallthrough_stmt(static_cast<fallthrough_stmt*>(stmt));
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown statement type in dispatch_stmt");
    }
}

checked_result<void> interpreter::dispatch_decl(declaration* decl) {
    switch (decl->get_type()) {
        case node_type::variable_decl:
            return visit_variable_decl(static_cast<variable_decl*>(decl));
        case node_type::function_decl:
            return visit_function_decl(static_cast<function_decl*>(decl));
        case node_type::class_decl:
            return visit_class_decl(static_cast<class_decl*>(decl));
        case node_type::namespace_decl:
            return visit_namespace_decl(static_cast<namespace_decl*>(decl));
        case node_type::expression_decl:
            return visit_expression_decl(static_cast<expression_decl*>(decl));
        case node_type::include_decl:
            return visit_include_decl(static_cast<include_decl*>(decl));
        case node_type::import_decl:
            return visit_import_decl(static_cast<import_decl*>(decl));
        case node_type::statement_decl:
            return dispatch_stmt(static_cast<statement_decl*>(decl)->statement.get());
        default:
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                "Unknown declaration type in dispatch_decl");
    }
}

} // namespace jai
