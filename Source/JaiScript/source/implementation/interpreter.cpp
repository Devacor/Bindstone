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
            return script_value::make_null(interp->get_engine_ref());
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
        self = script_value::make_null(interp->get_engine_ref());
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
    auto it = values_.find(id);
    if (it == values_.end()) {
        // New variable - track declaration order
        declaration_order_.push_back(id);
    }
    values_[id] = value;
}

void environment::define(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it == values_.end()) {
        // New variable - track declaration order
        declaration_order_.push_back(id);
    }
    values_[id] = std::move(value);
}

void environment::define(uint64_t id, const script_value& value) {
    auto it = values_.find(id);
    if (it == values_.end()) {
        // New variable - track declaration order
        declaration_order_.push_back(id);
    }
    values_[id] = value;
}

void environment::define(uint64_t id, script_value&& value) {
    auto it = values_.find(id);
    if (it == values_.end()) {
        // New variable - track declaration order
        declaration_order_.push_back(id);
    }
    values_[id] = std::move(value);
}

checked_result<script_value> environment::get(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }

    if (parent_) {
        return parent_->get(name);
    }

    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<script_value> environment::get(uint64_t id) const {
    return get(id, 0);
}

checked_result<script_value> environment::get(uint64_t id, int depth) const {
    // Prevent infinite recursion in environment chains
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        std::string name{symbolizer_->get_string(id)};
        return checked_result<script_value>(make_error_code(runtime_error_code::max_recursion_depth),
            "Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }



    auto it = values_.find(id);
    if (it != values_.end()) {
        return it->second;
    }

    if (parent_) {
        return parent_->get(id, depth + 1);
    }

    // Need to get the name for error message
    std::string name{symbolizer_->get_string(id)};
    return checked_result<script_value>(make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, const script_value& value) {
    uint64_t id = symbolizer_->intern(name);
    // Use numeric ID for recursion to avoid re-interning at each parent level
    assign(id, value);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    // Use numeric ID for recursion to avoid re-interning at each parent level
    return get_ref(id);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(uint64_t id) const {
    std::string name{symbolizer_->get_string(id)};
    if (name == "getValue") {
        std::cerr << "  this type: " << typeid(*this).name() << "\n";
    }
    return get_ref(id, 0);
}

checked_result<std::reference_wrapper<const script_value>> environment::get_ref(uint64_t id, int depth) const {
    // This version with depth is only called internally for recursion tracking
    // The public version delegates here
    const int MAX_RECURSION_DEPTH = 100;
    if (depth > MAX_RECURSION_DEPTH) {
        std::string name{symbolizer_->get_string(id)};
        return checked_result<std::reference_wrapper<const script_value>>(
            make_error_code(runtime_error_code::max_recursion_depth),
            "Maximum environment recursion depth exceeded for variable '" + name + "' at depth " + std::to_string(depth));
    }

    auto it = values_.find(id);
    if (it != values_.end()) {
        return std::cref(it->second);
    }

    if (parent_) {
        // For proper virtual dispatch, we need to call the public virtual method
        // on the parent, not this internal version
        std::string name{symbolizer_->get_string(id)};
        if (name == "getValue") {
            std::cerr << "  parent type: " << typeid(*parent_).name() << "\n";
        }
        // Cast to const to call the const overload
        const environment* const_parent = parent_.get();
        return const_parent->get_ref(id);
    }

    // Need to get the name for error message
    std::string name{symbolizer_->get_string(id)};
    return checked_result<std::reference_wrapper<const script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    // Use numeric ID for recursion to avoid re-interning at each parent level
    return get_ref(id);
}

checked_result<std::reference_wrapper<script_value>> environment::get_ref(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return std::ref(it->second);
    }

    if (parent_) {
        return parent_->get_ref(id);
    }

    std::string name{symbolizer_->get_string(id)};
    return checked_result<std::reference_wrapper<script_value>>(
        make_error_code(runtime_error_code::undefined_variable),
        "Undefined variable '" + name + "'");
}

void environment::assign(const std::string& name, script_value&& value) {
    uint64_t id = symbolizer_->intern(name);
    // Use numeric ID for recursion to avoid re-interning at each parent level
    assign(id, std::move(value));
}

void environment::assign(uint64_t id, const script_value& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    if (parent_) {
        parent_->assign(id, value);
        return;
    }
    
    std::string name{symbolizer_->get_string(id)};
    throw runtime_error("Undefined variable '" + name + "'");
}

void environment::assign(uint64_t id, script_value&& value) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }
    
    if (parent_) {
        parent_->assign(id, std::move(value));
        return;
    }
    
    std::string name{symbolizer_->get_string(id)};
    throw runtime_error("Undefined variable '" + name + "'");
}

bool environment::contains(const std::string& name) const {
    uint64_t id = symbolizer_->intern(name);
    // Use numeric ID for recursion to avoid re-interning at each parent level
    return contains(id);
}

bool environment::contains(uint64_t id) const {
    if (values_.find(id) != values_.end()) {
        return true;
    }
    return parent_ ? parent_->contains(id) : false;
}

std::unordered_map<std::string_view, script_value> environment::get_local_variables() const {
    std::unordered_map<std::string_view, script_value> result;
    for (const auto& [id, value] : values_) {
        result[symbolizer_->get_string(id)] = value;
    }
    return result;
}

void environment::clear_values() {
    // Destroy variables in reverse declaration order (LIFO) for proper destructor ordering
    // This clears all local variables but keeps the parent chain intact
    for (auto it = declaration_order_.rbegin(); it != declaration_order_.rend(); ++it) {
        values_.erase(*it);  // This destroys the script_value, firing destructors
    }

    // Clear the tracking vector
    declaration_order_.clear();
}

void environment::reset(std::shared_ptr<environment> new_parent) {
    // Clear all values first
    clear_values();

    // Validate the parent chain before setting (debug mode only)
    validate_parent_chain(new_parent);

    parent_ = new_parent;
}

std::unordered_map<std::string_view, script_value> environment::get_all_variables() const {
    std::unordered_map<std::string_view, script_value> allVars;

    // Start with parent's variables (if any)
    if (parent_) {
        allVars = parent_->get_all_variables();
    }

    // Add/override with local variables
    for (const auto& [id, value] : values_) {
        allVars[symbolizer_->get_string(id)] = value;
    }

    return allVars;
}

script_value* environment::get_value_ptr(uint64_t id) {
    auto it = values_.find(id);
    if (it != values_.end()) {
        return &it->second;
    }
    
    if (parent_) {
        return parent_->get_value_ptr(id);
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

// method_environment implementation
checked_result<script_value> method_environment::get(const std::string& name) const {
    // DEBUG: Log method environment lookup
    // std::cerr << "DEBUG: method_environment::get(\"" << name << "\")\n";

    // Special handling for 'this'
    if (name == "this") {
        return this_object_;
    }

    // First try normal environment lookup
    auto env_result = environment::get(name);
    if (env_result) {
        return env_result;
    }

    // If not found in environment, check 'this' object fields and methods
    // During class definition, we shouldn't resolve methods - let the unresolved identifier handling take over
    // The this_object_ might be invalid during class parsing
    auto this_type = this_object_.type();
    if (name != "this" && (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) && !this_object_.is_null()) {
        auto obj_holder = this_object_.get_object_holder();
        if (obj_holder && obj_holder->data) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
                
                // Try to get field first (non-throwing) - now returns a reference
                const script_value& field_ref = instance->get_field(name, false);
                if (!field_ref.is_invalid()) {
                    return field_ref;
                }
                
            // Try to get method (non-throwing)
            script_value method = instance->get_method(name, false);
            if (!method.is_invalid()) {
                // Found the method! Store in instance member
                bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                return bound_method_storage_;
            }

            // Try to get static field from class definition
            auto class_def = instance->get_class_definition();
            if (class_def && class_def->has_static_field(name)) {
                return class_def->get_static_field(name);
            }
        }
    }

    // Nothing found - return the original error from environment lookup
    return env_result;
}

checked_result<script_value> method_environment::get(uint64_t id) const {
    // Special handling for 'this' using cached ID
    if (id == symbolizer_->get_this_id()) {
        return this_object_;
    }

    // First try normal environment lookup
    auto env_result = environment::get(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check 'this' object fields and methods
    std::string name{symbolizer_->get_string(id)};
    auto this_type = this_object_.type();
    if (name != "this" && (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type) && !this_object_.is_null()) {
        auto obj_holder = this_object_.get_object_holder();
        if (obj_holder && obj_holder->data) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

            // Try to get field first (non-throwing) - now returns a reference
            const script_value& field_ref = instance->get_field(name, false);
            if (!field_ref.is_invalid()) {
                return field_ref;
            }

            // Try to get method (non-throwing)
            script_value method = instance->get_method(name, false);
            if (!method.is_invalid()) {
                // Found the method! Store in instance member
                bound_method_storage_ = interpreter::create_bound_method(this_object_, method);
                return bound_method_storage_;
            }

            // Try to get static field from class definition
            auto class_def = instance->get_class_definition();
            if (class_def && class_def->has_static_field(name)) {
                return class_def->get_static_field(name);
            }
        }
    }

    // Nothing found - return the original error from environment lookup
    return env_result;
}

checked_result<std::reference_wrapper<const script_value>> method_environment::get_ref(const std::string& name) const {
    // For 'this', we can't return a reference to this_object_ because it might be temporary
    // Just delegate to base class which will throw
    uint64_t id = symbolizer_->intern(name);
    return environment::get_ref(id);
}

checked_result<std::reference_wrapper<const script_value>> method_environment::get_ref(uint64_t id) const {
    // Delegate to base class - method_environment doesn't support get_ref for 'this' object fields
    return environment::get_ref(id);
}

checked_result<std::reference_wrapper<script_value>> method_environment::get_ref(const std::string& name) {
    uint64_t id = symbolizer_->intern(name);
    return environment::get_ref(id);
}

checked_result<std::reference_wrapper<script_value>> method_environment::get_ref(uint64_t id) {
    // Delegate to base class - method_environment doesn't support get_ref for 'this' object fields
    return environment::get_ref(id);
}

void method_environment::assign(const std::string& name, const script_value& value) {
    // First check if it's a local variable or parameter
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }
    
    // Check parent environments
    if (parent_ && parent_->contains(name)) {
        parent_->assign(name, value);
        return;
    }
    
    // If not found anywhere and 'this' has this field, update it
    auto this_type = this_object_.type();
    if (name != "this" && (this_type == script_value_type::jai_object_type || this_type == script_value_type::jai_shared_ptr_type)) {
        auto obj_holder = this_object_.get_object_holder();
        if (obj_holder && obj_holder->data) {
            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);
            if (instance && instance->has_field(name)) {
                instance->set_field(name, value.clone());
                return;
            }
        }
    }
    
    // Not found anywhere - define it locally (this matches normal environment behavior)
    define(name, value);
}

void method_environment::assign(uint64_t id, const script_value& value) {
    // Convert to string name and use the string version
    std::string name{symbolizer_->get_string(id)};
    assign(name, value);
}

bool method_environment::contains(const std::string& name) const {
    // Check if it's 'this'
    if (name == "this") {
        return true;
    }
    // Otherwise use base environment::contains
    return environment::contains(name);
}

bool method_environment::contains(uint64_t id) const {
    // Check if it's 'this' using cached ID
    if (id == symbolizer_->get_this_id()) {
        return true;
    }
    // Otherwise use base environment::contains
    return environment::contains(id);
}

// static_method_environment implementation
checked_result<script_value> static_method_environment::get(const std::string& name) const {
    // First try normal environment lookup
    auto env_result = environment::get(name);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields
    if (class_def_ && class_def_->has_static_field(name)) {
        return class_def_->get_static_field(name);
    }

    // Also check static methods (for calling other static methods)
    if (class_def_ && class_def_->has_static_method(name)) {
        return class_def_->get_static_method(name);
    }

    // Nothing found - return the original error from environment lookup
    return env_result;
}

checked_result<script_value> static_method_environment::get(uint64_t id) const {
    // First try normal environment lookup
    auto env_result = environment::get(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields
    std::string name{symbolizer_->get_string(id)};

    if (class_def_ && class_def_->has_static_field(name)) {
        return class_def_->get_static_field(name);
    }

    // Also check static methods (for calling other static methods)
    if (class_def_ && class_def_->has_static_method(name)) {
        return class_def_->get_static_method(name);
    }

    // Nothing found - return the original error from environment lookup
    return env_result;
}

checked_result<std::reference_wrapper<const script_value>> static_method_environment::get_ref(const std::string& name) const {
    // First try normal environment lookup
    uint64_t id = symbolizer_->intern(name);
    auto env_result = environment::get_ref(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields (return reference to the field in class_def)
    if (class_def_ && class_def_->has_static_field(name)) {
        const script_value* field_ptr = class_def_->get_static_field_ptr(name);
        if (field_ptr) {
            return std::cref(*field_ptr);
        }
    }

    // Nothing found - return the original error
    return env_result;
}

checked_result<std::reference_wrapper<const script_value>> static_method_environment::get_ref(uint64_t id) const {
    // First try normal environment lookup
    auto env_result = environment::get_ref(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields
    std::string name{symbolizer_->get_string(id)};
    if (class_def_ && class_def_->has_static_field(name)) {
        const script_value* field_ptr = class_def_->get_static_field_ptr(name);
        if (field_ptr) {
            return std::cref(*field_ptr);
        }
    }

    // Nothing found - return the original error
    return env_result;
}

checked_result<std::reference_wrapper<script_value>> static_method_environment::get_ref(const std::string& name) {
    // First try normal environment lookup
    uint64_t id = symbolizer_->intern(name);
    auto env_result = environment::get_ref(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields (return non-const reference)
    if (class_def_ && class_def_->has_static_field(name)) {
        script_value* field_ptr = class_def_->get_static_field_ptr(name);
        if (field_ptr) {
            return std::ref(*field_ptr);
        }
    }

    // Nothing found - return the original error
    return env_result;
}

checked_result<std::reference_wrapper<script_value>> static_method_environment::get_ref(uint64_t id) {
    // First try normal environment lookup
    auto env_result = environment::get_ref(id);
    if (env_result) {
        return env_result;
    }

    // If not found, check static fields (return non-const reference)
    std::string name{symbolizer_->get_string(id)};
    if (class_def_ && class_def_->has_static_field(name)) {
        script_value* field_ptr = class_def_->get_static_field_ptr(name);
        if (field_ptr) {
            return std::ref(*field_ptr);
        }
    }

    // Nothing found - return the original error
    return env_result;
}

void static_method_environment::assign(const std::string& name, const script_value& value) {
    // First check if it's a local variable or parameter
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = value;
        return;
    }

    // Check parent environments
    if (parent_ && parent_->contains(name)) {
        parent_->assign(name, value);
        return;
    }

    // Check if it's a static field (set_static_field does the lookup)
    if (class_def_ && class_def_->set_static_field(name, value)) {
        return;
    }

    // Not found anywhere - throw error
    throw runtime_error("Undefined variable '" + name + "'");
}

void static_method_environment::assign(const std::string& name, script_value&& value) {
    // First check if it's a local variable or parameter
    uint64_t id = symbolizer_->intern(name);
    auto it = values_.find(id);
    if (it != values_.end()) {
        it->second = std::move(value);
        return;
    }

    // Check parent environments
    if (parent_ && parent_->contains(name)) {
        parent_->assign(name, std::move(value));
        return;
    }

    // Check if it's a static field (set_static_field does the lookup)
    if (class_def_ && class_def_->set_static_field(name, value)) {
        return;
    }

    // Not found anywhere - throw error
    throw runtime_error("Undefined variable '" + name + "'");
}

void static_method_environment::assign(uint64_t id, const script_value& value) {
    std::string name{symbolizer_->get_string(id)};
    assign(name, value);
}

void static_method_environment::assign(uint64_t id, script_value&& value) {
    std::string name{symbolizer_->get_string(id)};
    assign(name, std::move(value));
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

void interpreter::prepare_for_execution() {
    // Clear execution state
    valueStack_.clear();
    returnValue_ = make_value();
    hasReturnValue_ = false;

    // Clear exception state
    current_exception_.reset();
    is_unwinding_ = false;
    active_exception_value_ = make_value();
    current_catch_var_.clear();

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
    script_value last_script_value = script_value::make_null(engine_ref_);
    hasReturnValue_ = false;  // Reset return value state

    for (size_t i = 0; i < declarations.size(); i++) {
        const auto& decl = declarations[i];
        // std::cerr << "  Declaration " << i << " type: " << typeid(*decl).name() << "\n";

        // Execute declaration with exception handling
        try {
            // std::cerr << "  About to visit declaration " << i << "\n";
            auto result = decl->accept(this);
            if (!result) [[unlikely]] {
                // Convert error code to exception at boundary
                throw std::system_error(result.error());
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
        if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
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
    auto result = expr->accept(this);
    if (!result) {
        // Return null on error
        return script_value::make_null(engine_ref_);
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
    if (expr->name == "getValue") {
    }
    // Check if this identifier is the current catch variable
    if (!current_catch_var_.empty() && expr->name == current_catch_var_) {
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
    // Try to get the variable from environment
    auto ref_result = environment_->get_ref(expr->symbol_id);
    if (ref_result) {
        const script_value& val = ref_result.value().get();
        push_value(val.deref());  // Automatically handles references
    } else {
        // If we're in a class method context, collect unresolved identifier
        if (current_class_context_ && current_class_context_->in_method) {
            // Add to unresolved identifiers for later validation
            current_class_context_->unresolved_identifiers.insert(expr->name);
            // Push a placeholder value to continue parsing
            push_value(make_value());
            return checked_result<void>();
        }


        // Variable not found - check if it's a member of 'this'
        auto this_result = environment_->get("this");
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
                    // Check instance fields first
                    if (instance->has_field(expr->name)) {
                        push_value(instance->get_field(expr->name));
                        return checked_result<void>();
                    }

                    // Check for methods (returns bound method)
                    script_value method = instance->get_method(expr->name, false);
                    if (!method.is_invalid()) {
                        script_value bound_method = create_bound_method(this_val, method);
                        push_value(std::move(bound_method));
                        return checked_result<void>();
                    }

                    // Check for static fields of the class
                    auto class_def = instance->get_class_definition();
                    if (class_def && class_def->has_static_field(expr->name)) {
                        push_value(class_def->get_static_field(expr->name));
                        return checked_result<void>();
                    }
                }
            }
        }

        // Use error code instead of exception state
        return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable  // [ErrorText] Undefined variable
    }
    return checked_result<void>();
}

checked_result<void> interpreter::visit_binary_expr(binary_expr* expr) {
    // ULTRA-FAST PATH: Literal expressions like "2 + 3" - avoid all AST traversal
    if (auto* leftLit = dynamic_cast<literal_expr*>(expr->left.get())) {
        if (auto* rightLit = dynamic_cast<literal_expr*>(expr->right.get())) {
            const script_value& leftVal = leftLit->value;
            const script_value& rightVal = rightLit->value;
            
            // Fast path for integer arithmetic (most common case) - but only if no custom ops
            if (leftVal.is_int() && rightVal.is_int() && can_use_fast_path(expr->op.type)) {
                script_int leftInt = leftVal.as_int();
                script_int rightInt = rightVal.as_int();
                
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
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));  // [ErrorText] Division by zero
                        }
                        push_value(make_value(leftInt / rightInt));
                        return {};
                    case token_type::percent:
                        if (rightInt == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));  // [ErrorText] Modulo by zero
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
                    case token_type::spaceship:
                        push_value(make_value(leftInt < rightInt ? script_int(-1) : (leftInt > rightInt ? script_int(1) : script_int(0))));
                        return {};
                    default:
                        break; // Fall through to normal path
                }
            }
            // Fast path for float arithmetic - but only if no custom ops
            else if ((leftVal.is_float() || leftVal.is_int()) && (rightVal.is_float() || rightVal.is_int()) && can_use_fast_path(expr->op.type)) {
                script_float leftFloat = leftVal.is_int() ? static_cast<script_float>(leftVal.as_int()) : leftVal.as_float();
                script_float rightFloat = rightVal.is_int() ? static_cast<script_float>(rightVal.as_int()) : rightVal.as_float();
                
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
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));  // [ErrorText] Division by zero
                        }
                        push_value(make_value(leftFloat / rightFloat));
                        return {};
                    case token_type::percent:
                        if (rightFloat == 0.0) {
                            return checked_result<void>(make_error_code(runtime_error_code::modulo_by_zero));  // [ErrorText] Modulo by zero
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
                    case token_type::spaceship:
                        push_value(make_value(leftFloat < rightFloat ? script_int(-1) : (leftFloat > rightFloat ? script_int(1) : script_int(0))));
                        return {};
                    default:
                        break;
                }
            }
            // Fast path for string concatenation
            else if (expr->op.type == token_type::plus && leftVal.is_string() && rightVal.is_string()) {
                push_value(make_value(leftVal.as_string() + rightVal.as_string()));
                return {};
            }
        }
    }

    // Handle logical operators specially for short-circuit evaluation
    if (expr->op.type == token_type::ampersand_ampersand || expr->op.type == token_type::pipe_pipe) {
        JAISCRIPT_TRY(expr->left->accept(this));
        script_value left = pop_value();

        bool leftTruthy = is_truthy(left);

        if (expr->op.type == token_type::ampersand_ampersand) {
            if (!leftTruthy) {
                push_value(left);  // Short-circuit: return left (falsy)
                return {};
            }
        } else { // pipe_pipe
            if (leftTruthy) {
                push_value(left);  // Short-circuit: return left (truthy)
                return {};
            }
        }

        // Evaluate right side
        JAISCRIPT_TRY(expr->right->accept(this));
        // Result is already on stack
        return {};
    }

    // Evaluate operands once and use them throughout
    JAISCRIPT_TRY(expr->left->accept(this));
    script_value left_raw = pop_value();  // Keep raw value for subscript handling
    script_value left = left_raw.deref();  // Dereferenced version for most operations

    JAISCRIPT_TRY(expr->right->accept(this));
    // Check if we're unwinding due to an exception in the right expression
    if (is_unwinding_) {
        // Don't try to pop a value that wasn't pushed due to the exception
        return {};
    }
    script_value right = pop_value().deref();  // Handle references safely
    
    // Check for custom operator functions first
    std::string opName;
    switch (expr->op.type) {
        case token_type::plus: opName = "+"; break;
        case token_type::minus: opName = "-"; break;
        case token_type::star: opName = "*"; break;
        case token_type::slash: opName = "/"; break;
        case token_type::percent: opName = "%"; break;
        case token_type::less: opName = "<"; break;
        case token_type::less_equal: opName = "<="; break;
        case token_type::greater: opName = ">"; break;
        case token_type::greater_equal: opName = ">="; break;
        case token_type::equal_equal: opName = "=="; break;
        case token_type::bang_equal: opName = "!="; break;
        case token_type::spaceship: opName = "<=>"; break;
        case token_type::ampersand: opName = "&"; break;
        case token_type::pipe: opName = "|"; break;
        case token_type::caret: opName = "^"; break;
        case token_type::left_shift: opName = "<<"; break;
        case token_type::right_shift: opName = ">>"; break;
        default: break;
    }
    
    // Check for custom operator function (excluding subscript)
    if (!opName.empty() && environment_ && environment_->contains(opName)) {
        auto op_result = environment_->get(opName);
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
            bool is_lvalue = dynamic_cast<identifier_expr*>(expr->left.get()) != nullptr ||
                            dynamic_cast<member_expr*>(expr->left.get()) != nullptr ||
                            (dynamic_cast<binary_expr*>(expr->left.get()) != nullptr &&
                             dynamic_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

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
                bool is_lvalue = dynamic_cast<identifier_expr*>(expr->left.get()) != nullptr ||
                                dynamic_cast<member_expr*>(expr->left.get()) != nullptr ||
                                (dynamic_cast<binary_expr*>(expr->left.get()) != nullptr &&
                                 dynamic_cast<binary_expr*>(expr->left.get())->op.type == token_type::left_bracket);

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
                    script_value method = instance->get_method("[]", false);
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
    if (auto* literal = dynamic_cast<literal_expr*>(expr->operand.get())) {
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
    JAISCRIPT_TRY(expr->operand->accept(this));
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
            // Handle increment/decrement
            if (auto* identifier = dynamic_cast<identifier_expr*>(expr->operand.get())) {
                // Cache symbol ID if not already cached
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }
                auto val_result = environment_->get(identifier->symbol_id);
                if (!val_result) {
                    return checked_result<void>(val_result.error(), val_result.message());
                }
                script_value currentValue = std::move(val_result.value());
                script_value newValue = script_value::make_null(engine_ref_);

                // Use single type() call + switch for faster type checking
                switch (currentValue.type()) {
                    case script_value_type::jai_int_type: {
                        int64_t val = currentValue.as_int();
                        newValue = make_value(expr->op.type == token_type::plus_plus ? val + 1 : val - 1);
                        break;
                    }
                    case script_value_type::jai_float_type: {
                        double val = currentValue.as_float();
                        newValue = make_value(expr->op.type == token_type::plus_plus ? val + 1.0 : val - 1.0);
                        break;
                    }
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::invalid_numeric_operand));  // [ErrorText] Cannot increment/decrement non-numeric value
                }

                // Check if this is a reference variable
                script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
                if (varPtr && varPtr->is_reference()) {
                    // This is a reference - update the target
                    varPtr->deref() = newValue.deref();
                } else {
                    // Regular variable assignment
                    environment_->assign(identifier->symbol_id, newValue);
                }

                // For prefix, return the new value; for postfix, return the old value
                if (expr->is_postfix) {
                    push_value(std::move(currentValue));
                } else {
                    push_value(std::move(newValue));
                }
            } else {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_assignment_target));  // [ErrorText] Increment/decrement requires a variable
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
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
            // Cache symbol ID if not already cached
            if (identifier->symbol_id == UINT64_MAX) {
                identifier->symbol_id = string_symbolizer_->intern(identifier->name);
            }
            auto ref_result = environment_->get_ref(identifier->symbol_id);
            if (!ref_result) {
                throw runtime_error(ref_result.message());
            }
            script_value currentValue = ref_result.value().get();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(expr->value->accept(this));
            script_value rightValue = pop_value();
            
            // Perform the compound operation - try custom operators first, then built-in types
            script_value resultValue = script_value::make_null(engine_ref_);
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    // Only check for custom operators if we know they exist (fast path optimization)
                    if (has_custom_numeric_ops_ && environment_ && environment_->contains("+")) {
                        auto op_result = environment_->get("+");
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
                            customOpFound = true;
                        }
                    }

                    // Fall back to built-in operators
                    if (!customOpFound) {
                        // Use single type() call + switch for faster type checking
                        // Dereference if needed to get actual value type
                        auto& derefCurrent = currentValue.deref();
                        auto& derefRight = rightValue.deref();
                        auto leftType = derefCurrent.type();
                        auto rightType = derefRight.type();

                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            resultValue = make_value(derefCurrent.as_int() + derefRight.as_int());
                        } else if ((leftType == script_value_type::jai_int_type || leftType == script_value_type::jai_float_type) &&
                                   (rightType == script_value_type::jai_int_type || rightType == script_value_type::jai_float_type)) {
                            resultValue = make_value(derefCurrent.as_float() + derefRight.as_float());
                        } else if (leftType == script_value_type::jai_string_type && rightType == script_value_type::jai_string_type) {
                            resultValue = make_value(derefCurrent.as_string() + derefRight.as_string());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Invalid operands for +=
                        }
                    }
                    break;
                }

                case token_type::minus_equal: {
                    // Only check for custom operators if we know they exist (fast path optimization)
                    customOpFound = false;
                    if (has_custom_numeric_ops_ && environment_ && environment_->contains("-")) {
                        auto op_result = environment_->get("-");
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
                            customOpFound = true;
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        // Use single type() call + switch for faster type checking
                        // Dereference if needed to get actual value type
                        auto& derefCurrent = currentValue.deref();
                        auto& derefRight = rightValue.deref();
                        auto leftType = derefCurrent.type();
                        auto rightType = derefRight.type();

                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            resultValue = make_value(derefCurrent.as_int() - derefRight.as_int());
                        } else if ((leftType == script_value_type::jai_int_type || leftType == script_value_type::jai_float_type) &&
                                   (rightType == script_value_type::jai_int_type || rightType == script_value_type::jai_float_type)) {
                            resultValue = make_value(derefCurrent.as_float() - derefRight.as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Invalid operands for -=
                        }
                    }
                    break;
                }

                case token_type::star_equal: {
                    // Only check for custom operators if we know they exist (fast path optimization)
                    customOpFound = false;
                    if (has_custom_numeric_ops_ && environment_ && environment_->contains("*")) {
                        auto op_result = environment_->get("*");
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
                            customOpFound = true;
                        }
                    }
                    
                    // Fall back to built-in operators
                    if (!customOpFound) {
                        // Use single type() call + switch for faster type checking
                        // Dereference if needed to get actual value type
                        auto& derefCurrent = currentValue.deref();
                        auto& derefRight = rightValue.deref();
                        auto leftType = derefCurrent.type();
                        auto rightType = derefRight.type();

                        if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                            resultValue = make_value(derefCurrent.as_int() * derefRight.as_int());
                        } else if ((leftType == script_value_type::jai_int_type || leftType == script_value_type::jai_float_type) &&
                                   (rightType == script_value_type::jai_int_type || rightType == script_value_type::jai_float_type)) {
                            resultValue = make_value(derefCurrent.as_float() * derefRight.as_float());
                        } else {
                            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Invalid operands for *=
                        }
                    }
                    break;
                }

                case token_type::slash_equal: {
                    // Use single type() call + switch for faster type checking
                    // Dereference if needed to get actual value type
                    auto& derefCurrent = currentValue.deref();
                    auto& derefRight = rightValue.deref();
                    auto leftType = derefCurrent.type();
                    auto rightType = derefRight.type();

                    // Check for division by zero
                    if (rightType == script_value_type::jai_int_type && derefRight.as_int() == 0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));  // [ErrorText] Division by zero
                    }
                    if (rightType == script_value_type::jai_float_type && derefRight.as_float() == 0.0) {
                        return checked_result<void>(make_error_code(runtime_error_code::division_by_zero));  // [ErrorText] Division by zero
                    }

                    if (leftType == script_value_type::jai_int_type && rightType == script_value_type::jai_int_type) {
                        resultValue = make_value(derefCurrent.as_int() / derefRight.as_int());
                    } else if ((leftType == script_value_type::jai_int_type || leftType == script_value_type::jai_float_type) &&
                               (rightType == script_value_type::jai_int_type || rightType == script_value_type::jai_float_type)) {
                        resultValue = make_value(derefCurrent.as_float() / derefRight.as_float());
                    } else {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Invalid operands for /=
                    }
                    break;
                }

                default:
                    return checked_result<void>(make_error_code(runtime_error_code::unknown_operator));  // [ErrorText] Unknown operator
            }
            
            // Check if this is a reference variable
            script_value* varPtr = environment_->get_value_ptr(identifier->symbol_id);
            if (varPtr && varPtr->is_reference()) {
                // This is a reference - update the target (deep copy)
                varPtr->deref() = std::move(resultValue.deref().clone());
            } else {
                // Regular assignment (deep copy the result)
                environment_->assign(identifier->symbol_id, std::move(resultValue.clone()));
            }
            push_value(resultValue);
        } else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Handle compound assignment to member expression (e.g., obj.value += 10)
            // First, get the current value of the property
            JAISCRIPT_TRY(memberExpr->accept(this));
            script_value currentValue = pop_value().deref();
            
            // Evaluate the right-hand side
            JAISCRIPT_TRY(expr->value->accept(this));
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue = script_value::make_null(engine_ref_);
            bool customOpFound = false;
            
            switch (expr->op.type) {
                case token_type::plus_equal: {
                    if (!customOpFound) {
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
            JAISCRIPT_TRY(memberExpr->object->accept(this));
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
            
            // Check if there's a property setter
            script_value setter = instance->get_method("_set_" + memberExpr->member, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {objectValue, std::move(resultValue.clone())};
                auto result = func(args);
                if (!result) {
                    // Setter failed - propagate error
                    return checked_result<void>(result.error(), result.message());
                }
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, std::move(resultValue.clone()));
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
            JAISCRIPT_TRY(expr->target->accept(this));
            script_value currentValue = pop_value();

            // Evaluate the right-hand side
            JAISCRIPT_TRY(expr->value->accept(this));
            script_value rightValue = pop_value();
            
            // Perform the compound operation
            script_value resultValue = script_value::make_null(engine_ref_);
            
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
                    case token_type::plus_equal:
                        if (currentValue.is_string() || rightValue.is_string()) {
                            resultValue = make_value(currentValue.to_string() + rightValue.to_string());
                        } else {
                            resultValue = evaluate_arithmetic(currentValue, token_type::plus, rightValue);
                        }
                        break;
                    case token_type::minus_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::minus, rightValue);
                        break;
                    case token_type::star_equal:
                        resultValue = evaluate_arithmetic(currentValue, token_type::star, rightValue);
                        break;
                    case token_type::slash_equal:
                        if ((rightValue.is_int() && rightValue.as_int() == 0) ||
                            (rightValue.is_float() && rightValue.as_float() == 0.0)) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Division by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::slash, rightValue);
                        break;
                    case token_type::percent_equal:
                        if (rightValue.is_int() && rightValue.as_int() == 0) {
                            return checked_result<void>(make_error_code(runtime_error_code::division_by_zero),
                                "Modulo by zero");
                        }
                        resultValue = evaluate_arithmetic(currentValue, token_type::percent, rightValue);
                        break;
                    default:
                        return checked_result<void>(make_error_code(runtime_error_code::unsupported_operation),
                            "Unknown compound assignment operator");
                }
            }
            
            // Directly assign the result without creating new AST nodes (optimization)
            if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
                // Fast path for simple identifier assignment
                if (identifier->symbol_id == UINT64_MAX) {
                    identifier->symbol_id = string_symbolizer_->intern(identifier->name);
                }
                environment_->assign(identifier->symbol_id, std::move(resultValue));
                push_value(resultValue);
            } else {
                // Fall back to AST creation for complex lvalues
                auto regularAssignment = std::make_shared<assignment_expr>(
                    expr->location,
                    expr->target,
                    token(token_type::equal, "=", expr->op.location),
                    std::make_shared<literal_expr>(expr->location, resultValue)
                );
                JAISCRIPT_TRY(regularAssignment->accept(this));
            }
        }
    } else {
        // Regular assignment

        JAISCRIPT_TRY(expr->value->accept(this));
        // Check if we're unwinding due to an exception in the value expression
        if (is_unwinding_) {
            // Don't try to pop a value that wasn't pushed due to the exception
            return {};
        }
        script_value value = pop_value();
        
        
        // Check if target is an identifier
        if (auto* identifier = dynamic_cast<identifier_expr*>(expr->target.get())) {
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
                        environment_->assign(identifier->symbol_id, script_value::make_empty_weak_ptr(type_info, engine_ref_));
                    } else if (value.is_weak_ptr()) {
                        // Assign another weak_ptr
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else if (value.type() == script_value_type::jai_shared_ptr_type) {
                        // Convert shared_ptr to weak_ptr
                        auto weak_result = script_value::make_weak_ptr(value, engine_ref_);
                        if (!weak_result) {
                            return checked_result<void>(weak_result.error(), weak_result.message());
                        }
                        environment_->assign(identifier->symbol_id, std::move(weak_result.value()));
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Helpful error for value-semantic objects
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign value-semantic object to weak_ptr. Use shared_ptr<T> to enable reference semantics.");
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign " + type_name + " to weak_ptr");
                    }
                } else if (currentVal && currentVal->get_type_info() &&
                          currentVal->get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                    // Special handling for shared_ptr assignment
                    if (value.is_null()) {
                        // Assign null - that's fine
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else if (value.is_weak_ptr()) {
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign weak_ptr to shared_ptr - use weak.lock() instead");
                    } else if (value.type() == script_value_type::jai_object_type) {
                        // Assign object to shared_ptr - just update the value but keep the shared_ptr type info
                        auto type_info = currentVal->get_type_info();
                        value.set_type_info(type_info);
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else {
                        auto type_info = value.get_type_info();
                        std::string type_name = type_info ? type_info->type_name : "unknown";
                        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                            "Cannot assign " + type_name + " to shared_ptr");
                    }
                } else {
                    // Regular variable assignment
                    // For objects, use move semantics to transfer ownership instead of cloning
                    // This ensures destructors fire at the right time
                    if (value.is_object()) {
                        environment_->assign(identifier->symbol_id, std::move(value));
                    } else {
                        // For other types, clone to maintain value semantics
                        environment_->assign(identifier->symbol_id, std::move(value.clone()));
                    }
                }
            } else {
                // Variable doesn't exist in environment
                // Try static_method_environment's assign (which handles static fields)
                // or instance method's 'this' field assignment
                bool assigned_to_member = false;
                auto this_result = environment_->get("this");
                if (this_result) {
                    script_value this_val = std::move(this_result.value());
                    if (this_val.is_object()) {
                        auto obj_holder = this_val.get_object_holder();
                        if (obj_holder->is_class_instance_wrapper) {
                            auto instance = std::static_pointer_cast<class_instance>(obj_holder->data);

                            // First try instance fields
                            if (instance->has_field(identifier->name)) {
                                instance->set_field(identifier->name, std::move(value.clone()));
                                assigned_to_member = true;
                            }
                            // Then try static fields
                            else {
                                auto class_def = instance->get_class_definition();
                                if (class_def && class_def->set_static_field(identifier->name, value)) {
                                    assigned_to_member = true;
                                }
                            }
                        }
                    }
                }

                if (!assigned_to_member) {
                    // Use environment->assign() which will:
                    // - For static_method_environment: check static fields, then throw if not found
                    // - For regular environment: throw error if variable doesn't exist
                    environment_->assign(identifier->symbol_id, std::move(value.clone()));
                }
            }

            push_value(std::move(value));  // Assignment expressions return the assigned value
        }
        // Check if target is a member expression (property assignment)
        else if (auto* memberExpr = dynamic_cast<member_expr*>(expr->target.get())) {
            // Check if this is a static member assignment
            if (memberExpr->is_static) {
                // For static assignment, get the class definition
                auto* ident_expr = dynamic_cast<identifier_expr*>(memberExpr->object.get());
                if (!ident_expr) {
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch),
                        "Static member assignment requires a class name");
                }
                
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
                JAISCRIPT_TRY(expr->value->accept(this));
                script_value value = pop_value();
                
                // Set the static field
                if (!class_def->set_static_field(memberExpr->member, value.clone())) {
                    return checked_result<void>(make_error_code(runtime_error_code::undefined_variable),
                        "Cannot assign to static member: field '" + memberExpr->member + "' not found");
                }


                push_value(value);
                return {};
            }

            // Regular member assignment - evaluate the object
            JAISCRIPT_TRY(memberExpr->object->accept(this));
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

            // Check if there's a property setter first (for C++ properties)
            script_value setter = instance->get_method("_set_" + memberExpr->member, false);
            if (!setter.is_null()) {
                // Call the setter with 'this' and the value
                const script_function& func = setter.as_function();
                std::vector<script_value> args = {dereferenced, std::move(value.clone())};
                auto result = func(args);
                if (!result) {
                    // Setter failed - propagate error
                    return checked_result<void>(result.error(), result.message());
                }
            } else if (instance->has_field(memberExpr->member)) {
                // Direct field assignment (deep copy)
                instance->set_field(memberExpr->member, std::move(value.clone()));
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
        else if (auto* binaryExpr = dynamic_cast<binary_expr*>(expr->target.get())) {
            if (binaryExpr->op.type == token_type::left_bracket) {
                // Evaluate the entire target expression (e.g., nested["nums"][1])
                // This should return a reference if it's a valid lvalue
                JAISCRIPT_TRY(expr->target->accept(this));
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
    JAISCRIPT_TRY(stmt->expression->accept(this));

    // Early exit if exception is propagating
    if (is_unwinding_) return {};

    // Pop the result - expression statements don't produce values
    // (except for top-level expressions in global scope, which are handled by expression_decl)
    pop_value();
    return {};
}

checked_result<void> interpreter::visit_block_stmt(block_stmt* stmt) {
    // Create new environment for the block scope
    auto previous = environment_;
    auto block_env = get_pooled_environment(environment_);
    environment_ = block_env;

    try {
        for (const auto& decl : stmt->declarations) {
            auto result = decl->accept(this);

            // IMPORTANT: Clear value stack after each declaration to prevent accumulation
            // This ensures objects are destroyed at statement boundaries, not just at block exit
            // Variable declarations pop their values, but some other declarations (like expression_decl)
            // may leave values on the stack
            if (valueStack_.size() > 0) {
                valueStack_.clear();
            }

            if (!result) {
                // Release the block environment before returning
                release_environment(block_env);
                environment_ = previous;
                return result;
            }
            if (is_unwinding_) break;
        }
    } catch (...) {
        // Release the block environment even if an error occurs
        release_environment(block_env);
        environment_ = previous;
        throw;
    }

    // Release the block environment to destroy all local variables
    // Clear the value stack before releasing environment
    // This ensures any lingering object references on the stack are released
    // Block statements don't produce a value, so the stack should be cleared
    valueStack_.clear();
    release_environment(block_env);

    // Restore previous environment
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
            JAISCRIPT_TRY(decl->initializer->accept(this));
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
                throw runtime_error("Cannot initialize weak_ptr from a value-semantic object. Use shared_ptr<T> to enable reference semantics.");
            } else {
                auto type_info = value.get_type_info();
                std::string type_name = type_info ? type_info->type_name : "unknown";
                throw runtime_error("Cannot initialize weak_ptr with " + type_name);
            }
        }
    } else if (is_shared_ptr) {
        // shared_ptr<T> variable - handle initialization
        if (!decl->initializer) {
            // No initializer - create null shared_ptr
            script_value null_ptr = script_value::make_null(engine_ref_);
            null_ptr.set_type_info(decl->type);  // Mark as shared_ptr type
            environment_->define(decl->name_id, std::move(null_ptr));
        } else {
            // Evaluate initializer
            JAISCRIPT_TRY(decl->initializer->accept(this));
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
        if (auto identExpr = dynamic_cast<identifier_expr*>(decl->initializer.get())) {
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
            JAISCRIPT_TRY(decl->initializer->accept(this));
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
        script_value value = script_value::make_null(engine_ref_);
        if (decl->initializer) {
            JAISCRIPT_TRY(decl->initializer->accept(this));
            value = std::move(pop_value());

            // Only clone if initializing from an lvalue (existing object)
            // Temporaries (constructor calls, expressions) should use move semantics
            bool is_lvalue_init = dynamic_cast<identifier_expr*>(decl->initializer.get()) != nullptr ||
                                  dynamic_cast<member_expr*>(decl->initializer.get()) != nullptr ||
                                  (dynamic_cast<binary_expr*>(decl->initializer.get()) != nullptr &&
                                   dynamic_cast<binary_expr*>(decl->initializer.get())->op.type == token_type::left_bracket);

            if (is_lvalue_init) {
                // Initializing from an existing object - deep copy
                value = value.clone();
            }
            // else: Initializing from a temporary - use move semantics (no clone)
        }
        // If no initializer, value remains null

        environment_->define(decl->name_id, std::move(value));
        // After move, value is in moved-from state, so don't access it
    }
    return {};
}

// Binary operation helpers
script_value interpreter::evaluate_arithmetic(const script_value& left, token_type op, const script_value& right) {
    // Special case for string concatenation (use move to avoid copying the temporary)
    if (op == token_type::plus && (left.is_string() || right.is_string())) {
        return make_value(left.to_string() + right.to_string());  // Move overload selected automatically
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
                    throw runtime_error("Division by zero");
                }
                // Integer division returns integer (C++ semantics)
                return make_value(leftInt / rightInt);
            case token_type::percent:
                if (rightInt == 0) {
                    throw runtime_error("Division by zero");
                }
                return make_value(leftInt % rightInt);
            default:
                throw runtime_error("Unknown arithmetic operator");
        }
    }
    
    // Mixed or floating point arithmetic path
    script_float leftNum, rightNum;
    
    if (left.is_int()) {
        leftNum = static_cast<script_float>(left.as_int());
    } else if (left.is_float()) {
        leftNum = left.as_float();
    } else {
        throw runtime_error("Left operand must be numeric");
    }
    
    if (right.is_int()) {
        rightNum = static_cast<script_float>(right.as_int());
    } else if (right.is_float()) {
        rightNum = right.as_float();
    } else {
        throw runtime_error("Right operand must be numeric");
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
                throw runtime_error("Division by zero");
            }
            return make_value(leftNum / rightNum);
        case token_type::percent:
            if (rightNum == 0.0) {
                throw runtime_error("Division by zero");
            }
            return make_value(std::fmod(leftNum, rightNum));
        default:
            throw runtime_error("Unknown arithmetic operator");
    }
}

script_value interpreter::evaluate_comparison(const script_value& left, token_type op, const script_value& right) {
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
                throw runtime_error("Cannot compare null values with relational operators");
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
                throw runtime_error("Unknown comparison operator");
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
            throw runtime_error("Unknown comparison operator");
    }
}

script_value interpreter::evaluate_logical(const script_value& left, token_type op, const script_value& right) {
    bool leftTruthy = is_truthy(left);
    
    switch (op) {
        case token_type::ampersand_ampersand:
            // Short-circuit: if left is false, return left
            if (!leftTruthy) {
                return left;
            }
            return right;
            
        case token_type::pipe_pipe:
            // Short-circuit: if left is true, return left
            if (leftTruthy) {
                return left;
            }
            return right;
            
        default:
            throw runtime_error("Unknown logical operator");
    }
}

script_value interpreter::evaluate_bitwise(const script_value& left, token_type op, const script_value& right) {
    // Bitwise operations only work on integers
    if (!left.is_int() || !right.is_int()) {
        throw runtime_error("Bitwise operations require integer operands");
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
            throw runtime_error("Unknown bitwise operator");
    }
}


// Placeholder implementations for remaining visitors
checked_result<void> interpreter::visit_call_expr(call_expr* expr) {
    // Special handling for weak_from_this() and shared_from_this()
    if (auto* ident_expr = dynamic_cast<identifier_expr*>(expr->callee.get())) {
        // Use interned symbol IDs for fast comparison
        if (ident_expr->symbol_id == weak_from_this_id_ || ident_expr->symbol_id == shared_from_this_id_) {
            // These functions take no arguments
            if (!expr->arguments.empty()) {
                return checked_result<void>(make_error_code(runtime_error_code::argument_count_mismatch),
                    ident_expr->name + "() takes no arguments");
            }

            // Get 'this' from the current environment
            auto this_result = environment_->get("this");
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
    JAISCRIPT_TRY(expr->callee->accept(this));
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
        if (auto identExpr = dynamic_cast<identifier_expr*>(argExpr.get())) {
            // Get the symbol ID for this variable
            uint64_t symbol_id = string_symbolizer_->intern(identExpr->name);
            argMetadata.emplace_back(symbol_id, environment_);
        } else {
            // Not an identifier - can't take reference
            argMetadata.emplace_back(UINT64_MAX, nullptr);
        }
        
        // Evaluate argument with exception handling
        try {
            JAISCRIPT_TRY(argExpr->accept(this));
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
        auto* ident_expr = dynamic_cast<identifier_expr*>(expr->object.get());
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
        } else if (auto* member_expr_obj = dynamic_cast<member_expr*>(expr->object.get())) {
            // Nested namespace: outer::inner where the object is "outer::inner" (a member_expr)
            // Recursively build the full path
            std::function<std::string(expression*)> build_namespace_path = [&](expression* e) -> std::string {
                if (auto* ident = dynamic_cast<identifier_expr*>(e)) {
                    return ident->name;
                } else if (auto* member = dynamic_cast<member_expr*>(e)) {
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

                    // Try to get the static method
                    script_value static_method = class_def->get_static_method(expr->member, false);
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
                std::string member_name = expr->member;
                script_function namespace_func = [this, overloads, name, namespace_id, fallback_class, member_name](const std::vector<script_value>& args) -> checked_result<script_value> {
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
                        script_value static_method = fallback_class->get_static_method(member_name, false);
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
        script_value static_method = class_def->get_static_method(expr->member, false);
        if (!static_method.is_null()) {
            push_value(static_method);
            return {};
        }

        // Try static field access
        // get_static_field returns null if not found
        script_value static_value = class_def->get_static_field(expr->member);
        if (!static_value.is_null()) {
            push_value(static_value);
            return {};
        }

        // Try getter method as fallback (for C++ bound properties)
        std::string getter_name = "_get_" + expr->member;
        // get_static_method with false doesn't throw - returns null if not found
        script_value getter_method = class_def->get_static_method(getter_name, false);
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
    bool is_super_access = dynamic_cast<super_expr*>(expr->object.get()) != nullptr;

    // Evaluate the object expression
    JAISCRIPT_TRY(expr->object->accept(this));
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

        // Look for the method in the parent class
        script_value method = parent_def->get_method(expr->member);
        if (method.is_null()) {
            // Parent class has no method
            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] Undefined variable
        }

        // Return a bound method that calls the parent's implementation
        push_value(create_bound_method(objectValue, method));
        return {};
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

    // First check if it's a field (registered by the property() method)
    bool has_field_result = instance->has_field(expr->member);
    if (has_field_result) {
        // For script classes, always access fields directly
        // For C++ classes, check if there's a property getter method
        if (!instance->is_script_class()) {
            try {
                script_value getter = instance->get_method("_get_" + expr->member);
                if (!getter.is_null()) {
                    // Call the getter with 'this' as argument
                    const script_function& func = getter.as_function();
                    std::vector<script_value> args = {objectValue};
                    auto result = func(args);
                    if (!result) {
                        // Function returned error - propagate it up
                        return checked_result<void>(result.error(), result.message());
                    }
                    push_value(std::move(result.value()));
                    return {};
                }
            } catch (const std::exception&) {
                // If get_method fails (e.g., class definition expired), fall back to direct field access
            }
        }
        // Return the field value directly (for script classes or if no getter)
        push_value(instance->get_field(expr->member));
        return {};
    }

    // Otherwise, look for a method (pass false to avoid throwing)
    script_value method = instance->get_method(expr->member, false);
    if (!method.is_null() && !method.is_invalid()) {
        // Return a bound method (function that has 'this' pre-bound)
        // We'll create a wrapper function that includes the object as first argument
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
            if (auto* ident = dynamic_cast<identifier_expr*>(e)) {
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
            } else if (auto* binary = dynamic_cast<binary_expr*>(e)) {
                find_identifiers(binary->left.get());
                find_identifiers(binary->right.get());
            } else if (auto* unary = dynamic_cast<unary_expr*>(e)) {
                find_identifiers(unary->operand.get());
            } else if (auto* call = dynamic_cast<call_expr*>(e)) {
                find_identifiers(call->callee.get());
                for (const auto& arg : call->arguments) {
                    find_identifiers(arg.get());
                }
            } else if (auto* member = dynamic_cast<member_expr*>(e)) {
                find_identifiers(member->object.get());
            } else if (auto* assign = dynamic_cast<assignment_expr*>(e)) {
                find_identifiers(assign->target.get());
                find_identifiers(assign->value.get());
            } else if (auto* ternary = dynamic_cast<ternary_expr*>(e)) {
                find_identifiers(ternary->condition.get());
                find_identifiers(ternary->then_expression.get());
                find_identifiers(ternary->else_expression.get());
            }
            // Add more expression types as needed
        };
        
        // Helper to find identifiers in statements
        std::function<void(statement*)> find_in_statement;
        find_in_statement = [&](statement* s) {
            if (auto* expr_stmt = dynamic_cast<expression_stmt*>(s)) {
                find_identifiers(expr_stmt->expression.get());
            } else if (auto* block = dynamic_cast<block_stmt*>(s)) {
                for (const auto& decl : block->declarations) {
                    if (auto* expr_decl = dynamic_cast<expression_decl*>(decl.get())) {
                        find_identifiers(expr_decl->expression.get());
                    } else if (auto* stmt_decl = dynamic_cast<statement_decl*>(decl.get())) {
                        find_in_statement(stmt_decl->statement.get());
                    }
                }
            } else if (auto* if_s = dynamic_cast<if_stmt*>(s)) {
                find_identifiers(if_s->condition.get());
                find_in_statement(if_s->then_statement.get());
                if (if_s->else_statement) {
                    find_in_statement(if_s->else_statement.get());
                }
            } else if (auto* while_s = dynamic_cast<while_stmt*>(s)) {
                find_identifiers(while_s->condition.get());
                find_in_statement(while_s->body.get());
            } else if (auto* return_s = dynamic_cast<return_stmt*>(s)) {
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
    script_function funcWrapper = [this, lambdaFunc](const std::vector<script_value>& args) -> script_value {
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
            JAISCRIPT_TRY(expr->arguments[0]->accept(this));
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
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Type error
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
            push_value(script_value::make_null(engine_ref_));
        } else if (expr->arguments.size() == 1) {
            // One argument - mark it as shared_ptr type
            JAISCRIPT_TRY(expr->arguments[0]->accept(this));
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
        JAISCRIPT_TRY(argExpr->accept(this));
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
    JAISCRIPT_TRY(expr->condition->accept(this));
    script_value conditionValue = pop_value();

    // Check if condition is truthy
    bool conditionIsTruthy = is_truthy(conditionValue);

    // Evaluate only the selected branch (short-circuit evaluation)
    if (conditionIsTruthy) {
        JAISCRIPT_TRY(expr->then_expression->accept(this));
    } else {
        JAISCRIPT_TRY(expr->else_expression->accept(this));
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
        JAISCRIPT_TRY(element->accept(this));
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
        JAISCRIPT_TRY(entry.first->accept(this));
        script_value key = pop_value();

        // Evaluate value
        JAISCRIPT_TRY(entry.second->accept(this));
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
    auto this_result = environment_->get("this");
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
    auto this_result = environment_->get("this");
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
        JAISCRIPT_TRY(expr->value->accept(this));
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
    JAISCRIPT_TRY(stmt->condition->accept(this));
    script_value conditionValue = pop_value();

    // Execute appropriate branch based on truthiness
    if (is_truthy(conditionValue)) {
        JAISCRIPT_TRY(stmt->then_statement->accept(this));
    } else if (stmt->else_statement) {
        JAISCRIPT_TRY(stmt->else_statement->accept(this));
    }
    return {};
}

checked_result<void> interpreter::visit_while_stmt(while_stmt* stmt) {
    while (true) {
        // Evaluate the condition
        JAISCRIPT_TRY(stmt->condition->accept(this));

        // Phase 2: Boolean fast path optimization (ChaiScript-style)
        const auto& val = valueStack_.top();
        bool is_true = is_truthy(val);

        valueStack_.discard();

        if (!is_true) {
            break;
        }

        // Execute the loop body
        auto result = stmt->body->accept(this);
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
    // Create new scope for the for loop (initialization variables should be scoped)
    auto previous = environment_;
    auto loop_env = get_pooled_environment(environment_);
    environment_ = loop_env;

    // Error capture for lambdas (avoid throwing from hot path)
    std::optional<checked_result<void>> error;

    // Lambda helpers (ChaiScript-style) for cleaner, more optimizable code
    auto eval_condition = [&]() -> bool {
        if (!stmt->condition) return true;  // No condition = infinite loop

        auto result = stmt->condition->accept(this);
        if (!result) {
            error = result;  // Capture error, signal loop termination
            return false;
        }

        // Optimized: Use is_truthy with unchecked accessors
        const auto& val = valueStack_.top();
        bool is_true = is_truthy(val);

        valueStack_.discard();
        return is_true;
    };

    auto eval_update = [&]() {
        if (!stmt->update) return;

        auto result = stmt->update->accept(this);
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
        auto result = stmt->initializer->accept(this);
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
        auto result = stmt->body->accept(this);
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
    JAISCRIPT_TRY(stmt->container->accept(this));
    script_value container = pop_value();

    // Create a new scope for the loop variable
    push_scope();

    try {
        if (container.is_array()) {
            // Iterate over array
            auto& array_storage = container.get_array_storage();

            for (size_t i = 0; i < array_storage->size(); ++i) {
                script_value loop_var = script_value::make_null(engine_ref_);

                if (stmt->is_reference) {
                    // Create a reference to the actual array element
                    loop_var = script_value::make_reference(&(*array_storage)[i], environment_, engine_ref_);
                } else {
                    // Make a copy of the element
                    loop_var = (*array_storage)[i].clone();
                }

                // Define the loop variable in current scope
                environment_->define(stmt->variable_name, std::move(loop_var));

                // Execute loop body
                try {
                    auto body_result = stmt->body->accept(this);
                    if (!body_result) {
                        pop_scope();
                        return body_result;
                    }
                } catch (const continue_exception&) {
                    continue; // Skip to next iteration
                } catch (const break_exception&) {
                    break; // Exit loop
                }

                // Check for return or exception
                if (hasReturnValue_ || is_unwinding_) {
                    break;
                }
            }
            
        } else if (container.is_map()) {
            // Iterate over map - return key-value pairs with first/second access
            auto& map_storage = container.get_map_storage();
            
            for (auto it = map_storage->begin(); it != map_storage->end(); ++it) {
                // Create a pair object using the registered stdlib::script_pair type
                script_value loop_var = script_value::make_null(engine_ref_);
                
                try {
                    if (stmt->is_reference) {
                        // For references, create a pair with a reference to the map value
                        // Cast away const to get a pointer (safe because we own the map)
                        script_value* value_ptr = const_cast<script_value*>(&it->second);
                        
                        // Create pair using the constructor with first as copy and second as reference
                        std::vector<script_value> args;
                        args.push_back(it->first);  // Don't clone - just pass the key
                        args.push_back(script_value::make_reference(value_ptr, environment_, engine_ref_));
                        
                        // Look up the pair constructor function
                        uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
                        auto pair_result = environment_->get_ref(pair_symbol_id);
                        if (!pair_result) {
                            pop_scope();
                            return checked_result<void>(pair_result.error(), pair_result.message());
                        }
                        const script_value& pairConstructor = pair_result.value().get();

                        if (pairConstructor.is_function()) {
                            const script_function& func = pairConstructor.as_function();
                            auto result = func(args);
                            if (!result) {
                                // Function returned error - propagate it up
                                return checked_result<void>(result.error(), result.message());
                            }
                            loop_var = std::move(result.value());
                        } else {
                            pop_scope();
                            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] pair type not registered - make sure stdlib is loaded
                        }
                    } else {
                        // For copies, use regular pair constructor
                        std::vector<script_value> args;
                        args.push_back(it->first.clone());
                        args.push_back(it->second.clone());

                        // Look up the pair constructor function using symbol ID
                        uint64_t pair_symbol_id = string_symbolizer_->intern("pair");
                        auto pair_result = environment_->get_ref(pair_symbol_id);
                        if (!pair_result) {
                            pop_scope();
                            return checked_result<void>(pair_result.error(), pair_result.message());
                        }
                        const script_value& pairConstructor = pair_result.value().get();

                        if (pairConstructor.is_function()) {
                            const script_function& func = pairConstructor.as_function();
                            auto result = func(args);
                            if (!result) {
                                // Function returned error - propagate it up
                                return checked_result<void>(result.error(), result.message());
                            }
                            loop_var = std::move(result.value());
                        } else {
                            pop_scope();
                            return checked_result<void>(make_error_code(runtime_error_code::undefined_variable));  // [ErrorText] pair type not registered - make sure stdlib is loaded
                        }
                    }
                } catch (const runtime_error&) {
                    pop_scope();
                    return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Failed to create pair for map iteration
                }
                
                // Define the loop variable in current scope
                environment_->define(stmt->variable_name, std::move(loop_var));
                
                // Execute loop body
                try {
                    auto body_result = stmt->body->accept(this);
                    if (!body_result) {
                        pop_scope();
                        return body_result;
                    }
                } catch (const continue_exception&) {
                    continue; // Skip to next iteration
                } catch (const break_exception&) {
                    break; // Exit loop
                }
                
                // Check for return or exception
                if (hasReturnValue_ || is_unwinding_) {
                    break;
                }
            }

        } else {
            pop_scope();
            return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] Range-based for loop requires an array or map
        }

    } catch (const break_exception&) {
        // Break caught from inner loop
    } catch (const continue_exception&) {
        // Continue should not escape the loop
        pop_scope();
        return checked_result<void>(make_error_code(runtime_error_code::type_mismatch));  // [ErrorText] 'continue' statement not in loop
    }

    // Pop the loop scope
    pop_scope();
    return {};
}

checked_result<void> interpreter::visit_return_stmt(return_stmt* stmt) {
    if (stmt->value) {
        // Evaluate the return expression
        JAISCRIPT_TRY(stmt->value->accept(this));
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
    auto saved_catch_var = current_catch_var_;

    // Reset state for try block
    // Don't reset exception state if we're in a catch block (allows re-throw)
    if (current_catch_var_.empty()) {
        current_exception_.reset();
        active_exception_value_ = make_value();
    }
    is_unwinding_ = false;
    current_catch_var_.clear();

    // Execute try block
    auto try_result = stmt->try_block->accept(this);

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

        // Set the current catch variable name so identifier lookup can find it
        current_catch_var_ = stmt->catch_var;

        // Execute catch block
        auto catch_result = stmt->catch_block->accept(this);
        // Propagate errors from catch block
        if (!catch_result) {
            current_catch_var_ = saved_catch_var;
            return catch_result;
        }

        // Clear catch variable
        current_catch_var_.clear();

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
    current_catch_var_ = saved_catch_var;
    return {};
}

checked_result<void> interpreter::visit_switch_stmt(switch_stmt* stmt) {
    // Evaluate the switch condition
    JAISCRIPT_TRY(stmt->condition->accept(this));
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
            auto result = case_stmt->value->accept(this);
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
                    auto case_result = case_stmt->accept(this);
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
                auto default_result = stmt->default_case->accept(this);
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
        auto result = s->accept(this);
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
        auto result = s->accept(this);
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
    script_value functionValue = script_value::make_function([this, scriptFunc](const std::vector<script_value>& args) -> script_value {
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
    
    auto existing_result = environment_->get(class_var_name);
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
    
    // Collect new field defaults and methods
    std::unordered_map<std::string, script_value> new_field_defaults;
    std::unordered_map<std::string, script_value> new_methods;
    std::unordered_map<std::string, script_value> new_static_methods;
    
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
            script_value base_class_var = script_value::make_null(engine_ref_);
            auto base_result = environment_->get("__class_" + base_name);
            if (base_result) {
                base_class_var = std::move(base_result.value());
            }

            std::shared_ptr<class_definition> base_class_def;

            if (!base_class_var.is_null() && base_class_var.is_object()) {
                // Found a script class - extract from object holder
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

                        // Set as C++ base class (for first base only, maintaining compatibility)
                        if (parent_defs.empty()) {
                            class_def->set_cpp_base_class(cpp_class_def);
                        }
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

    // Collect field names defined in this class (before evaluating them)
    // This is needed for multiple inheritance conflict detection
    std::unordered_set<std::string> derived_field_names;
    for (const auto& member : decl->members) {
        auto* var_decl = dynamic_cast<variable_decl*>(member.declaration.get());
        if (var_decl && !var_decl->is_static) {
            derived_field_names.insert(var_decl->name);
        }
    }

    // Check for field name conflicts in multiple inheritance (only for fields NOT redefined in derived class)
    // C++ doesn't allow ambiguous field access - we follow the same semantics
    // But if the derived class defines its own version, it shadows the parent fields (allowed)
    if (!decl->base_classes.empty() && decl->base_classes.size() > 1) {
        const auto& parent_classes = class_def->get_parent_classes();
        std::unordered_map<std::string, std::vector<std::string>> field_sources;

        // Collect all fields from each parent (including inherited ones)
        for (const auto& parent : parent_classes) {
            const auto& parent_fields = parent->get_all_field_defaults();
            for (const auto& [field_name, _] : parent_fields) {
                // Skip fields that are redefined in the derived class (shadowing is allowed)
                if (derived_field_names.find(field_name) == derived_field_names.end()) {
                    field_sources[field_name].push_back(parent->get_name());
                }
            }
        }

        // Check for conflicts (field appears in multiple parents and NOT redefined in derived)
        for (const auto& [field_name, sources] : field_sources) {
            if (sources.size() > 1) {
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
        auto* var_decl = dynamic_cast<variable_decl*>(member.declaration.get());
        auto* func_decl = dynamic_cast<function_decl*>(member.declaration.get());

        if (var_decl) {
            // Field declaration
            script_value default_val(std::monostate{}, engine_ref_);  // Ensure engine reference
            std::string field_name = var_decl->name;
            expression_ptr initializer_ast = nullptr;

            if (var_decl->initializer) {
                // Check if the initializer is an assignment expression
                // This happens when the parser sees "x = 0" and creates assignment_expr
                auto* assign_expr = dynamic_cast<assignment_expr*>(var_decl->initializer.get());
                if (assign_expr) {
                    // For field declarations like "x = 0", we need to get the field name from the assignment
                    if (auto* ident_expr = dynamic_cast<identifier_expr*>(assign_expr->target.get())) {
                        field_name = ident_expr->name;
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
                    JAISCRIPT_TRY(initializer_ast->accept(this));
                    default_val = pop_value();

                    // Ensure the default value has an engine reference
                    if (default_val.get_engine_ref().expired() && !engine_ref_.expired()) {
                        default_val.set_engine_ref(engine_ref_);
                    }
                }

                // Add static field directly to the class
                if (!field_name.empty()) {
                    class_def->add_static_field(field_name, default_val);
                }
            } else {
                // Instance field - store initializer AST for evaluation at construction time
                if (!field_name.empty()) {
                    if (initializer_ast) {
                        // Store the initializer AST in the script class definition
                        class_def->add_field_initializer_ast(field_name, initializer_ast);
                    }
                    // Also add a null default value to the field_defaults map
                    // This ensures the field exists but will be properly initialized later
                    new_field_defaults[field_name] = default_val;
                }
            }

        } else if (func_decl) {
            // Method declaration
            auto method_name = func_decl->name;
            
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
                            auto static_env = std::make_shared<static_method_environment>(
                                definition_env,
                                self->string_symbolizer_,
                                class_def
                            );

                            // Call the interpreter method directly without 'this'
                            return self->execute_method_ast(method_ast, static_env, args);
                        };
                        
                        new_static_methods[method_name] = script_value::make_function(static_method_func, engine_ref_);
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
                        
                        new_methods[method_name] = script_value::make_function(method_func, engine_ref_);
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
        // Capture the definition environment for constructor execution
        auto definition_env = environment_;

        // Create a constructor dispatcher that selects based on argument count
        auto ctor_dispatcher = [weak_self = std::weak_ptr<interpreter>(shared_from_this()),
                               class_def,
                               definition_env,
                               class_name = decl->name](const std::vector<script_value>& args) -> script_value {
            auto self = weak_self.lock();
            if (!self) {
                throw runtime_error("Interpreter was destroyed before constructor call");
            }
            
            // Get all constructor ASTs
            const auto& ctor_asts = class_def->get_constructor_asts();
            
            // Find constructor with matching parameter count
            std::shared_ptr<function_decl> matching_ctor;
            for (const auto& ctor_ast : ctor_asts) {
                if (ctor_ast->parameters.size() == args.size()) {
                    matching_ctor = ctor_ast;
                    break;
                }
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
                            auto result = arg_expr->accept(self.get());
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
                                    // Execute parent constructor with method environment
                                    // Use definition_env as parent
                                    scoped_method_environment parent_method_env(
                                        self.get(),
                                        definition_env,
                                        this_value
                                    );

                                    self->execute_method_ast(parent_ctor, parent_method_env.get(), init_args);
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
                                            if (cpp_instance && cpp_instance->has_field("_cpp_object")) {
                                                // Copy _cpp_object from parent to derived instance
                                                instance->set_field("_cpp_object", cpp_instance->get_field("_cpp_object"));
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
                        auto result = arg_expr->accept(self.get());
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
            self->evaluate_field_initializers(instance, class_def, init_env);

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
        
        // Register the dispatcher
        environment_->define(decl->name_id, script_value::make_function(ctor_dispatcher, engine_ref_));
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
        
        // Register default constructor
        environment_->define(decl->name_id, script_value::make_function(default_ctor_func, engine_ref_));
        // std::cerr << "DEBUG: Registered default constructor for class: " << decl->name << std::endl;
    }
    
    // If this is a redefinition, we need to call redefine_class to update all instances
    if (is_redefinition) {
        // Evaluate field initializer ASTs to get actual default values for hot reload
        std::unordered_map<std::string, script_value> field_defaults_with_engine;
        field_defaults_with_engine.reserve(new_field_defaults.size());

        for (const auto& [name, value] : new_field_defaults) {
            // Get the field initializer AST from the class definition
            auto initializer_ast = class_def->get_field_initializer_ast(name);
            script_value evaluated_value = value;

            if (initializer_ast) {
                // Evaluate the initializer AST in the current (definition) environment
                // to get the actual default value
                JAISCRIPT_TRY(initializer_ast->accept(this));
                evaluated_value = pop_value();
            }

            // Ensure the value has an engine reference
            if (evaluated_value.get_engine_ref().expired() && !engine_ref_.expired()) {
                evaluated_value.set_engine_ref(engine_ref_);
            }

            field_defaults_with_engine[name] = evaluated_value;
        }
        
        // Generate getter and setter methods for all fields (including new ones)
        // This is needed for hot reload to work properly with property access
        for (const auto& [field_name, default_val] : field_defaults_with_engine) {
            // Add getter method
            auto getter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Get the field value
                return instance->get_field(field_name);
            };
            new_methods["_get_" + field_name] = script_value::make_function(getter, engine_ref_);
            
            // Add setter method
            auto setter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Set the field value
                instance->set_field(field_name, args[1]);
                
                // Return the value that was set
                return args[1];
            };
            new_methods["_set_" + field_name] = script_value::make_function(setter, engine_ref_);
        }
        
        
        // Call redefine_class with the new field defaults and methods
        // Call redefine_class to migrate existing instances
        class_def->redefine_class(field_defaults_with_engine, new_methods, new_static_methods, engine_ref_);
    } else {
        // For new classes, add the fields normally
        for (const auto& [field_name, default_val] : new_field_defaults) {
            class_def->add_field(field_name, default_val);
            
            // Generate getter and setter methods for script class fields
            // This enables property-style access (obj.field) to work properly
            
            // Add getter method
            auto getter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.empty()) {
                    throw runtime_error("Property getter called without 'this' object");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Get the field value
                return instance->get_field(field_name);
            };
            class_def->add_method("_get_" + field_name, getter);
            
            // Add setter method
            auto setter = [field_name, weak_eng = engine_ref_](const std::vector<script_value>& args) -> script_value {
                if (args.size() != 2) {
                    throw runtime_error("Property setter requires 'this' object and value");
                }
                
                // Extract the class_instance from the first argument (this)
                auto instance = args[0].as<std::shared_ptr<class_instance>>();
                
                // Set the field value
                instance->set_field(field_name, args[1]);
                
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
        
        // Check each unresolved identifier
        std::vector<std::string> undefined_identifiers;
        for (const auto& identifier : current_class_context_->unresolved_identifiers) {
            // Skip special keywords that are always valid in methods
            if (identifier == "this" || identifier == "super") continue;
            
            // Check if it's a field
            if (all_fields.find(identifier) != all_fields.end()) continue;
            
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
    environment_->define(class_var_name, script_value::make_object("class_definition", class_definition_type_id_, class_def, engine_ref_, false));

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
        if (auto* func_decl = dynamic_cast<function_decl*>(member_decl.get())) {
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

        } else if (auto* var_decl = dynamic_cast<variable_decl*>(member_decl.get())) {
            // Intern the variable name if not already done
            if (var_decl->name_id == UINT64_MAX) {
                var_decl->name_id = string_symbolizer_->intern(var_decl->name);
            }

            // Evaluate variable initializer and store value
            if (var_decl->initializer) {
                JAISCRIPT_TRY(var_decl->initializer->accept(this));
                script_value value = pop_value();
                ns_data->variables[var_decl->name_id] = value;
            } else {
                // No initializer - store null
                ns_data->variables[var_decl->name_id] = make_value();
            }

        } else if (auto* class_decl_ptr = dynamic_cast<class_decl*>(member_decl.get())) {
            // Intern the class name if not already done
            if (class_decl_ptr->name_id == UINT64_MAX) {
                class_decl_ptr->name_id = string_symbolizer_->intern(class_decl_ptr->name);
            }

            // Process class declaration normally to register it globally
            // Then also store reference in namespace
            JAISCRIPT_TRY(class_decl_ptr->accept(this));

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
            JAISCRIPT_TRY(member_decl->accept(this));
        }
    }

    return {};
}

checked_result<void> interpreter::visit_expression_decl(expression_decl* decl) {
    // Evaluate the expression and leave the result on the stack
    // This allows top-level expressions to return values
    return decl->expression->accept(this);
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
        JAISCRIPT_TRY(decl->path_expr->accept(this));
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
        JAISCRIPT_TRY(decl->path_expr->accept(this));
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
    return call_function(script_func, args);
}

// Evaluate field initializers for a script class instance at construction time
void interpreter::evaluate_field_initializers(std::shared_ptr<class_instance> instance,
                                             std::shared_ptr<script_class_definition> class_def,
                                             std::shared_ptr<environment> init_env) {
    // First, evaluate parent class field initializers (if any)
    // Support multiple inheritance by iterating over all parent classes
    for (const auto& parent : class_def->get_parent_classes()) {
        auto parent_script_class = std::dynamic_pointer_cast<script_class_definition>(parent);
        if (parent_script_class) {
            // Recursively evaluate parent field initializers
            evaluate_field_initializers(instance, parent_script_class, init_env);
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
            auto result = initializer_ast->accept(this);
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

            // Set the field on the instance
            instance->set_field(field_name, field_value);
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

// Function call implementation
script_value interpreter::call_function(const script_defined_function& function, const std::vector<script_value>& args) {
    // Validate arguments
    validate_function_arguments(function.parameters, args);
    
    
    // Create new environment for function execution using pool optimization
    // Both lambdas and functions need a fresh environment for their parameters
    auto previousEnv = environment_;
    
    // For lambdas with closures, the execution environment needs to chain:
    // [parameter env] -> [closure env] -> [global env]
    // For regular functions:
    // [parameter env] -> [current env]
    if (function.closure_env) {
        // Check if the closure is a method_environment
        if (auto method_env = std::dynamic_pointer_cast<method_environment>(function.closure_env)) {
            // Create a new method_environment that preserves implicit 'this' lookups
            environment_ = get_pooled_method_environment(method_env->get_parent(), method_env->get_this_object());
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
    
    try {
        
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
                            throw runtime_error("Cannot take reference of undefined variable");
                        }
                        
                        // If the argument is itself a reference, get the final target
                        if (argPtr->is_reference()) {
                            auto refHolder = argPtr->get_reference_holder();
                            if (!refHolder || !refHolder->target) {
                                throw runtime_error("Reference target is null");
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
                        throw runtime_error("Cannot pass non-lvalue to reference parameter");
                    }
                } else {
                    // No metadata - can't create reference
                    throw runtime_error("Cannot pass non-lvalue to reference parameter");
                }
            } else {
                // Non-reference parameter
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
                if (arg.get_type_info() && arg.get_type_info()->base_type == script_value_type::jai_shared_ptr_type) {
                    should_share = true;
                }

                if (param.symbol_id != UINT64_MAX) {
                    if (should_share) {
                        // Shallow copy - share ownership (reference semantics)
                        environment_->define(param.symbol_id, arg);
                    } else {
                        // Deep copy - value semantics (C++-like default)
                        environment_->define(param.symbol_id, arg.clone());
                    }
                } else {
                    // Fallback to parameter name if symbol_id not set
                    if (should_share) {
                        environment_->define(param.name, arg);
                    } else {
                        environment_->define(param.name, arg.clone());
                    }
                }
            }
        }
        
        // Execute function body without creating another environment
        // (since we already created one for the function call)
        for (const auto& decl : function.body->declarations) {
            auto result = decl->accept(this);

            // Check for error codes
            if (!result) {
                environment_ = previousEnv;
                hasReturnValue_ = previousHasReturn;
                returnValue_ = previousReturn;
                return make_value();  // Return null on error
            }

            // Check if we hit a return statement and break early
            if (hasReturnValue_) {
                break;
            }
        }
        
        // Get return value
        script_value result = script_value::make_null(engine_ref_);
        bool is_constructor_with_implicit_return = false;

        if (hasReturnValue_) {
            result = std::move(returnValue_.value());
        } else {
            // Check if this is a constructor (method_environment with no explicit return)
            // Constructors implicitly return 'this'
            auto function_env = environment_;
            if (auto method_env = std::dynamic_pointer_cast<method_environment>(function_env)) {
                result = method_env->get_this_object();
                is_constructor_with_implicit_return = true;
            } else {
                // Regular function with no return statement returns null
                result = make_value();
            }
        }

        // Clear this_object_ reference for method environments to ensure timely destructors
        // IMPORTANT: We clear this for BOTH constructors and regular methods
        // For constructors, we've already grabbed the 'this' object and stored it in 'result' above,
        // so there's no reason to keep it alive in the method environment
        auto function_env = environment_;
        if (auto method_env = std::dynamic_pointer_cast<method_environment>(function_env)) {
            method_env->clear_this_reference();
        }

        environment_ = previousEnv;
        release_environment(function_env, false);

        // Restore previous state
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;

        return result;

    } catch (...) {
        // Release environment even on exception
        auto function_env = environment_;

        // Clear this_object_ reference for method environments to ensure timely destructors
        // Note: On exception, we can clear constructors too since they won't return a value anyway
        if (auto method_env = std::dynamic_pointer_cast<method_environment>(function_env)) {
            method_env->clear_this_reference();
        }

        environment_ = previousEnv;
        release_environment(function_env, false);

        // Restore state on exception
        hasReturnValue_ = previousHasReturn;
        returnValue_ = previousReturn;
        throw;
    }
}

void interpreter::validate_function_arguments(const std::vector<parameter>& params, const std::vector<script_value>& args) {
    if (params.size() != args.size()) {
        throw runtime_error("Function expected " + std::to_string(params.size()) + 
                         " arguments but got " + std::to_string(args.size()));
    }
    
    // TODO: Add type checking for parameters
    // For now, we'll just check argument count
}

script_value interpreter::make_function(std::shared_ptr<script_defined_function> func) {
    // Create a wrapper that handles reference parameters properly
    script_function wrapper = [this, func](const std::vector<script_value>& args) -> script_value {
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

    // Check if this is a method_environment and decrement the appropriate pool index
    if (std::dynamic_pointer_cast<method_environment>(env)) {
        if (method_environment_pool_index_ > 0) {
            --method_environment_pool_index_;
        }
    } else {
        if (environment_pool_index_ > 0) {
            --environment_pool_index_;
        }
    }
}

std::shared_ptr<method_environment> interpreter::get_pooled_method_environment(std::shared_ptr<environment> parent, script_value this_obj) {
    if (method_environment_pool_index_ < method_environment_pool_.size()) {
        // Reuse existing method environment from pool
        auto env = method_environment_pool_[method_environment_pool_index_++];
        // Reset with new parent and this object
        env->reset(parent, std::move(this_obj));
        return env;
    } else {
        // Pool is exhausted, create new method environment and add to pool
        auto newEnv = std::make_shared<method_environment>(parent, string_symbolizer_, std::move(this_obj));
        method_environment_pool_.emplace_back(newEnv);
        ++method_environment_pool_index_;
        return newEnv;
    }
}

void interpreter::reset_environment_pool() {
    environment_pool_index_ = 0;
    method_environment_pool_index_ = 0;

    // Clear the environment values to release references
    for (auto& env : environment_pool_) {
        // Reset the environment by clearing its parent and values
        env->reset(nullptr);
    }
    for (auto& env : method_environment_pool_) {
        // Reset the method environment (clear values and this_object to release references)
        env->reset(nullptr, script_value::make_null(engine_ref_));
    }
}

} // namespace jai
