#include <jaiscript/jaiscript.hpp>
#include <jaiscript/core/engine_time.hpp>
#include <jaiscript/core/execution_backend.hpp>
#include <jaiscript/detail/class_runtime.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/lexer.hpp>
#include <jaiscript/detail/parser.hpp>
#include <jaiscript/detail/interpreter.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <jaiscript/detail/float_text.hpp>
#include <jaiscript/serialization/json_archive.hpp>
#include <jaiscript/stdlib/json.hpp>   // reconstruct_archive_value (hybrid field hydration)
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/integer_ops.hpp>   // kCheckedOverflow (overflow policy)
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/math_namespace.hpp>
#include <jaiscript/detail/array_namespace.hpp>
#include <jaiscript/detail/aggregate_lowering.hpp>
#include <jaiscript/detail/operator_table.hpp>
#include <jaiscript/detail/execution_limits.hpp>
#include <jaiscript/detail/parallel_transform.hpp>
#include <jaiscript/detail/ast_serializer.hpp>   // jaibite save/load
#include <jaiscript/detail/structural_equality.hpp>
#include <jaiscript/detail/format_spec.hpp>       // format_value builtin (template-string specs)
#include <jaiscript/detail/binary_data.hpp>
#include <jaiscript/debug/controller.hpp>         // engine::debugger()
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>
#include <jaiscript/detail/arg_capture.hpp>
#include <jaiscript/detail/math_component_store.hpp>
#include <jaiscript/detail/release_machine.hpp>
#ifdef JAISCRIPT_VM_COVERAGE
#include <jaiscript/vm/coverage.hpp>
#include <mutex>
#endif
#include <charconv>
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <filesystem>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

// One-syscall (mtime, size) stat for the execute_file fast lane.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace jai {
// the engine's classes by name: transparent lookups, so a type name held as an interned view finds its class without a copy
struct class_name_hash { using is_transparent = void; size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); } };
using class_name_map = std::unordered_map<std::string, std::shared_ptr<class_definition>, class_name_hash, std::equal_to<>>;

// Factory defined by the VM backend TU (source/implementation/vm/vm_backend.cpp)
std::unique_ptr<execution_backend> create_vm_backend();

namespace {

// A checked error leaves a backend as "<text>: <category>" (std::system_error's what()). A text says everything the
// category would: the host sees the text alone, and the category only for an error that carries no text.
script_exception without_category_suffix(const script_exception& e) {
    if (e.thrown) { return e; }
    const std::string_view text = e.what();
    const auto& errors = runtime_error_category();
    // matched as a whole suffix: a category text may itself hold ": "
    for (int ev = 1; ev <= static_cast<int>(runtime_error_code::coroutine_suspended); ++ev) {
        const std::string category = errors.message(ev);
        if (text.size() > category.size() + 2 && text.ends_with(category) &&
            text.substr(text.size() - category.size() - 2, 2) == ": ") {
            return script_exception(std::string(text.substr(0, text.size() - category.size() - 2)), e.location());
        }
    }
    return e;
}

}

checked_result<script_value> script_callable_thunk::operator()(const std::vector<script_value>& args) const {
    // bound_method: make_bound_method's EXACT opaque behavior (prepend the receiver,
    // call the dispatch value, anchor a non-owning result on the receiver) — no
    // host-boundary wrapping, matching the anonymous-lambda mint this replaces. Hot
    // vm call paths never get here (they recover the payload and enter in-loop).
    if (payload.kind == script_callable::kind_type::bound_method) {
        const script_value& this_obj = *payload.this_obj;
        std::vector<script_value> method_args;
        method_args.reserve(args.size() + 1);
        method_args.push_back(this_obj);
        method_args.insert(method_args.end(), args.begin(), args.end());

        const auto& method_func = payload.bound_dispatch->as_function();
        auto result = method_func(method_args);

        if (result && this_obj.is_object()) {
            script_value& rv = result.value();
            if (rv.is_non_owning_object()) {
                auto rv_holder = rv.get_object_holder();
                auto recv_holder = this_obj.get_object_holder();
                if (rv_holder && recv_holder) {
                    rv_holder->keep_alive = recv_holder->data ? recv_holder->data
                                                              : recv_holder->keep_alive;
                }
            }
        }
        return result;
    }
    if (!eng) {
        return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
    }
    struct call { const script_callable& payload; const std::vector<script_value>& args; } c{payload, args};
    return eng->checked_host_entry([](execution_backend& be, void* ctx) {
        auto* cc = static_cast<call*>(ctx);
        return be.execute_callable(cc->payload, cc->args);
    }, &c);
}

checked_result<script_value> engine::checked_host_entry(host_entry_body run, void* context) {
    execution_backend* backend = calling_backend();
    if (!backend) {
        return checked_result<script_value>(make_error_code(runtime_error_code::engine_destroyed), "Engine backend unavailable");
    }
    // Mid-script host callbacks keep the latched unwinding flag: the calling script's catch must still see the thrown VALUE
    if (backend->is_executing() || backend->is_unwinding()) { return run(*backend, context); }
    note_host_entry();
    backend->begin_host_call();
    const auto failure = [&](const char* text) {
        backend->prepare_for_execution();
        return checked_result<script_value>(make_error_code(runtime_error_code::evaluation_failed), "{0}", park_error_text(text));
    };
    try {
        auto result = run(*backend, context);
        if (result.has_value() && backend->is_unwinding()) [[unlikely]] {   // an uncaught script throw: its text, as execute() throws it
            return failure(without_category_suffix(backend->get_current_exception()).what());
        }
        return result;
    } catch (const std::exception& e) {   // a nested run's failure (an import that is missing or fails its check) leaves as an exception
        return failure(e.what());
    }
}

namespace {

const char* spec_value_type_name(const script_value& v) {
    switch (v.type()) {
        case script_value_type::jai_null_type: return "null";
        case script_value_type::jai_bool_type: return "bool";
        case script_value_type::jai_int_type: return "int";
        case script_value_type::jai_float_type: return "float";
        case script_value_type::jai_char_type: return "char";
        case script_value_type::jai_string_type: return "string";
        case script_value_type::jai_utf8_string_type: return "utf8_string";
        case script_value_type::jai_array_type: return "array";
        case script_value_type::jai_map_type: return "map";
        case script_value_type::jai_function_type: return "function";
        case script_value_type::jai_object_type: return "object";
        case script_value_type::jai_shared_ptr_type: return "object";
        case script_value_type::jai_reference_type: return "reference";
        case script_value_type::jai_weak_ptr_type: return "weak_ptr";
        case script_value_type::jai_vec2_type:
        case script_value_type::jai_vec3_type:
        case script_value_type::jai_vec4_type:
        case script_value_type::jai_color_type:
        case script_value_type::jai_ivec2_type:
        case script_value_type::jai_ivec3_type: return math_type_name(v.type());
        default: return "unknown";
    }
}

std::string spec_format_int(script_int v, char type) {
    char buf[72];   // 64 binary digits + sign fits easily
    int base = type == 'b' ? 2 : (type == 'x' || type == 'X') ? 16 : 10;
    auto res = std::to_chars(buf, buf + sizeof(buf), v, base);
    std::string s(buf, res.ptr);
    if (type == 'X') {
        for (auto& c : s) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
    }
    return s;
}

std::string spec_format_fixed(script_float v, int precision) {
    // Worst case: sign + 309 integral digits + '.' + capped precision (1024)
    if (std::isnan(v)) { return std::string(detail::k_nan_text); }
    char buf[1500];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, precision);
    return std::string(buf, res.ptr);
}

std::string spec_format_float(script_float v, std::chars_format fmt, int precision) {
    if (std::isnan(v)) { return std::string(detail::k_nan_text); }
    char buf[1500];
    auto res = std::to_chars(buf, buf + sizeof(buf), v, fmt, precision);
    return std::string(buf, res.ptr);
}

} // namespace

// Single runtime formatter behind `${expr:spec}` splices and stdlib {:spec} placeholders
// (declared in detail/format_spec.hpp). One definition = byte-identical output and error
// text on both backends.
std::string detail::format_value_with_spec(const script_value& value, const format_spec& spec, std::string_view raw_spec) {
    const script_value& v = value.is_reference() ? value.deref() : value;
    const bool numeric = v.is_int() || v.is_float();
    std::string body;
    if ((spec.sign || spec.zero) && !numeric) {
        throw runtime_error("format spec ':" + std::string(raw_spec) + "' does not apply to " + spec_value_type_name(v) + " value");
    }
    if (spec.type == 'x' || spec.type == 'X' || spec.type == 'b' || spec.type == 'd') {
        if (!v.is_int()) {
            throw runtime_error("format spec ':" + std::string(raw_spec) + "' does not apply to " + spec_value_type_name(v) + " value");
        }
        body = spec_format_int(v.as_int(), spec.type);
    } else if (spec.type == 'e' || spec.type == 'g') {
        if (!numeric) {
            throw runtime_error("format spec ':" + std::string(raw_spec) + "' does not apply to " + spec_value_type_name(v) + " value");
        }
        const int precision = spec.precision >= 0 ? spec.precision : 6;   // std::format's {:e} / {:g} default
        const script_float f = v.is_int() ? static_cast<script_float>(v.as_int()) : v.as_float();
        body = spec_format_float(f, spec.type == 'e' ? std::chars_format::scientific : std::chars_format::general, spec.type == 'g' && precision == 0 ? 1 : precision);
    } else if (spec.type == 'f' || spec.precision >= 0) {
        if (!numeric) {
            throw runtime_error("format spec ':" + std::string(raw_spec) + "' does not apply to " + spec_value_type_name(v) + " value");
        }
        const int precision = spec.precision >= 0 ? spec.precision : 6;   // std::format's {:f} default
        body = spec_format_fixed(v.is_int() ? static_cast<script_float>(v.as_int()) : v.as_float(), precision);
    } else {
        body = v.to_string();
    }
    if (numeric && spec.sign && !body.empty() && body[0] != '-') {
        body.insert(0, 1, spec.sign);   // '+' or ' ' before a non-negative number
    }
    if (numeric && spec.zero && !spec.align && static_cast<int>(body.size()) < spec.width) {
        // zero padding goes after the sign
        const size_t at = !body.empty() && (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0;
        body.insert(at, static_cast<size_t>(spec.width) - body.size(), '0');
    }
    if (static_cast<int>(body.size()) < spec.width) {
        const size_t pad = static_cast<size_t>(spec.width) - body.size();
        const char align = spec.align ? spec.align : (numeric ? '>' : '<');
        if (align == '<') {
            body.append(pad, spec.fill);
        } else if (align == '>') {
            body.insert(0, pad, spec.fill);
        } else {   // '^' — like std::format, the extra fill char goes to the right
            const size_t left = pad / 2;
            body.insert(0, left, spec.fill);
            body.append(pad - left, spec.fill);
        }
    }
    return body;
}

// Cost of matching args[first_arg .. first_arg+paramTypes.size()) against paramTypes, using the
// same ranking as free-function overload resolution: exact 0 > int<->float small > convertible >
// object/inheritance, with a non-match returning -1. Single source of truth shared by
// OverloadSet::findBestMatch (free functions / constructors, first_arg = 0) and
// engine::select_cpp_overload (same-arity C++ method dispatch, first_arg = 1 to skip 'this').
// Cost of an object argument for an object, shared_ptr<T> or weak_ptr<T> parameter, or -1 when its class cannot reach T
static int object_param_cost(
        const script_value& arg,
        script_value_type argType,
        const param_type_info& paramInfo,
        const std::unordered_map<std::type_index, std::shared_ptr<class_definition>>* classes_by_type,
        const class_name_map* classes_by_name) {
    if (!classes_by_type || paramInfo.cpp_type == std::type_index(typeid(void))) {
        return 50;                               // no C++ type info - typed overloads preferred
    }
    auto paramClassIt = classes_by_type->find(paramInfo.cpp_type);
    if (paramClassIt == classes_by_type->end()) {
        return 100;                              // expected class not found - accept with high cost
    }
    const std::string& expectedClassName = paramClassIt->second->get_name();
    auto instance = const_cast<script_value&>(arg).get_class_instance();
    class_definition* argClassDef = nullptr;
    std::optional<std::string_view> argClassName;
    if (instance) {
        argClassDef = instance->get_class_definition();
        argClassName = instance->get_class_name();
    } else {
        // cpp_bound / raw C++ holders carry the registered name on the holder
        auto holder = arg.get_object_holder();
        if (holder && !holder->type_name.empty()) {
            argClassName = holder->type_name;
            if (classes_by_name) {
                auto argClassIt = classes_by_name->find(holder->type_name);
                if (argClassIt != classes_by_name->end()) {
                    argClassDef = argClassIt->second.get();
                }
            }
        }
    }
    if (argClassDef) {
        if (argClassDef->get_name() == expectedClassName) {
            // Exact match; shared_ptr<T> -> T counts as a small conversion.
            return argType == script_value_type::jai_shared_ptr_type && paramInfo.base_type == script_value_type::jai_object_type ? 1 : 0;
        }
        if (argClassDef->is_subtype_of(expectedClassName)) { return 1; }
        // a layer speaks for its object as the object's own C++ type too: a Sprite under class Ext : Node is a Sprite
        if (instance && argClassDef->is_script_class() && classes_by_name) {
            const script_value* native = instance->native();
            const auto* native_holder = native ? native->peek_object_holder() : nullptr;
            if (native_holder && !native_holder->is_class_instance_wrapper && !native_holder->type_name.empty()) {
                auto stored = classes_by_name->find(native_holder->type_name);
                if (stored != classes_by_name->end() &&
                    (stored->second->get_name() == expectedClassName || stored->second->is_subtype_of(expectedClassName))) {
                    return 1;
                }
            }
        }
        return -1;
    }
    if (argClassName) {
        return *argClassName == expectedClassName ? 0 : -1;
    }
    return -1;                                   // no class identity - not a valid object
}

static int compute_param_match_cost(
        const std::vector<script_value>& args,
        size_t first_arg,
        const std::vector<param_type_info>& paramTypes,
        const conversions::conversion_registry* convReg,
        const std::unordered_map<std::type_index, std::shared_ptr<class_definition>>* classes_by_type,
        const class_name_map* classes_by_name) {
    int totalCost = 0;
    for (size_t i = 0; i < paramTypes.size(); ++i) {
        const auto& paramInfo = paramTypes[i];
        // Lvalue call args (arr[i], obj.field) arrive as references: match their targets
        const script_value& arg = args[first_arg + i].deref();
        script_value_type argType = arg.type();

        // A wildcard (script_value) parameter accepts any argument at a small fixed cost — low
        // enough to stay viable, high enough that a concrete-typed overload still wins.
        if (paramInfo.base_type == script_value_type::jai_null_type) {
            totalCost += 5;
            continue;
        }

        const bool handleParam = paramInfo.base_type == script_value_type::jai_shared_ptr_type ||
                                 paramInfo.base_type == script_value_type::jai_weak_ptr_type;
        // null converts to an empty handle, as nullptr does in C++ (a typed handle variable may hold null too)
        if (handleParam && arg.is_null()) {
            totalCost += 1;
            continue;
        }

        if ((paramInfo.base_type == script_value_type::jai_object_type || handleParam) &&
            (argType == script_value_type::jai_object_type ||
             argType == script_value_type::jai_shared_ptr_type)) {
            const int cost = object_param_cost(arg, argType, paramInfo, classes_by_type, classes_by_name);
            if (cost < 0) {
                return -1;
            }
            // shared_ptr<T> -> weak_ptr<T> is a conversion
            totalCost += cost + (paramInfo.base_type == script_value_type::jai_weak_ptr_type ? 1 : 0);
        } else if (paramInfo.base_type == script_value_type::jai_weak_ptr_type && argType == script_value_type::jai_weak_ptr_type) {
            auto locked = arg.get_weak_ptr().lock();
            if (!locked) {
                totalCost += 1;                  // an expired weak_ptr is an empty one
                continue;
            }
            script_value target(std::monostate{}, arg.get_engine());
            target.set_object_holder(locked);
            const int cost = object_param_cost(target, script_value_type::jai_shared_ptr_type, paramInfo, classes_by_type, classes_by_name);
            if (cost < 0) {
                return -1;
            }
            totalCost += cost;
        } else if (is_math_type(paramInfo.base_type)) {
            // an adapted host parameter takes its kind exactly and the other precision by the typed-slot table
            if (argType == paramInfo.base_type) { continue; }
            if (!detail::math::converts(argType, paramInfo.base_type)) { return -1; }
            totalCost += 1;
        } else {
            int cost = convReg ? convReg->get_builtin_conversion_cost(argType, paramInfo.base_type)
                               : (argType == paramInfo.base_type ? 0 : 1000);
            if (cost >= 1000) {
                return -1;
            }
            totalCost += cost;
        }
    }
    return totalCost;
}

struct engine::implementation {
    // Engine-thread state. A C++ callback may change grants before returning to
    // script; parallel regions keep them immutable until their workers join.
    uint64_t privileges_ = static_cast<uint64_t>(privilege::filesystem);
    // Per-engine reference_holder block pool, type-erased (value.cpp owns the type).
    // Before other owning members so it is destroyed LAST - members releasing reference
    // values on teardown returns its blocks to a live pool. The teardown orphans the
    // pool if user-held references still exist; the last release then frees it.
    struct ref_holder_pool_slot_holder {
        void* pool = nullptr;
        void (*teardown)(void*) = nullptr;
        ~ref_holder_pool_slot_holder() {
            if (pool && teardown) {
                teardown(pool);
            }
        }
    } ref_holder_pool_;
    ref_holder_pool_slot_holder object_holder_pool_;
    // Outlives every value-owning member below: their releases at teardown can still run on it
    detail::release_machine release_;

    // Unified conversion registry (replaces both type_conversions and custom_conversions)
    std::shared_ptr<conversions::conversion_registry> conversions;

    // Engine-owned deterministic RNG for the math:: intrinsics (fixed default seed)
    detail::math_rng_state math_rng_;

    std::array<engine::math_alias_row, engine::k_math_alias_rows> math_alias_rows_{};
    std::unordered_map<uint64_t, uint8_t> math_alias_symbols_;
    std::vector<engine::host_direct_row> host_direct_rows_;
    const uint64_t host_direct_generation_ = 0;

    // Current parameter storage for function calls (replaces thread_local)
    detail::parameter_storage* current_parameter_storage = nullptr;

    // Script execution budget in seconds (0 = unlimited); mirrored into the backend
    double execution_budget_seconds_ = 5.0;

    // Per-engine execution-limit state (terminal latch, memory accounting): the active
    // backend caches a pointer, so reentrant executes share one instance
    detail::execution_limits limits_;

    // Parallel machinery (parallel_transform v0): null until first touched, so an engine
    // that never runs a region carries only this pointer
    std::unique_ptr<detail::parallel_engine_state> parallel_;

    // Custom output stream for print() - nullptr means use std::cout
    std::shared_ptr<std::ostream> output_stream;

    // Sink for script errors surfacing at the C++ boundary - nullptr means std::cerr
    std::function<void(const std::string&)> script_error_handler;

    // Hybrid reverse lookup (complete C++ object pointer -> owning script instance),
    // weak entries swept lazily when the watermark is crossed
    // Script layers of objects whose type carries no script_extensible slot, keyed by the
    // pointer stored in _cpp_object (internal/script_extensible.md D3).
    std::unordered_map<const void*, std::weak_ptr<class_instance>> script_layers_;
    std::unordered_set<std::string> serial_functions_;
    std::unordered_set<std::string> host_function_names_;
    int library_registration_depth_ = 0;
    size_t script_layer_sweep_watermark_ = 64;
    // Layers of slotless objects extended through this engine: nothing else can own them,
    // so the engine does until script_layer_detach (the layer keeps its object alive).
    std::unordered_map<const void*, std::shared_ptr<class_instance>> owned_script_layers_;

    // Sink for advisory warnings (builtin-method shadowing) - nullptr means std::cerr
    std::function<void(const std::string&)> script_warning_handler;

    // Serialization registry for class metadata (non-static to ensure test isolation)
    serialization::serialization_registry serialization_registry;
    
    // Class registry for both C++ and script classes (replaces global singleton)
    class_registry class_registry_;
    

    // Structure to hold overloaded functions with type information
    struct OverloadSet {
        // Marker for variadic functions that accept any number of arguments
        static constexpr size_t VARIADIC_ARITY = SIZE_MAX;

        struct Overload {
            size_t argCount;
            script_value function;
            std::vector<param_type_info> paramTypes; // Extended type signature for type-based matching
            std::function<bool(const std::vector<script_value>&)> typeMatcher; // Custom type matcher
            engine::host_direct_entry direct;
            const void* direct_target = nullptr;   // the binding inside function that direct.invoke runs

            Overload(size_t count, const script_value& func, const std::vector<param_type_info>& types = {})
                : argCount(count), function(func), paramTypes(types) {}

            bool is_variadic() const { return argCount == VARIADIC_ARITY; }
        };
        
        std::vector<Overload> overloads;
        uint64_t generation = 0;   // moves on every change to overloads (a vm call site's direct lane compares it)

        // Reference to the conversion registry and class lookups
        std::shared_ptr<const conversions::conversion_registry> conversions;
        std::unordered_map<std::type_index, std::shared_ptr<class_definition>>* classes_by_type = nullptr;
        class_name_map* classes_by_name = nullptr;

        OverloadSet() : conversions() {}

        void set_conversion_registry(std::shared_ptr<const conversions::conversion_registry> registry) {
            conversions = registry;
        }

        void set_class_lookup(std::unordered_map<std::type_index, std::shared_ptr<class_definition>>* lookup,
                              class_name_map* name_lookup) {
            classes_by_type = lookup;
            classes_by_name = name_lookup;
        }

        // Convert legacy script_value_type vector to param_type_info vector
        void add_overload(size_t argCount, const script_value& func, const std::vector<script_value_type>& types = {}) {
            std::vector<param_type_info> paramTypes;
            paramTypes.reserve(types.size());
            for (auto t : types) {
                paramTypes.emplace_back(t);
            }
            add_overload_with_types(argCount, func, paramTypes);
        }

        void add_overload_with_types(size_t argCount, const script_value& func, const std::vector<param_type_info>& paramTypes,
                                     engine::host_direct_entry direct = {}) {
            ++generation;
            resolved_[0] = resolved_[1] = resolved_shape{};
            auto set_direct = [&](Overload& o) {
                o.direct = direct;
                o.direct_target = direct.locate ? direct.locate(o.function.as_function()) : nullptr;
                if (!o.direct_target) { o.direct = {}; }
            };
            // If we have type info, check for exact type match first
            if (!paramTypes.empty()) {
                auto it = std::find_if(overloads.begin(), overloads.end(),
                    [&](const Overload& o) {
                        return o.argCount == argCount && o.paramTypes == paramTypes;
                    });
                if (it != overloads.end()) {
                    it->function = func;
                    set_direct(*it);
                    return;
                }
            } else {
                // No type info - remove any existing overload with same arg count and no type info
                auto it = std::find_if(overloads.begin(), overloads.end(),
                    [argCount](const Overload& o) {
                        return o.argCount == argCount && o.paramTypes.empty();
                    });
                if (it != overloads.end()) {
                    it->function = func;
                    set_direct(*it);
                    return;
                }
            }
            
            overloads.emplace_back(argCount, func, paramTypes);
            set_direct(overloads.back());
        }
        
        // The overload an argument shape (the count and each argument's type, no object among them: an object's cost
        // follows its class) selects is fixed while the set is: the newest two shapes are remembered, an added overload
        // forgets them. Only the engine thread caches (may_cache); a parallel worker resolves in full.
        struct resolved_shape { uint64_t shape = 0; uint32_t index = 0; };
        mutable resolved_shape resolved_[2];

        static uint64_t argument_shape(const std::vector<script_value>& args) {
            if (args.size() > 7) { return 0; }
            uint64_t shape = args.size() + 1;
            for (size_t i = 0; i < args.size(); ++i) {
                const script_value_type t = args[i].deref().type();
                if (t == script_value_type::jai_object_type || t == script_value_type::jai_shared_ptr_type) { return 0; }
                shape |= static_cast<uint64_t>(static_cast<uint8_t>(t)) << (8 + 8 * i);
            }
            return shape;
        }

        // C++-style resolution in one pass: the viable overload of lowest cost, the first of equal costs (a variadic one
        // costs 2000, an untyped one 1000, a typed one the shared scorer's sum)
        script_value findBestMatch(const std::vector<script_value>& args, bool may_cache = false) const {
            const size_t argCount = args.size();
            const uint64_t shape = may_cache && overloads.size() > 1 ? argument_shape(args) : 0;
            if (shape) {
                for (const resolved_shape& r : resolved_) {
                    if (r.shape == shape) { return overloads[r.index].function; }
                }
            }
            const Overload* best = nullptr;
            int best_cost = 0;
            for (const auto& overload : overloads) {
                int cost;
                if (overload.is_variadic()) {
                    cost = 2000;
                } else if (overload.argCount != argCount) {
                    continue;
                } else if (overload.paramTypes.empty()) {
                    cost = 1000;
                } else {
                    cost = compute_param_match_cost(args, 0, overload.paramTypes, conversions.get(), classes_by_type, classes_by_name);
                    if (cost < 0) { continue; }
                }
                if (!best || cost < best_cost) {
                    best = &overload;
                    best_cost = cost;
                }
            }
            if (!best) {
                return script_value::make_null(static_cast<engine*>(nullptr));   // the dispatcher names the miss (detail::overload_miss)
            }
            if (shape) {
                resolved_[1] = resolved_[0];
                resolved_[0] = {shape, static_cast<uint32_t>(best - overloads.data())};
            }
            return best->function;
        }
    };
    
    // Track which globals should NOT be serialized (most are serializable by default)
    // Uses string_view pointing into symbolizer storage for efficiency
    std::unordered_set<std::string_view, sv_hash, sv_equal> nonSerializableGlobals;
    
    // Overloaded functions (support multiple functions with same name)
    std::unordered_map<std::string, OverloadSet> overloadedFunctions;
    
    // Registered classes
    class_name_map classes;

    // Type ID to class definition mapping for fast lookups (type_id is interned string)
    std::unordered_map<uint64_t, std::shared_ptr<class_definition>> classesByTypeId;

    // Type index to class definition mapping for efficient lookups
    std::unordered_map<std::type_index, std::shared_ptr<class_definition>> classesByType;

    // Derived classes whose base_class<Base>() ran before Base registered (registrar
    // iteration order is type_index order, not dependency order) — linked when the
    // base's definition arrives in register_class_by_type.
    std::unordered_map<std::type_index, std::vector<std::shared_ptr<class_definition>>> pendingBaseLinks;
    
    // Function arity info for proper overloading
    std::unordered_map<std::string, size_t> functionArities;
    
    // Shared string symbolizer for consistent variable name mapping
    string_symbolizer string_symbolizer_;

    // Cached symbol IDs for common type names (initialized in constructor)
    uint64_t class_definition_type_id_;

    // Cached symbol IDs for operator overloading (initialized in initialize_engine_reference)
    uint64_t op_plus_id_ = 0;
    uint64_t op_minus_id_ = 0;
    uint64_t op_star_id_ = 0;
    uint64_t op_slash_id_ = 0;
    uint64_t op_percent_id_ = 0;
    uint64_t op_less_id_ = 0;
    uint64_t op_less_equal_id_ = 0;
    uint64_t op_greater_id_ = 0;
    uint64_t op_greater_equal_id_ = 0;
    uint64_t op_equal_equal_id_ = 0;
    uint64_t op_bang_equal_id_ = 0;
    uint64_t op_spaceship_id_ = 0;
    uint64_t op_ampersand_id_ = 0;
    uint64_t op_pipe_id_ = 0;
    uint64_t op_caret_id_ = 0;
    uint64_t op_left_shift_id_ = 0;
    uint64_t op_right_shift_id_ = 0;
    uint64_t post_load_id_ = 0;

    // Type name registry for custom classes (maps typeid name to user-friendly name)

    // Composite key for type interning - avoids string construction on lookup
    struct type_key {
        script_value_type base_type;
		bool dynamic_slot = false;
		type_key(script_value_type a_base, uint64_t a_first = 0, uint64_t a_second = 0, bool a_dynamic = false) :
			base_type(a_base), dynamic_slot(a_dynamic), param1_id(a_first), param2_id(a_second) {}
        uint64_t param1_id = 0;  // For array<T>, map<K,_>, weak_ptr<T>, reference<T>, etc.
        uint64_t param2_id = 0;  // For map<_,V>

        bool operator==(const type_key& other) const {
            return base_type == other.base_type && dynamic_slot == other.dynamic_slot &&
                   param1_id == other.param1_id &&
                   param2_id == other.param2_id;
        }
    };

    struct type_key_hash {
        size_t operator()(const type_key& k) const {
            size_t h = std::hash<int>{}(static_cast<int>(k.base_type));
            h ^= k.param1_id + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= k.param2_id + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<size_t>(k.dynamic_slot) << 1;
            return h;
        }
    };

    // Type info storage - engine owns all type_info objects (raw pointers are safe because map doesn't invalidate on insert)
    std::unordered_map<type_key, type_info, type_key_hash> type_infos_;

    // Secondary index for O(1) lookup by type ID (stores pointers into type_infos_)
    std::unordered_map<uint64_t, type_info*> type_id_index_;

    // Commonly used type_info pointers for fast access
    type_info* type_info_int_ = nullptr;
    type_info* type_info_float_ = nullptr;
    type_info* type_info_string_ = nullptr;
    type_info* type_info_utf8_string_ = nullptr;
    type_info* type_info_bool_ = nullptr;
    type_info* type_info_char_ = nullptr;
    type_info* type_info_uint8_ = nullptr;
    type_info* type_info_int8_ = nullptr;
    type_info* type_info_void_ = nullptr;
    type_info* type_info_invalid_ = nullptr;
    type_info* type_info_any_ = nullptr;
    type_info* type_info_reference_null_ = nullptr;   // interned "null&" twin (get_type_info_reference fast path)
    type_info* type_info_untyped_function_ = nullptr;

    
    // Template type registry for parsing (stores base template names like "Point", "MyMap")
    std::unordered_set<std::string> registeredTemplateTypes;
    
    // Global environment shared with interpreter
    std::shared_ptr<environment> global_environment_;
    
    execution_backend_ptr backend;   // constructed lazily via engine::backend()
    backend_type current_backend_type = backend_type::vm;
    bool has_executed_ = false; // Once true, set_backend is forbidden (defined callables capture the backend)
    bool tearing_down = false;  // engine dtor: backend() must not reconstruct for late thunks
    bool custom_numeric_ops_flag_ = false; // sticky signal from add_function; applied at wiring
    // Sticky: ANY binary-operator / "[]" symbol registered as a global function. While set,
    // transient subscript reads keep minting references (detail/transient_read.hpp gate).
    bool custom_binary_ops_flag_ = false;

    // Flat operator dispatch table (detail/operator_table.hpp - THE operator enumeration):
    // one mask test + array read replaces every env/string probe on operator paths in both
    // backends. Holds the current global dispatcher per operator; overload/arity/type
    // matching stays inside that value.
    detail::engine_operator_table operator_table_;

    void latch_custom_binary_ops() {
        if (!custom_binary_ops_flag_) {
            custom_binary_ops_flag_ = true;
            if (backend) {
                backend->set_has_custom_binary_ops(true);
            }
        }
    }

    // Sync the table with the global environment after a registration. Class operator
    // METHODS deliberately do not participate: an engine-wide latch would kill the
    // transient-read elision for any host binding operator types (dynamic_binder
    // overloading), and elided operands match the interpreter's historical
    // read-at-evaluation order (see transient_read.hpp).
    void refresh_operator_entry(std::string_view name) {
        detail::op_slot slot = detail::op_slot_for_name(name);
        if (slot == detail::op_slot::none) {
            return;
        }
        auto val = global_environment_->get(string_symbolizer_.intern(name));
        if (val && val.value().is_function()) {
            operator_table_.set(slot, std::move(val.value()));
            latch_custom_binary_ops();
        } else {
            // Operator name rebound to a non-function: dispatch falls back to builtins
            // (matches the old contains+is_function probe); the transient gate stays
            // conservatively latched
            operator_table_.clear(slot);
        }
    }

    // Step-debugger controller, lazily created on first engine::debugger() call.
    std::unique_ptr<debug::controller> debugger_;
    // Optional debug transport (e.g. a DAP socket server). Stopped before teardown.
    std::shared_ptr<debug::transport> debug_transport_;

    // Script namespace registry (engine-owned so namespaces survive across backends)
    std::unordered_map<uint64_t, std::shared_ptr<script_namespace_data>> script_namespaces_;
    std::unordered_set<uint64_t> enum_names_;

    // Include/Import support
    std::vector<std::string> include_paths;
    engine::import_behavior import_behavior = engine::import_behavior::file_timestamp; // Default
    
    struct closure_stamp {
        std::string path;
        uint64_t mtime = 0;
        uint64_t size = 0;
    };
    struct import_record {
        std::string resolved_path;
        // (mtime, size) stamp via stat_mtime_size, matching the script cache: 100 ns
        // FILETIME on Windows on every compiler - libstdc++'s last_write_time answers
        // whole SECONDS there, which missed same-second edits (save + import inside
        // one second never re-imported under GCC builds)
        uint64_t mtime = 0;
        uint64_t size = 0;
        uint64_t checked_pass = 0;   // the check pass that last looked at the stamp
        // a check failure at this stamp: the module (and a group naming it) waits until the file changes again
        bool failed = false;
        uint64_t failed_mtime = 0;
        uint64_t failed_size = 0;
        bool missing = false;   // its file was gone at a check (raised once): it waits until a check finds the file again
        uint64_t runs = 0;      // runs started: a nested module queued to re-run is skipped when something ran it meanwhile
        // the rest of the module's closure: every file its last run included (top level, nested, spliced into a function
        // body), each stamped as it was read; a pass queries each like the module itself
        std::vector<closure_stamp> closure;
        // the modules its last run imported: a pass that checks this module checks them too, and a changed one re-runs alone
        std::vector<std::string> children;
    };
    std::unordered_map<std::string, import_record> import_cache;
    // the engine-wide import check pass (engine::import_recheck_interval): the window in steady ticks, the generation every
    // setting that changes what an import would do moves (a site that loaded under another generation resolves again), the
    // pass counter, the reach counter that decides when the coarse clock is refreshed, the filesystem query counter
    int64_t import_recheck_ticks_ = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(200)).count();
    double import_recheck_seconds_ = 0.2;
    uint64_t import_generation_ = 1;
    uint64_t import_pass_id_ = 0;
    uint64_t import_stats_ = 0;
    std::string cwd_;   // the working directory resolution keys use, refreshed when a check pass opens
    // caught errors (engine::make_caught_error): the catch variable is its message as a string whose node keys the error's
    // place, stack and thrown value; an entry goes when nothing but this table holds the string
    struct caught_error {
        script_value text;
        bool has_site = false;   // a throw's own place; else the innermost frame's statement, read from the trace
        std::string site_file;
        size_t site_line = 0;
        size_t site_column = 0;
        std::vector<stack_frame> trace;   // spelled when e.stack / e.file is read
        script_value value;
    };
    std::unordered_map<const void*, caught_error> caught_errors_;
    size_t caught_errors_sweep_at_ = 32;   // twice the live count after a sweep: a catch per level of a deep recursion sweeps O(1) amortized
    struct resolution_entry {
        std::vector<std::string> misses;   // the earlier rungs' candidates, which held no file
        std::string candidate;             // the rung that answered
        std::string resolved;              // its canonical spelling
    };
    std::unordered_map<std::string, resolution_entry> resolution_memo_;
    std::optional<std::array<std::unordered_set<uint64_t>, 4>> builtin_method_ids_;
    std::unordered_set<std::string> importing_;   // resolved paths mid-execution: a cyclic import's back edge is a no-op
    std::unordered_map<std::string, std::string> import_vanished_;   // resolution key -> the module whose file the ladder lost
    struct import_frame {
        std::vector<closure_stamp> files;    // the files its run included
        std::vector<std::string> children;   // the modules its run imported
    };
    std::vector<import_frame> import_frames_;   // per running import, innermost last

    // a file the running import read: a second read under another stamp leaves a zero stamp, which the next pass sees moved
    void note_closure_file(const std::string& path, bool stamped, uint64_t mtime, uint64_t size) {
        if (import_frames_.empty()) { return; }
        if (!stamped) { mtime = 0; size = 0; }
        auto& closure = import_frames_.back().files;
        for (auto& s : closure) {
            if (s.path != path) { continue; }
            if (s.mtime != mtime || s.size != size) { s.mtime = 0; s.size = 0; }
            return;
        }
        closure.push_back({path, mtime, size});
    }
    void note_import_child(const std::string& resolved) {
        if (import_frames_.empty()) { return; }
        auto& children = import_frames_.back().children;
        if (std::find(children.begin(), children.end(), resolved) == children.end()) { children.push_back(resolved); }
    }
    import_frame end_import_frame() {
        import_frame frame = std::move(import_frames_.back());
        import_frames_.pop_back();
        return frame;
    }
    // a run that failed keeps the closure it had and adds what it read, so fixing any file it touched re-runs it
    void merge_import_frame(const std::string& resolved, const import_frame& read) {
        auto rec = import_cache.find(resolved);
        if (rec == import_cache.end()) { return; }
        for (const auto& s : read.files) {
            bool known = false;
            for (auto& k : rec->second.closure) { if (k.path == s.path) { k = s; known = true; break; } }
            if (!known) { rec->second.closure.push_back(s); }
        }
        auto& children = rec->second.children;
        for (const auto& c : read.children) {
            if (std::find(children.begin(), children.end(), c) == children.end()) { children.push_back(c); }
        }
    }
    bool closure_moved(const import_record& record) {
        for (const auto& s : record.closure) {
            ++import_stats_;
            uint64_t mtime = 0, size = 0;
            if (!stat_mtime_size(s.path, mtime, size) || mtime != s.mtime || size != s.size) { return true; }
        }
        return false;
    }

    // Source-string execution cache: re-executing identical source skips lex/parse
    // (both backends) and bytecode compilation (the vm fills the compiled slot), with
    // the same semantics as re-executing a jaibite. Bounded LRU; parse-affecting
    // registrations (template types, classes) bump the epoch and invalidate.
    struct script_cache_entry {
        std::vector<declaration_ptr> declarations;
        std::string source_path;   // the spelling the unit was parsed under ("<script>" for raw content)
        std::shared_ptr<void> compiled;
        std::unordered_map<uint32_t, std::shared_ptr<void>> textual_compiled;   // an include's program per including unit: a chunk belongs to one unit (internal/global_shadowing.md)
        uint64_t epoch = 0;
        uint64_t last_used = 0;
        uint32_t unit = 0;   // shadowing unit (the path for a file, the content for a literal); 0 until first run
        // Static-check result, cached beside the parse (amortized once per unique
        // source); re-run when the registration surface epoch moves.
        bool checked = false;
        uint64_t checked_epoch = 0;
        check_report check_result;
        uint64_t proof_epoch = UINT64_MAX;   // surface epoch the nodes' proofs were stamped at; UINT64_MAX = unproven
        bool autos_typed = false;            // the first proof pass types the auto slots, in every mode
        bool proofs_deferred = false;        // the proof pass met a class the engine had not defined yet: re-stamp after an import defines one
        std::vector<std::string> deferred_classes;   // those classes' names
        // Program closure (checked_report): gathered once per surface epoch, and again
        // whenever a unit's (mtime, size) stamp moves or the program is failing.
        uint64_t closure_epoch = UINT64_MAX;
        bool closure_root = false;   // has literal top-level import/include paths
        bool closure_filesystem = true;   // permission under which dependencies were gathered
        std::vector<closure_stamp> closure_stamps;
        std::vector<closure_stamp> spliced_stamps;   // files spliced into function bodies at parse time: a change re-parses this entry; such a unit writes no jaibite sibling
        // the unit's reload hook (hot_reload_migrate at its top level): kept off the program - never defined, never callable - and
        // run after a re-run of the unit; check_declarations = the program and the hook, what the checker and the proof pass walk
        std::shared_ptr<function_decl> reload_hook;
        std::vector<declaration_ptr> check_declarations;
        // the jaibite sibling this entry last wrote or loaded, by its (mtime, size) stamp: while it stays in place the sibling
        // holds this source, so a hit queries one stat; the source's identity is computed once, when a sibling first needs it
        uint64_t sibling_mtime = 0, sibling_size = 0;
        bool sibling_stamped = false;
        bool identity_known = false;
        detail::jaibite_source_identity identity;
        std::string closure_listing;                   // erroring units' listings, each against its own source + path
        std::vector<check_diagnostic> closure_diags;   // every unit diagnostic, path-qualified in the message
        std::string closure_parse_failures;            // units that failed to parse: the program stops there, before any check
        // The aggregate lowering's sites: the checker's diagnostics and the jaibite writer see the parsed bodies, the proof
        // pass and both backends the lowered ones (engine::aggregate_lowering)
        mutable detail::aggregate_lowering_state lowering;
    };
    uint64_t script_cache_epoch = 0;
    uint64_t script_cache_clock = 0;
    static constexpr size_t script_cache_max = 64;

    // Literal fast lane (engine::execute(script_source)): consteval tagging guarantees
    // the pointer is immutable static storage, so pointer+length IS content identity —
    // a hit touches zero source bytes. Entries are shared with (and outlive eviction
    // from) the content-keyed cache; epoch staleness is re-checked on every hit.
    struct literal_key {
        const char* ptr;
        size_t len;
        bool operator==(const literal_key&) const = default;
    };
    struct literal_key_hash {
        size_t operator()(const literal_key& k) const noexcept {
            size_t h = std::hash<const void*>{}(k.ptr);
            h ^= k.len + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<literal_key, std::shared_ptr<script_cache_entry>, literal_key_hash> literal_script_cache;

    // Static type checking (strict by default; off = zero cost beyond one branch at cache entry).
    // check_surface_epoch_ moves on every registration the checker can see
    // (functions, globals, classes, template types), invalidating cached check results
    // without touching the parse cache.
    check_mode static_check_mode_ = check_mode::strict;
    check_report last_check_;
    uint64_t check_surface_epoch_ = 0;
    // Parse-time type proofs (engine::type_proofs): the proof pass rides the same walk.
    bool type_proofs_ = true;
    bool ir_passes_ = true;
    uint32_t cells_miss_every_ = 0;   // the miss injection lever; the VM reads it at block entry under JAISCRIPT_MISS_INJECTION
#ifdef JAISCRIPT_VM_COVERAGE
    std::mutex coverage_mutex_;
    std::vector<std::shared_ptr<vm::coverage_tables>> coverage_tables_;   // every vm backend's census tables (vm/coverage.hpp), the workers' included
#endif
    engine::disassembly_sink_fn disassembly_sink_;   // the listing tool's sink; unset = no listing
    proof_stats last_proof_stats_;

    // Program closure (internal/static_checking.md "Programs"): a unit with literal top-level
    // import/include paths is checked together with every unit those paths reach, before
    // any of them runs. The union of their top-level names lives for the engine's lifetime
    // and rides every check as extra_globals - a superset never makes a false error, and a
    // leaf re-checked alone after a hot edit still knows its program. program_classes_ carries
    // the same units' class declarations (a subclass resolves a parent another unit declares).
    std::unordered_set<std::string> program_names_;
    detail::program_class_map program_classes_;
    detail::program_enum_map program_enums_;   // the closure units' top-level enums (an enum value read is no user code: member_expr::enum_constant)
    detail::program_function_map program_functions_;   // the closure units' free functions (internal/static_checking.md "Programs"; the inliner's cross-unit leaves)
    detail::program_global_map program_globals_;   // the closure units' top-level variables: the proof pass types them (internal/static_checking.md "Proofs")
    struct closure_unit {
        std::string path;   // resolved spelling: the key the unit later executes under
        std::string source;
        std::shared_ptr<script_cache_entry> entry;
    };
    // The root's closure findings, republished by every checked_report: the strict throw
    // text and the diagnostics folded into the caller-visible report.
    std::string program_check_listing_;
    std::vector<check_diagnostic> program_check_diags_;
    std::string program_parse_failures_;
    // Host functions' declared scalar results (engine::host_function_result): jai_null_type = unknown or disagreeing
    std::unordered_map<std::string, script_value_type> host_results_;
    script_value_type staged_host_result_ = script_value_type::jai_null_type;
    engine::host_direct_entry staged_host_direct_;

    static void literal_unit_paths(const std::vector<declaration_ptr>& decls, std::vector<std::string>& out) {
        for (const auto& d : decls) {
            if (!d) { continue; }
            if (d->get_type() == node_type::import_decl) {
                auto* imp = static_cast<const import_decl*>(d.get());
                if (!imp->path_expr) { out.push_back(imp->path); }
            } else if (d->get_type() == node_type::include_decl) {
                auto* inc = static_cast<const include_decl*>(d.get());
                if (!inc->path_expr) { out.push_back(inc->path); }
            }
        }
        detail::collect_unit_imports(decls, out);
    }

    // A unit's reload hook leaves its program: the parser checked its shape; it checks and runs under an unspellable name
    std::shared_ptr<function_decl> take_reload_hook(std::vector<declaration_ptr>& decls) {
        for (auto it = decls.begin(); it != decls.end(); ++it) {
            if (!*it || (*it)->get_type() != node_type::function_decl) { continue; }
            auto f = std::static_pointer_cast<function_decl>(*it);
            if (f->name != "hot_reload_migrate" && f->name != k_reload_hook_name) { continue; }
            decls.erase(it);
            f->name_id = string_symbolizer_.intern(k_reload_hook_name);
            f->name = string_symbolizer_.get_string(f->name_id);
            return f;
        }
        return nullptr;
    }
    static constexpr std::string_view k_reload_hook_name = "<hot_reload_migrate>";
    void adopt_reload_hook(script_cache_entry& entry, std::shared_ptr<function_decl> hook) {
        entry.reload_hook = std::move(hook);
        entry.check_declarations.clear();
        if (!entry.reload_hook) { return; }
        entry.check_declarations = entry.declarations;
        entry.check_declarations.push_back(entry.reload_hook);
    }
    static const std::vector<declaration_ptr>& checked_declarations(const script_cache_entry& entry) {
        return entry.reload_hook ? entry.check_declarations : entry.declarations;
    }

    // The parse-cache entry for (sourcePath, content): a hit, a fresh jaibite sibling, or a
    // fresh parse stored under that key. Throws parse_error. sibling/siblingFresh tell the
    // caller whether to rewrite the sibling afterwards.
    std::shared_ptr<script_cache_entry> acquire_entry(engine& self, const std::string& content, const std::string& sourcePath,
                                                      std::filesystem::path& sibling, bool& siblingFresh) {
        sibling.clear();
        siblingFresh = false;
        if (jaibite_cache_enabled && sourcePath != "<script>" && self.is_enabled(privilege::filesystem)) {
            sibling = jaibite_sibling_path(sourcePath);
        }
        auto cached = find_cached_script(sourcePath, content);
        std::optional<detail::jaibite_source_identity> identity;
        if (cached) {
            siblingFresh = !sibling.empty() && sibling_holds(*cached, sibling, content);
        } else if (!sibling.empty()) {
            // a sibling loads only when it records this exact source; anything else parses below and is rewritten
            cached = try_load_jaibite_entry(self, sibling, sourcePath, content, identity);
            siblingFresh = cached != nullptr;
        }
        if (!cached) {
            lexer lex(content, &string_symbolizer_, sourcePath);
            auto tokens = lex.tokenize();
            parser prs(tokens, &string_symbolizer_, &self, registeredTemplateTypes);
            auto parse_result = prs.parse();
            if (!parse_result) {
                for (const auto& f : prs.spliced_files()) { uint64_t mtime = 0, size = 0; const bool ok = stat_mtime_size(f, mtime, size); note_closure_file(f, ok, mtime, size); }
                throw parse_error(self.format_error(parse_result));
            }
            auto decls = std::move(parse_result.value());
            auto hook = take_reload_hook(decls);
            cached = store_cached_script(sourcePath, content, std::move(decls));
            adopt_reload_hook(*cached, std::move(hook));
            stamp_spliced(*cached, prs.spliced_files());
            if (identity) { cached->identity = *identity; cached->identity_known = true; }
        }
        if (debugger_) { debugger_->note_source(sourcePath); }
        return cached;
    }

    // Depth-first in source order; a unit the runtime cannot resolve or read is skipped here
    // and reported by the runtime exactly as before, when it executes. A unit that fails to
    // parse is collected in parse_failures (its text, path and position included). Every
    // resolved unit leaves a stamp (taken before the read, so a later edit shows as newer).
    void collect_closure_units(engine& self, const std::vector<std::string>& paths, std::unordered_set<std::string>& visited,
                               std::vector<closure_unit>& out, std::vector<closure_stamp>& stamps, const std::string& from_file,
                               std::string& parse_failures) {
        for (const auto& path : paths) {
            closure_unit unit;
            try {
                auto resolved = resolve_include_path(path, &self, from_file);
                if (!resolved || !visited.insert(resolved.value()).second) { continue; }
                unit.path = std::move(resolved.value());
                closure_stamp stamp{unit.path, 0, 0};
                stat_mtime_size(unit.path, stamp.mtime, stamp.size);
                stamps.push_back(std::move(stamp));
                std::ifstream file(unit.path);
                if (!file.is_open()) { continue; }
                std::stringstream buffer;
                buffer << file.rdbuf();
                unit.source = buffer.str();
                std::filesystem::path sibling;
                bool siblingFresh = false;
                unit.entry = acquire_entry(self, unit.source, unit.path, sibling, siblingFresh);
            } catch (const parse_error& e) {
                parse_failures += e.what();
                if (parse_failures.empty() || parse_failures.back() != '\n') { parse_failures += '\n'; }
                continue;
            } catch (...) {
                continue;
            }
            std::vector<std::string> nested;
            literal_unit_paths(unit.entry->declarations, nested);
            detail::collect_unit_names(unit.entry->declarations, program_names_);
            detail::collect_unit_classes(unit.entry->declarations, program_classes_);
            detail::collect_unit_enums(unit.entry->declarations, unit.path, program_enums_);
            detail::collect_unit_functions(unit.entry->declarations, unit.path, program_functions_);
            detail::collect_unit_globals(unit.entry->declarations, unit.path, program_globals_);
            std::string unit_path = unit.path;
            out.push_back(std::move(unit));
            collect_closure_units(self, nested, visited, out, stamps, unit_path, parse_failures);
        }
    }

    // A gather is due once per surface epoch, then whenever a unit's stamp moved or the
    // program is failing - a fix in any file, or a missing file appearing, is seen on the
    // next run without waiting for the epoch.
    bool closure_due(const script_cache_entry& entry) const {
        if (entry.closure_epoch != check_surface_epoch_) { return true; }
        if (!entry.closure_root) { return false; }
        const bool filesystem_allowed = (privileges_ &
            static_cast<uint64_t>(privilege::filesystem)) != 0;
        if (entry.closure_filesystem != filesystem_allowed) { return true; }
        if (!filesystem_allowed) { return false; }
        if (!entry.closure_listing.empty() || !entry.closure_parse_failures.empty() || entry.check_result.has_errors()) { return true; }
        for (const auto& s : entry.closure_stamps) {
            uint64_t mtime = 0, size = 0;
            stat_mtime_size(s.path, mtime, size);
            if (mtime != s.mtime || size != s.size) { return true; }
        }
        return false;
    }

    // A proof-only walk over an entry (diagnostics off): the program's globals type here, and a class-typed one waits for its class
    // (entry.proofs_deferred; restamp_deferred after an import completes)
    void stamp_proofs(engine& self, script_cache_entry& entry) {
        detail::check_options o;
        o.diagnostics = false;
        o.proofs = type_proofs_ ? detail::check_options::proofs_t::stamp : detail::check_options::proofs_t::clear;
        o.stats = &last_proof_stats_;
        o.program_names = &program_names_;
        o.program_classes = &program_classes_; o.program_enums = &program_enums_;
        o.program_functions = &program_functions_;
        o.program_globals = &program_globals_;
        entry.deferred_classes.clear();
        o.deferred_classes = &entry.deferred_classes;
        detail::run_static_check(self, checked_declarations(entry), nullptr, o);
        entry.proofs_deferred = type_proofs_ && !entry.deferred_classes.empty();
    }
    bool class_defined_now(engine& self, const std::vector<std::string>& names) {
        for (const auto& n : names) { if (self.get_class_definition(n) || self.get_class_registry().find_script_class(n)) { return true; } }
        return false;
    }
    void restamp_deferred(engine& self) {
        if (!type_proofs_) { return; }
        for (auto& node : script_cache) { if (node.entry && node.entry->proofs_deferred && class_defined_now(self, node.entry->deferred_classes)) { stamp_proofs(self, *node.entry); } }
        for (auto& [key, entry] : literal_script_cache) { if (entry && entry->proofs_deferred && class_defined_now(self, entry->deferred_classes)) { stamp_proofs(self, *entry); } }
    }

    // One entry's check: diagnostics when wanted (and stale), proofs whenever the entry's
    // stamps disagree with the switch/surface epoch (stamp on, clear off). A trusted
    // jaibite entry (checked, epoch current) never re-collects diagnostics; its proofs
    // still stamp.
    // The lowering runs where it pays: the vm's typed rows and cells run component slots at the float-local floor, while the
    // interpreter's per-node dispatch makes a scalarized vector slower than the boxed kernel (an explicit setting wins)
    bool lowering_on() const { return aggregate_lowering_ < 0 ? current_backend_type == backend_type::vm : aggregate_lowering_ != 0; }

    // The view the backends run: the lowered bodies (built on first use) or the parsed ones
    void lowering_view(engine& self, const std::vector<declaration_ptr>& declarations, detail::aggregate_lowering_state& state) {
        if (lowering_on() && !state.built) {
            detail::build_aggregate_lowering(self, declarations, state);
            aggregate_lowered_locals_ += state.lowered_locals;
            aggregate_lowered_sites_ += state.sites.size();
        }
        detail::set_aggregate_view(state, lowering_on());
    }

    void check_entry(engine& self, script_cache_entry& entry, bool want_diagnostics) {
        const uint64_t want_proof_epoch = type_proofs_ ? check_surface_epoch_ : UINT64_MAX;
        const bool need_check = want_diagnostics && (!entry.checked || entry.checked_epoch != check_surface_epoch_);
        const bool need_proofs = entry.proof_epoch != want_proof_epoch || !entry.autos_typed ||
                                 (lowering_on() && !entry.lowering.built) || entry.lowering.lowered_view != lowering_on();
        if (need_check) {   // the diagnostics walk leaves the proofs alone: the proof pass follows, and it alone reads the program's globals
            detail::set_aggregate_view(entry.lowering, false);   // diagnostics read the parsed bodies
            detail::check_options o;
            o.diagnostics = true;
            o.proofs = detail::check_options::proofs_t::leave;
            o.program_names = &program_names_;
            o.program_classes = &program_classes_; o.program_enums = &program_enums_;
            o.program_functions = &program_functions_;
            o.program_globals = &program_globals_;
            entry.check_result = detail::run_static_check(self, checked_declarations(entry), nullptr, o);
            entry.checked = true;
            entry.checked_epoch = check_surface_epoch_;
        }
        if (need_check || need_proofs) {
            lowering_view(self, checked_declarations(entry), entry.lowering);
            stamp_proofs(self, entry);
            entry.proof_epoch = want_proof_epoch;
            entry.autos_typed = true;
        }
    }

    // The ONE cached checker entry point, mode-independent. A root with literal imports
    // gathers its closure when due (names first, so the root's own check already knows
    // them; the root's verdict is re-derived because the union may have moved), then
    // checks every closure unit whose entry is stale so the unit's later execute through
    // import finds it checked. A unit that fails to parse stops the program there: its
    // parse error is the whole report (the names it would have declared are unknown, so
    // any check would only cascade). The findings live on the root entry and are
    // republished on every call, gathered or not.
    const check_report& checked_report(engine& self, script_cache_entry& entry, bool want_diagnostics) {
        if (want_diagnostics && closure_due(entry)) {
            std::vector<std::string> roots;
            literal_unit_paths(entry.declarations, roots);
            entry.closure_root = !roots.empty();
            entry.closure_stamps.clear();
            entry.closure_listing.clear();
            entry.closure_diags.clear();
            entry.closure_parse_failures.clear();
            std::vector<closure_unit> closure;
            if (entry.closure_root) {
                detail::collect_unit_names(entry.declarations, program_names_);
                detail::collect_unit_classes(entry.declarations, program_classes_);
                detail::collect_unit_enums(entry.declarations, entry.source_path, program_enums_);
                detail::collect_unit_functions(entry.declarations, entry.source_path, program_functions_);
                detail::collect_unit_globals(entry.declarations, entry.source_path, program_globals_);
                std::unordered_set<std::string> visited;
                collect_closure_units(self, roots, visited, closure, entry.closure_stamps, entry.source_path, entry.closure_parse_failures);
                entry.checked = false;
            }
            entry.closure_epoch = check_surface_epoch_;
            entry.closure_filesystem = self.is_enabled(privilege::filesystem);
            if (!entry.closure_parse_failures.empty()) {
                entry.check_result = {};
                check_entry(self, entry, false);
                closure.clear();
                size_t start = 0;
                const std::string& failures = entry.closure_parse_failures;
                while (start < failures.size()) {
                    size_t end = failures.find('\n', start);
                    if (end == std::string::npos) { end = failures.size(); }
                    if (end > start) { entry.closure_diags.push_back({check_diagnostic::level::error, 0, 0, failures.substr(start, end - start)}); }
                    start = end + 1;
                }
            } else {
                check_entry(self, entry, true);
            }
            for (const auto& unit : closure) {
                check_entry(self, *unit.entry, true);
                const check_report& r = unit.entry->check_result;
                if (r.diagnostics.empty()) { continue; }
                if (r.has_errors()) { entry.closure_listing += r.format(unit.source, unit.path); }
                for (const auto& d : r.diagnostics) {
                    entry.closure_diags.push_back({d.severity, 0, 0,
                        unit.path + ":" + std::to_string(d.line) + ":" + std::to_string(d.column) + ": " + d.message,
                        d.related_line, d.related_column, d.strict_region});
                }
            }
        } else {
            check_entry(self, entry, want_diagnostics);
        }
        program_check_listing_ = entry.closure_listing;
        program_check_diags_ = entry.closure_diags;
        program_parse_failures_ = entry.closure_parse_failures;
        return entry.check_result;
    }

    // What a caller sees: the root's diagnostics followed by the closure's (line/column 0,
    // the location travels in the message - the snippet belongs to another source).
    check_report program_report(const check_report& root) const {
        check_report r = root;
        r.diagnostics.insert(r.diagnostics.end(), program_check_diags_.begin(), program_check_diags_.end());
        return r;
    }

    // Strict gates every error; warn gates only the ones a [[strict_mode]] region raised.
    bool gates(const check_report& r) const {
        return static_check_mode_ == check_mode::strict ? r.has_errors() : r.has_strict_errors();
    }

    // The tail every checked execute shares: publish last_check_, gate with the root listing
    // followed by each closure unit's own. A unit that failed to parse fails a strict load as
    // the parse error itself (warn runs on, and the import raises it when it executes).
    void gate_checked(const check_report& root, std::string_view source, std::string_view name = "<script>") {
        last_check_ = program_report(root);
        if (static_check_mode_ == check_mode::strict && !program_parse_failures_.empty()) {
            throw parse_error(program_parse_failures_.substr(0, program_parse_failures_.size() - 1));
        }
        if (gates(last_check_)) {
            throw static_check_error("static check failed:\n" + root.format(source, name) + program_check_listing_);
        }
    }

    // An include that runs inside a function or a block runs there: an enum the file declares is local to that scope
    // at run time, so one that names a program-scope enum leaves it unchanged (the checker's rule for an enum written in
    // a function body, applied at the include site: the file's own check cannot see where it runs)
    void warn_scoped_enums(engine& self, const script_cache_entry& entry, const std::string& path, std::string_view scope) {
        for (const auto& d : entry.declarations) {
            if (!d || d->get_type() != node_type::enum_decl) { continue; }
            const auto* e = static_cast<const enum_decl*>(d.get());
            const std::string name(e->name);
            if (!self.has_variable(name) && !program_names_.count(name)) { continue; }
            std::string text = path + ":" + std::to_string(e->location.line) + ":" + std::to_string(e->location.column) + ": enum '" + name +
                "' declared in " + std::string(scope) + " is local to it: the program-scope '" + name + "' keeps its values";
            self.report_script_warning(text);   // a run-time include has no load listing: the host's warning channel carries it
            last_check_.diagnostics.push_back({check_diagnostic::level::warning, 0, 0, std::move(text)});
        }
    }

    // Per-call check with host instance variables: diagnostics only, never touches the
    // entry's stamps (proofs_t::leave).
    check_report check_with_instance_vars(engine& self, const script_cache_entry& entry, const instance_variables& instanceVars) {
        std::vector<std::string> extra;
        extra.reserve(instanceVars.size());
        for (const auto& [name, value] : instanceVars) { extra.push_back(name); }
        detail::aggregate_source_view source(&entry.lowering);
        detail::check_options o;
        o.proofs = detail::check_options::proofs_t::leave;
        o.program_names = &program_names_;
        o.program_classes = &program_classes_; o.program_enums = &program_enums_;
        o.program_functions = &program_functions_;
        o.program_globals = &program_globals_;
        return detail::run_static_check(self, checked_declarations(entry), &extra, o);
    }

    // The one cache-hit execution tail (execute_file fast lane + the literal lane):
    // mirrors execute_source's hit path — prepare, static-check hook, execute_parsed.
    script_value execute_cache_hit(engine& self, script_cache_entry& entry,
                                   const instance_variables& instanceVars, std::string_view source) {
        has_executed_ = true;
        self.backend()->prepare_for_execution();
        entry.last_used = ++script_cache_clock;
        const check_report& rep = checked_report(self, entry, static_check_mode_ != check_mode::off);
        if (static_check_mode_ != check_mode::off) {
            if (instanceVars.empty()) {
                gate_checked(rep, source);
            } else {
                gate_checked(check_with_instance_vars(self, entry, instanceVars), source);
            }
        }
        return run_unit(self, entry, entry.compiled, instanceVars.empty() ? nullptr : &instanceVars, source, false);
    }

    // A unit's run. A re-run of a unit whose source declares a reload hook snapshots the unit's program-scope variables first and
    // hands them to the hook once the run's top-level code finishes; a unit without one pays nothing. A textual include runs under
    // the including unit: its re-run is the same file included again by the same unit, and its snapshot is that unit's.
    std::unordered_set<uint64_t> textual_runs_;   // (included file's unit, including unit) pairs that ran
    bool unit_ran_before(const script_cache_entry& entry, std::string_view content) const {
        if (entry.unit != 0) { return true; }
        const std::string key = entry.source_path == "<script>" ? std::string("\n").append(content)
                                                                : std::filesystem::path(entry.source_path).lexically_normal().string();
        return unit_index_.count(key) != 0;
    }
    script_value unit_snapshot(engine& self, uint32_t unit) {
        auto previous = script_value::make_map(self.get_type_info_string(), nullptr, &self);
        auto& map = const_cast<script_map&>(previous.as_map());
        for (const auto& [id, cell] : global_environment_->unit_variables(unit)) {
            map[script_value(std::string(string_symbolizer_.get_string(id)), &self)] = cell->deref().clone_element();
        }
        return previous;
    }
    void run_reload_hook(engine& self, const function_decl& hook, uint32_t unit, script_value previous) {
        script_callable payload;
        payload.kind = script_callable::kind_type::function;
        payload.fn = std::make_shared<script_defined_function>(string_symbolizer_.get_string(string_symbolizer_.intern("hot_reload_migrate")),
                                                                shared_parameters_for(&hook), hook.return_type, hook.body, nullptr, hook.local_count);
        payload.fn->unit = unit_stamp{unit, string_symbolizer_.next_execution()};
        execution_backend* be = self.backend();
        auto result = script_callable_thunk{&self, std::move(payload)}({std::move(previous)});
        if (be->is_unwinding()) {
            if (be->is_executing()) { be->park_nested_failure(); }
            throw without_category_suffix(be->get_current_exception());
        }
        if (!result) { throw runtime_error(self.format_error(result)); }
    }
    script_value run_unit(engine& self, script_cache_entry& entry, std::shared_ptr<void>& compiled_slot,
                          const instance_variables* instanceVars, std::string_view source, bool textual) {
        std::optional<script_value> previous;
        uint32_t runner = 0;
        if (textual) {
            runner = self.backend()->current_unit().id;
            const bool rerun = !textual_runs_.insert((uint64_t(unit_of(entry, source)) << 32) | runner).second;
            if (rerun && entry.reload_hook) { previous = unit_snapshot(self, runner); }
        } else if (entry.reload_hook && unit_ran_before(entry, source)) {
            runner = unit_of(entry, source);
            previous = unit_snapshot(self, runner);
        }
        script_value result = self.execute_parsed(entry.declarations, compiled_slot, instanceVars, unit_of(entry, source), textual);
        if (previous) { run_reload_hook(self, *entry.reload_hook, runner, std::move(*previous)); }
        return result;
    }

    // Keyed on (sourcePath, content) so identical text under different paths does not
    // alias one AST (each AST is stamped with its own filename — breakpoints/traces
    // stay distinct). The INDEX is a constant-time discriminator, not a digest: lengths
    // + 24 sampled content bytes narrow the ≤64-node flat scan, then ONE memcmp against
    // the stored source verifies exactly — no content hashing anywhere on the hot path,
    // and change detection stays byte-exact (hot-reload semantics). A discriminator
    // collision just keeps scanning; memcmp is the arbiter.
    static void content_samples(const std::string& source, uint64_t& head, uint64_t& mid, uint64_t& tail) {
        head = mid = tail = 0;
        const char* data = source.data();
        size_t n = source.size();
        if (n >= 8) {
            std::memcpy(&head, data, 8);
            std::memcpy(&mid, data + (n / 2) - 4, 8);
            std::memcpy(&tail, data + n - 8, 8);
        } else if (n > 0) {
            std::memcpy(&head, data, n);
        }
    }

    struct script_cache_node {
        uint64_t path_len = 0;
        uint64_t content_len = 0;
        uint64_t head = 0, mid = 0, tail = 0;
        std::string path;
        std::string source;
        std::shared_ptr<script_cache_entry> entry;
    };
    std::vector<script_cache_node> script_cache;

    std::shared_ptr<script_cache_entry> find_cached_script(const std::string& sourcePath, const std::string& source) {
        uint64_t head, mid, tail;
        content_samples(source, head, mid, tail);
        for (size_t i = 0; i < script_cache.size(); ++i) {
            auto& node = script_cache[i];
            if (node.content_len != source.size() || node.path_len != sourcePath.size() ||
                node.head != head || node.mid != mid || node.tail != tail) {
                continue;
            }
            if (std::memcmp(node.path.data(), sourcePath.data(), sourcePath.size()) != 0 ||
                std::memcmp(node.source.data(), source.data(), source.size()) != 0) {
                continue;   // discriminator collision: memcmp is the arbiter, keep scanning
            }
            if (node.entry->epoch != script_cache_epoch || spliced_changed(*node.entry)) {
                script_cache[i] = std::move(script_cache.back());
                script_cache.pop_back();
                return nullptr;
            }
            node.entry->last_used = ++script_cache_clock;
            return node.entry;
        }
        return nullptr;
    }

    // the files a parse spliced into function bodies, stamped now; a later stamp change makes the entry stale
    void stamp_spliced(script_cache_entry& entry, const std::vector<std::string>& files) {
        entry.spliced_stamps.clear();
        for (const auto& f : files) { closure_stamp s{f, 0, 0}; (void)stat_mtime_size(f, s.mtime, s.size); entry.spliced_stamps.push_back(std::move(s)); }
    }
    bool spliced_changed(const script_cache_entry& entry) {
        for (const auto& s : entry.spliced_stamps) { uint64_t mtime = 0, size = 0; if (!stat_mtime_size(s.path, mtime, size) || mtime != s.mtime || size != s.size) return true; }
        return false;
    }
    std::shared_ptr<script_cache_entry> store_cached_script(const std::string& sourcePath, const std::string& source, std::vector<declaration_ptr> decls) {
        if (script_cache.size() >= script_cache_max) {
            size_t victim = 0;
            for (size_t i = 1; i < script_cache.size(); ++i) {
                if (script_cache[i].entry->last_used < script_cache[victim].entry->last_used) {
                    victim = i;
                }
            }
            script_cache[victim] = std::move(script_cache.back());
            script_cache.pop_back();
        }
        auto entry = std::make_shared<script_cache_entry>();
        entry->declarations = std::move(decls);
        entry->source_path = sourcePath;
        entry->epoch = script_cache_epoch;
        entry->last_used = ++script_cache_clock;
        script_cache_node node;
        node.path_len = sourcePath.size();
        node.content_len = source.size();
        content_samples(source, node.head, node.mid, node.tail);
        node.path = sourcePath;
        node.source = source;
        node.entry = entry;
        script_cache.push_back(std::move(node));
        return entry;
    }

    // File fast lane (engine::execute_file): canonical-path keyed, gated by ONE stat.
    // Reuse touches zero file bytes; stamp-equal content is verified by a single
    // read+memcmp after the initial store (same-tick-edit hazard), then trusted while
    // the (mtime, size) stamp is equal. Any other stamp, older included, = stale.
    struct file_cache_node {
        uint64_t mtime = 0;
        uint64_t size = 0;
        bool verified = false;        // one-shot equal-stamp re-read confirmed the content
        std::string attributed_path;  // as-given spelling the cached AST was stamped with
        std::string source;           // for the verify memcmp + check-report formatting
        std::shared_ptr<script_cache_entry> entry;
    };
    std::unordered_map<std::string, file_cache_node> file_script_cache;
    std::unordered_map<std::string, std::string> canonical_path_cache;   // as-given -> weakly_canonical

    // One stat of a file or folder: on Windows the times are 100 ns FILETIME ticks (the import cache's stamp unit)
    struct entry_stat {
        bool is_directory = false;
        uint64_t size = 0;
        int64_t modified_ms = 0;   // since the Unix epoch
        bool has_created = false;
        int64_t created_ms = 0;
        uint64_t mtime_ticks = 0;
    };
    static bool stat_entry(const std::string& path, entry_stat& out) {
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        std::filesystem::path widePath(path);
        if (!GetFileAttributesExW(widePath.c_str(), GetFileExInfoStandard, &fad)) {
            return false;
        }
        const auto ticks = [](const FILETIME& ft) { return (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; };
        const auto unix_ms = [](uint64_t t) { return (static_cast<int64_t>(t) - 116444736000000000LL) / 10000; };
        out.is_directory = (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.size = out.is_directory ? 0 : (uint64_t(fad.nFileSizeHigh) << 32) | fad.nFileSizeLow;
        out.mtime_ticks = ticks(fad.ftLastWriteTime);
        out.modified_ms = unix_ms(out.mtime_ticks);
        out.has_created = true;
        out.created_ms = unix_ms(ticks(fad.ftCreationTime));
        return true;
#else
        struct stat st{};
        if (::stat(path.c_str(), &st) != 0) { return false; }
        out.is_directory = S_ISDIR(st.st_mode);
        out.size = out.is_directory ? 0 : static_cast<uint64_t>(st.st_size);
#ifdef __APPLE__
        out.modified_ms = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1000 + st.st_mtimespec.tv_nsec / 1000000;
        out.has_created = true;
        out.created_ms = static_cast<int64_t>(st.st_birthtimespec.tv_sec) * 1000 + st.st_birthtimespec.tv_nsec / 1000000;
#else
        out.modified_ms = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
        return true;
#endif
    }

    // mtime (100 ns FILETIME on Windows) + size from a single syscall. false = file
    // unreachable; callers fall back to the legacy read path (today's error text).
    static bool stat_file(const std::string& path, uint64_t& mtime, uint64_t& size) {
#ifdef _WIN32
        entry_stat st;
        if (!stat_entry(path, st) || st.is_directory) {
            return false;
        }
        mtime = st.mtime_ticks;
        size = st.size;
        return true;
#else
        std::error_code timeEc, sizeEc;
        auto writeTime = std::filesystem::last_write_time(path, timeEc);
        auto byteSize = std::filesystem::file_size(path, sizeEc);
        if (timeEc || sizeEc) {
            return false;
        }
        mtime = uint64_t(writeTime.time_since_epoch().count());
        size = uint64_t(byteSize);
        return true;
#endif
    }

    // A file's stamp through the host's engine::file_stamp_source, else the file system's
    engine::file_stamp_reading file_stamp_source;
    bool stat_mtime_size(const std::string& path, uint64_t& mtime, uint64_t& size) const {
        if (!file_stamp_source) { return stat_file(path, mtime, size); }
        const std::optional<engine::file_stamp> stamp = file_stamp_source(path);
        if (!stamp) { return false; }
        mtime = stamp->mtime;
        size = stamp->size;
        return true;
    }

    // A file's text read under the (mtime, size) stamp taken just before the read: the stamp queried again after it must
    // agree, or a save landed mid-read and the text may mix two versions (torn: read it again later)
    enum class stamped_read { missing, torn, stable };
    stamped_read read_under_stamp(const std::string& path, bool stamped, uint64_t mtime, uint64_t size, std::string& content) {
        {
            std::ifstream file(path);
            if (!file.is_open()) { return stamped_read::missing; }
            std::stringstream buffer;
            buffer << file.rdbuf();
            content = buffer.str();
        }
        if (!stamped) { return stamped_read::stable; }
        uint64_t after_mtime = 0, after_size = 0;
        return stat_mtime_size(path, after_mtime, after_size) && after_mtime == mtime && after_size == size
            ? stamped_read::stable : stamped_read::torn;
    }

    // Jaibite disk cache (engine.hpp doc): sibling <stem>.jaibite maintained beside every
    // file-based load. std::filesystem only; every touch is best-effort via error_code.
    bool jaibite_cache_enabled = true;
    size_t jaibite_cache_write_failures_ = 0;
    uint64_t jaibite_temporary_serial_ = 0;

    std::string root_source_file_;   // engine::root_source_file
    std::function<std::string(const std::string&)> app_data_resolver;    // engine::app_directory_resolvers
    std::function<std::string(const std::string&)> app_cache_resolver;
    engine_time::clock_reading monotonic_clock, wall_clock;   // engine_time::source (empty = system)
    engine_time::clock_sleep sleep_clock;
    std::chrono::nanoseconds system_origin = std::chrono::steady_clock::now().time_since_epoch();
    std::chrono::nanoseconds entry_start{};
    uint64_t entry_generation = 0;
    uint64_t stamped_generation = UINT64_MAX;
    bool entry_stamps = false;
    void note_root_source(const std::string& path) {
        if (path.empty() || path == "<script>") return;
        if (!backend || !backend->is_executing()) root_source_file_ = path;
    }

    // Class-shape cache validity (engine::class_definition_epoch): starts at 1 so a
    // cache initialized to epoch 0 always misses its first query.
    uint64_t class_definition_epoch_ = 1;
    uint64_t definition_epoch_ = 1;
    std::unordered_set<const script_value*> const_cells_;
    // Shadowing units (environment::define_variable): a file by its lexically normal path, a
    // literal by its content, numbered at first execution; 0 is the host
    std::unordered_map<std::string, uint32_t> unit_index_;
    uint32_t unit_count_ = 0;
    std::unordered_map<uint32_t, unsigned> unit_first_run_;   // each unit's first execution (engine::unit_rerun)
    uint32_t unit_for(const std::string& key) {
        auto it = unit_index_.find(key);
        if (it != unit_index_.end()) { return it->second; }
        return unit_index_.emplace(key, ++unit_count_).first->second;
    }
    uint32_t unit_of(script_cache_entry& entry, std::string_view content) {
        if (entry.unit == 0) {
            entry.unit = entry.source_path == "<script>"
                ? unit_for(std::string("\n").append(content))
                : unit_for(std::filesystem::path(entry.source_path).lexically_normal().string());
        }
        return entry.unit;
    }
    uint64_t parameter_alias_binds_ = 0;
    bool parameter_alias_ = true;
    uint64_t local_alias_binds_ = 0;
    bool local_alias_ = true;

    // Empty result = caching inactive for this source (a .jaibite loaded as source
    // must never overwrite itself).
    static std::filesystem::path jaibite_sibling_path(const std::string& sourcePath) {
        std::filesystem::path src(sourcePath);
        if (src.extension() == k_jaibite_extension) {
            return {};
        }
        return src.replace_extension(k_jaibite_extension);
    }

    static bool stat_sibling(const std::filesystem::path& sibling, uint64_t& mtime, uint64_t& size) {
        return stat_file(sibling.string(), mtime, size);
    }
    const detail::jaibite_source_identity& identity_of(script_cache_entry& entry, const std::string& source) {
        if (!entry.identity_known) { entry.identity = detail::jaibite_identity_of(source); entry.identity_known = true; }
        return entry.identity;
    }

    // An in-memory hit rewrites its sibling unless it is the file the entry last wrote or loaded, or a file whose header
    // records this source (another engine's write, adopted whatever its surface, so engines sharing a file never take
    // turns rewriting it on every hit; a miss still loads only under a matching fingerprint)
    bool sibling_holds(script_cache_entry& entry, const std::filesystem::path& sibling, const std::string& source) {
        uint64_t mtime = 0, size = 0;
        if (!stat_sibling(sibling, mtime, size)) { return false; }
        if (entry.sibling_stamped && mtime == entry.sibling_mtime && size == entry.sibling_size) { return true; }
        uint8_t header[detail::k_jaibite_header_size];
        std::ifstream file(sibling, std::ios::binary);
        if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) { return false; }
        uint64_t fingerprint = 0;
        detail::jaibite_source_identity recorded;
        if (!detail::read_jaibite_header(header, sizeof(header), fingerprint, recorded) || recorded != identity_of(entry, source)) {
            return false;
        }
        entry.sibling_mtime = mtime;
        entry.sibling_size = size;
        entry.sibling_stamped = true;
        return true;
    }

    // Load a sibling into the normal parse-entry pipeline when its header records exactly this source (size and hash,
    // compared for equality: a file put in place with an older mtime, or restored with its old one, is judged by its
    // bytes) and this engine's registration fingerprint. nullptr = parse instead (absent, corrupt, truncated, another
    // version, another source, another surface); never throws, the source is still in hand. The deserialized AST
    // carries its saved filename stamps, so stack traces and the debugger still name the .jai.
    std::shared_ptr<script_cache_entry> try_load_jaibite_entry(engine& self, const std::filesystem::path& sibling,
                                                               const std::string& sourcePath, const std::string& source,
                                                               std::optional<detail::jaibite_source_identity>& identity) {
        try {
            uint64_t siblingMtime = 0, siblingSize = 0;
            if (!stat_sibling(sibling, siblingMtime, siblingSize)) {
                return nullptr;
            }
            identity = detail::jaibite_identity_of(source);
            std::ifstream file(sibling, std::ios::binary);
            if (!file.is_open()) {
                return nullptr;
            }
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            uint64_t savedFingerprint = 0;
            detail::jaibite_source_identity recorded;
            if (!detail::read_jaibite_header(bytes.data(), bytes.size(), savedFingerprint, recorded) ||
                recorded != *identity || savedFingerprint != self.registration_fingerprint()) {
                return nullptr;
            }
            uint32_t flags = 0;
            auto declarations = detail::deserialize_jaibite(bytes.data(), bytes.size(), &self, savedFingerprint, &flags, nullptr, sourcePath);
            // jaibite carries structure, not the parser-pass flags (ref-escape,
            // value-decl naming, typed-store proofs, transient reads) — re-derive
            // them exactly like the parse tail does
            if (detail::run_ast_marking_passes(declarations, &string_symbolizer_)) { entry_stamps = true; }
            auto hook = take_reload_hook(declarations);
            auto entry = store_cached_script(sourcePath, source, std::move(declarations));
            adopt_reload_hook(*entry, std::move(hook));
            entry->identity = *identity;
            entry->identity_known = true;
            entry->sibling_mtime = siblingMtime;
            entry->sibling_size = siblingSize;
            entry->sibling_stamped = true;
            if (detail::jaibite_stamped_clean(flags)) {
                // Trusted stamp (fingerprint matched): skip the re-check exactly like a
                // loaded bite does — check_result stays default (no diagnostics).
                entry->checked = true;
                entry->checked_epoch = check_surface_epoch_;
            }
            return entry;
        } catch (...) {
            return nullptr;
        }
    }

    // Best-effort sibling write: failures are SILENT (cache is an optimization, never an
    // error — read-only asset dirs, shipped games) and counted for diagnostics. The bytes go
    // to a temporary file in the sibling's folder that is renamed over the sibling, so a
    // reader (another engine, another process, a crash mid-write) sees the old file or the
    // new one, never part of one. A failed write keeps the sibling it could not replace: its
    // recorded source identity stops it standing in for other text, and deleting it threw away
    // another engine's fresh write whenever two engines raced on one file.
    void write_jaibite_sibling(engine& self, const std::filesystem::path& sibling, script_cache_entry& entry,
                               const std::string& sourcePath, const std::string& source) noexcept {
        if (!self.is_enabled(privilege::filesystem)) { return; }
        std::filesystem::path temporary;
        try {
            uint32_t flags = 0;
            if (static_check_mode_ != check_mode::off && entry.checked &&
                entry.checked_epoch == check_surface_epoch_ && !entry.check_result.has_errors()) {
                flags |= detail::jaibite_checked_clean_flags();
            }
            std::vector<uint8_t> bytes;
            {
                detail::aggregate_source_view view(&entry.lowering);   // a sibling holds the parsed program
                bytes = detail::serialize_jaibite(checked_declarations(entry), string_symbolizer_, self.registration_fingerprint(), flags, sourcePath,
                                                  identity_of(entry, source));
            }
#ifdef _WIN32
            const uint64_t process = GetCurrentProcessId();
#else
            const uint64_t process = static_cast<uint64_t>(getpid());
#endif
            temporary = sibling;
            temporary += "." + std::to_string(process) + "-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-" +
                         std::to_string(++jaibite_temporary_serial_) + ".tmp";
            bool written = false;
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (file.is_open()) {
                    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    file.close();
                    written = static_cast<bool>(file);
                }
            }
            std::error_code ec;
            if (written) {
                std::filesystem::rename(temporary, sibling, ec);
                if (!ec) {
                    entry.sibling_stamped = stat_sibling(sibling, entry.sibling_mtime, entry.sibling_size);
                    return;
                }
            }
        } catch (...) {
        }
        ++jaibite_cache_write_failures_;
        entry.sibling_stamped = false;
        std::error_code ec;
        if (!temporary.empty()) { std::filesystem::remove(temporary, ec); }
    }

    implementation();
    ~implementation();
    
    // Update interpreter when an overloaded function changes
    void updateOverloadedFunction(const std::string& name, engine* engine_ptr);
    
    // Parallel-region backstop: the engine's type_info intern tables are frozen while a
    // region's workers run (pre-warmed at the barrier). A miss here means the warm pass
    // missed a shape - fail the worker cleanly instead of racing the shared tables.
    void deny_type_intern_in_region() const {
        if (parallel_ && parallel_->active_region) [[unlikely]] {
            throw jai::runtime_error("parallel_transform: type shape was not pre-warmed at the region barrier (internal) - please report this body shape");
        }
    }

    // Helper to ensure overload set has conversion registry
    OverloadSet& getOrCreateOverloadSet(const std::string& name) {
        auto& overloadSet = overloadedFunctions[name];
        if (!overloadSet.conversions) {
            overloadSet.set_conversion_registry(conversions);
            overloadSet.set_class_lookup(&classesByType, &classes);
        }
        return overloadSet;
    }

    // Optional host configuration: engines without custom permissions do not
    // allocate a registry or shift the offsets of existing hot members.
    struct custom_privilege_state {
        struct record {
            uint64_t name_id;
            bool enabled = false;
        };
        std::vector<record> records;
        std::unordered_map<uint64_t, uint64_t> slots;
    };
    std::unique_ptr<custom_privilege_state> custom_privileges_;

    // The math vectors' canonical stamps and node pool, last for the same reason (the pool's teardown orphans it while
    // any vector is still out, so its place in the destruction order does not matter)
    type_info* type_info_math_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};   // vec2, vec3, ivec2, ivec3, vec4, color
    ref_holder_pool_slot_holder vec_node_pool_;
    ref_holder_pool_slot_holder cell_cycle_registry_;
    int8_t aggregate_lowering_ = -1;   // engine::aggregate_lowering: 1 on, 0 off, -1 the backend's default (on for the vm)
    uint64_t aggregate_lowered_locals_ = 0;
    uint64_t aggregate_lowered_sites_ = 0;
};

engine::implementation::implementation()
    : conversions(std::make_shared<conversions::conversion_registry>()),
      class_definition_type_id_(string_symbolizer_.intern("class_definition")) {
    global_environment_ = std::make_shared<environment>(&string_symbolizer_);

    // The backend is constructed lazily by engine::backend() (callbacks/config applied
    // there via engine::wire_backend()), so unused engines never pay for one.


    // Initialize commonly used type_info objects for fast access
    // These are interned immediately so basic types are always available
    type_info int_info = type_info::make_int(string_symbolizer_);
    type_info_int_ = &type_infos_.emplace(type_key{script_value_type::jai_int_type, 0, 0}, int_info).first->second;
    type_id_index_[type_info_int_->id] = type_info_int_;

    type_info float_info = type_info::make_float(string_symbolizer_);
    type_info_float_ = &type_infos_.emplace(type_key{script_value_type::jai_float_type, 0, 0}, float_info).first->second;
    type_id_index_[type_info_float_->id] = type_info_float_;

    type_info string_info = type_info::make_string(string_symbolizer_);
    type_info_string_ = &type_infos_.emplace(type_key{script_value_type::jai_string_type, 0, 0}, string_info).first->second;
    type_id_index_[type_info_string_->id] = type_info_string_;
    type_info utf8_info = type_info::make_utf8_string(string_symbolizer_);
    type_info_utf8_string_ = &type_infos_.emplace(type_key{script_value_type::jai_utf8_string_type, 0, 0}, utf8_info).first->second;
    type_id_index_[type_info_utf8_string_->id] = type_info_utf8_string_;

    type_info bool_info = type_info::make_bool(string_symbolizer_);
    type_info_bool_ = &type_infos_.emplace(type_key{script_value_type::jai_bool_type, 0, 0}, bool_info).first->second;
    type_id_index_[type_info_bool_->id] = type_info_bool_;

    type_info char_info = type_info::make_char(string_symbolizer_);
    type_info_char_ = &type_infos_.emplace(type_key{script_value_type::jai_char_type, 0, 0}, char_info).first->second;
    type_id_index_[type_info_char_->id] = type_info_char_;

    type_info uint8_info = type_info::make_uint8(string_symbolizer_);
    type_info_uint8_ = &type_infos_.emplace(type_key{script_value_type::jai_int_type, uint8_info.id, 0}, uint8_info).first->second;
    type_id_index_[type_info_uint8_->id] = type_info_uint8_;
    type_info int8_info = type_info::make_int8(string_symbolizer_);
    type_info_int8_ = &type_infos_.emplace(type_key{script_value_type::jai_int_type, int8_info.id, 0}, int8_info).first->second;
    type_id_index_[type_info_int8_->id] = type_info_int8_;

    type_info void_info = type_info::make_void(string_symbolizer_);
    type_info_void_ = &type_infos_.emplace(type_key{script_value_type::jai_null_type, 0, 0}, void_info).first->second;
    type_id_index_[type_info_void_->id] = type_info_void_;

    type_info invalid_info = type_info::make_invalid(string_symbolizer_);
    type_info_invalid_ = &type_infos_.emplace(type_key{script_value_type::jai_invalid_type, 0, 0}, invalid_info).first->second;
    type_id_index_[type_info_invalid_->id] = type_info_invalid_;

    type_info any_info = type_info::make_any(string_symbolizer_);
    type_info_any_ = &type_infos_.emplace(type_key{script_value_type::jai_any_type, 0, 0}, any_info).first->second;
    type_id_index_[type_info_any_->id] = type_info_any_;

    // after the older canonical types, so their interned ids stay put
    const script_value_type math_kinds[6] = {script_value_type::jai_vec2_type, script_value_type::jai_vec3_type,
                                             script_value_type::jai_ivec2_type, script_value_type::jai_ivec3_type,
                                             script_value_type::jai_vec4_type, script_value_type::jai_color_type};
    for (size_t k = 0; k < 6; ++k) {
        type_info math_info = type_info::make_math(string_symbolizer_, math_kinds[k]);
        type_info_math_[k] = &type_infos_.emplace(type_key{math_kinds[k], 0, 0}, math_info).first->second;
        type_id_index_[type_info_math_[k]->id] = type_info_math_[k];
    }

    // Register standard C++ implicit conversions
    // NOTE: These conversions can't use engine reference yet because the engine isn't fully constructed
    // They will be updated to use proper engine references in initialize_engine_reference()
    // For now, we'll leave them empty and register them properly after engine construction
}

engine::implementation::~implementation() = default;

void*& engine::reference_holder_pool_slot() {
    return impl->ref_holder_pool_.pool;
}

void engine::set_reference_holder_pool_teardown(void (*teardown)(void*)) {
    impl->ref_holder_pool_.teardown = teardown;
}

void*& engine::cell_cycle_registry_slot() {
    return impl->cell_cycle_registry_.pool;
}

void engine::set_cell_cycle_registry_teardown(void (*teardown)(void*)) {
    impl->cell_cycle_registry_.teardown = teardown;
}

void*& engine::object_holder_pool_slot() {
    return impl->object_holder_pool_.pool;
}

void engine::set_object_holder_pool_teardown(void (*teardown)(void*)) {
    impl->object_holder_pool_.teardown = teardown;
}

void*& engine::vec_node_pool_slot() {
    return impl->vec_node_pool_.pool;
}

void engine::set_vec_node_pool_teardown(void (*teardown)(void*)) {
    impl->vec_node_pool_.teardown = teardown;
}

std::unique_lock<std::mutex> engine::parallel_instance_registry_lock() {
    return std::unique_lock<std::mutex>(parallel_state().instance_registry_mutex);
}

bool engine::park_parallel_registration(std::weak_ptr<class_instance>& instance) {
    if (!impl->parallel_ || !impl->parallel_->active_region) { return false; }
    const auto* chunk = impl->parallel_->active_region->find_entry(std::this_thread::get_id());
    if (!chunk || !chunk->pending_registrations) { return false; }
    chunk->pending_registrations->push_back(detail::pending_registration{ chunk->element(), std::move(instance) });
    return true;
}

uint32_t engine::parallel_region_serial() const {
    return impl->parallel_ && impl->parallel_->active_region ? impl->parallel_->region_serial : 0;
}

execution_backend* engine::region_chunk_backend() const {
    if (impl->parallel_ && impl->parallel_->active_region) {
        if (const auto* chunk = impl->parallel_->active_region->find_entry(std::this_thread::get_id()); chunk && chunk->backend) {
            return chunk->backend;
        }
    }
    return get_execution_backend();
}

void engine::recover_parallel_registrations() {
    if (impl->parallel_) { detail::recover_pending_registrations(*impl->parallel_); }
}

detail::math_rng_state& engine::math_rng() {
    return impl->math_rng_;
}

void engine::implementation::updateOverloadedFunction(const std::string& name, engine* engine_ptr) {
    // Create a dispatch function that selects the right overload
    script_function dispatcher = engine::overload_dispatch{ engine_ptr, string_symbolizer_.intern(name), name, &overloadedFunctions.at(name) };
    
    // Update in global environment
    script_value dispatcherValue = script_value::make_function(dispatcher, engine_ptr);
    global_environment_->define(name, dispatcherValue);
    if (engine_ptr) { engine_ptr->bump_definition_epoch(); }
}

// The one overload, typed, every argument of its parameter's exact kind: the selection findBestMatch makes, run on the
// arguments where they are (no vector, no hop)
bool engine::overload_dispatch::try_direct(const script_value* args, size_t argc, std::optional<checked_result<script_value>>& out) const {
    const auto* overloads = static_cast<const implementation::OverloadSet*>(set);
    if (!overloads || overloads->overloads.size() != 1) { return false; }
    const auto& only = overloads->overloads.front();
    if (!only.direct_target || only.argCount != argc || only.paramTypes.size() != argc) { return false; }
    for (size_t i = 0; i < argc; ++i) {
        const script_value_type want = only.paramTypes[i].base_type;
        if (!(want == script_value_type::jai_int_type || want == script_value_type::jai_float_type || want == script_value_type::jai_bool_type ||
              want == script_value_type::jai_char_type || want == script_value_type::jai_string_type) || args[i].deref().type() != want) { return false; }
    }
    const script_value pin = only.function;   // a re-registration during the call cannot free the running binding
    out.emplace(only.direct.invoke(only.direct_target, args, argc));
    return true;
}

bool engine::overload_dispatch::direct_entry(direct_snapshot& out) const {
    const auto* overloads = static_cast<const implementation::OverloadSet*>(set);
    if (!overloads || overloads->overloads.size() != 1) { return false; }
    const auto& only = overloads->overloads.front();
    if (!only.direct_target || only.argCount != only.paramTypes.size() || only.argCount > std::size(out.kinds)) { return false; }
    for (size_t i = 0; i < only.argCount; ++i) {
        const script_value_type want = only.paramTypes[i].base_type;
        if (!(want == script_value_type::jai_int_type || want == script_value_type::jai_float_type || want == script_value_type::jai_bool_type ||
              want == script_value_type::jai_char_type || want == script_value_type::jai_string_type)) { return false; }
        out.kinds[i] = want;
    }
    out.target = only.direct_target;
    out.invoke = only.direct.invoke;
    out.argc = only.argCount;
    out.owner = only.function;
    out.generation = &overloads->generation;
    out.at = overloads->generation;
    return true;
}

checked_result<script_value> engine::overload_dispatch::operator()(const std::vector<script_value>& args) const {
    const auto* overloads = static_cast<const implementation::OverloadSet*>(set);
    if (!overloads) {
        auto it = eng->impl->overloadedFunctions.find(name);
        if (it == eng->impl->overloadedFunctions.end()) {
            return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                "Overloaded function '{0}' not found", name_id);
        }
        overloads = &it->second;
    }

    if (std::optional<checked_result<script_value>> out; try_direct(args.data(), args.size(), out)) { return std::move(*out); }
    script_value bestMatch = overloads->findBestMatch(args, !eng->impl->string_symbolizer_.frozen());
    if (bestMatch.is_null()) {
        return detail::overload_miss(make_error_code(runtime_error_code::argument_count_mismatch),
            name_id, args.data(), args.size(), eng);
    }

    const script_function& func = bestMatch.as_function();
    return func(args);
}

uint8_t engine::overload_dispatch::argument_binding(size_t i, size_t argc) const {
    uint8_t bits = 0;
    if (auto it = eng->impl->overloadedFunctions.find(name); it != eng->impl->overloadedFunctions.end()) {
        for (const auto& overload : it->second.overloads) {
            if (argc != SIZE_MAX && overload.argCount != argc) { continue; }
            bits |= static_cast<uint8_t>(detail::callee_binding(overload.function, i, false, argc));
        }
    }
    return bits ? bits : static_cast<uint8_t>(detail::arg_binding::opaque);
}

namespace detail {

host_reference host_reference_argument(engine* eng, const std::vector<script_value>& args, size_t index, size_t offset) {
    host_reference ref{ script_value(std::monostate{}, eng), script_value(std::monostate{}, eng) };
    execution_backend* backend = eng ? eng->get_execution_backend() : nullptr;
    if (!backend || eng->get_symbolizer()->frozen()) {
        return ref;   // parallel workers never write through a host reference
    }
    host_slot slot;
    auto resolved = backend->host_reference_argument(args, index, offset, slot);
    if (!resolved) {
        throw runtime_error(format_error_message_numeric(resolved.message(), resolved.symbol_id(), resolved.symbol_id2()));
    }
    if (slot.frame) {
        ref.value = std::move(resolved.value());
        ref.slot = slot;
        return ref;
    }
    script_value target = std::move(resolved.value());
    if (!target.is_reference()) {
        // no script site named the argument: a reference the caller passed (a builtin hands its callback its elements)
        const script_value& received = args[index + offset];
        if (!received.is_reference() || !received.get_reference_holder()) {
            return ref;
        }
        target = received;
    }
    auto current = ref_checked_deref(target);
    if (!current) {
        throw runtime_error(format_error_message_numeric(current.message(), current.symbol_id(), current.symbol_id2()));
    }
    ref.value = std::move(current.value());
    ref.target = std::move(target);
    return ref;
}

checked_result<void> host_reference_store(engine* eng, const host_slot& slot, script_value& target, script_value&& value) {
    if (slot.frame) {
        execution_backend* backend = eng->get_execution_backend();
        return backend ? backend->host_slot_store(slot, value) : checked_result<void>{};
    }
    try {
        return ref_store_through(target, value, eng, eng->get_symbolizer());
    } catch (const std::exception& e) {
        return host_exception_error(eng, e.what());
    }
}

}

engine::engine() : impl(std::make_unique<implementation>()) {}

bool engine_loading(const engine* a_engine) {
    return a_engine && a_engine->loading();
}

engine::load_session::load_session(engine* a_engine) : eng(a_engine) {
    if (eng) { ++eng->load_depth_; }
}

engine::load_session::~load_session() {
    if (!eng || completed) { return; }
    if (--eng->load_depth_ == 0) {
        eng->deferred_post_loads_.clear();
        eng->load_field_orders_.clear();
    }
}

const std::vector<engine::load_field>& engine::load_field_order(const class_definition& cls) {
    if (load_depth_ == 0) { load_field_orders_.clear(); }   // kept only while a load is in progress (a reload may follow)
    auto it = load_field_orders_.find(&cls);
    if (it != load_field_orders_.end()) { return it->second; }
    std::vector<load_field> order;
    for (const std::string& name : cls.get_property_names()) {
        order.push_back(load_field{symbolize(name), symbolize("_set_" + name), script_value(name, this)});
    }
    return load_field_orders_.emplace(&cls, std::move(order)).first->second;
}

void engine::load_session::complete() {
    if (!eng || completed) { return; }
    completed = true;
    if (--eng->load_depth_ > 0) { return; }
    eng->load_field_orders_.clear();
    // the outermost load is done and its objects are whole: their post_load hooks run once each in completion order,
    // outside the load (a hook's writes notify as usual, and a hook that loads again gets a load of its own)
    std::vector<std::pair<script_value, script_int>> hooks = std::move(eng->deferred_post_loads_);
    eng->deferred_post_loads_.clear();
    const uint64_t post_load_id = eng->symbolize("post_load");
    for (auto& [self, version] : hooks) {
        auto instance = self.is_object() ? self.get_class_instance() : nullptr;
        auto* def = instance ? instance->get_class_definition() : nullptr;
        script_value post_load = def ? def->get_method(post_load_id, false) : script_value(std::monostate{}, eng);
        if (post_load.type() != script_value_type::jai_function_type) { continue; }
        auto ran = post_load.as_function()({ self, script_value(version, eng) });
        if (!ran) { throw runtime_error(eng->format_error(ran)); }
        if (eng->backend_unwinding()) { throw runtime_error(eng->backend_exception_text()); }
    }
}

void engine::defer_post_load(const script_value& self, script_int version) {
    if (load_depth_ > 0) {
        deferred_post_loads_.emplace_back(self, version);
        return;
    }
    load_session session(this);
    deferred_post_loads_.emplace_back(self, version);
    session.complete();
}

std::weak_ptr<const void> engine_lifetime_token(engine* a_engine) {
    return a_engine ? a_engine->lifetime_token() : std::weak_ptr<const void>();
}

// Reset the backend before impl members unwind so callable thunks (destructor deleters,
// stored function values) firing during teardown see get_execution_backend() == nullptr
// and fail soft instead of re-entering a half-destroyed backend.
engine::~engine() {
    if (impl) {
        // Tear the transport down first (join its thread, detach the controller) so no
        // parked script thread stays blocked in the hook once anything it touches dies.
        if (impl->debug_transport_) {
            impl->debug_transport_->stop();
            impl->debug_transport_.reset();
        }
        // Program-scope variables die in reverse construction order while the engine still runs their destructors;
        // a destructor's error is reported through the script error handler, never propagated
        if (impl->backend && destructors_armed_ && impl->global_environment_) {
            try { impl->global_environment_->release_values_in_reverse(); } catch (...) {}
        }
        impl->tearing_down = true;
        impl->backend.reset();
        // Without a destructor nothing a value releases runs script: program-scope values die while their classes still
        // reach the engine, so a long chain releases on the release stack
        if (!destructors_armed_ && impl->global_environment_) { impl->global_environment_->clear_values(); }
        // Definitions a host or a captured environment keeps alive past this point must not
        // consult the engine from an instance destructor (registry_lock reads it)
        for (auto& [name, def] : impl->classes) { if (def) { def->detach_for_teardown(); } }
        for (auto& [id, def] : impl->classesByTypeId) { if (def) { def->detach_for_teardown(); } }
        for (auto& [type, def] : impl->classesByType) { if (def) { def->detach_for_teardown(); } }
        for (auto& [name, def] : impl->class_registry_.script_classes_) { if (def) { def->detach_for_teardown(); } }
        // Program-scope values close cycles through the environment (a class's methods and a closure reach it as their
        // definition scope while it holds them): emptying it lets every one of them die with the engine
        if (impl->global_environment_) { impl->global_environment_->clear_values(); }
        for (auto& [name, def] : impl->class_registry_.script_classes_) { if (def) { def->release_values_for_teardown(); } }
        for (auto& [name, def] : impl->classes) { if (def && def->is_script_class()) { def->release_values_for_teardown(); } }
        script_value::release_deferred_cell_cycles(this);
    }
}

execution_backend* engine::backend() const {
    if (!impl->backend) {
        if (impl->tearing_down) {
            return nullptr;
        }
        auto* self = const_cast<engine*>(this);
        if (impl->current_backend_type == backend_type::vm) {
            impl->backend = create_vm_backend();
        } else {
            impl->backend = std::make_unique<interpreter>(&impl->string_symbolizer_, impl->global_environment_);
        }
        self->wire_backend();
    }
    return impl->backend.get();
}

int engine::select_cpp_overload(const std::vector<script_value>& args,
                                const std::vector<std::vector<param_type_info>>& candidateParamTypes) const {
    // args[0] is 'this'; rank each candidate's C++ parameter signature against args[1..] using the
    // same scorer as free-function overloads. Empty signatures are untyped (legacy) entries that
    // stay viable at a high cost so a typed overload always wins. Returns the best index, or -1.
    int bestIdx = -1;
    int bestCost = std::numeric_limits<int>::max();
    for (size_t c = 0; c < candidateParamTypes.size(); ++c) {
        const auto& sig = candidateParamTypes[c];
        int cost = sig.empty()
            ? 1000
            : compute_param_match_cost(args, 1, sig, impl->conversions.get(), &impl->classesByType, &impl->classes);
        if (cost >= 0 && cost < bestCost) {
            bestCost = cost;
            bestIdx = static_cast<int>(c);
        }
    }
    return bestIdx;
}

void engine::initialize_engine_reference() {
    library_registration library(*this);
    // Pass the engine reference to the conversion registry
    impl->conversions->set_engine(this);

    // Initialize cached operator symbol IDs for fast operator overload lookup
    impl->op_plus_id_ = impl->string_symbolizer_.intern("+");
    impl->op_minus_id_ = impl->string_symbolizer_.intern("-");
    impl->op_star_id_ = impl->string_symbolizer_.intern("*");
    impl->op_slash_id_ = impl->string_symbolizer_.intern("/");
    impl->op_percent_id_ = impl->string_symbolizer_.intern("%");
    impl->op_less_id_ = impl->string_symbolizer_.intern("<");
    impl->op_less_equal_id_ = impl->string_symbolizer_.intern("<=");
    impl->op_greater_id_ = impl->string_symbolizer_.intern(">");
    impl->op_greater_equal_id_ = impl->string_symbolizer_.intern(">=");
    impl->op_equal_equal_id_ = impl->string_symbolizer_.intern("==");
    impl->op_bang_equal_id_ = impl->string_symbolizer_.intern("!=");
    impl->op_spaceship_id_ = impl->string_symbolizer_.intern("<=>");
    impl->op_ampersand_id_ = impl->string_symbolizer_.intern("&");
    impl->op_pipe_id_ = impl->string_symbolizer_.intern("|");
    impl->op_caret_id_ = impl->string_symbolizer_.intern("^");
    impl->op_left_shift_id_ = impl->string_symbolizer_.intern("<<");
    impl->op_right_shift_id_ = impl->string_symbolizer_.intern(">>");
    impl->post_load_id_ = impl->string_symbolizer_.intern("post_load");

    // Now that we have a proper engine reference, add conversions and functions that create script_value
    auto engine_weak = this;
    
    // Set up custom extractor for class_instance objects in the conversion registry
    impl->conversions->set_custom_extractor([this](std::string_view type_name, std::shared_ptr<void> obj) -> std::shared_ptr<void> {
        // dynamic_binder creates objects with type_name matching the class name
        auto classIt = impl->classes.find(type_name);
        if (classIt != impl->classes.end()) {
            // This is a registered class, obj should be a class_instance
            auto instance = std::static_pointer_cast<class_instance>(obj);

            if (instance->script_layer_retired()) { instance->throw_retired_script_layer(); }
            script_value cppObjValue = instance->native_or_throw();
            if (!cppObjValue.is_null() && cppObjValue.type() == script_value_type::jai_object_type) {
                // Direct access to the object_holder to avoid recursive extraction. An extended
                // object's holder is non-owning: hand its pointer out as an unowned alias (the
                // shared_ptr extraction path upgrades it through engine_lock_instance_cpp_object).
                auto objHolder = cppObjValue.get_object_holder();
                if (objHolder->data) { return objHolder->data; }
                if (objHolder->bound_ptr) { return std::shared_ptr<void>(std::shared_ptr<void>(), objHolder->bound_ptr); }
            }
        }
        return nullptr;
    });
    
    // Register standard container conversions for common types
    conversions::conversion_manager conv_mgr(impl->conversions, this);
    
    // Vector conversions for common types
    conv_mgr.add_vector_conversion<int>();           // Platform int (usually int32_t)
    conv_mgr.add_vector_conversion<int32_t>();       // Explicit 32-bit
    conv_mgr.add_vector_conversion<int64_t>();       // Explicit 64-bit (same as script_int)
    conv_mgr.add_vector_conversion<float>();         // 32-bit float
    conv_mgr.add_vector_conversion<double>();        // 64-bit double (same as script_float)
    conv_mgr.add_vector_conversion<char>();          // Single character
    conv_mgr.add_vector_conversion<std::string>();   // Strings
    conv_mgr.add_vector_conversion<bool>();          // Booleans
    
    // Map conversions for common key/value combinations
    // String keys (most common)
    conv_mgr.add_map_conversion<std::string, int>();
    conv_mgr.add_map_conversion<std::string, int32_t>();
    conv_mgr.add_map_conversion<std::string, int64_t>();
    conv_mgr.add_map_conversion<std::string, double>();
    conv_mgr.add_map_conversion<std::string, float>();
    conv_mgr.add_map_conversion<std::string, std::string>();
    conv_mgr.add_map_conversion<std::string, bool>();
    
    // Integer keys
    conv_mgr.add_map_conversion<int, int>();
    conv_mgr.add_map_conversion<int, int32_t>();
    conv_mgr.add_map_conversion<int, int64_t>();
    conv_mgr.add_map_conversion<int, double>();
    conv_mgr.add_map_conversion<int, float>();
    conv_mgr.add_map_conversion<int, std::string>();
    conv_mgr.add_map_conversion<int, bool>();
    
    conv_mgr.add_map_conversion<int32_t, int>();
    conv_mgr.add_map_conversion<int32_t, int32_t>();
    conv_mgr.add_map_conversion<int32_t, int64_t>();
    conv_mgr.add_map_conversion<int32_t, double>();
    conv_mgr.add_map_conversion<int32_t, std::string>();
    
    conv_mgr.add_map_conversion<int64_t, int>();
    conv_mgr.add_map_conversion<int64_t, int32_t>();
    conv_mgr.add_map_conversion<int64_t, int64_t>();
    conv_mgr.add_map_conversion<int64_t, double>();
    conv_mgr.add_map_conversion<int64_t, std::string>();
    
    // Double keys
    conv_mgr.add_map_conversion<double, double>();
    conv_mgr.add_map_conversion<double, int>();
    conv_mgr.add_map_conversion<double, std::string>();
    
    // Nested vector conversions
    conv_mgr.add_vector_conversion<std::vector<int>>();
    conv_mgr.add_vector_conversion<std::vector<double>>();
    conv_mgr.add_vector_conversion<std::vector<std::string>>();
    
    // Map of vectors
    conv_mgr.add_map_conversion<std::string, std::vector<int>>();
    conv_mgr.add_map_conversion<std::string, std::vector<double>>();
    conv_mgr.add_map_conversion<std::string, std::vector<std::string>>();
    
    // Bound array conversions for zero-copy performance
    conv_mgr.add_bound_array_conversion<int>();
    conv_mgr.add_bound_array_conversion<int32_t>();
    conv_mgr.add_bound_array_conversion<int64_t>();
    conv_mgr.add_bound_array_conversion<float>();
    conv_mgr.add_bound_array_conversion<double>();
    conv_mgr.add_bound_array_conversion<char>();
    conv_mgr.add_bound_array_conversion<std::string>();
    conv_mgr.add_bound_array_conversion<bool>();
    
    // Bound map conversions
    conv_mgr.add_bound_map_conversion<std::string, int>();
    conv_mgr.add_bound_map_conversion<std::string, int64_t>();
    conv_mgr.add_bound_map_conversion<std::string, double>();
    conv_mgr.add_bound_map_conversion<std::string, float>();
    conv_mgr.add_bound_map_conversion<std::string, std::string>();
    conv_mgr.add_bound_map_conversion<std::string, bool>();
    
    // Register custom conversions for numeric types
    // int32_t conversion with bounds checking
    conv_mgr.add_custom_conversion<int32_t>(
        [](const script_value& v) -> int32_t {
            script_int val = v.as_int();
            if (val > std::numeric_limits<int32_t>::max() || val < std::numeric_limits<int32_t>::min()) {
                throw std::overflow_error("Value " + std::to_string(val) + " is out of range for int32_t");
            }
            return static_cast<int32_t>(val);
        },
        [engine_weak](const int32_t& val) -> script_value {
            return script_value(static_cast<script_int>(val), engine_weak);
        }
    );
    
    // int64_t is the same as script_int, so direct conversion
    conv_mgr.add_custom_conversion<int64_t>(
        [](const script_value& v) -> int64_t {
            return v.as_int();
        },
        [engine_weak](const int64_t& val) -> script_value {
            return script_value(val, engine_weak);
        }
    );
    
    // float conversion with potential precision loss
    conv_mgr.add_custom_conversion<float>(
        [](const script_value& v) -> float {
            return static_cast<float>(v.as_float());
        },
        [engine_weak](const float& val) -> script_value {
            return script_value(static_cast<script_float>(val), engine_weak);
        }
    );

    // Register standard container conversions now that we have engine reference
    add_standard_conversions();

    // BOOTSTRAP: Manually register class_definition to avoid chicken-and-egg problem
    // We can't use dynamic_binder because it would try to create a script_value wrapping
    // a class_definition, which requires class_definition to already be registered!
    {
        auto class_def = std::make_shared<class_definition>("class_definition",
            impl->class_definition_type_id_, class_definition::cpp_class, engine_weak);
        // Manually set the persistent type_info using engine's interning
        class_def->set_type_info(get_type_info_object("class_definition"));
        // Register in all the maps
        impl->classes["class_definition"] = class_def;
        impl->classesByTypeId[impl->class_definition_type_id_] = class_def;
        auto reg_result = impl->class_registry_.register_cpp_class(class_def);
        if (!reg_result) {
            throw runtime_error("Failed to register class_definition metaclass during bootstrap");
        }
    }

    // Register int64_t to int conversion with bounds checking
    conversions::conversion_manager conv_manager(impl->conversions, this);
    conv_manager.add_custom_conversion<int>(
        [](const script_value& v) -> int {
            script_int val = v.as_int();  // Use as_int() instead of as<int64_t>()
            if (val > std::numeric_limits<int>::max() || val < std::numeric_limits<int>::min()) {
                throw std::overflow_error("Value " + std::to_string(val) + " is out of range for int");
            }
            return static_cast<int>(val);
        },
        [engine_weak](const int& val) -> script_value {
            return script_value(static_cast<script_int>(val), engine_weak);
        }
    );
    
    // Add built-in functions
    add_variadic_function("print", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
        std::string line;
        for (const auto& arg : args) {
            if (arg.deref().type() != script_value_type::jai_object_type) {
                line += arg.to_string();
                continue;
            }
            auto text = engine_weak->text_of(arg);   // a class's own to_string() speaks for its instances
            if (!text) { return text.error_value(); }
            if (engine_weak->backend_unwinding()) { return script_value(std::monostate{}, engine_weak); }
            line += text.value();
        }
        auto& out = engine_weak->get_output_stream();
        out << line;
        out << std::endl;
        return script_value(std::monostate{}, engine_weak);
    });
    
    // Template-string format specs (`${hp:.1f}`) desugar to this call at lex time; it is
    // engine-core (not stdlib) so the language feature works on every engine. Directly
    // callable too: format_value(3.14159, ".2f") -> "3.14".
    add_variadic_function("format_value", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2 || !args[1].is_string()) {
            throw runtime_error("format_value expects (value, spec_string)");
        }
        const std::string& raw = args[1].as_string();
        detail::format_spec spec;
        if (!detail::parse_format_spec(raw, spec)) {
            throw runtime_error("unsupported format spec ':" + raw + "'");
        }
        return script_value(detail::format_value_with_spec(args[0], spec, raw), engine_weak);
    });

    // same_object(x, y): the identity question `==` no longer asks (== is memberwise on
    // arrays, maps and class instances). References resolve to their referents, shared_ptr
    // handles to the shared object; primitives and strings carry no identity and compare
    // as values (detail/structural_equality.hpp). Engine-core so it exists on every engine.
    add_variadic_function("same_object", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            throw runtime_error("'same_object' expects 2 arguments, got " + std::to_string(args.size()));
        }
        return script_value(detail::same_object(args[0], args[1]), engine_weak);
    });

    // Locations (docs/site/guide/10-modules.html "Where files are"): where the running code, the
    // program's root file and the executable are, and the same resolution ladder import walks.
    // Engine-core so a program answers the same on every host; the file IO that USES a path
    // (read_file, write_file, list_dir) stays the host's to grant. Defined after the impl so
    // the helpers above this constructor's TU position are visible: see location builtins below.
	detail::register_math_namespace(*this);
	detail::register_array_namespace(*this);
	detail::register_map_namespace(*this);
    register_privilege_builtins();
    register_location_builtins();

    // extend(object, Class): a script class layer over a C++ object, or a re-derived script
    // instance (docs/site/embedding/13-script-extensible.html). Engine-core so the language feature exists on
    // every engine.
    add_variadic_function("extend", [engine_weak](const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.size() != 2) {
            return checked_result<script_value>(make_error_code(runtime_error_code::extend_rejected), "extend expects (object, Class)");
        }
        auto extended = engine_weak->extend_checked(args[0], args[1], std::string());
        if (!extended) { return extended; }
        // A pure script value is C++'s T& extend(T& object, Class): the result names the argument, so `var w = extend(v, B)`
        // copies and `var& w = extend(v, B)` aliases v (the call-result rule)
        const script_value& object = args[0].deref();
        auto holder = object.raw_storage_index() == script_value::TYPEID_OBJECT && !object.is_shared_ptr_type() ? object.get_object_holder() : nullptr;
        if (!holder || !holder->is_class_instance_wrapper || !holder->data ||
            static_cast<class_instance*>(holder->data.get())->has_cpp_object()) {
            return extended;
        }
        if (auto* backend = engine_weak->get_execution_backend()) {
            host_slot slot;
            auto named = backend->host_reference_argument(args, 0, 0, slot);
            if (named && named.value().is_reference() && detail::same_object(named.value(), extended.value())) { return std::move(named.value()); }
        }
        return script_value::make_cell_reference(std::move(extended.value()), engine_weak);
    });

    // Parallel builtins (parallel_transform v0; internal/parallel_design.md). thread_count()
    // reports the worker count a region uses without constructing the pool.
    add_function("thread_count", [this]() -> script_int {
        return static_cast<script_int>(parallel_thread_count());
    });
    add_variadic_function("parallel_transform", [this](const std::vector<script_value>& args) -> checked_result<script_value> {
        return detail::run_parallel_transform(*this, args);
    });
    add_functionWithArity("parallel_reduce", [this](const std::vector<script_value>& args) -> checked_result<script_value> {
        return detail::run_parallel_reduce(*this, args);
    }, 4);

    // binary::reader / writer / layout and the width tokens (detail/binary_data.hpp): string's reader() builtin
    // returns a binary::reader, so the library exists on every engine
    binary::register_library(*this);

    // Note: weak_ptr and shared_ptr are now handled as type constructors in the interpreter
    // They are keywords and follow C++ semantics:
    // - weak_ptr<T> var;           // Creates empty weak_ptr
    // - weak_ptr<T> var = obj;     // Creates weak_ptr from object
    // - weak_ptr<T>(obj)           // Constructor syntax
    // - shared_ptr<T> var;         // Creates empty shared_ptr  
    // - shared_ptr<T>(obj)         // Constructor syntax
    
    // weak_ptr methods are now registered as instance methods on weak_ptr objects
    // See interpreter::weak_ptr_methods_ for lock() and expired() implementations

    // expired() is now a method on weak_ptr objects - see interpreter::weak_ptr_methods_
}


script_value engine::execute(const std::string& scriptContent) {
    return execute(scriptContent, instance_variables{});
}

script_value engine::execute(const std::string& scriptContent, const instance_variables& instanceVars) {
    return execute_source(scriptContent, instanceVars, "<script>");
}

script_value engine::execute_file_source(const std::string& resolvedPath, const std::string& content) {
    return execute_source(content, instance_variables{}, resolvedPath);
}

bool engine::read_include_file(const std::string& resolvedPath, std::string& content) {
    uint64_t mtime = 0, size = 0;
    const bool stamped = impl->stat_mtime_size(resolvedPath, mtime, size);
    const auto read = impl->read_under_stamp(resolvedPath, stamped, mtime, size, content);
    if (read == implementation::stamped_read::missing) { return false; }
    impl->note_closure_file(resolvedPath, stamped && read == implementation::stamped_read::stable, mtime, size);
    return true;
}

script_value engine::execute_include_source(const std::string& resolvedPath, const std::string& content, std::string_view scope) {
    return execute_source(content, instance_variables{}, resolvedPath, true, scope);
}

script_value engine::execute(script_source source) {
    if (source.literal()) {
        auto it = impl->literal_script_cache.find({source.data(), source.size()});
        if (it != impl->literal_script_cache.end()) {
            if (it->second->epoch == impl->script_cache_epoch && !impl->spliced_changed(*it->second)) {   // a spliced include's change re-parses on this tier too
                auto entry = it->second;
                return impl->execute_cache_hit(*this, *entry, instance_variables{},
                                               std::string_view(source.data(), source.size()));
            }
            impl->literal_script_cache.erase(it);
        }
    }
    std::string content(source.data(), source.size());
    script_value result = execute_source(content, instance_variables{}, "<script>");
    if (source.literal()) {
        if (auto entry = impl->find_cached_script("<script>", content)) {
            if (impl->literal_script_cache.size() >= implementation::script_cache_max) {
                impl->literal_script_cache.clear();   // simple bound; repopulates on use
            }
            impl->literal_script_cache.emplace(
                implementation::literal_key{source.data(), source.size()}, std::move(entry));
        }
    }
    return result;
}

script_value engine::execute_source(const std::string& scriptContent, const instance_variables& instanceVars, const std::string& sourcePath, bool textual, std::string_view include_scope) {
    impl->has_executed_ = true;
    impl->note_root_source(sourcePath);
    try {
        // Prepare backend for new execution
        backend()->prepare_for_execution();

        // Identical source re-executes through its cached parse (and, on the vm, its
        // cached compiled chunk) — same semantics as re-executing a jaibite. Keyed on
        // (sourcePath, content): the same text under two paths gets two ASTs, each
        // stamped with its own filename (breakpoints/traces stay distinct). The jaibite
        // sibling is real-file-path only ("<script>" is the raw-content sentinel; string
        // roots have no sibling; dependencies can still load files — engine.hpp doc).
        // Disabling filesystem skips both sidecar probes and writes. A parse failure surfaces as
        // parse_error carrying the interned detail (file:line:col + message per error).
        std::filesystem::path sibling;
        bool siblingFresh = false;
        auto cached = impl->acquire_entry(*this, scriptContent, sourcePath, sibling, siblingFresh);
        for (const auto& s : cached->spliced_stamps) { impl->note_closure_file(s.path, true, s.mtime, s.size); }

        // Static-check hook: one place, at the parse-cache entry — covers execute(),
        // hot-reload re-parses (new source = new entry) and registration-epoch changes.
        // Runs in every mode (the proof pass rides it); diagnostics surface under warn/strict.
        const check_report& rep = impl->checked_report(*this, *cached, impl->static_check_mode_ != check_mode::off);
        if (impl->static_check_mode_ != check_mode::off) {
            // a unit from a file (the root file, an include, an import) lists under its path
            const std::string_view listing_name = sourcePath.empty() ? std::string_view("<script>") : std::string_view(sourcePath);
            if (instanceVars.empty()) {
                impl->gate_checked(rep, scriptContent, listing_name);
            } else {
                // host-supplied instance variables are per-call: check fresh with those
                // names defined instead of caching a result that depends on them
                impl->gate_checked(impl->check_with_instance_vars(*this, *cached, instanceVars), scriptContent, listing_name);
            }
            if (!include_scope.empty()) { impl->warn_scoped_enums(*this, *cached, sourcePath, include_scope); }
        }

        // Rewrite a missing/stale/rejected sibling AFTER the check hook so a clean check
        // stamps checked_clean, and BEFORE execution on purpose: a runtime error doesn't
        // invalidate a good parse.
        if (!sibling.empty() && !siblingFresh && cached->spliced_stamps.empty()) {   // a unit with spliced includes keeps no sibling: the sibling's freshness knows only its own file
            impl->write_jaibite_sibling(*this, sibling, *cached, sourcePath, scriptContent);
        }

        // a textual include runs under the including unit and compiles once per such unit: the VM's chunk caches belong to one unit
        std::shared_ptr<void>& compiled_slot = textual ? cached->textual_compiled[backend()->current_unit().id] : cached->compiled;
        return impl->run_unit(*this, *cached, compiled_slot, instanceVars.empty() ? nullptr : &instanceVars, scriptContent, textual);

    } catch (const parse_error&) {
        // Parse errors should propagate as-is (don't wrap compilation errors)
        backend()->prepare_for_execution();
        throw;
    }
}

script_value engine::execute_parsed(const std::vector<declaration_ptr>& declarations,
                                    std::shared_ptr<void>& compiled_slot,
                                    const instance_variables* instanceVars, uint32_t unit, bool textual) {
    ++impl->definition_epoch_;   // a run may (re)define anything a cached cross-function proof relied on
    // after a nested run the ambient re-aligns with the frame that resumes (the run may have made the first shadow under it)
    struct unit_scope { string_symbolizer& s; unit_stamp prev; execution_backend* be; ~unit_scope() { s.set_current_unit(prev); if (be) { be->sync_unit(); } } } unitScope{ impl->string_symbolizer_, impl->string_symbolizer_.current_unit(), backend() };
    if (!textual) {   // an include runs under the including unit
        const unit_stamp stamp{ unit, impl->string_symbolizer_.next_execution() };
        impl->unit_first_run_.emplace(stamp.id, stamp.seq);
        impl->string_symbolizer_.set_current_unit(stamp);
    }
    try {
        execution_backend* be = backend();
        if (!be->is_executing()) { note_host_entry(); }

        // Push scope for instance variables if any
        bool hasInstanceVars = instanceVars && !instanceVars->empty();
        struct instance_scope_guard { execution_backend* be; ~instance_scope_guard() { if (be) { be->pop_instance_scope(); } } } instanceScope{ hasInstanceVars ? be : nullptr };
        if (hasInstanceVars) {
            be->push_instance_scope();
            for (const auto& [name, value] : *instanceVars) {
                be->define_variable(name, value);
            }
        }

        script_value result = textual ? be->execute_textual(declarations, compiled_slot) : be->execute(declarations, compiled_slot);

        if (be->is_unwinding()) {
            if (be->is_executing()) { be->park_nested_failure(); }
            throw without_category_suffix(be->get_current_exception());
        }

        // No need to sync globals - they're already in the shared environment!


        return result;

    } catch (const script_exception&) {
        // Script exceptions bubble up to C++
        backend()->prepare_for_execution();
        throw;
    } catch (const std::runtime_error& e) {
        // Wrap runtime C++ exceptions as script exceptions for consistency
        backend()->prepare_for_execution();
        throw script_exception(std::string("C++ exception: ") + e.what());
    } catch (const std::exception& e) {
        // Wrap other C++ exceptions with their actual message
        backend()->prepare_for_execution();
        throw script_exception(std::string("C++ exception: ") + e.what());
    }
}

jai::jaibite engine::jaibite(const std::string& scriptContent) {
    lexer lexer(scriptContent, &impl->string_symbolizer_);
    auto tokens = lexer.tokenize();
    parser parser(tokens, &impl->string_symbolizer_, this, impl->registeredTemplateTypes);
    auto parse_result = parser.parse();
    if (!parse_result) {
        throw parse_error(format_error(parse_result));
    }
    auto declarations = std::move(parse_result.value());
    auto hook = impl->take_reload_hook(declarations);
    jai::jaibite bite(weak_from_this(), std::move(declarations));
    bite.reload_hook_ = std::move(hook);
    check_bite(bite, &scriptContent);
    return bite;
}

// Static-check a bite once (warn/strict only - a bite is checked once, diagnostics never
// re-trigger) and gate under strict; the proof pass re-stamps whenever the switch or the
// surface epoch moved, in every mode. Source is only available at creation time (loaded
// bites format their listing without snippets).
void engine::check_bite(jai::jaibite& bite, const std::string* source) {
    if (!bite.lowering_) { bite.lowering_ = std::make_shared<detail::aggregate_lowering_state>(); }
    auto& lowering = *std::static_pointer_cast<detail::aggregate_lowering_state>(bite.lowering_);
    const uint64_t want_proof_epoch = impl->type_proofs_ ? impl->check_surface_epoch_ : UINT64_MAX;
    const bool need_check = impl->static_check_mode_ != check_mode::off &&
                            bite.check_state_ == jaibite::check_state::unchecked;
    const bool need_proofs = bite.proof_epoch_ != want_proof_epoch || !bite.autos_typed_ ||
                             (impl->lowering_on() && !lowering.built) || lowering.lowered_view != impl->lowering_on();
    if (need_check) { detail::set_aggregate_view(lowering, false); }   // diagnostics read the parsed bodies
    if (need_check || need_proofs) {
        detail::check_options o;
        o.diagnostics = need_check;
        o.proofs = need_check ? detail::check_options::proofs_t::leave : impl->type_proofs_ ? detail::check_options::proofs_t::stamp : detail::check_options::proofs_t::clear;
        o.stats = &impl->last_proof_stats_;
        o.program_names = &impl->program_names_;
        o.program_classes = &impl->program_classes_;
        o.program_enums = &impl->program_enums_;
        o.program_functions = &impl->program_functions_;
        o.program_globals = &impl->program_globals_;
        if (!need_check) { impl->lowering_view(*this, bite.declarations_, lowering); }   // this walk is the proof pass
        check_report r = detail::run_static_check(*this, bite.declarations_, nullptr, o);
        if (need_check) {
            bite.check_report_ = std::move(r);
            bite.check_state_ = bite.check_report_.has_errors()
                ? jaibite::check_state::flagged : jaibite::check_state::clean;
            detail::check_options p = o;   // the proof pass alone reads the program's globals and types the auto slots
            p.diagnostics = false;
            p.proofs = impl->type_proofs_ ? detail::check_options::proofs_t::stamp : detail::check_options::proofs_t::clear;
            impl->lowering_view(*this, bite.declarations_, lowering);
            detail::run_static_check(*this, bite.declarations_, nullptr, p);
        }
        bite.proof_epoch_ = want_proof_epoch;
        bite.autos_typed_ = true;
    }
    if (impl->static_check_mode_ == check_mode::off) { return; }
    impl->last_check_ = bite.check_report_;
    if (impl->gates(bite.check_report_)) {
        throw static_check_error("static check failed:\n" +
                                 bite.check_report_.format(source ? std::string_view(*source) : std::string_view{}));
    }
}

script_value engine::execute(jai::jaibite& bite) {
    impl->has_executed_ = true;
    check_bite(bite, nullptr);   // diagnostics once (warn/strict); proofs re-stamp on surface moves
    backend()->prepare_for_execution();
    if (bite.unit_ == 0) { bite.unit_ = ++impl->unit_count_; }
    return execute_parsed(bite.declarations_, bite.compiled_, nullptr, bite.unit_);
}

script_value jaibite::execute() {
    auto owner = engine_.lock();
    if (!owner) {
        throw script_exception("jaibite: owning engine no longer exists");
    }
    return owner->execute(*this);
}

std::vector<uint8_t> jaibite::save_bytes() const {
    auto owner = engine_.lock();
    if (!owner) {
        throw script_exception("jaibite: owning engine no longer exists");
    }
    uint32_t flags = (check_state_ == check_state::clean) ? detail::jaibite_checked_clean_flags() : 0;
    detail::aggregate_source_view source(lowering_ ? std::static_pointer_cast<detail::aggregate_lowering_state>(lowering_).get() : nullptr);
    if (!reload_hook_) { return detail::serialize_jaibite(declarations_, *owner->get_symbolizer(), owner->registration_fingerprint(), flags); }
    std::vector<declaration_ptr> with_hook = declarations_;
    with_hook.push_back(reload_hook_);
    return detail::serialize_jaibite(with_hook, *owner->get_symbolizer(), owner->registration_fingerprint(), flags);
}

void jaibite::save(const std::string& path) const {
    auto owner = engine_.lock();
    if (!owner) { throw script_exception("jaibite: owning engine no longer exists"); }
    auto allowed = owner->require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(owner->format_error(allowed)); }
    auto bytes = save_bytes();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        throw runtime_error("jaibite: failed to open file for writing: " + path);
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good()) {
        throw runtime_error("jaibite: failed to write file: " + path);
    }
}

jai::jaibite engine::jaibite_load(const std::string& path) {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(format_error(allowed)); }
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw runtime_error("jaibite: failed to open file: " + path);
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return jaibite_load_bytes(bytes.data(), bytes.size(), path);
}

jai::jaibite engine::jaibite_load_bytes(const uint8_t* data, size_t size, const std::string& own_path) {
    uint64_t saved_fingerprint = 0;
    uint32_t flags = 0;
    auto declarations = detail::deserialize_jaibite(data, size, this, saved_fingerprint, &flags, nullptr, own_path);
    // jaibite carries structure, not the parser-pass flags — re-derive them (see
    // try_load_jaibite_entry's twin)
    if (detail::run_ast_marking_passes(declarations, get_symbolizer())) { note_execute_elapsed_use(); }
    auto hook = impl->take_reload_hook(declarations);
    jai::jaibite bite(weak_from_this(), std::move(declarations));
    bite.reload_hook_ = std::move(hook);
    bite.registration_mismatch_ = (saved_fingerprint != registration_fingerprint());
    // Trust the checked-clean stamp only when the registration surface matches the
    // saving engine's; otherwise the bite loads unchecked and is re-checked here
    // (relocated AST, this engine's surface) when a checking mode is active.
    if (detail::jaibite_stamped_clean(flags) && !bite.registration_mismatch_) {
        bite.check_state_ = jaibite::check_state::clean;
    }
    check_bite(bite, nullptr);
    return bite;
}

void engine::record_field_migration(std::string_view class_name, uint64_t field_id, field_migration_outcome outcome,
                                    const script_value* before, const script_value* after) {
    if (!hot_reload_reporting_) { return; }
    auto type_name = [this](const script_value* stored) -> std::string {
        if (!stored) { return "<absent>"; }
        const auto& value = stored->deref();
        if (value.is_null()) { return "null"; }
        if (auto type = value.get_type_info(); type && type->base_type != script_value_type::jai_any_type && type->type_name != "auto") {
            return type->spelling();
        }
        return detail::slot_source_type_name(value, this);
    };
    const std::string old_type = type_name(before);
    const std::string new_type = type_name(after);
    if (outcome == field_migration_outcome::converted && old_type == new_type) { outcome = field_migration_outcome::preserved; }
    const std::string_view field_name = get_symbolizer()->get_string(field_id);
    for (auto& report : hot_reload_reports_) {
        if (report.class_name == class_name && report.field_name == field_name &&
            report.old_type == old_type && report.new_type == new_type && report.outcome == outcome) {
            ++report.count;
            return;
        }
    }
    hot_reload_reports_.push_back(field_migration_report{std::string(class_name), std::string(field_name), old_type, new_type, outcome, 1});
}

void engine::static_checking(check_mode mode) {
    impl->static_check_mode_ = mode;
}

check_mode engine::static_checking() const {
    return impl->static_check_mode_;
}

void engine::type_proofs(bool on) {
    impl->type_proofs_ = on;   // the next checked_report/check_bite re-runs with stamp or clear
}

bool engine::type_proofs() const {
    return impl->type_proofs_;
}

void engine::aggregate_lowering(bool on) {
    if (impl->aggregate_lowering_ == (on ? 1 : 0)) { return; }
    if (impl->has_executed_) {
        throw runtime_error("aggregate_lowering after execute() is unsupported: defined functions capture the body they were declared with");
    }
    impl->aggregate_lowering_ = on ? 1 : 0;
}

bool engine::aggregate_lowering() const {
    return impl->lowering_on();
}

uint64_t engine::aggregate_lowered_locals() const {
    return impl->aggregate_lowered_locals_;
}

uint64_t engine::aggregate_lowered_sites() const {
    return impl->aggregate_lowered_sites_;
}

void engine::ir_passes(bool on) {
    impl->ir_passes_ = on;   // the next compile follows it
}

bool engine::ir_passes() const {
    return impl->ir_passes_;
}

void engine::cells_miss_every(uint32_t n) {
#ifdef JAISCRIPT_MISS_INJECTION
    impl->cells_miss_every_ = n;   // the next block entry follows it
#else
    if (n != 0) { throw std::logic_error("engine::cells_miss_every: this build carries no miss injection (define JAISCRIPT_MISS_INJECTION)"); }
    impl->cells_miss_every_ = 0;
#endif
}

uint32_t engine::cells_miss_every() const {
    return impl->cells_miss_every_;
}

bool engine::miss_injection_available() {
#ifdef JAISCRIPT_MISS_INJECTION
    return true;
#else
    return false;
#endif
}

cell_counters engine::cells_counters() const {
    cell_counters result = impl && impl->backend ? impl->backend->cells_counters() : cell_counters{};
    if (impl && impl->parallel_) {
        const cell_counters& w = impl->parallel_->worker_cells;
        result.entries += w.entries; result.boundaries += w.boundaries; result.resumes += w.resumes;
        result.misses += w.misses; result.row_entries += w.row_entries;
    }
    return result;
}

void engine::disassembly_sink(disassembly_sink_fn sink) {
    impl->disassembly_sink_ = std::move(sink);
}

const engine::disassembly_sink_fn& engine::disassembly_sink() const {
    return impl->disassembly_sink_;
}

void engine::vm_profile_reset() {
    if (impl && impl->backend) { impl->backend->profile_reset(); }
}

bool engine::vm_coverage_available() {
#ifdef JAISCRIPT_VM_COVERAGE
    return true;
#else
    return false;
#endif
}

void engine::vm_coverage_reset() {
#ifdef JAISCRIPT_VM_COVERAGE
    std::lock_guard<std::mutex> lock(impl->coverage_mutex_);
    for (const auto& t : impl->coverage_tables_) { t->reset(); }
#else
    throw std::logic_error("engine::vm_coverage_reset: this build carries no coverage counters (define JAISCRIPT_VM_COVERAGE)");
#endif
}

std::string engine::vm_coverage_report(size_t top) const {
#ifdef JAISCRIPT_VM_COVERAGE
    std::lock_guard<std::mutex> lock(impl->coverage_mutex_);
    return vm::coverage_report(impl->coverage_tables_, top);
#else
    (void)top;
    throw std::logic_error("engine::vm_coverage_report: this build carries no coverage counters (define JAISCRIPT_VM_COVERAGE)");
#endif
}

vm_coverage_totals engine::vm_coverage_summary() const {
#ifdef JAISCRIPT_VM_COVERAGE
    std::lock_guard<std::mutex> lock(impl->coverage_mutex_);
    vm_coverage_totals out;
    vm::coverage_summary(impl->coverage_tables_, out);
    return out;
#else
    throw std::logic_error("engine::vm_coverage_summary: this build carries no coverage counters (define JAISCRIPT_VM_COVERAGE)");
#endif
}

#ifdef JAISCRIPT_VM_COVERAGE
void engine::vm_coverage_adopt(std::shared_ptr<vm::coverage_tables> tables) {
    std::lock_guard<std::mutex> lock(impl->coverage_mutex_);
    for (const auto& t : impl->coverage_tables_) { if (t == tables) return; }
    impl->coverage_tables_.push_back(std::move(tables));
}
#endif

const proof_stats& engine::last_proof_stats() const {
    return impl->last_proof_stats_;
}

const check_report& engine::last_check_diagnostics() const {
    return impl->last_check_;
}

check_report engine::check(const std::string& scriptContent) {
    auto cached = impl->find_cached_script("<script>", scriptContent);
    if (!cached) {
        lexer lexer(scriptContent, &impl->string_symbolizer_);
        auto tokens = lexer.tokenize();
        parser parser(tokens, &impl->string_symbolizer_, this, impl->registeredTemplateTypes);
        auto parse_result = parser.parse();
        if (!parse_result) {
            throw parse_error(format_error(parse_result));
        }
        cached = impl->store_cached_script("<script>", scriptContent, std::move(parse_result.value()));
        impl->stamp_spliced(*cached, parser.spliced_files());
    }
    return impl->program_report(impl->checked_report(*this, *cached, true));
}

bool engine::host_function_signatures(const std::string& name, std::vector<host_overload_signature>& out) const {
    auto it = impl->overloadedFunctions.find(name);
    if (it != impl->overloadedFunctions.end()) {
        out.reserve(out.size() + it->second.overloads.size());
        for (const auto& o : it->second.overloads) {
            out.push_back({o.argCount, o.paramTypes});
        }
        return true;
    }
    auto arityIt = impl->functionArities.find(name);
    if (arityIt != impl->functionArities.end()) {
        out.push_back({arityIt->second, {}});
        return true;
    }
    return false;
}

uint64_t engine::registration_fingerprint() const {
    std::vector<std::string_view> names;
    names.reserve(impl->overloadedFunctions.size() + impl->class_registry_.cpp_classes_.size() +
                  impl->registeredTemplateTypes.size());
    for (const auto& [name, _] : impl->overloadedFunctions) names.push_back(name);
    for (const auto& [name, _] : impl->class_registry_.cpp_classes_) names.push_back(name);
    for (const auto& name : impl->registeredTemplateTypes) names.push_back(name);
    std::sort(names.begin(), names.end());
    uint64_t hash = 1469598103934665603ull;
    for (auto name : names) {
        for (unsigned char c : name) { hash ^= c; hash *= 1099511628211ull; }
        hash ^= 0xFFu; hash *= 1099511628211ull;  // name separator
    }
    // Plain-global function registrations (zero-arg add_function, add_variadic_function,
    // single untyped bindings) never enter overloadedFunctions, so they are included
    // here — otherwise a .jaibite saved against them loads with registration_mismatch()==false and
    // dies at execute. Folded in AFTER
    // the sorted overload surface with an arity-class marker, so engines registering
    // none of these hash exactly as before; engines that do get a CHANGED fingerprint
    // (old .jaibites then report the advisory mismatch — correct, loads never hard-fail).
    std::vector<std::pair<std::string_view, char>> plain;
    plain.reserve(impl->functionArities.size());
    for (const auto& [name, arity] : impl->functionArities) {
        plain.emplace_back(name, arity == SIZE_MAX ? 'v' : (arity == 0 ? 'z' : 'n'));
    }
    std::sort(plain.begin(), plain.end());
    for (const auto& [name, kind] : plain) {
        for (unsigned char c : name) { hash ^= c; hash *= 1099511628211ull; }
        hash ^= 0xFEu; hash *= 1099511628211ull;  // plain-function separator
        hash ^= static_cast<unsigned char>(kind); hash *= 1099511628211ull;
    }
    return hash;
}

script_value engine::execute_file(const std::string& scriptPath) {
    return execute_file(scriptPath, instance_variables{});
}

script_value engine::execute_file(const std::string& scriptPath, const instance_variables& instanceVars) {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(format_error(allowed)); }
    // File fast lane (parse-avoidance ladder): canonical-path key + one-stat gate. A
    // stamp-valid re-execute touches zero file bytes. Any stamp change, epoch bump or a
    // failed verify falls through to the read below. The unit is attributed to the
    // canonical path, the spelling import uses and source breakpoints normalize to.
    uint64_t mtime = 0, size = 0;
    bool statOk = impl->stat_mtime_size(scriptPath, mtime, size);
    std::string canon;   // by value: a script can re-enter execute_file and rehash the maps
    if (statOk) {
        std::string& canonSlot = impl->canonical_path_cache[scriptPath];
        if (canonSlot.empty()) {
            std::error_code canonEc;
            auto resolved = std::filesystem::weakly_canonical(scriptPath, canonEc);
            canonSlot = canonEc ? scriptPath : resolved.string();
        }
        canon = canonSlot;
    }
    const std::string& attributed = canon.empty() ? scriptPath : canon;
    impl->note_root_source(attributed);
    if (statOk) {
        auto it = impl->file_script_cache.find(canon);
        if (it != impl->file_script_cache.end()) {
            auto& node = it->second;
            bool stampValid = node.entry && node.entry->epoch == impl->script_cache_epoch &&
                              node.attributed_path == attributed &&
                              node.size == size && mtime == node.mtime &&
                              !impl->spliced_changed(*node.entry);   // the spliced includes' stamps, as find_cached_script checks them: a changed dependency is a miss on every tier
            if (stampValid && !node.verified) {
                // One-shot equal-stamp verify: an edit in the same filesystem tick as
                // the original read is invisible to the stat, so compare bytes once.
                std::ifstream verifyFile(scriptPath);
                std::stringstream verifyBuffer;
                if (verifyFile.is_open()) {
                    verifyBuffer << verifyFile.rdbuf();
                }
                if (verifyFile.is_open() && verifyBuffer.view() == node.source) {
                    node.verified = true;
                } else {
                    stampValid = false;
                }
            }
            if (stampValid) {
                return impl->execute_cache_hit(*this, *node.entry, instanceVars, node.source);
            }
            impl->file_script_cache.erase(it);
        }
    }

    // a save landing mid-read re-reads at once (bounded); text still torn after that runs, but is not remembered
    std::string content;
    bool stable = false;
    for (int attempt = 0; ; ++attempt) {
        const auto read = impl->read_under_stamp(scriptPath, statOk, mtime, size, content);
        if (read == implementation::stamped_read::missing) {
            throw runtime_error("Failed to open script file: " + scriptPath);
        }
        stable = read == implementation::stamped_read::stable;
        if (stable || attempt == 3) { break; }
        statOk = impl->stat_mtime_size(scriptPath, mtime, size);
    }
    script_value result = execute_source(content, instanceVars, attributed);

    // Stamp AFTER a successful execute, with the stamp the read was taken under
    if (statOk && stable && !canon.empty()) {
        if (auto entry = impl->find_cached_script(attributed, content)) {
            if (impl->file_script_cache.size() >= implementation::script_cache_max &&
                impl->file_script_cache.find(canon) == impl->file_script_cache.end()) {
                auto victim = impl->file_script_cache.begin();
                for (auto it = impl->file_script_cache.begin(); it != impl->file_script_cache.end(); ++it) {
                    if (it->second.entry->last_used < victim->second.entry->last_used) {
                        victim = it;
                    }
                }
                impl->file_script_cache.erase(victim);
            }
            implementation::file_cache_node node;
            node.mtime = mtime;
            node.size = size;
            node.verified = false;
            node.attributed_path = attributed;
            node.source = std::move(content);
            node.entry = std::move(entry);
            impl->file_script_cache[canon] = std::move(node);
        }
    }
    return result;
}

void engine::jaibite_cache(bool enabled) {
    impl->jaibite_cache_enabled = enabled;
}

bool engine::jaibite_cache() const {
    return impl->jaibite_cache_enabled;
}

size_t engine::jaibite_cache_write_failures() const {
    return impl->jaibite_cache_write_failures_;
}

void engine::add_global(const std::string& name, script_value value, bool is_serializable) {
    if (is_const_global(name)) { throw runtime_error("Cannot assign to '" + name + "': it is read-only"); }   // internal/const.md
    ++impl->check_surface_epoch_;   // the static checker can see globals
    // Intern the name and get a stable string_view for tracking
    uint64_t id = impl->string_symbolizer_.intern(name);
    if (value.is_function()) { impl->host_function_names_.insert(name); }
    if (impl->library_registration_depth_ > 0 && value.is_function() && !detail::parallel_pure_function(name)) { impl->serial_functions_.insert(name); }
    impl->global_environment_->define(id, std::move(value));
    ++impl->definition_epoch_;
    impl->refresh_operator_entry(name);

    // Track if this global should NOT be serialized (using string_view from symbolizer)
    std::string_view name_view = impl->string_symbolizer_.get_string(id);
    if (!is_serializable) {
        impl->nonSerializableGlobals.insert(name_view);
    } else {
        // In case it was previously marked as non-serializable
        impl->nonSerializableGlobals.erase(name_view);
    }
}

void engine::add_variadic_function(const std::string& name, script_function func, privilege required) {
    func = guard_function(std::move(func), required);
    // A variadic callable takes its arguments by value: an element reference an argument site minted (`f(a[i])`) reaches it
    // as the element's value, as a typed parameter's conversion already reads it
    func = [inner = std::move(func)](const std::vector<script_value>& args) -> checked_result<script_value> {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i].is_reference()) [[unlikely]] {
                std::vector<script_value> values;
                values.reserve(args.size());
                for (const script_value& arg : args) {
                    if (!arg.is_reference()) { values.push_back(arg); continue; }
                    auto value = detail::ref_checked_deref(arg);
                    if (!value) { return value.error_value(); }
                    values.push_back(std::move(value.value()));
                }
                return inner(values);
            }
        }
        return inner(args);
    };
    // Variadic function registration - handles any number of arguments
    // Register with SIZE_MAX to indicate it's a wildcard function
    constexpr size_t VARIADIC = SIZE_MAX;
    if (has_function(name)) {
        // Add as overload with SIZE_MAX (wildcard - accepts any number of args)
        add_overloaded_function(name, VARIADIC, func);
    } else {
        // Register with SIZE_MAX to mark it as variadic
        add_functionWithArity(name, func, VARIADIC);
    }
}

// One overloaded-function registration algorithm shared by every entry point. State:
//   hasExisting = a function is already bound to `name` in the global env
//   setExists   = an overload set already exists for `name` (so that global IS its dispatcher)
// The invariant that prevents the dispatcher-self-recursion crash: migrate the existing global into
// a fresh set ONLY when no set exists yet (!setExists) — never migrate a dispatcher into its own set
// (the empty-signature dedup would overwrite the real overload, and a call would dispatch to itself).
// createSetOnFirst is the only policy difference between entry points: the add_overloaded_* family
// always builds a set (even for a lone untyped overload); the add_function* family defers (a single
// untyped function stays a plain global until a second one of the same name arrives).
void engine::stage_host_result(script_value_type kind) {
    impl->staged_host_result_ = kind;
}

void engine::stage_host_direct(host_direct_entry entry) {
    impl->staged_host_direct_ = entry;
}

bool engine::host_function_result(const std::string& name, script_value_type& out) const {
    auto it = impl->host_results_.find(name);
    if (it == impl->host_results_.end() || it->second == script_value_type::jai_null_type) { return false; }
    out = it->second;
    return true;
}

void engine::serial_function(const std::string& name) { impl->serial_functions_.insert(name); }

bool engine::host_function_serial(const std::string& name) const { return impl->serial_functions_.count(name) != 0; }

bool engine::host_function_registered(const std::string& name) const { return impl->host_function_names_.count(name) != 0; }

uint64_t engine::host_function_writeback(const std::string& name) const {
    uint64_t result = 0;
    auto mask_of = [](const script_value& fn) -> uint64_t {
        if (!fn.is_function()) { return 0; }
        const auto* w = fn.as_function().target<host_writeback_function>();
        return w ? w->mask : 0;
    };
    if (auto it = impl->overloadedFunctions.find(name); it != impl->overloadedFunctions.end()) {
        for (const auto& o : it->second.overloads) { result |= mask_of(o.function); }
        return result;
    }
    if (auto global = impl->global_environment_->get(name); global) { result |= mask_of(global.value()); }
    return result;
}

engine::library_registration::library_registration(engine& a_engine) : eng(a_engine) { ++eng.impl->library_registration_depth_; }

engine::library_registration::~library_registration() { --eng.impl->library_registration_depth_; }

void engine::register_overload_impl(const std::string& name, size_t arity, script_function func,
                                    const std::vector<param_type_info>& paramTypes, bool createSetOnFirst) {
    impl->host_function_names_.insert(name);
    if (impl->library_registration_depth_ > 0 && !detail::parallel_pure_function(name)) { impl->serial_functions_.insert(name); }
    ++impl->check_surface_epoch_;   // the static checker can see registered functions
    const script_value_type result = impl->staged_host_result_;
    impl->staged_host_result_ = script_value_type::jai_null_type;
    const host_direct_entry direct = std::exchange(impl->staged_host_direct_, host_direct_entry{});
    if (auto [it, fresh] = impl->host_results_.try_emplace(name, result); !fresh && it->second != result) { it->second = script_value_type::jai_null_type; }
    auto existing_result = impl->global_environment_->get(name);
    bool hasExisting = existing_result && existing_result.value().is_function();
    bool setExists = impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end();

    if (hasExisting && !setExists) {
        // Migrate the existing plain global into a new set. getOrCreateOverloadSet wires the
        // conversion registry + class lookup on creation, so no explicit set_conversion_registry.
        auto arityIt = impl->functionArities.find(name);
        size_t existingArity = (arityIt != impl->functionArities.end()) ? arityIt->second : 0;
        impl->getOrCreateOverloadSet(name).add_overload_with_types(existingArity, std::move(existing_result.value()), {});
        if (arityIt != impl->functionArities.end()) {
            impl->functionArities.erase(arityIt);
        }
        setExists = true;
    }

    if (setExists || createSetOnFirst || !paramTypes.empty()) {
        // A set exists (or we want one now): add this overload and (re)install the dispatcher.
        impl->getOrCreateOverloadSet(name).add_overload_with_types(arity, script_value::make_function(std::move(func), this), paramTypes, direct);
        impl->updateOverloadedFunction(name, this);
    } else {
        // First registration of a single untyped function: keep it a plain global. A set is built
        // only if/when a second function of this name is registered.
        impl->global_environment_->define(name, script_value::make_function(std::move(func), this));
        ++impl->definition_epoch_;
        impl->functionArities[name] = arity;
    }
    impl->refresh_operator_entry(name);
}

void engine::add_functionWithArity(const std::string& name, script_function func, size_t arity) {
    register_overload_impl(name, arity, std::move(func), {}, /*createSetOnFirst=*/false);
}

void engine::add_overloaded_function(const std::string& name, size_t argCount, script_function func) {
    register_overload_impl(name, argCount, std::move(func), {}, /*createSetOnFirst=*/true);
}

void engine::add_overloaded_function_with_full_types(const std::string& name, size_t argCount, script_function func, const std::vector<param_type_info>& paramTypes) {
    register_overload_impl(name, argCount, std::move(func), paramTypes, /*createSetOnFirst=*/true);
}

void engine::add_function_with_arity_and_full_types(const std::string& name, script_function func, size_t arity, const std::vector<param_type_info>& paramTypes) {
    register_overload_impl(name, arity, std::move(func), paramTypes, /*createSetOnFirst=*/false);
}

string_symbolizer* engine::get_symbolizer() {
    return &impl->string_symbolizer_;
}

uint64_t engine::symbolize(std::string_view str) const {
    return impl->string_symbolizer_.intern(str);
}

uint64_t engine::park_error_text(std::string_view text) const {
    const string_symbolizer& symbols = impl->string_symbolizer_;
    if (symbols.frozen()) {
        const auto known = symbols.find(text);
        return known ? *known : 0;
    }
    return symbols.park(text);
}

void engine::add_class_impl(const std::string& name, std::shared_ptr<class_definition> classDef) {
    ++impl->script_cache_epoch;   // class registration can change how source parses
    ++impl->check_surface_epoch_;
    impl->classes[name] = classDef;
    impl->classesByTypeId[classDef->get_type_id()] = classDef;  // Fast lookup by interned type_id
    // Also register with the unified class_registry for both C++ and script classes
    auto reg_result = impl->class_registry_.register_cpp_class(classDef);
    if (!reg_result) {
        throw runtime_error("Failed to register C++ class: " + name);
    }

    // Store the class definition in the global environment with __class_ prefix
    // This allows static member access (ClassName::static_field) to work for C++ classes
    // Use optimized make_object with cached type_id for fast type checking
    uint64_t name_id = impl->string_symbolizer_.intern(name);
    auto [class_var_id, class_var_name] = impl->string_symbolizer_.get_class_var_id_with_view(name_id);
    impl->global_environment_->define(class_var_id, script_value::make_object("class_definition", impl->class_definition_type_id_, classDef, this));
}

std::vector<std::string> engine::registered_class_names() const {
    std::set<std::string> names;
    for (const auto& [name, def] : impl->classes) {
        names.insert(name);
    }
    for (const auto& [name, def] : impl->class_registry_.script_classes_) {
        names.insert(name);
    }
    return std::vector<std::string>(names.begin(), names.end());
}

std::shared_ptr<class_definition> engine::get_class_definition(const std::string& name) const {
    JAI_CENSUS(class_by_name);
    // First check C++ classes
    auto it = impl->classes.find(name);
    if (it != impl->classes.end()) {
        return it->second;
    }

    // Also check script classes (script_class_definition inherits from class_definition)
    auto script_class = impl->class_registry_.find_script_class(name);
    if (script_class) {
        return std::static_pointer_cast<class_definition>(script_class);
    }

    return nullptr;
}

std::shared_ptr<class_definition> engine::get_class_definition(uint64_t type_id) const {
    JAI_CENSUS(class_by_id);
    // First check C++ classes by type_id
    auto it = impl->classesByTypeId.find(type_id);
    if (it != impl->classesByTypeId.end()) {
        return it->second;
    }

    // For script classes, we need to check each one since they're not indexed by type_id
    // This is slower but script classes should eventually be added to classesByTypeId as well
    // TODO: Index script classes by type_id for O(1) lookup
    for (const auto& [name, script_class] : impl->class_registry_.script_classes_) {
        if (impl->string_symbolizer_.intern(name) == type_id) {
            return std::static_pointer_cast<class_definition>(script_class);
        }
    }

    return nullptr;
}

script_value engine::get_variable(const std::string& name) const {
    // Use backend to properly handle references
    try {
        return backend()->get_variable(name);
    } catch (...) {
        // Not in global environment, check overloaded functions
    }
    
    // Check overloaded functions
    auto overloadIt = impl->overloadedFunctions.find(name);
    if (overloadIt != impl->overloadedFunctions.end()) {
        // Create a dispatch function that selects the right overload
        // Make a copy of the name to ensure it survives the lambda lifetime
        std::string functionName = name;
        uint64_t name_id = symbolize(name);
        script_function dispatcher = [this, functionName, name_id](const std::vector<script_value>& args) -> checked_result<script_value> {
            auto it = impl->overloadedFunctions.find(functionName);
            if (it == impl->overloadedFunctions.end()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::not_a_function),
                    "Overloaded function '{0}' not found", name_id);
            }

            script_value bestMatch = it->second.findBestMatch(args);
            if (bestMatch.is_null()) {
                return detail::overload_miss(make_error_code(runtime_error_code::argument_count_mismatch),
                    name_id, args.data(), args.size(), const_cast<engine*>(this));
            }

            // Call the selected overload
            const script_function& func = bestMatch.as_function();
            return func(args);
        };

        return script_value::make_function(dispatcher, const_cast<engine*>(this));
    }
    
    throw runtime_error("Variable '" + name + "' not found");
}

bool engine::has_variable(const std::string& name) const {
    // Delegate to backend to ensure consistency
    return backend()->has_variable(name);
}

void engine::mark_const_cell(const script_value* cell, bool is_const) {
    if (!cell) { return; }
    if (is_const) { impl->const_cells_.insert(cell); } else { impl->const_cells_.erase(cell); }
}

bool engine::const_cell(const script_value* cell) const {
    return cell && !impl->const_cells_.empty() && impl->const_cells_.count(cell) != 0;
}

bool engine::is_const_global(const std::string& name) const {
    if (impl->const_cells_.empty()) { return false; }
    auto env = get_global_environment();
    return env && const_cell(env->get_value_ptr(symbolize(name)));
}

bool engine::has_function(const std::string& name) const {
    // Check overloaded functions first (fast hashmap lookup)
    if (impl->overloadedFunctions.find(name) != impl->overloadedFunctions.end()) {
        return true;
    }

    // Check if there's a function in global environment
    // Use contains() first to avoid string allocation on "not found" path
    if (!impl->global_environment_->contains(name)) {
        return false;
    }

    // Name exists - get it and check if it's a function
    auto val_result = impl->global_environment_->get(name);
    return val_result && val_result.value().is_function();
}

bool engine::is_type_name(const std::string& name) const {
    // Check if it's a registered class
    return impl->classes.find(name) != impl->classes.end();
}

std::shared_ptr<environment> engine::get_global_environment() const {
    return impl->global_environment_;
}

void engine::set_has_custom_numeric_operators(bool value) {
    // Explicitly set whether custom numeric operators are in use
    // This allows users to opt-in to custom operator support when needed
    // By default, the fast path is enabled (no custom operators)
    impl->custom_numeric_ops_flag_ = value;
    if (value) {
        impl->latch_custom_binary_ops();   // numeric overrides imply the mint gate
    }
    if (impl->backend) {
        impl->backend->set_has_custom_numeric_ops(value);
    }
}

void engine::register_template_type(const std::string& baseTemplateName) {
    if (impl->registeredTemplateTypes.insert(baseTemplateName).second) {
        ++impl->script_cache_epoch;   // parses depend on the template-name set
        ++impl->check_surface_epoch_;
    }
}

std::unordered_set<std::string> engine::get_registered_template_types() const {
    return impl->registeredTemplateTypes;
}

// Single source of backend configuration: every backend, however installed (construction,
// set_backend by type, or injected instance), gets the full engine wiring.
void engine::wire_backend() {
    impl->backend->set_engine_reference(this);

    impl->backend->set_class_lookup_callback([this](const std::string& name) -> std::shared_ptr<class_definition> {
        auto it = impl->classes.find(name);
        return it != impl->classes.end() ? it->second : nullptr;
    });

    impl->backend->set_has_custom_numeric_ops(impl->custom_numeric_ops_flag_ ||
                                              impl->overloadedFunctions.count("+") > 0 ||
                                              impl->overloadedFunctions.count("-") > 0 ||
                                              impl->overloadedFunctions.count("*") > 0 ||
                                              impl->overloadedFunctions.count("/") > 0);
    impl->backend->set_has_custom_binary_ops(impl->custom_binary_ops_flag_ || impl->custom_numeric_ops_flag_);
    impl->backend->set_operator_table(&impl->operator_table_);

    auto budget = impl->execution_budget_seconds_ > 0
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(impl->execution_budget_seconds_))
        : std::chrono::nanoseconds(0);
    impl->backend->set_execution_budget(budget);

    // Re-wire the step-debugger across backend swaps/reconstruction: engine::debugger()
    // may have been called before set_backend picked the final backend.
    if (impl->debugger_) {
        impl->backend->set_debug_controller(impl->debugger_.get());
    }
}

void engine::set_backend(backend_type type) {
    if (type == backend_type::auto_select) {
        type = backend_type::vm;
    }
    if (type == impl->current_backend_type) {
        return;
    }
    if (impl->has_executed_) {
        throw runtime_error("set_backend after execute() is unsupported: defined functions and classes capture the executing backend");
    }
    impl->current_backend_type = type;

    // Constructed lazily by engine::backend() with the new type
    impl->backend.reset();
}

backend_type engine::get_backend_type() const {
    return impl->current_backend_type;
}

std::string engine::get_backend_name() const {
    execution_backend* be = backend();
    return be ? be->get_backend_name() : std::string();
}

execution_backend* engine::get_execution_backend() const {
    return impl ? backend() : nullptr;
}

bool engine::backend_unwinding() const {
    auto* be = get_execution_backend();
    return be && be->is_unwinding();
}

std::string engine::backend_exception_text() const {
    auto* be = get_execution_backend();
    return be ? std::string(be->get_current_exception().what()) : std::string();
}

checked_result<std::string> engine::text_of(const script_value& v) const {
    auto* be = get_execution_backend();
    return be ? be->text_of(v) : checked_result<std::string>(v.deref().to_string());
}

std::unordered_map<uint64_t, std::shared_ptr<script_namespace_data>>& engine::script_namespaces() {
    return impl->script_namespaces_;
}

void engine::note_enum_name(uint64_t name_id) {
    impl->enum_names_.insert(name_id);
}

bool engine::is_enum_name(uint64_t name_id) const {
    return impl->enum_names_.count(name_id) != 0;
}


void engine::register_class_by_type(std::type_index type, std::shared_ptr<class_definition> classDef) {
    impl->classesByType[type] = classDef;
    auto pending = impl->pendingBaseLinks.find(type);
    if (pending != impl->pendingBaseLinks.end()) {
        auto derivedDefs = std::move(pending->second);
        impl->pendingBaseLinks.erase(pending);
        for (auto&& derived : derivedDefs) {
            if (!derived->add_parent(classDef)) {
                throw std::runtime_error("Diamond inheritance detected while resolving a deferred base-class link");
            }
        }
    }
}

void engine::defer_base_link(std::type_index base, std::shared_ptr<class_definition> derived) {
    impl->pendingBaseLinks[base].push_back(std::move(derived));
}

std::shared_ptr<class_definition> engine::get_class_definition_by_type(const std::type_index& type) const {
    auto it = impl->classesByType.find(type);
    if (it != impl->classesByType.end()) {
        return it->second;
    }
    return nullptr;
}

void engine::add_standard_conversions() {
    conversions::register_all_standard_conversions(impl->conversions);
}

std::shared_ptr<conversions::conversion_registry> engine::get_conversion_registry() const {
    return impl->conversions;
}

// === TYPE INFO INTERNING METHODS ===

type_info* engine::get_type_info_int() {
    return impl->type_info_int_;
}

type_info* engine::get_type_info_float() {
    return impl->type_info_float_;
}

type_info* engine::get_type_info_string() {
    return impl->type_info_string_;
}

type_info* engine::get_type_info_math(script_value_type kind) {
    switch (kind) {
        case script_value_type::jai_vec2_type: return impl->type_info_math_[0];
        case script_value_type::jai_vec3_type: return impl->type_info_math_[1];
        case script_value_type::jai_ivec2_type: return impl->type_info_math_[2];
        case script_value_type::jai_ivec3_type: return impl->type_info_math_[3];
        case script_value_type::jai_vec4_type: return impl->type_info_math_[4];
        case script_value_type::jai_color_type: return impl->type_info_math_[5];
        default: return nullptr;
    }
}

type_info* engine::get_type_info_utf8_string() {
    return impl->type_info_utf8_string_;
}

type_info* engine::get_type_info_bool() {
    return impl->type_info_bool_;
}

type_info* engine::get_type_info_char() {
    return impl->type_info_char_;
}

type_info* engine::get_type_info_uint8() {
    return impl->type_info_uint8_;
}

type_info* engine::get_type_info_int8() {
    return impl->type_info_int8_;
}

type_info* engine::get_type_info_void() {
    return impl->type_info_void_;
}

type_info* engine::get_type_info_invalid() {
    return impl->type_info_invalid_;
}

type_info* engine::get_type_info_any() {
    return impl->type_info_any_;
}

size_t engine::interned_type_count() const {
    return impl->type_infos_.size();
}

type_info* engine::get_type_info_array(type_info* element_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_array_type, element_type ? element_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_array(impl->string_symbolizer_, element_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_map(type_info* key_type, type_info* value_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_map_type,
                              key_type ? key_type->id : 0,
                              value_type ? value_type->id : 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_map(impl->string_symbolizer_, key_type, value_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_object(const std::string& class_name) {
    // Use composite key for fast lookup
    uint64_t class_name_id = impl->string_symbolizer_.intern(class_name);
    implementation::type_key key{script_value_type::jai_object_type, class_name_id, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_object(impl->string_symbolizer_, class_name);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_object(uint64_t type_id) {
    // Use composite key for fast lookup
    implementation::type_key key{script_value_type::jai_object_type, type_id, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // If not found, we need to look up the class name from the type_id
    // This should only happen if the type_id was created but the type_info wasn't interned yet
    std::string_view class_name = impl->string_symbolizer_.get_string(type_id);
    if (class_name.empty()) {
        return nullptr; // Invalid type_id
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_object(impl->string_symbolizer_, class_name);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_weak_ptr(type_info* pointee_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_weak_ptr_type, pointee_type ? pointee_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_weak_ptr(impl->string_symbolizer_, pointee_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_shared_ptr(type_info* pointee_type) {
    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_shared_ptr_type, pointee_type ? pointee_type->id : 0, 0};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Cache miss - construct full type_info and insert
    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_shared_ptr(impl->string_symbolizer_, pointee_type);
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_shared_ptr_dynamic(type_info* pointee_type) {
    // var-held-handle twin (param2=1 keys it apart from the plain spelling): same
    // name/id/element so every existing read behaves identically; dynamic_pointee
    // lets the assignment route rebind unchecked
    implementation::type_key key{script_value_type::jai_shared_ptr_type, pointee_type ? pointee_type->id : 0, 1};

    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    impl->deny_type_intern_in_region();
    type_info temp = type_info::make_shared_ptr(impl->string_symbolizer_, pointee_type);
    temp.dynamic_pointee = true;
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_.try_emplace(ptr->id, ptr);   // never displace the plain spelling
    return ptr;
}

type_info* engine::get_type_info_array_dynamic(type_info* array_type) {
    // var-held typed array twin (param2=1 keys it apart from the plain spelling): same name/id/element, so every element
    // boundary reads the element type; dynamic_slot keeps the slot a var
    if (!array_type || array_type->dynamic_slot) { return array_type; }
    const type_info_ptr element = array_type->element_type();
    implementation::type_key key{script_value_type::jai_array_type, element ? element->id : 0, 1};
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }
    impl->deny_type_intern_in_region();
    type_info temp = *array_type;
    temp.dynamic_slot = true;
    temp.cached_reference_type = nullptr;
    temp.cached_declared_reference_type = nullptr;
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_.try_emplace(ptr->id, ptr);   // never displace the plain spelling
    return ptr;
}

type_info* engine::get_type_info_map_dynamic(type_info* a_mapType) {
	if (!a_mapType || a_mapType->dynamic_slot) { return a_mapType; }
	const auto keyType = a_mapType->key_type(), valueType = a_mapType->value_type();
	implementation::type_key key{script_value_type::jai_map_type, keyType ? keyType->id : 0, valueType ? valueType->id : 0, true};
	if (auto found = impl->type_infos_.find(key); found != impl->type_infos_.end()) { return &found->second; }
	impl->deny_type_intern_in_region();
	type_info temp = *a_mapType;
	temp.dynamic_slot = true;
	temp.cached_reference_type = nullptr;
	temp.cached_declared_reference_type = nullptr;
	auto result = impl->type_infos_.emplace(key,temp);
	auto* ptr = &result.first->second;
	impl->type_id_index_.try_emplace(ptr->id,ptr);
	return ptr;
}

type_info* engine::get_type_info_declared_reference(type_info* referenced_type) {
    if (!referenced_type) { return nullptr; }
    if (referenced_type->cached_declared_reference_type) { return referenced_type->cached_declared_reference_type; }
    implementation::type_key key{script_value_type::jai_reference_type, referenced_type->id, 1};
    type_info* ptr = nullptr;
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        ptr = &it->second;
    } else {
        impl->deny_type_intern_in_region();
        type_info temp = *get_type_info_reference(referenced_type);
        temp.declared_reference = true;
        temp.declared_referent = referenced_type;
        temp.declared_kind = detail::declared_ref_kind(referenced_type) ? detail::declared_ref_kind(referenced_type) : uint8_t{0xFF};
        temp.cached_reference_type = nullptr;
        temp.cached_declared_reference_type = nullptr;
        auto result = impl->type_infos_.emplace(key, temp);
        ptr = &result.first->second;
        impl->type_id_index_.try_emplace(ptr->id, ptr);   // never displace the plain spelling
    }
    // the get_type_info_reference rule: only a type this engine interned, never inside a parallel region
    if (!parallel_region_active()) {
        auto owned = impl->type_id_index_.find(referenced_type->id);
        if (owned != impl->type_id_index_.end() && owned->second == referenced_type) {
            referenced_type->cached_declared_reference_type = ptr;
        }
    }
    return ptr;
}

type_info* engine::get_type_info_reference(type_info* referenced_type) {
    // Hot mint path: the interned "T&" twin is cached on the referenced type itself
    // (and on the engine for T == null), so repeat mints skip the intern-map lookup
    if (referenced_type && referenced_type->cached_reference_type) {
        return referenced_type->cached_reference_type;
    }
    if (!referenced_type && impl->type_info_reference_null_) {
        return impl->type_info_reference_null_;
    }

    // Use composite key for fast lookup - no string construction needed on cache hit
    implementation::type_key key{script_value_type::jai_reference_type, referenced_type ? referenced_type->id : 0, 0};

    type_info* ptr = nullptr;
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        ptr = &it->second;
    } else {
        // Cache miss - construct full type_info and insert
        impl->deny_type_intern_in_region();
        type_info temp = type_info::make_reference(impl->string_symbolizer_, referenced_type);
        auto result = impl->type_infos_.emplace(key, temp);
        ptr = &result.first->second;
        impl->type_id_index_[ptr->id] = ptr;
    }

    // Fill the twin cache only on the region-free path: workers may reach here for a
    // shape that was interned but never prewarmed, and must not race the cache write.
    // Stamp only type_infos THIS engine interned (pointer identity via type_id_index_) -
    // a value minted by another engine must not carry our twin pointer, which would
    // dangle in ITS engine if we are destroyed first.
    if (!parallel_region_active()) {
        if (referenced_type) {
            auto owned = impl->type_id_index_.find(referenced_type->id);
            if (owned != impl->type_id_index_.end() && owned->second == referenced_type) {
                referenced_type->cached_reference_type = ptr;
            }
        } else {
            impl->type_info_reference_null_ = ptr;
        }
    }
    return ptr;
}

type_info* engine::get_type_info_function(type_info* return_type, const std::vector<type_info*>& arg_types) {
    // Convert raw pointers to type_info_ptr
    std::vector<type_info_ptr> arg_type_ptrs;
    arg_type_ptrs.reserve(arg_types.size());
    for (auto* arg : arg_types) {
        arg_type_ptrs.push_back(type_info_ptr(arg));
    }

    // Create type_info and use generic get_type_info which handles composite keys
    type_info temp = type_info::make_function(impl->string_symbolizer_, return_type, arg_type_ptrs);
    return get_type_info(temp);
}

type_info* engine::get_type_info_untyped_function() {
    if (impl->type_info_untyped_function_) { return impl->type_info_untyped_function_; }
    type_info* ptr = get_type_info_function(get_type_info_void(), {});
    if (!parallel_region_active()) { impl->type_info_untyped_function_ = ptr; }
    return ptr;
}

type_info* engine::get_type_info(const type_info& temp) {
    JAI_CENSUS(type_info_intern);
    // Build composite key from temp's fields
    uint64_t param1_id = 0, param2_id = 0;

    switch (temp.base_type) {
        case script_value_type::jai_array_type:
        case script_value_type::jai_weak_ptr_type:
        case script_value_type::jai_reference_type:
        // shared_ptr MUST key on its pointee: it fell into the parameterless default
        // before, so every parsed shared_ptr<T> collapsed onto the first-interned T
        // engine-wide - new Bar() silently constructed Foo
        case script_value_type::jai_shared_ptr_type:
            // Single type parameter
            param1_id = temp.type_params.empty() ? 0 : (temp.type_params[0] ? temp.type_params[0]->id : 0);
            break;
        case script_value_type::jai_map_type:
            // Two type parameters
            param1_id = temp.type_params.empty() ? 0 : (temp.type_params[0] ? temp.type_params[0]->id : 0);
            param2_id = temp.type_params.size() < 2 ? 0 : (temp.type_params[1] ? temp.type_params[1]->id : 0);
            break;
        case script_value_type::jai_object_type:
        case script_value_type::jai_function_type:
            // Object and function types: use temp.id which encodes full signature
            param1_id = temp.id;
            break;
        case script_value_type::jai_int_type:
            if (temp.is_byte_element()) { param1_id = temp.id; }
            break;
        default:
            // Primitive types have no parameters
            break;
    }

    implementation::type_key key{temp.base_type, param1_id, param2_id};

    // Check if we already have this type interned
    auto it = impl->type_infos_.find(key);
    if (it != impl->type_infos_.end()) {
        return &it->second;
    }

    // Insert the new type_info and return pointer to it
    impl->deny_type_intern_in_region();
    auto result = impl->type_infos_.emplace(key, temp);
    type_info* ptr = &result.first->second;
    impl->type_id_index_[ptr->id] = ptr;
    return ptr;
}

type_info* engine::get_type_info_by_id(uint64_t type_id) {
    // O(1) lookup via secondary index
    auto it = impl->type_id_index_.find(type_id);
    if (it != impl->type_id_index_.end()) {
        return it->second;
    }
    return nullptr;
}

type_info* engine::get_type_info_by_name(const std::string& canonical_name) {
    JAI_CENSUS(type_info_by_name);
    // Intern the name to get the ID, then do ID-based lookup
    uint64_t type_id = impl->string_symbolizer_.intern(canonical_name);
    return get_type_info_by_id(type_id);
}

void engine::set_conversion_registry(std::shared_ptr<conversions::conversion_registry> registry) {
    impl->conversions = registry;
}

serialization::serialization_registry& engine::get_serialization_registry() {
    return impl->serialization_registry;
}

class_registry& engine::get_class_registry() {
    return impl->class_registry_;
}

uint64_t engine::class_definition_epoch() const {
    return impl->class_definition_epoch_;
}

void engine::bump_class_definition_epoch() {
    ++impl->class_definition_epoch_;
    ++impl->definition_epoch_;
}

unit_stamp engine::current_unit() const {
    if (auto* be = backend()) { return be->current_unit(); }
    return impl->string_symbolizer_.current_unit();
}

uint64_t engine::definition_epoch() const {
    return impl->definition_epoch_;
}

std::string engine::unit_label(uint32_t unit) const {
    for (const auto& [key, id] : impl->unit_index_) {
        if (id == unit) { return key.empty() || key.front() == '\n' ? std::string("<script>") : key; }
    }
    return "<host>";
}

void engine::bump_definition_epoch() {
    ++impl->definition_epoch_;
}

uint64_t engine::parameter_alias_binds() const {
    return impl->parameter_alias_binds_;
}

void engine::note_parameter_alias_bind() {
    ++impl->parameter_alias_binds_;
}

void engine::parameter_alias(bool on) {
    impl->parameter_alias_ = on;
}

bool engine::parameter_alias() const {
    return impl->parameter_alias_;
}

detail::release_machine& engine::release_machine() { return impl->release_; }

detail::release_machine* engine::release_machine_here() {
    return impl->parallel_ && impl->parallel_->active_region ? nullptr : &impl->release_;
}

void engine::release_recursion_limit(uint32_t levels) { impl->release_.depth_limit = levels; }

uint32_t engine::release_recursion_limit() const { return impl->release_.depth_limit; }


uint64_t engine::local_alias_binds() const {
    return impl->local_alias_binds_;
}

void engine::note_local_alias_bind() {
    ++impl->local_alias_binds_;
}

void engine::local_alias(bool on) {
    impl->local_alias_ = on;
}

bool engine::local_alias() const {
    return impl->local_alias_;
}

void engine::note_class_shape_change() {
    ++impl->check_surface_epoch_;
}

bool engine::throw_on_overflow() const {
    return kCheckedOverflow;  // compile-time policy (interpreter.hpp)
}

void engine::execution_budget(double seconds) {
    impl->execution_budget_seconds_ = seconds;
    if (impl->backend) {
        auto budget = seconds > 0
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(seconds))
            : std::chrono::nanoseconds(0);
        impl->backend->set_execution_budget(budget);
    }
    // Not yet constructed: wire_backend applies the stored value at first use
}

void engine_time::extend_execution_deadline(std::chrono::nanoseconds by) {
    if (owner_->impl->backend) { owner_->impl->backend->extend_execution_deadline(by); }
}

void engine::execution_step_budget(uint64_t checks) {
    execution_limits().step_budget = checks;
}

uint64_t engine::execution_step_budget() const {
    return const_cast<engine*>(this)->execution_limits().step_budget;
}

double engine::execution_budget() const {
    return impl->execution_budget_seconds_;
}

void engine::enable(privilege flags) {
    set_privileges(flags, true);
}

void engine::disable(privilege flags) {
    set_privileges(flags, false);
}

void engine::set_privileges(privilege flags, bool enabled) {
    if (!valid_privileges(flags)) { throw runtime_error("Invalid privilege flags"); }
    check_privilege_mutation();
    const auto bits = static_cast<uint64_t>(flags);
    if (enabled) { impl->privileges_ |= bits; }
    else { impl->privileges_ &= ~bits; }
}

void engine::check_privilege_mutation() const {
    if (impl->parallel_ && impl->parallel_->active_region) {
        throw runtime_error("Cannot change privileges during a parallel region");
    }
}

void engine::register_privilege(std::string_view name) {
    check_privilege_mutation();
    if (name.empty()) { throw runtime_error("Privilege name must not be empty"); }
    if (name == "filesystem") { return; }
    const auto name_id = impl->string_symbolizer_.intern(name);
    if (!impl->custom_privileges_) {
        impl->custom_privileges_ = std::make_unique<implementation::custom_privilege_state>();
    }
    auto& state = *impl->custom_privileges_;
    if (state.slots.contains(name_id)) { return; }
    const auto slot = static_cast<uint64_t>(state.records.size());
    // A failed map insertion rolls back the record; an interned but unregistered
    // name is inert. Nothing exposes these engine-local slots to the host.
    state.records.push_back({name_id, false});
    try {
        state.slots.emplace(name_id, slot);
    } catch (...) {
        state.records.pop_back();
        throw;
    }
}

std::optional<uint64_t> engine::find_custom_privilege(std::string_view name) const noexcept {
    if (!impl->custom_privileges_) { return std::nullopt; }
    const auto name_id = impl->string_symbolizer_.find(name);
    if (!name_id) { return std::nullopt; }
    const auto& slots = impl->custom_privileges_->slots;
    const auto it = slots.find(*name_id);
    if (it == slots.end()) { return std::nullopt; }
    return it->second;
}

engine::resolved_privileges engine::resolve_privileges(std::span<const std::string_view> names) const {
    resolved_privileges result;
    for (const auto name : names) {
        if (name == "filesystem") { result.builtins = result.builtins | privilege::filesystem; }
        else {
            const auto slot = find_custom_privilege(name);
            if (!slot) { throw runtime_error("Unknown privilege name"); }
            result.custom.push_back(*slot);
        }
    }
    std::sort(result.custom.begin(), result.custom.end());
    result.custom.erase(std::unique(result.custom.begin(), result.custom.end()), result.custom.end());
    return result;
}

void engine::enable(std::span<const std::string_view> names) {
    set_privileges(names, true);
}

void engine::disable(std::span<const std::string_view> names) {
    set_privileges(names, false);
}

void engine::set_privileges(std::span<const std::string_view> names, bool enabled) {
    check_privilege_mutation();
    const auto required = resolve_privileges(names);
    // Resolve every name before changing any grant. The updates cannot allocate.
    set_privileges(required.builtins, enabled);
    for (const auto slot : required.custom) { impl->custom_privileges_->records[slot].enabled = enabled; }
}

bool engine::is_enabled(std::span<const std::string_view> names) const noexcept {
    return bool(require_privilege(names));
}

checked_result<void> engine::require_privilege(std::span<const std::string_view> names) const noexcept {
    privilege flags = privilege::none;
    std::optional<uint64_t> missing;
    for (const auto name : names) {
        if (name == "filesystem") { flags = flags | privilege::filesystem; }
        else {
            const auto slot = find_custom_privilege(name);
            if (!slot) {
                return checked_result<void>(make_error_code(runtime_error_code::invalid_operation), "Unknown privilege name");
            }
            if (!impl->custom_privileges_->records[*slot].enabled && (!missing || *slot < *missing)) { missing = *slot; }
        }
    }
    // Same deterministic denial order as a resolved binding, without allocating
    // a temporary slot vector for name queries or manual checked guards.
    return require_privileges(flags, missing ? std::span(&*missing, 1) : std::span<const uint64_t>{});
}

checked_result<void> engine::require_privileges(privilege flags, std::span<const uint64_t> slots) const noexcept {
    auto allowed = require_privilege(flags);
    if (!allowed) { return allowed; }
    for (const auto slot : slots) {
        const auto& record = impl->custom_privileges_->records[slot];
        if (!record.enabled) {
            return checked_result<void>(make_error_code(runtime_error_code::insufficient_privilege),
                "Insufficient privilege: {0}.", record.name_id);
        }
    }
    return {};
}

bool engine::is_enabled(privilege flags) const noexcept {
    const auto bits = static_cast<uint64_t>(flags);
    return valid_privileges(flags) && (impl->privileges_ & bits) == bits;
}

checked_result<void> engine::require_privilege(privilege flags) const noexcept {
    if (!valid_privileges(flags)) {
        return checked_result<void>(make_error_code(runtime_error_code::invalid_operation), "Invalid privilege flags");
    }
    if (is_enabled(flags)) { return {}; }
    return checked_result<void>(make_error_code(runtime_error_code::insufficient_privilege),
        "Insufficient privilege: filesystem.");
}

script_function engine::guard_function(script_function function, privilege required) {
    if (!valid_privileges(required)) { throw runtime_error("Invalid privilege flags"); }
    if (required == privilege::none) { return function; }
    return [this, function = std::move(function), required](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto allowed = require_privilege(required);
        if (!allowed) { return allowed.error_value(); }
        return function(args);
    };
}

script_function engine::guard_function(script_function function, std::span<const std::string_view> names) {
    auto required = resolve_privileges(names);
    // Preserve both the original unguarded callable and the builtin-only fast path.
    if (required.custom.empty()) { return guard_function(std::move(function), required.builtins); }
    return [this, function = std::move(function), required = std::move(required)](const std::vector<script_value>& args) -> checked_result<script_value> {
        auto allowed = require_privileges(required.builtins, required.custom);
        if (!allowed) { return allowed.error_value(); }
        return function(args);
    };
}

void engine::register_privilege_builtins() {
    auto& jai_namespace = script_namespaces()[symbolize("jai")];
    if (!jai_namespace) { jai_namespace = std::make_shared<script_namespace_data>(); }
    jai_namespace->variables[symbolize("filesystem")] = script_value(static_cast<script_int>(privilege::filesystem), this);
    jai_namespace->variables[symbolize("force")] = script_value(std::string("jai::force"), this);   // the import directive (import("m.jai", jai::force)), visible as a value
    auto& reflect_namespace = script_namespaces()[symbolize("reflect")];
    if (!reflect_namespace) { reflect_namespace = std::make_shared<script_namespace_data>(); }
    reflect_namespace->variables[symbolize("is_enabled")] = script_value::make_function(
        [this](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.empty()) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                    "reflect::is_enabled expects at least 1 argument");
            }
            bool enabled = true;
            for (const auto& arg : args) {
                const auto& value = arg.deref();
                if (value.is_string()) {
                    enabled = is_enabled(value.unchecked_as_string()) && enabled;
                    continue;
                }
                if (!value.is_int() || !valid_privileges(static_cast<privilege>(value.unchecked_as_int()))) {
                    return checked_result<script_value>(make_error_code(runtime_error_code::type_mismatch),
                        "reflect::is_enabled expects valid privilege flags");
                }
                enabled = is_enabled(static_cast<privilege>(value.unchecked_as_int())) && enabled;
            }
            return script_value(enabled, this);
        }, this);
}

void engine::max_call_depth(backend_type type, size_t depth) {
    if (type != backend_type::interpreter && type != backend_type::vm) {
        throw runtime_error("max_call_depth requires a built-in backend type");
    }
    if (depth == 0 || depth > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw runtime_error("max_call_depth must be between 1 and INT_MAX");
    }
    if ((impl->parallel_ && impl->parallel_->active_region) ||
        (impl->backend && impl->backend->is_executing())) {
        throw runtime_error("Cannot change max_call_depth during execution");
    }
    int& result = type == backend_type::vm
        ? impl->limits_.vm_call_depth_limit : impl->limits_.interpreter_call_depth_limit;
    result = static_cast<int>(depth);
    impl->limits_.vm_depth_gate = 0;
}

size_t engine::max_call_depth(backend_type type) const {
    if (type != backend_type::interpreter && type != backend_type::vm) {
        throw runtime_error("max_call_depth requires a built-in backend type");
    }
    return static_cast<size_t>(type == backend_type::vm
        ? impl->limits_.vm_call_depth_limit : impl->limits_.interpreter_call_depth_limit);
}

debug::controller& engine::debugger() {
    if (!impl->debugger_) {
        impl->debugger_ = std::make_unique<debug::controller>(this);
        for (const auto& node : impl->script_cache) { impl->debugger_->note_source(node.path); }
        // Wire the active backend's statement hook (backend() constructs it lazily).
        backend()->set_debug_controller(impl->debugger_.get());
    }
    return *impl->debugger_;
}

void engine::set_debug_connector(std::shared_ptr<debug::transport> connector) {
    if (impl->debug_transport_ == connector) return;
    if (impl->debug_transport_) impl->debug_transport_->stop();
    impl->debug_transport_ = std::move(connector);
    if (impl->debug_transport_) impl->debug_transport_->start(this);
}

void engine::memory_cap(size_t bytes) {
    impl->limits_.memory_cap = bytes;
    impl->limits_.memory_limit = bytes ? bytes : SIZE_MAX;
    // Interned once here so the raise path stays allocation-free ({0} in the static
    // error message resolves to this symbol)
    impl->limits_.memory_cap_symbol_id = bytes ? impl->string_symbolizer_.intern(std::to_string(bytes)) : 0;
}

size_t engine::memory_cap() const {
    return impl->limits_.memory_cap;
}

void engine::binary_scan_limit(size_t bytes) {
    impl->limits_.binary_scan_limit = bytes;
}

size_t engine::binary_scan_limit() const {
    return impl->limits_.binary_scan_limit;
}

detail::execution_limits& engine::execution_limits() noexcept {
    // While a parallel region's workers run, every charge site resolves to the calling
    // worker's own accounting through this ONE chokepoint (internal/parallel_design.md §2).
    // Outside a region: a single never-taken null test on an allocation-path accessor.
    if (detail::parallel_engine_state* p = impl->parallel_.get()) [[unlikely]] {
        if (detail::parallel_region_table* region = p->active_region) {
            if (auto* worker_limits = region->find(std::this_thread::get_id())) {
                return *worker_limits;
            }
        }
    }
    return impl->limits_;
}

detail::parallel_engine_state& engine::parallel_state() {
    if (!impl->parallel_) {
        impl->parallel_ = std::make_unique<detail::parallel_engine_state>();
    }
    return *impl->parallel_;
}

void engine::parallel_thread_count(size_t workers) {
    parallel_state().thread_count_override = workers;
}

void engine::parallel_spin_wait_us(size_t microseconds) {
    auto& state = parallel_state();
    state.spin_wait_us = microseconds;
    if (state.pool) { state.pool->spin_budget_ns(static_cast<int64_t>(microseconds) * 1000); }
}

size_t engine::parallel_spin_wait_us() const {
    return impl->parallel_ ? impl->parallel_->spin_wait_us : size_t(1000);
}

size_t engine::parallel_thread_count() const {
    if (impl->parallel_ && impl->parallel_->thread_count_override) {
        return impl->parallel_->thread_count_override;
    }
    // Auto is CAPPED, not hardware_concurrency: region entry pays a serial per-worker
    // provisioning cost, and on high-core hybrid CPUs raw hw (24 on a 285HX) was
    // measurably WORSE than serial (GLOOM 11.7 ms/tick at 24 vs 9.4 serial vs 7.0 at
    // the sweep sweet spot). Auto = min(hardware_concurrency - 1, 6) - a consistent set of
    // pre-allocated pool threads, built once per engine and reused by every region; the
    // region's cost-adaptive rule wakes fewer for light bodies. Callers that know their
    // workload set an explicit count.
    constexpr size_t k_auto_worker_cap = 6;
    const unsigned hw = std::thread::hardware_concurrency();
    const size_t base = hw > 1 ? hw - 1 : 1;
    return base < k_auto_worker_cap ? base : k_auto_worker_cap;
}

const std::vector<size_t>& engine::last_parallel_chunk_bounds() const {
    static const std::vector<size_t> empty;
    return impl->parallel_ ? impl->parallel_->last_chunk_bounds : empty;
}

const std::vector<std::pair<uint64_t, detail::parallel_capture_kind>>& engine::last_parallel_captures() const {
    static const std::vector<std::pair<uint64_t, detail::parallel_capture_kind>> empty;
    return impl->parallel_ ? impl->parallel_->last_captures : empty;
}

// Per-worker copy of a registered callable: invoking the copy must bump no refcount the
// engine's stored values share, so the inner std::function is copied into fresh function
// values, and overload sets dispatch over a per-copy snapshot (findBestMatch returns a
// copy of the stored overload value — the snapshot makes those copies worker-private).
void engine::register_math_alias(std::string_view name, std::string_view intrinsic) {
    static_assert(static_cast<size_t>(detail::math_fn::degrees_to_radians) < k_math_alias_rows);   // every alias kernel (math_alias_kernel) has a row
    const detail::math_fn fn = detail::math_intrinsic_from_name(intrinsic);
    if (fn == detail::math_fn::none || !detail::math_alias_kernel(fn)) { return; }
    const std::string key(name);
    auto set = impl->overloadedFunctions.find(key);
    if (set != impl->overloadedFunctions.end() && set->second.overloads.size() != 1) { return; }
    script_value* global = impl->global_environment_->get_value_ptr(impl->string_symbolizer_.intern(name));
    if (!global || global->raw_storage_index() != script_value::TYPEID_FUNCTION) { return; }
    math_alias_row& row = impl->math_alias_rows_[static_cast<size_t>(fn)];
    row.value = *global;
    row.identity = &row.value.unchecked_as_function();
    impl->math_alias_symbols_[impl->string_symbolizer_.intern(name)] = static_cast<uint8_t>(fn);
}

uint8_t engine::math_alias_fn(uint64_t symbol) const {
    auto it = impl->math_alias_symbols_.find(symbol);
    return it == impl->math_alias_symbols_.end() ? 0xFF : it->second;
}

const engine::math_alias_row* engine::math_alias_rows() const {
    return impl->math_alias_rows_.data();
}

void engine::add_host_direct(std::string_view name, size_t arity, host_direct_invoke invoke, const void* target) {
    if (!invoke || arity == 0 || arity > 8) { return; }
    script_value* global = impl->global_environment_->get_value_ptr(impl->string_symbolizer_.intern(name));
    if (!global || global->raw_storage_index() != script_value::TYPEID_FUNCTION) { return; }
    const void* identity = &global->unchecked_as_function();
    for (auto& row : impl->host_direct_rows_) {
        if (row.identity == identity) { row.invoke = invoke; row.target = target; row.arity = arity; return; }
    }
    host_direct_row& row = impl->host_direct_rows_.emplace_back();
    row.value = *global;
    row.identity = &row.value.unchecked_as_function();
    row.invoke = invoke;
    row.target = target;
    row.arity = arity;
}

const engine::host_direct_row* engine::host_direct_for(const void* identity) const {
    for (const auto& row : impl->host_direct_rows_) {
        if (row.identity == identity) { return &row; }
    }
    return nullptr;
}

const uint64_t* engine::host_direct_generation() const {
    return &impl->host_direct_generation_;
}

script_value engine::make_parallel_host_function_copy(const std::string& name) {
    auto setIt = impl->overloadedFunctions.find(name);
    if (setIt != impl->overloadedFunctions.end()) {
        auto snapshot = std::make_shared<implementation::OverloadSet>();
        snapshot->set_conversion_registry(impl->conversions);
        snapshot->set_class_lookup(setIt->second.classes_by_type, setIt->second.classes_by_name);
        snapshot->overloads.reserve(setIt->second.overloads.size());
        for (auto overload : setIt->second.overloads) {   // copy, then re-mint the value
            script_function inner = overload.function.as_function();
            overload.function = script_value::make_function(std::move(inner), this);
            snapshot->overloads.push_back(std::move(overload));
        }
        uint64_t name_id = impl->string_symbolizer_.intern(name);
        script_function dispatcher = [snapshot, name_id, this](const std::vector<script_value>& args) -> checked_result<script_value> {
            script_value bestMatch = snapshot->findBestMatch(args);
            if (bestMatch.is_null()) {
                return detail::overload_miss(make_error_code(runtime_error_code::argument_count_mismatch),
                    name_id, args.data(), args.size(), const_cast<engine*>(this));
            }
            const script_function& func = bestMatch.as_function();
            return func(args);
        };
        return script_value::make_function(std::move(dispatcher), this);
    }
    auto existing = impl->global_environment_->get(name);
    if (existing && existing.value().is_function()) {
        script_function inner = existing.value().as_function();
        return script_value::make_function(std::move(inner), this);
    }
    return script_value(std::monostate{}, this);
}

std::vector<engine::stack_frame> engine::last_stack_trace() const {
    return impl->backend ? impl->backend->last_stack_trace() : std::vector<stack_frame>{};
}

std::string engine::current_source_file() const {
    return impl->backend ? impl->backend->current_source_file() : std::string();
}

std::string engine::root_source_file() const { return impl->root_source_file_; }

namespace {
    // Absolute, forward-slash, no trailing separator: the one spelling every location
    // builtin answers with
    std::string location_spelling(std::filesystem::path p) {
        std::error_code ec;
        std::filesystem::path canon = std::filesystem::weakly_canonical(p, ec);
        if (!ec && !canon.empty()) p = canon;
        std::string s = p.lexically_normal().generic_string();
        while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':')) s.pop_back();
        return s;
    }
    std::string env_value(const char* name) {
#ifdef _MSC_VER
        char* v = nullptr;
        size_t n = 0;
        if (_dupenv_s(&v, &n, name) != 0 || !v) { return std::string(); }
        std::string out(v);
        std::free(v);
        return out;
#else
        const char* v = std::getenv(name);
        return v && *v ? std::string(v) : std::string();
#endif
    }
    std::string home_directory() {
        std::string h = env_value("HOME");
#ifdef _WIN32
        if (h.empty()) h = env_value("USERPROFILE");
#endif
        return h;
    }
    std::string default_app_data_dir(const std::string& app) {
        namespace fs = std::filesystem;
#ifdef _WIN32
        std::string local = env_value("LOCALAPPDATA");
        if (local.empty()) local = env_value("APPDATA");
        return location_spelling((local.empty() ? fs::path(home_directory()) : fs::path(local)) / app);
#elif defined(__APPLE__)
        return location_spelling(fs::path(home_directory()) / "Library" / "Application Support" / app);
#else
        std::string xdg = env_value("XDG_DATA_HOME");
        return location_spelling((xdg.empty() ? fs::path(home_directory()) / ".local" / "share" : fs::path(xdg)) / app);
#endif
    }
    std::string default_app_cache_dir(const std::string& app) {
        namespace fs = std::filesystem;
#ifdef _WIN32
        return location_spelling(fs::path(default_app_data_dir(app)) / "cache");
#elif defined(__APPLE__)
        return location_spelling(fs::path(home_directory()) / "Library" / "Caches" / app);
#else
        std::string xdg = env_value("XDG_CACHE_HOME");
        return location_spelling((xdg.empty() ? fs::path(home_directory()) / ".cache" : fs::path(xdg)) / app);
#endif
    }
    std::string working_directory() {
        std::error_code ec;
        return location_spelling(std::filesystem::current_path(ec));
    }
}

std::string engine::executable_path() {
#ifdef _WIN32
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) { buf.resize(n); break; }
        buf.resize(buf.size() * 2);
    }
    return std::filesystem::path(buf).generic_string();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    buf.resize(std::char_traits<char>::length(buf.c_str()));
    return buf;
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    buf[n] = '\0';
    return std::string(buf);
#endif
}

void engine::app_directory_resolvers(std::function<std::string(const std::string&)> data,
                                     std::function<std::string(const std::string&)> cache) {
    impl->app_data_resolver = std::move(data);
    impl->app_cache_resolver = std::move(cache);
}

std::string engine::app_data_dir(const std::string& app) const {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(std::string(allowed.message())); }
    return impl->app_data_resolver ? impl->app_data_resolver(app) : default_app_data_dir(app);
}

std::string engine::app_cache_dir(const std::string& app) const {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(std::string(allowed.message())); }
    return impl->app_cache_resolver ? impl->app_cache_resolver(app) : default_app_cache_dir(app);
}

void engine::file_stamp_source(file_stamp_reading source) {
    impl->file_stamp_source = std::move(source);
}

engine_time engine::time() {
    return engine_time(*this);
}

void engine_time::source(clock_reading monotonic, clock_reading wall, clock_sleep sleep) {
    auto& impl = owner_->impl;
    impl->monotonic_clock = std::move(monotonic);
    impl->wall_clock = std::move(wall);
    impl->sleep_clock = std::move(sleep);
    impl->stamped_generation = UINT64_MAX;   // an entry stamp from the previous source means nothing on this one
}

std::chrono::nanoseconds engine_time::monotonic_now() {
    auto& impl = owner_->impl;
    if (impl->monotonic_clock) { return impl->monotonic_clock(); }
    const auto now = std::chrono::steady_clock::now();
    if (!owner_->parallel_region_active()) { impl->limits_.note_clock_sample(now); }
    return now.time_since_epoch();
}

std::chrono::nanoseconds engine_time::wall_now() {
    auto& impl = owner_->impl;
    return impl->wall_clock ? impl->wall_clock()
                            : std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
}

void engine_time::sleep_for(std::chrono::nanoseconds duration) {
    auto& impl = owner_->impl;
    if (impl->sleep_clock) { impl->sleep_clock(duration); }
    else if (duration.count() > 0) { std::this_thread::sleep_for(duration); }
    if (!impl->monotonic_clock && !owner_->parallel_region_active()) { impl->limits_.note_clock_sample(std::chrono::steady_clock::now()); }
}

double engine::engine_elapsed() {
    const auto origin = impl->monotonic_clock ? std::chrono::nanoseconds{} : impl->system_origin;
    return std::chrono::duration<double>(time().monotonic_now() - origin).count();
}

double engine::execute_elapsed() {
    const auto now = time().monotonic_now();
    if (impl->stamped_generation != impl->entry_generation) {   // an entry that began before any loaded program named execute_elapsed counts from here
        impl->entry_stamps = true;
        impl->entry_start = now;
        impl->stamped_generation = impl->entry_generation;
    }
    return std::chrono::duration<double>(now - impl->entry_start).count();
}

uint64_t engine::host_entry_generation() const noexcept { return impl->entry_generation; }

bool engine::unit_rerun(unit_stamp unit) const {
    auto it = impl->unit_first_run_.find(unit.id);
    return it != impl->unit_first_run_.end() && it->second != unit.seq;
}

void engine::note_host_entry() {
    ++impl->entry_generation;
    if (impl->entry_stamps) {
        impl->entry_start = time().monotonic_now();
        impl->stamped_generation = impl->entry_generation;
    }
}

void engine::note_execute_elapsed_use() {
    impl->entry_stamps = true;
}

void engine::register_location_builtins() {
    add_function("root_script_path", [this]() -> std::string {
        std::string root = root_source_file();
        if (root.empty() || root == "<script>") return {};
        std::error_code ec;
        return location_spelling(std::filesystem::absolute(root, ec));
    }, privilege::filesystem);
    add_function("root_script_dir", [this]() -> std::string {
        std::string root = root_source_file();
        if (root.empty() || root == "<script>") return working_directory();
        std::error_code ec;
        return location_spelling(std::filesystem::absolute(root, ec).parent_path());
    }, privilege::filesystem);
    add_function("current_script_path", [this]() -> std::string {
        std::string cur = current_source_file();
        if (cur.empty() || cur == "<script>") cur = root_source_file();
        if (cur.empty() || cur == "<script>") return {};
        std::error_code ec;
        return location_spelling(std::filesystem::absolute(cur, ec));
    }, privilege::filesystem);
    add_function("current_script_dir", [this]() -> std::string {
        std::string cur = current_source_file();
        if (cur.empty() || cur == "<script>") cur = root_source_file();
        if (cur.empty() || cur == "<script>") return working_directory();
        std::error_code ec;
        return location_spelling(std::filesystem::absolute(cur, ec).parent_path());
    }, privilege::filesystem);
    // resolve_path(name): the file import would load for `name` asked from the calling file,
    // "" when no rung of the ladder has it (the calling file's folder, the include paths in
    // order, the working directory; an absolute name stands or falls as given)
    add_function_with_arity_and_full_types("resolve_path", guard_function(
        [this](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                    "Function expects {0} arguments, got {1}", 1, args.size());
            }
            auto name = args[0].checked_as<std::string>();
            if (!name) { return name.error_value(); }
            auto result = resolve_include_path(name.value(), this);
            if (!result) {
                if (result.error() == make_error_code(runtime_error_code::file_not_found)) {
                    return script_value(std::string(), this);
                }
                return result.error_value();
            }
            return script_value(location_spelling(result.value()), this);
        }, privilege::filesystem), 1, {param_type_info(script_value_type::jai_string_type)});
    add_function("executable_path", []() -> std::string { return location_spelling(executable_path()); }, privilege::filesystem);
    auto executable_dir = []() -> std::string {
        std::string exe = executable_path();
        return exe.empty() ? working_directory() : location_spelling(std::filesystem::path(exe).parent_path());
    };
    add_function("executable_dir", executable_dir, privilege::filesystem);
    add_function("bin_dir", executable_dir, privilege::filesystem);
    add_function("working_dir", []() -> std::string { return working_directory(); }, privilege::filesystem);
    add_function("parent_dir", [](const std::string& path) -> std::string {
        std::filesystem::path p = std::filesystem::path(path).lexically_normal();
        if (p.filename().empty()) p = p.parent_path();
        return location_spelling(p.parent_path());
    }, privilege::filesystem);
    add_function("file_name", [](const std::string& path) -> std::string {
        return std::filesystem::path(path).filename().generic_string();
    }, privilege::filesystem);
    // file_data(path): a file's or folder's metadata, found by the resolve_path ladder; null when nothing is there
    add_function_with_arity_and_full_types("file_data", guard_function(
        [this](const std::vector<script_value>& args) -> checked_result<script_value> {
            if (args.size() != 1) {
                return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch),
                    "Function expects {0} arguments, got {1}", 1, args.size());
            }
            auto name = args[0].checked_as<std::string>();
            if (!name) { return name.error_value(); }
            auto resolved = resolve_include_path(name.value(), this);
            if (!resolved) {
                if (resolved.error() == make_error_code(runtime_error_code::file_not_found)) { return script_value(std::monostate{}, this); }
                return resolved.error_value();
            }
            implementation::entry_stat st;
            if (!implementation::stat_entry(resolved.value(), st)) { return script_value(std::monostate{}, this); }
            const std::filesystem::path p(resolved.value());
            auto data = script_value::make_map(get_type_info_string(), nullptr, this);
            auto& m = const_cast<script_map&>(data.as_map());
            m[script_value(std::string("path"), this)] = script_value(location_spelling(p), this);
            m[script_value(std::string("filename"), this)] = script_value(p.filename().generic_string(), this);
            m[script_value(std::string("extension"), this)] = script_value(st.is_directory ? std::string() : p.extension().generic_string(), this);
            m[script_value(std::string("created"), this)] = st.has_created ? script_value(static_cast<script_int>(st.created_ms), this) : script_value(std::monostate{}, this);
            m[script_value(std::string("modified"), this)] = script_value(static_cast<script_int>(st.modified_ms), this);
            m[script_value(std::string("size"), this)] = script_value(static_cast<script_int>(st.size), this);
            m[script_value(std::string("is_directory"), this)] = script_value(st.is_directory, this);
            return data;
        }, privilege::filesystem), 1, {param_type_info(script_value_type::jai_string_type)});
    // import_recheck_interval(seconds) sets the import re-check window, import_recheck_interval() reads it (not a file operation)
    add_variadic_function("import_recheck_interval", [this](const std::vector<script_value>& args) -> checked_result<script_value> {
        if (args.empty()) { return script_value(import_recheck_interval(), this); }
        const script_value& v = args[0].deref();
        if (args.size() != 1 || !(v.is_int() || v.is_float())) {
            return checked_result<script_value>(make_error_code(runtime_error_code::argument_count_mismatch), "import_recheck_interval() takes no argument or one number of seconds");
        }
        const double seconds = v.is_int() ? static_cast<double>(v.unchecked_as_int()) : v.unchecked_as_float();
        if (!(seconds >= 0.0) || seconds > 1.0e9) {
            return checked_result<script_value>(make_error_code(runtime_error_code::invalid_operation), "import_recheck_interval() takes 0 to 1e9 seconds");
        }
        import_recheck_interval(seconds);
        return script_value(std::monostate{}, this);
    });
    add_function("app_data_dir", [this](const std::string& app) -> std::string { return app_data_dir(app); }, privilege::filesystem);
    add_function("app_cache_dir", [this](const std::string& app) -> std::string { return app_cache_dir(app); }, privilege::filesystem);
}

std::string engine::format_stack_trace() const {
    return impl->backend ? impl->backend->format_stack_trace() : std::string();
}

void engine::set_script_error_handler(std::function<void(const std::string&)> a_handler) {
    impl->script_error_handler = std::move(a_handler);
}

void engine::report_script_error(const std::string& a_message, std::error_code a_code) {
    std::string message = a_message;
    if (message.empty()) {
        message = a_code
            ? std::string("script error (") + a_code.category().name() + ": " + a_code.message() + ", no message)"
            : std::string("script error (no message)");
    }
    std::string trace = format_stack_trace();
    if (!trace.empty()) {
        message += "\n" + trace;
    }
    if (impl->script_error_handler) {
        impl->script_error_handler(message);
    } else {
        std::cerr << "[jaiscript] " << message << std::endl;
    }
}

std::type_index engine_context_type_index() noexcept {
    return std::type_index(typeid(engine));
}

void engine::set_script_warning_handler(std::function<void(const std::string&)> a_handler) {
    impl->script_warning_handler = std::move(a_handler);
}

void engine::report_script_warning(const std::string& a_message) {
    if (impl->script_warning_handler) {
        impl->script_warning_handler(a_message);
    } else {
        std::cerr << "[jaiscript] warning: " << a_message << std::endl;
    }
}

void engine::push_external_call_scope() {
    backend()->push_external_call_scope();
}

void engine::pop_external_call_scope() {
    backend()->pop_external_call_scope();
}

conversions::conversion_manager engine::get_conversion_manager() {
    return conversions::conversion_manager(impl->conversions, this);
}

// Include/Import path management
void engine::include_paths(const std::vector<std::string>& paths) {
    impl->include_paths = paths;
    impl->resolution_memo_.clear();
    ++impl->import_generation_;
}

void engine::add_include_path(const std::string& path) {
    impl->include_paths.push_back(path);
    impl->resolution_memo_.clear();
    ++impl->import_generation_;
}

void engine::clear_include_paths() {
    impl->include_paths.clear();
    impl->resolution_memo_.clear();
    ++impl->import_generation_;
}

void engine::import_recheck_interval(double seconds) {
    if (!(seconds >= 0.0) || seconds > 1.0e9) { throw runtime_error("import_recheck_interval must be between 0 and 1e9 seconds"); }
    impl->import_recheck_seconds_ = seconds;
    impl->import_recheck_ticks_ = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds)).count();
    impl->limits_.import_pass_open = false;   // the next import reached opens a pass under the new window
    impl->limits_.import_window_until = 0;
}

double engine::import_recheck_interval() const {
    return impl->import_recheck_seconds_;
}

namespace {
    uint64_t import_path_hash(const std::string& path) noexcept {
        uint64_t h = 1469598103934665603ull;
        for (const unsigned char c : path) { h = (h ^ c) * 1099511628211ull; }
        return h;
    }
    bool import_hash_seen(const std::vector<uint64_t>& seen, uint64_t h) noexcept {
        for (const uint64_t s : seen) { if (s == h) { return true; } }
        return false;
    }
}

bool engine::import_skips(const import_site& site) noexcept {
    const detail::execution_limits& limits = impl->limits_;
    return !limits.import_pass_open && site.seen_generation == impl->import_generation_ && limits.sampled_now < limits.import_window_until &&
           impl->import_behavior != import_behavior::always && (impl->privileges_ & static_cast<uint64_t>(privilege::filesystem)) != 0 &&
           impl->import_frames_.empty();   // a module's run records every import it reaches (its tree)
}

bool engine::import_skips(const import_site& site, const script_value* const* args, size_t count) {
    const detail::execution_limits& limits = impl->limits_;
    if (limits.import_pass_open || site.seen_paths_generation != impl->import_generation_ || limits.sampled_now >= limits.import_window_until ||
        impl->import_behavior == import_behavior::always || (impl->privileges_ & static_cast<uint64_t>(privilege::filesystem)) == 0 ||
        !impl->import_frames_.empty()) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {   // the paths hash straight off the argument values: a skip copies nothing
        const script_value& v = args[i]->deref();
        if (const std::string* text = v.text_bytes()) {
            if (!import_hash_seen(site.seen_paths, import_path_hash(*text))) { return false; }
            continue;
        }
        if (!v.is_array()) { return false; }
        bool seen = true;
        v.unchecked_array_node()->for_each(this, [&](const script_value& element) {
            const std::string* text = seen ? element.deref().text_bytes() : nullptr;
            seen = text && import_hash_seen(site.seen_paths, import_path_hash(*text));
        });
        if (!seen) { return false; }
    }
    return true;
}

uint64_t engine::import_stat_count() const noexcept {
    return impl->import_stats_;
}

uint64_t engine::clock_sample_count() const noexcept {
    return impl->limits_.clock_samples;
}

script_value engine::make_caught_error(const script_value& thrown_in, bool by_throw, const source_location* throw_site,
                                       const std::vector<stack_frame>& trace, const char* thrown_text) {
    return make_caught_error(thrown_in, by_throw, throw_site, std::vector<stack_frame>(trace), thrown_text);
}

script_value engine::make_caught_error(const script_value& thrown_in, bool by_throw, const source_location* throw_site,
                                       std::vector<stack_frame>&& trace, const char* thrown_text) {
    const script_value& thrown = thrown_in.deref();
    const std::string* text = thrown.text_bytes();
    script_value caught(text ? *text : thrown_text ? std::string(thrown_text) : thrown.to_string(), this);
    caught.set_type_info(type_info_ptr{get_type_info_any()});   // the catch variable stays a dynamic local
    if (parallel_region_active()) { return caught; }
    auto& table = impl->caught_errors_;
    if (table.size() >= impl->caught_errors_sweep_at_) {
        for (auto it = table.begin(); it != table.end();) {
            it = it->second.text.unchecked_string_handle().unique() ? table.erase(it) : std::next(it);
        }
        impl->caught_errors_sweep_at_ = std::max<size_t>(32, table.size() * 2);
    }
    implementation::caught_error info{caught, false, {}, 0, 0, std::move(trace), script_value(std::monostate{}, this)};
    if (throw_site && throw_site->line > 0) {
        info.has_site = true;
        info.site_file = throw_site->filename;
        info.site_line = throw_site->line;
        info.site_column = throw_site->column;
    }
    if (by_throw) { info.value = thrown; }
    table.erase(&caught.unchecked_as_string());
    table.emplace(&caught.unchecked_as_string(), std::move(info));
    return caught;
}

std::optional<script_value> engine::caught_error_member(const script_value& receiver, uint64_t member_id) {
    const script_value& r = receiver.deref();
    if (impl->caught_errors_.empty() || r.raw_storage_index() != script_value::TYPEID_STRING) { return std::nullopt; }
    auto it = impl->caught_errors_.find(&r.unchecked_as_string());
    if (it == impl->caught_errors_.end()) { return std::nullopt; }
    const implementation::caught_error& info = it->second;
    const std::string_view name = get_symbolizer()->get_string(member_id);
    const auto spell = [](const std::string& file) { return file.empty() ? std::string("<script>") : file; };
    const stack_frame* innermost = info.trace.empty() ? nullptr : &info.trace.front();
    if (name == "message") { return script_value(r.unchecked_as_string(), this); }
    if (name == "file") {
        return script_value(info.has_site ? spell(info.site_file) : innermost ? spell(innermost->file) : std::string(), this);
    }
    if (name == "line") {
        return script_value(static_cast<script_int>(info.has_site ? info.site_line : innermost ? innermost->line : 0), this);
    }
    if (name == "column") {
        return script_value(static_cast<script_int>(info.has_site ? info.site_column : innermost ? innermost->column : 0), this);
    }
    if (name == "value") { return info.value; }
    if (name == "stack") {
        script_value frames = script_value::make_array(get_type_info_string(), this);
        for (const auto& frame : info.trace) {
            frames.unchecked_get_array_storage()->push(script_value(
                frame.elided ? elided_frames_text(frame.elided) : spell(frame.file) + ":" + std::to_string(frame.line), this));
        }
        return frames;
    }
    // a thrown object or map answers its own fields through the error (catch (e) { e.code })
    script_value v = info.value.deref();
    if (auto instance = v.is_object() ? v.get_class_instance() : nullptr) {
        if (const script_value* field = instance->find_field_value(member_id)) { return script_value(field->deref()); }
    } else if (v.is_map()) {
        const auto& map = v.as_map();
        auto found = map.find(script_value(std::string(name), this));
        if (found != map.end()) { return script_value(found->second.deref()); }
    }
    return std::nullopt;
}

// The reach the pass lets through. A pass opens here when forced or when none is open and the window expired; a site that
// reaches a path it already checked in the open pass (the frame loop came around) closes it, and a new one opens only once
// the window expired. Outside a pass only modules that never ran load. Inside one every module is stat'd once per pass; the
// changed ones (a module whose current stamp failed its check holds the whole list back) are read, parsed and checked
// before any of them runs, then run in order.
checked_result<bool> engine::import_modules(import_site& site, const std::vector<std::string>& paths, bool force) {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { return allowed.error_value(); }
    implementation& I = *impl;
    detail::execution_limits& limits = I.limits_;
    const bool always = I.import_behavior == import_behavior::always;
    const bool once = I.import_behavior == import_behavior::once;
    const auto open_pass = [&] {
        limits.import_pass_open = true;
        limits.import_window_until = limits.sampled_now + I.import_recheck_ticks_;
        ++I.import_pass_id_;
        I.cwd_.clear();
    };
    const bool nested = !I.importing_.empty();
    const bool fresh = force || I.import_recheck_ticks_ == 0;   // interval 0: every reach checks, whichever site reaches it
    if (!parallel_region_active()) {
        if (fresh) { open_pass(); }
        else if (!limits.import_pass_open && limits.sampled_now >= limits.import_window_until) { open_pass(); }
    }

    // an answer the ladder remembers from the asking folder itself stands unverified until the module's stamp query, which
    // verifies it (the file is gone: the full ladder); anything else takes the ladder now
    // a module that loaded before and whose file the ladder no longer finds is missing: it raises once, then waits quietly
    struct import_unit { const std::string* path = nullptr; std::string resolved; uint64_t hash = 0; uint64_t mtime = 0, size = 0, runs = 0; bool stamped = false, remembered = false, missing = false; };
    std::vector<import_unit> mods;
    mods.reserve(paths.size());
    const std::string from = current_source_file();
    const auto lost_module = [&](const std::string& key, const std::string& last) -> std::string {
        if (!last.empty() && I.import_cache.count(last)) { I.import_vanished_[key] = last; return last; }
        auto it = I.import_vanished_.find(key);
        return it != I.import_vanished_.end() && I.import_cache.count(it->second) ? it->second : std::string();
    };
    for (const auto& path : paths) {
        import_unit m;
        m.path = &path;
        m.hash = import_path_hash(path);
        const std::string key = resolution_key(path, from);
        auto memo = I.resolution_memo_.find(key);
        if (memo != I.resolution_memo_.end() && memo->second.misses.empty()) {
            m.resolved = memo->second.resolved;
            m.remembered = true;
        } else {
            const std::string last = memo != I.resolution_memo_.end() ? memo->second.resolved : std::string();
            ++I.import_stats_;
            auto resolved = resolve_include_path(path, this, from, true);
            if (!resolved) {
                m.resolved = lost_module(key, last);
                if (m.resolved.empty()) { return resolved.error_value(); }
                m.missing = true;
            } else {
                m.resolved = std::move(resolved.value());
                if (!I.import_vanished_.empty()) { I.import_vanished_.erase(key); }
            }
        }
        mods.push_back(std::move(m));
    }
    const auto stamp = [&](import_unit& m) -> checked_result<void> {
        ++I.import_stats_;
        m.stamped = impl->stat_mtime_size(m.resolved, m.mtime, m.size);
        if (m.stamped || !m.remembered) { return {}; }
        m.remembered = false;
        ++I.import_stats_;
        auto resolved = resolve_include_path(*m.path, this, from, true);
        if (!resolved) { return resolved.error_value(); }
        m.resolved = std::move(resolved.value());
        m.stamped = impl->stat_mtime_size(m.resolved, m.mtime, m.size);
        return {};
    };

    if (limits.import_pass_open && !nested && !fresh) {
        if (site.pass_id != I.import_pass_id_) { site.pass_id = I.import_pass_id_; site.pass_paths.clear(); }
        bool wrapped = false;
        for (const auto& m : mods) { wrapped = wrapped || import_hash_seen(site.pass_paths, m.hash); }
        if (wrapped) {
            limits.import_pass_open = false;
            if (limits.sampled_now >= limits.import_window_until) { open_pass(); site.pass_id = I.import_pass_id_; }
            site.pass_paths.clear();
        }
    }
    const bool checking = limits.import_pass_open || always;
    if (limits.import_pass_open && !nested) {
        if (site.pass_id != I.import_pass_id_) { site.pass_id = I.import_pass_id_; site.pass_paths.clear(); }
        for (const auto& m : mods) { if (!import_hash_seen(site.pass_paths, m.hash)) { site.pass_paths.push_back(m.hash); } }
    }

    std::vector<size_t> changed;
    bool held = false;
    std::string missing_now;   // the first module found gone in this reach: raised once, before anything runs
    // a module checked unchanged walks the modules it imported, each once per pass: a changed one re-runs on its own (its
    // importer does not), an unchanged one walks its own; a changed module's re-run reaches its imports itself
    std::vector<import_unit> nested_changed;
    std::vector<std::string> walking;
    const auto walk = [&](const implementation::import_record& from_record) {
        walking.assign(from_record.children.rbegin(), from_record.children.rend());
        while (!walking.empty()) {
            const std::string child = std::move(walking.back());
            walking.pop_back();
            if (I.importing_.count(child)) { continue; }
            auto crec = I.import_cache.find(child);
            if (crec == I.import_cache.end() || crec->second.checked_pass == I.import_pass_id_) { continue; }
            crec->second.checked_pass = I.import_pass_id_;
            import_unit u;
            u.resolved = child;
            u.runs = crec->second.runs;
            ++I.import_stats_;
            u.stamped = impl->stat_mtime_size(child, u.mtime, u.size);
            if (!u.stamped) {
                if (!crec->second.missing && missing_now.empty()) { missing_now = child; }
                crec->second.missing = true;
                continue;
            }
            if (crec->second.missing) { nested_changed.push_back(std::move(u)); continue; }
            const bool same = u.mtime == crec->second.mtime && u.size == crec->second.size;
            if (same && !I.closure_moved(crec->second)) {
                walking.insert(walking.end(), crec->second.children.rbegin(), crec->second.children.rend());
                continue;
            }
            if (!same && crec->second.failed && u.mtime == crec->second.failed_mtime && u.size == crec->second.failed_size) { continue; }
            nested_changed.push_back(std::move(u));
        }
    };
    for (size_t i = 0; i < mods.size(); ++i) {
        import_unit& m = mods[i];
        bool dup = false;
        for (const size_t c : changed) { dup = dup || mods[c].resolved == m.resolved; }
        if (dup || I.importing_.count(m.resolved)) { continue; }
        auto rec = I.import_cache.find(m.resolved);
        if (rec == I.import_cache.end()) {
            auto stamped = stamp(m);
            if (!stamped) { return stamped.error_value(); }
            changed.push_back(i);
            continue;
        }
        if (always && !m.missing) { changed.push_back(i); continue; }
        if (once || !checking || rec->second.checked_pass == I.import_pass_id_) { continue; }
        rec->second.checked_pass = I.import_pass_id_;
        const std::string before = m.resolved;
        const auto gone = [&] {
            if (!rec->second.missing && missing_now.empty()) { missing_now = before; }
            rec->second.missing = true;
        };
        if (m.missing) { gone(); continue; }
        auto stamped = stamp(m);
        if (!stamped) {
            if (lost_module(resolution_key(*m.path, from), before).empty()) { return stamped.error_value(); }
            m.resolved = before;
            gone();
            continue;
        }
        if (m.resolved != before) {   // the remembered answer moved: the module the ladder finds now
            rec = I.import_cache.find(m.resolved);
            if (rec == I.import_cache.end()) { changed.push_back(i); continue; }
        }
        if (!m.stamped) { changed.push_back(i); continue; }
        if (rec->second.missing) { changed.push_back(i); continue; }
        const bool same = m.mtime == rec->second.mtime && m.size == rec->second.size;
        if (same && !I.closure_moved(rec->second)) { walk(rec->second); continue; }
        if (!same && rec->second.failed && m.mtime == rec->second.failed_mtime && m.size == rec->second.failed_size) { held = true; continue; }
        changed.push_back(i);
    }
    for (const auto& m : mods) { I.note_import_child(m.resolved); }

    const auto mark_seen = [&] {
        bool all = true;
        for (const auto& m : mods) { all = all && I.import_cache.count(m.resolved) != 0; }
        if (all) { site.seen_generation = I.import_generation_; }
        if (site.seen_paths_generation != I.import_generation_) { site.seen_paths.clear(); site.seen_paths_generation = I.import_generation_; }
        for (size_t i = 0; i < mods.size() && i < paths.size(); ++i) {
            if (I.import_cache.count(mods[i].resolved) && !import_hash_seen(site.seen_paths, mods[i].hash)) { site.seen_paths.push_back(mods[i].hash); }
        }
    };

    if (!missing_now.empty()) { mark_seen(); throw runtime_error("Failed to open import file: " + missing_now); }

    const auto record_failure = [&](const import_unit& m) {
        auto rec = I.import_cache.find(m.resolved);
        if (rec == I.import_cache.end()) { return; }
        rec->second.failed = true;
        rec->second.failed_mtime = m.mtime;
        rec->second.failed_size = m.size;
    };
    // reads, checks and runs units[idx...] as one decision; false = nothing ran (a module that ran before was saved mid-read)
    const auto apply = [&](std::vector<import_unit>& units, const std::vector<size_t>& idx) -> bool {
        // a module runs only from text read under its stamp: a save landing mid-read holds a module that ran before until
        // the next pass (its record keeps the old stamp), and a first load reads again at once (bounded)
        std::vector<std::string> contents(idx.size());
        for (size_t k = 0; k < idx.size(); ++k) {
            import_unit& m = units[idx[k]];
            const bool loaded_before = I.import_cache.count(m.resolved) != 0;
            for (int attempt = 0; ; ++attempt) {
                const auto read = impl->read_under_stamp(m.resolved, m.stamped, m.mtime, m.size, contents[k]);
                if (read == implementation::stamped_read::missing) { throw runtime_error("Failed to open import file: " + m.resolved); }
                if (read == implementation::stamped_read::stable) { break; }
                if (loaded_before) { return false; }
                if (attempt == 3) { break; }
                ++I.import_stats_;
                m.stamped = impl->stat_mtime_size(m.resolved, m.mtime, m.size);
            }
        }

        // two or more: every one parsed and checked (the names they declare known to each other) before any runs
        if (idx.size() > 1 && I.static_check_mode_ != check_mode::off) {
            std::vector<std::shared_ptr<implementation::script_cache_entry>> entries(idx.size());
            std::string failures;
            size_t failed = 0;
            std::exception_ptr first;
            for (size_t k = 0; k < idx.size(); ++k) {
                std::filesystem::path sibling;
                bool fresh = false;
                try {
                    entries[k] = I.acquire_entry(*this, contents[k], units[idx[k]].resolved, sibling, fresh);
                } catch (const std::exception& e) {
                    record_failure(units[idx[k]]);
                    failures += std::string(failures.empty() ? "" : "\n") + e.what();
                    if (!failed++) { first = std::current_exception(); }
                }
            }
            for (size_t k = 0; k < idx.size(); ++k) {
                if (!entries[k]) { continue; }
                detail::collect_unit_names(entries[k]->declarations, I.program_names_);
                detail::collect_unit_classes(entries[k]->declarations, I.program_classes_);
                detail::collect_unit_enums(entries[k]->declarations, units[idx[k]].resolved, I.program_enums_);
                detail::collect_unit_functions(entries[k]->declarations, units[idx[k]].resolved, I.program_functions_);
                detail::collect_unit_globals(entries[k]->declarations, units[idx[k]].resolved, I.program_globals_);
            }
            for (size_t k = 0; k < idx.size(); ++k) {
                if (!entries[k]) { continue; }
                const std::string& path = units[idx[k]].resolved;
                I.importing_.insert(path);
                try {
                    I.gate_checked(I.checked_report(*this, *entries[k], true), contents[k], path);
                } catch (const std::exception& e) {
                    record_failure(units[idx[k]]);
                    failures += std::string(failures.empty() ? "" : "\n") + e.what();
                    if (!failed++) { first = std::current_exception(); }
                }
                I.importing_.erase(path);
            }
            if (failed == 1) { std::rethrow_exception(first); }
            if (failed > 1) { throw static_check_error(failures); }
        }

        for (size_t k = 0; k < idx.size(); ++k) {
            const import_unit& m = units[idx[k]];
            auto rec = I.import_cache.find(m.resolved);
            if (rec != I.import_cache.end()) {   // stamped before it runs: a module that fails waits for its next change
                rec->second.mtime = m.mtime;
                rec->second.size = m.size;
                rec->second.checked_pass = I.import_pass_id_;
                rec->second.missing = false;
                ++rec->second.runs;
            }
            if (!I.importing_.insert(m.resolved).second) { continue; }
            struct importing_scope {
                std::unordered_set<std::string>& set;
                const std::string& path;
                ~importing_scope() { set.erase(path); }
            } scope{I.importing_, m.resolved};
            I.import_frames_.emplace_back();
            try {
                execute_source(contents[k], instance_variables{}, m.resolved);
            } catch (const static_check_error&) {
                I.merge_import_frame(m.resolved, I.end_import_frame());
                record_failure(m);
                throw;
            } catch (const parse_error&) {
                I.merge_import_frame(m.resolved, I.end_import_frame());
                record_failure(m);
                throw;
            } catch (...) {
                I.merge_import_frame(m.resolved, I.end_import_frame());
                throw;
            }
            auto frame = I.end_import_frame();
            I.restamp_deferred(*this);
            if (rec == I.import_cache.end()) {
                implementation::import_record record;
                record.resolved_path = m.resolved;
                record.mtime = m.mtime;
                record.size = m.size;
                record.checked_pass = I.import_pass_id_;
                record.runs = 1;
                record.closure = std::move(frame.files);
                record.children = std::move(frame.children);
                I.import_cache[m.resolved] = std::move(record);
            } else {
                rec = I.import_cache.find(m.resolved);
                if (rec != I.import_cache.end()) {
                    rec->second.failed = false;
                    rec->second.closure = std::move(frame.files);
                    rec->second.children = std::move(frame.children);
                }
            }
        }
        return true;
    };

    bool ran = false;
    if (!changed.empty() && !(held && mods.size() > 1)) {
        if (!apply(mods, changed)) { mark_seen(); return false; }
        ran = true;
    }
    for (size_t k = 0; k < nested_changed.size(); ++k) {
        auto rec = I.import_cache.find(nested_changed[k].resolved);
        if (rec != I.import_cache.end() && rec->second.runs != nested_changed[k].runs) { continue; }
        ran = apply(nested_changed, {k}) || ran;
    }
    mark_seen();
    return ran;
}

const std::string* engine::remembered_resolution(const std::string& key) {
    if (parallel_region_active()) { return nullptr; }
    auto it = impl->resolution_memo_.find(key);
    if (it == impl->resolution_memo_.end()) { return nullptr; }
    implementation::entry_stat st;
    bool valid = implementation::stat_entry(it->second.candidate, st);
    for (size_t i = 0; valid && i < it->second.misses.size(); ++i) { valid = !implementation::stat_entry(it->second.misses[i], st); }
    if (!valid) { impl->resolution_memo_.erase(it); return nullptr; }
    return &it->second.resolved;
}

const std::unordered_set<uint64_t>& engine::builtin_method_ids(int kind) {
    if (!impl->builtin_method_ids_) {
        builtin_method_registries registries;
        init_builtin_method_registries(&impl->string_symbolizer_, registries);
        auto& ids = impl->builtin_method_ids_.emplace();
        for (const auto& [id, fn] : registries.array_methods) { ids[0].insert(id); }
        for (const auto& [id, fn] : registries.map_methods) { ids[1].insert(id); }
        for (const auto& [id, fn] : registries.string_methods) { ids[2].insert(id); }
        for (const auto& [id, fn] : registries.utf8_methods) { ids[3].insert(id); }
    }
    return (*impl->builtin_method_ids_)[kind >= 0 && kind < 4 ? kind : 0];
}

std::string engine::resolution_key(const std::string& path, const std::string& from_file) {
    if (impl->cwd_.empty()) {
        std::error_code ec;
        impl->cwd_ = std::filesystem::current_path(ec).string();
    }
    const std::filesystem::path p(path);
    return path + '\n' + (p.is_absolute() || from_file == "<script>" ? std::string() : std::filesystem::path(from_file).parent_path().string()) +
           '\n' + impl->cwd_;
}

void engine::remember_resolution(const std::string& key, std::vector<std::string> misses, std::string candidate, std::string resolved) {
    if (parallel_region_active()) { return; }
    if (impl->resolution_memo_.size() >= 1024) { impl->resolution_memo_.clear(); }
    impl->resolution_memo_[key] = implementation::resolution_entry{std::move(misses), std::move(candidate), std::move(resolved)};
}

std::vector<std::string> engine::get_include_paths() const {
    return impl->include_paths;
}

// Import behavior configuration
void engine::set_import_behavior(import_behavior behavior) {
    impl->import_behavior = behavior;
    ++impl->import_generation_;
}

engine::import_behavior engine::get_import_behavior() const {
    return impl->import_behavior;
}

// Parameter storage for function calls (replaces thread_local)
detail::parameter_storage* engine::get_current_parameter_storage() const {
    return impl->current_parameter_storage;
}

void engine::set_current_parameter_storage(detail::parameter_storage* storage) {
    impl->current_parameter_storage = storage;
}

// Output stream redirection for print()
void engine::set_output_stream(std::shared_ptr<std::ostream> stream) {
    impl->output_stream = stream;
}

std::ostream& engine::get_output_stream() {
    if (impl->output_stream) {
        return *impl->output_stream;
    }
    return std::cout;
}

// Non-member accessor for parameter_storage - breaks circular dependency
namespace detail {
    parameter_storage* get_engine_parameter_storage(engine* eng) {
        return eng ? eng->get_current_parameter_storage() : nullptr;
    }
}

// Import management
void engine::reset_imports() {
    impl->import_cache.clear();
    ++impl->import_generation_;
}

void engine::reset_import(const std::string& path) {
    impl->import_cache.erase(path);
    ++impl->import_generation_;
}

bool engine::is_imported(const std::string& path) const {
    return impl->import_cache.find(path) != impl->import_cache.end();
}

std::vector<std::string> engine::get_imported_files() const {
    std::vector<std::string> result;
    result.reserve(impl->import_cache.size());
    for (const auto& [path, record] : impl->import_cache) {
        result.push_back(path);
    }
    return result;
}

script_value engine::execute_import(const std::string& resolved_path) {
    auto allowed = require_privilege(privilege::filesystem);
    if (!allowed) { throw runtime_error(format_error(allowed)); }
    // Check import behavior
    bool should_import = false;
    
    switch (impl->import_behavior) {
        case import_behavior::always:
            should_import = true;
            break;
            
        case import_behavior::once: {
            auto it = impl->import_cache.find(resolved_path);
            should_import = (it == impl->import_cache.end());
            break;
        }
        
        case import_behavior::file_timestamp: {
            auto it = impl->import_cache.find(resolved_path);
            if (it == impl->import_cache.end()) {
                should_import = true;
            } else {
                // Check if file has been modified
                uint64_t current_mtime = 0, current_size = 0;
                if (impl->stat_mtime_size(resolved_path, current_mtime, current_size)) {
                    should_import = current_mtime != it->second.mtime || current_size != it->second.size || impl->closure_moved(it->second);
                    if (should_import) {
                        // Update the stamp for next check
                        it->second.mtime = current_mtime;
                        it->second.size = current_size;
                    }
                } else {
                    // If we can't stat the file, import it
                    should_import = true;
                }
            }
            break;
        }
    }
    
    if (should_import) {
        if (!impl->importing_.insert(resolved_path).second) {
            return make_null();
        }
        struct importing_scope {
            std::unordered_set<std::string>& set;
            const std::string& path;
            ~importing_scope() { set.erase(path); }
        } scope{impl->importing_, resolved_path};

        // the record keeps the stamp the text was read under; a zero stamp (no stat, or a save landing mid-read) makes
        // the next file_timestamp check import again
        implementation::import_record record;
        record.resolved_path = resolved_path;
        const bool stamped = impl->stat_mtime_size(resolved_path, record.mtime, record.size);
        std::string content;
        const auto read = impl->read_under_stamp(resolved_path, stamped, record.mtime, record.size, content);
        if (read == implementation::stamped_read::missing) {
            throw runtime_error("Failed to open import file: " + resolved_path);
        }
        if (read != implementation::stamped_read::stable || !stamped) { record.mtime = 0; record.size = 0; }

        // Attribute the imported unit to its resolved path (stack traces / debugger).
        impl->import_frames_.emplace_back();
        script_value result = [&] {
            try {
                return execute_source(content, instance_variables{}, resolved_path);
            } catch (...) {
                impl->merge_import_frame(resolved_path, impl->end_import_frame());
                throw;
            }
        }();
        auto frame = impl->end_import_frame();
        record.closure = std::move(frame.files);
        record.children = std::move(frame.children);
        impl->restamp_deferred(*this);   // the unit's classes exist now: the proofs that waited for them
        impl->import_cache[resolved_path] = std::move(record);
        
        return result;
    } else {
        // File already imported and doesn't need re-import
        return make_null();
    }
}


// Note: get_engine_weak_ptr() function removed - no longer needed since script_value uses engine* directly

void engine::note_script_layer(const void* cpp_object, const std::weak_ptr<class_instance>& owner) {
    if (!cpp_object) { return; }
    if (parallel_region_active_ && impl->parallel_ && impl->parallel_->active_region) [[unlikely]] {   // a region's chunk: the join records it
        if (const auto* chunk = impl->parallel_->active_region->find_entry(std::this_thread::get_id()); chunk && chunk->pending_script_layers) {
            chunk->pending_script_layers->emplace_back(cpp_object, owner);
            return;
        }
    }
    auto& layers = impl->script_layers_;
    if (layers.size() >= impl->script_layer_sweep_watermark_) {
        for (auto it = layers.begin(); it != layers.end();) {
            it = it->second.expired() ? layers.erase(it) : std::next(it);
        }
        impl->script_layer_sweep_watermark_ = std::max<size_t>(64, layers.size() * 2);
    }
    layers[cpp_object] = owner;
}

std::shared_ptr<class_instance> engine::object_instance_of(const void* cpp_object) const {
    auto& layers = impl->script_layers_;
    if (layers.empty()) { return nullptr; }
    auto it = layers.find(cpp_object);
    return it == layers.end() ? nullptr : it->second.lock();
}

std::shared_ptr<class_instance> engine::script_layer_of(const void* cpp_object) const {
    auto instance = object_instance_of(cpp_object);
    const class_definition* def = instance ? instance->get_class_definition() : nullptr;
    return def && def->is_script_class() ? instance : nullptr;
}

bool engine::script_layer_save(const std::shared_ptr<class_instance>& layer, std::string& script_class_name, std::string& fields_json) {
    if (!layer || layer->get_engine() != this) { return false; }
    auto class_def = layer->get_class_definition();
    if (!class_def || !class_def->find_cpp_base_class()) { return false; }
    script_class_name = class_def->get_name();
    script_value fields = script_value::make_map(get_type_info_string(), nullptr, this);
    auto& fieldMap = const_cast<script_map&>(fields.as_map());
    // the layer's own state: instance fields the script classes of the chain declare (a C++ property is the object's)
    std::unordered_set<uint64_t> script_fields;
    std::function<void(const class_definition&)> collect = [&](const class_definition& def) {
        if (!def.is_script_class()) { return; }
        for (const auto& parent : def.get_parent_classes()) { if (parent) { collect(*parent); } }
        for (uint64_t id : def.field_order()) { if (!def.has_static_field(id)) { script_fields.insert(id); } }
    };
    collect(*class_def);
    layer->for_each_field([&](uint64_t id, const script_value& value) {
        if (!script_fields.count(id)) { return; }
        fieldMap.emplace(script_value(std::string(get_symbolizer()->get_string(id)), this), value);
    });
    serialization::json_archive_writer writer(-1, this);
    writer.write_value(fields);
    fields_json = writer.str();
    return true;
}

std::string engine::script_layer_base_name(const std::string& script_class_name) {
    auto script_def = get_class_registry().find_script_class(script_class_name);
    if (!script_def) { return {}; }
    auto base = script_def->find_cpp_base_class();
    return base ? base->get_name() : std::string();
}

bool engine::script_layer_object_owned(const std::shared_ptr<class_instance>& layer) const {
    auto class_def = layer ? layer->get_class_definition() : nullptr;
    auto base = class_def ? class_def->find_cpp_base_class() : nullptr;
    return base && base->slot_hooks();
}

bool engine::script_class_default_constructible(const class_definition& cls) const {
    if (!cls.is_script_class()) { return true; }
    const auto& script = static_cast<const script_class_definition&>(cls);
    if (!script.get_constructor_asts().empty()) {
        for (const auto& ctor : script.get_constructor_asts()) {
            if (arity_accepts(ctor->parameters, 0)) { return true; }
        }
        return false;
    }
    // no declared constructor: the implicit one default-constructs the base
    const class_definition* parent = script.parent_ptr();
    if (!parent) { return true; }
    if (parent->is_script_class()) { return script_class_default_constructible(*parent); }
    std::vector<host_overload_signature> signatures;
    if (!host_function_signatures(parent->get_name(), signatures)) { return false; }
    for (const auto& signature : signatures) {
        if (signature.arity == 0 || signature.arity == SIZE_MAX) { return true; }
    }
    return false;
}

std::shared_ptr<class_instance> engine::construct_script_layer_for_load(const std::string& script_class_name, std::shared_ptr<void>& cpp_object) {
    auto script_def = get_class_registry().find_script_class(script_class_name);
    if (!script_def || !script_class_default_constructible(*script_def)) { return nullptr; }
    script_value ctor = get_variable(script_class_name);
    if (!ctor.is_function()) { return nullptr; }
    checked_result<script_value> constructed = [&]() {
        construction_access_waiver waiver(this);
        return ctor.as_function()({});
    }();
    if (!constructed) { throw runtime_error(format_error(constructed)); }
    if (backend_unwinding()) { throw runtime_error(backend_exception_text()); }
    auto instance = constructed.value().is_object() ? constructed.value().get_class_instance() : nullptr;
    cpp_object = instance ? instance->get_cpp_object() : nullptr;
    if (!cpp_object) {
        throw runtime_error("Failed to reconstruct hybrid script class '" + script_class_name + "'");
    }
    return instance;
}

checked_result<void> engine::hydrate_script_layer(const std::shared_ptr<class_instance>& instance, const std::string& fields_json) {
    if (!instance) { return {}; }
    auto* script_def = instance->get_class_definition();
    if (!script_def) { return {}; }
    script_value self_value = script_value::make_object(script_def->get_name(), instance, this);
    if (!fields_json.empty()) {
        serialization::json_archive_reader reader(fields_json, this);
        script_value fields = reader.read_value();
        if (fields.is_map()) {
            stdlib::archive_handles handles;
            handles.root = &fields;
            stdlib::load_saved_fields(this, self_value, fields.as_map(), [this, &handles](const script_value& value) {
                return stdlib::reconstruct_archive_value(this, value, true, handles);
            });
        }
    }
    defer_post_load(self_value, 1);
    return {};
}

std::shared_ptr<class_instance> engine::extend_loaded(const std::string& script_class_name,
        const std::shared_ptr<void>& cpp_object, const std::string& fields_json) {
    auto script_def = get_class_registry().find_script_class(script_class_name);
    if (!script_def) { return nullptr; }
    auto base = script_def->find_cpp_base_class();
    if (!base || !cpp_object) { return nullptr; }
    auto layer = build_script_layer(script_def, base, cpp_object, false, nullptr, fields_json);
    return layer ? layer.value() : nullptr;
}

checked_result<std::shared_ptr<class_definition>> engine::resolve_script_class(const script_value& script_class) {
    using result = checked_result<std::shared_ptr<class_definition>>;
    std::string name;
    if (script_class.is_string()) {
        name = script_class.as_string();
    } else if (script_class.is_function()) {
        auto* thunk = script_class.as_function().target<script_callable_thunk>();
        if (!thunk || thunk->payload.kind != script_callable::kind_type::constructor || !thunk->payload.cls) {
            return result(make_error_code(runtime_error_code::extend_rejected),
                "extend: the second argument must name a script class (a class name or a string), not a function");
        }
        if (thunk->payload.cls->is_script_class()) { return result(thunk->payload.cls); }
        name = thunk->payload.cls->get_name();
    } else {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: the second argument must name a script class (a class name or a string)");
    }
    auto script_def = get_class_registry().find_script_class(name);
    if (!script_def) {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: '{0}' is not a script class defined in this engine", symbolize(name));
    }
    return result(std::shared_ptr<class_definition>(script_def));
}

checked_result<std::shared_ptr<class_instance>> engine::build_script_layer(const std::shared_ptr<class_definition>& script_def,
        const std::shared_ptr<class_definition>& base_def, const std::shared_ptr<void>& object, bool engine_owned,
        const std::shared_ptr<class_instance>& carry_from, const std::string& fields_json) {
    using result = checked_result<std::shared_ptr<class_instance>>;
    auto instance = script_def->create_instance();
    if (!instance) {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: could not instantiate script class '{0}'", script_def->get_type_id());
    }
    if (auto hooks = base_def->slot_hooks()) {
        // the object owns its layer: the layer only references the object (weakly for ownership)
        instance->set_native(script_value::make_cpp_reference(base_def->get_name(), base_def->get_type_id(), object.get(), object, this));
        hooks->own(object.get(), instance);
        base_def->note_extended();
    } else {
        instance->set_native(script_value::make_cpp_object(base_def->get_name(), base_def->get_type_id(), object, this));
        if (engine_owned) { impl->owned_script_layers_[object.get()] = instance; }
    }
    backend()->apply_field_initializers(instance, script_def);
    if (carry_from) {
        carry_from->for_each_field([&](uint64_t id, const script_value& value) {
            if (instance->has_field(id)) { instance->set_field(id, value.clone()); }
        });
    }
    if (!fields_json.empty()) {
        auto hydrated = hydrate_script_layer(instance, fields_json);
        if (!hydrated) { return hydrated.error_value(); }
    }
    return result(std::move(instance));
}

script_value engine::extend(const script_value& object, const script_value& script_class) {
    return extend(object, script_class, std::string());
}

script_value engine::extend(const script_value& object, const script_value& script_class, const std::string& fields_json) {
    auto layer = extend_checked(object, script_class, fields_json);
    if (!layer) {
        throw runtime_error(jai::format_error(layer, *get_symbolizer()));
    }
    return std::move(layer.value());
}

namespace {
// extend's in-place re-class: remember the fields the instance already carries, adopt the
// new class, seed and initialize every declared field, then put the carried values back
// where the new class declares them (fields only the old class declared stay in overflow).
void reclass_in_place(engine& eng, const std::shared_ptr<class_instance>& instance,
                      const std::shared_ptr<class_definition>& script_def) {
    std::vector<std::pair<uint64_t, script_value>> carried;
    // a C++ property's placeholder is no script state: the object holds the property, and a script field the new class declares
    // under its name keeps its initializer
    const class_definition* old_def = instance->get_class_definition();
    instance->for_each_field([&](uint64_t id, const script_value& value) {
        if (old_def && old_def->is_cpp_class()) { return; }
        if (old_def) {
            const uint32_t slot = old_def->field_slot(id);
            if (slot != class_definition::k_no_field_slot && old_def->slot_is_cpp_property(slot)) { return; }
        }
        carried.emplace_back(id, value);
    });
    const bool fresh = old_def && old_def->is_cpp_class();   // a plain wrapper holds no script state
    instance->reclass(script_def);
    // a script class exists, so the backend that defined it does
    auto* backend = eng.get_execution_backend();
    if (!(fresh && backend && backend->seed_fresh_layer_fields(instance, script_def))) {
        for (const auto& [id, default_value] : script_def->get_all_field_defaults()) {
            if (!instance->has_field_value(id)) { instance->set_field(id, default_value); }
        }
        if (backend) {
            backend->apply_field_initializers(instance, script_def);
            if (fresh) { backend->note_fresh_layer_fields(instance, script_def); }
        }
    }
    for (auto& [id, value] : carried) {
        if (instance->has_field(id)) { instance->set_field(id, value); }
    }
}
}

checked_result<script_value> engine::extend_checked(const script_value& object, const script_value& script_class, const std::string& fields_json) {
    using result = checked_result<script_value>;
    auto resolved = resolve_script_class(script_class);
    if (!resolved) { return resolved.error_value(); }
    const std::shared_ptr<class_definition>& script_def = resolved.value();
    const uint64_t class_id = script_def->get_type_id();
    auto holder = object.deref().get_object_holder();
    if (!holder || !holder->is_class_instance_wrapper || !holder->data) {
        return result(make_error_code(runtime_error_code::extend_rejected), "extend: '{0}' needs an object to extend", class_id);
    }
    auto instance = std::static_pointer_cast<class_instance>(holder->data);
    auto instance_def = instance->get_class_definition();
    if (!instance_def) {
        return result(make_error_code(runtime_error_code::extend_rejected), "extend: the object has no class", class_id);
    }
    auto as_layer = [&]() { return result(script_value::make_object(script_def->get_name(), instance, this)); };

    // A pure script instance: re-derive in place (Class must derive its class)
    if (!instance->has_cpp_object()) {
        if (instance_def == script_def.get()) { return result(object); }
        if (!script_def->derives_from(instance_def)) {
            return result(make_error_code(runtime_error_code::extend_rejected),
                "extend: '{0}' does not derive '{1}'", class_id, instance_def->get_type_id());
        }
        reclass_in_place(*this, instance, script_def);
        return as_layer();
    }

    // A C++ wrapper or a layer: Class's C++ base must be the object's C++ class or one of its bases
    auto base = instance_def->is_script_class() ? instance_def->find_cpp_base_class()
                                                : get_class_definition(instance_def->get_type_id());
    if (!base) {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: the object's class '{0}' has no bound C++ base", instance_def->get_type_id());
    }
    auto script_base = script_def->find_cpp_base_class();
    if (!script_base) {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: '{0}' does not derive '{1}' (the object's C++ class)", class_id, base->get_type_id());
    }
    if (instance->script_layer_retired()) {
        return result(make_error_code(runtime_error_code::script_layer_retired),
            "Script layer '{0}' was detached (script_layer_detach, or a move to another engine): this handle no longer reaches its object",
            instance_def->get_type_id());
    }
    script_value cpp_value = instance->native_or_throw();
    auto cpp_holder = cpp_value.get_object_holder();
    std::shared_ptr<void> cpp_object = cpp_holder->data ? cpp_holder->data : cpp_holder->weak_data.lock();
    if (!cpp_object) {
        return result(make_error_code(runtime_error_code::extend_rejected),
            "extend: the C++ object behind '{0}' no longer exists", instance_def->get_type_id());
    }
    // the class's C++ base is the object's C++ class or one of its bases, asked of the object's own stored type; the layer then
    // speaks for the object as that type, so objects of different C++ types share one script class
    if (cpp_holder->type_id != UINT64_MAX && cpp_holder->type_id != base->get_type_id()) {
        if (auto stored_def = get_class_definition(cpp_holder->type_id)) { base = std::move(stored_def); }
    }
    if (!base->derives_from(script_base.get())) {
        const std::string text = "extend: '" + script_def->get_name() + "' derives the C++ class '" + script_base->get_name() +
            "', which is not the object's C++ class '" + base->get_name() + "' or one of its bases";
        return result(make_error_code(runtime_error_code::extend_rejected), "{0}", park_error_text(text));
    }
    void* key = cpp_object.get();
    auto existing = base->slot_hooks() ? base->slot_hooks()->read(key) : script_layer_of(key);

    if (existing && existing->get_engine() != this) {
        // another engine's layer: move it here as a fresh instance (symbols are engine-local)
        std::string carried = fields_json;
        engine* owner = existing->get_engine();
        std::string moved_class, moved_fields;
        if (owner && owner->script_layer_save(existing, moved_class, moved_fields) && carried.empty()) { carried = moved_fields; }
        if (owner) { owner->detach_object(existing); }
        auto layer = build_script_layer(script_def, base, cpp_object, true, nullptr, carried);
        if (!layer) { return layer.error_value(); }
        return result(script_value::make_object(script_def->get_name(), layer.value(), this));
    }
    if (existing && existing != instance) {
        // a stale wrapper of an object that already carries a layer: the layer is the identity
        instance = existing;
        instance_def = instance->get_class_definition();
    }
    if (instance_def != script_def.get()) {
        const bool was_wrapper = !instance_def->is_script_class();
        reclass_in_place(*this, instance, script_def);
        if (was_wrapper) {
            // an object only the script owns at this moment stays the script's: the layer keeps owning it, as a constructed hybrid's does
            const bool script_owns_alone = cpp_holder->data && cpp_object.use_count() <= 2;   // the wrapper's holder and cpp_object
            if (auto hooks = base->slot_hooks(); hooks && script_owns_alone) {
                hooks->attach(key, instance);
            } else if (hooks) {
                // the host owns the object too: the object owns its layer, which must not own it back (S2)
                instance->set_native(script_value::make_cpp_reference(base->get_name(), base->get_type_id(), key, cpp_object, this));
                hooks->own(key, instance);
                base->note_extended();
            } else {
                impl->owned_script_layers_[key] = instance;
                note_script_layer(key, instance);
            }
        }
    }
    if (!fields_json.empty()) {
        serialization::json_archive_reader reader(fields_json, this);
        script_value fields = reader.read_value();
        if (fields.is_map()) {
            stdlib::archive_handles handles;
            handles.root = &fields;
            for (const auto& [name, value] : fields.as_map()) {
                if (!name.is_string()) { continue; }
                uint64_t field_id = symbolize(name.as_string());
                if (instance->has_field(field_id)) {
                    instance->set_field(field_id, stdlib::reconstruct_archive_value(this, value, true, handles));
                }
            }
        }
        script_value post_load = script_def->get_method(impl->post_load_id_, false);
        if (post_load.type() == script_value_type::jai_function_type) {
            std::vector<script_value> args = { script_value::make_object(script_def->get_name(), instance, this),
                                               script_value(static_cast<script_int>(1), this) };
            auto ran = post_load.as_function()(args);
            if (!ran) { return ran.error_value(); }
        }
    }
    return as_layer();
}

std::string engine::detach_object(const std::shared_ptr<class_instance>& layer) {
    std::string class_name, fields;
    script_layer_save(layer, class_name, fields);
    if (!layer->native_lookup_enabled() || !layer->native()) { return fields; }
    script_value owning = *layer->native();
    auto holder = owning.get_object_holder();
    if (!holder) { return fields; }
    void* object = holder->data ? holder->data.get() : holder->bound_ptr;
    if (!object) { return fields; }
    auto class_def = layer->get_class_definition();
    auto base = class_def ? class_def->find_cpp_base_class() : nullptr;
    // Retire first: an object-owned layer may die the moment the slot lets go
    layer->retire_script_layer();
    if (base && base->slot_hooks()) {
        base->slot_hooks()->detach(object);
    } else {
        impl->script_layers_.erase(object);
        impl->owned_script_layers_.erase(object);
    }
    return fields;
}

void detail::retire_script_layer(const std::weak_ptr<class_instance>& layer) {
    if (auto live = layer.lock()) { live->retire_script_layer(); }
}

std::string detail::script_layer_to_json(const std::shared_ptr<class_instance>& layer) {
    engine* eng = layer ? layer->get_engine() : nullptr;
    std::string class_name, fields;
    if (!eng || !eng->script_layer_save(layer, class_name, fields)) { return {}; }
    script_value envelope = script_value::make_map(eng->get_type_info_string(), nullptr, eng);
    auto& map = const_cast<script_map&>(envelope.as_map());
    map.emplace(script_value(std::string("$type"), eng), script_value(class_name, eng));
    serialization::json_archive_reader fields_reader(fields, eng);
    map.emplace(script_value(std::string("$script"), eng), fields_reader.read_value());
    serialization::json_archive_writer writer(-1, eng);
    writer.write_value(envelope);
    return writer.str();
}

bool detail::script_layer_from_json(const std::shared_ptr<class_instance>& layer, const std::string& json) {
    engine* eng = layer ? layer->get_engine() : nullptr;
    if (!eng || json.empty()) { return false; }
    serialization::json_archive_reader reader(json, eng);
    script_value envelope = reader.read_value();
    if (!envelope.is_map()) { return false; }
    const auto& map = envelope.as_map();
    auto type_it = map.find(script_value(std::string("$type"), eng));
    auto fields_it = map.find(script_value(std::string("$script"), eng));
    if (type_it == map.end() || !type_it->second.is_string() || fields_it == map.end()) { return false; }
    serialization::json_archive_writer fields_writer(-1, eng);
    fields_writer.write_value(fields_it->second);
    // a different $type re-derives (fields carried), the same class hydrates in place
    std::shared_ptr<class_instance> target = layer;
    if (layer->get_class_definition()->get_name() != type_it->second.as_string()) {
        auto rederived = eng->extend_checked(script_value::make_object(layer->get_class_definition()->get_name(), layer, eng),
                                             type_it->second, fields_writer.str());
        return rederived.has_value();
    }
    serialization::json_archive_reader fields_reader(fields_writer.str(), eng);
    script_value fields = fields_reader.read_value();
    if (!fields.is_map()) { return false; }
    stdlib::archive_handles handles;
    handles.root = &fields;
    for (const auto& [key, value] : fields.as_map()) {
        if (!key.is_string()) { continue; }
        uint64_t field_id = eng->symbolize(key.as_string());
        if (target->has_field(field_id)) {
            target->set_field(field_id, stdlib::reconstruct_archive_value(eng, value, true, handles));
        }
    }
    return true;
}

std::shared_ptr<void> engine_lock_instance_cpp_object(engine* eng, const std::shared_ptr<void>& instance_data) {
    if (!eng || !instance_data) { return nullptr; }
    auto instance = std::static_pointer_cast<class_instance>(instance_data);
    if (!instance->native_lookup_enabled() || !instance->native()) { return nullptr; }
    const script_value& cppObjValue = *instance->native();
    if (cppObjValue.is_null() || cppObjValue.type() != script_value_type::jai_object_type) { return nullptr; }
    auto objHolder = cppObjValue.get_object_holder();
    if (!objHolder) { return nullptr; }
    return objHolder->data ? objHolder->data : objHolder->weak_data.lock();
}

std::shared_ptr<conversions::conversion_registry> get_engine_conversion_registry(engine* eng) {
    return eng ? eng->get_conversion_registry() : nullptr;
}

type_info* get_engine_class_type_info_for(engine* eng, const std::type_info& cpp_type) {
    if (!eng) { return nullptr; }
    auto def = eng->get_class_definition_by_type(std::type_index(cpp_type));
    return def ? def->get_type_info() : nullptr;
}

// Subclass admission: does class `derived_id` equal or derive from
// `base_id`? Exact ids answer without a lookup; otherwise one registry lookup and a
// depth-capped walk over the parent chain (script parents and bound C++ bases alike).
namespace detail {
bool class_derives_from(engine* eng, uint64_t derived_id, uint64_t base_id) {
    if (derived_id == base_id) { return true; }
    if (!eng) { return false; }
    auto def = eng->get_class_definition(derived_id);
    if (!def) { return false; }
    std::vector<const class_definition*> stack;
    stack.push_back(def.get());
    int budget = 64;
    while (!stack.empty() && budget-- > 0) {
        const class_definition* cur = stack.back();
        stack.pop_back();
        for (const auto& parent : cur->get_parent_classes()) {
            if (!parent) { continue; }
            if (parent->get_type_id() == base_id) { return true; }
            stack.push_back(parent.get());
        }
    }
    return false;
}

bool object_element_admits(const script_value& v, const type_info& element_type, engine* eng) {
    return class_slot_admits(resolve_slot_class(v, eng), v, element_type, eng);
}

bool shared_element_admits(const script_value& v, const type_info_ptr& element_type, engine* eng) {
    return shared_handle_admits(v, element_type, eng);
}

bool weak_element_takes_shared(const script_value& v, const type_info_ptr& element_type, engine* eng) {
    return weak_slot_takes_shared(v, element_type, eng);
}

checked_result<script_value> weak_element_from_shared(const script_value& v, engine* eng) {
    return script_value::make_weak_ptr(v, eng);
}

type_info_ptr object_element_own_tag(const script_value& v, engine* eng) {
    auto view = resolve_slot_class(v, eng);
    return view.def ? view.def->get_type_info() : nullptr;
}

checked_result<void> store_through_held_reference(script_value& held, const script_value& value, engine* eng) {
    script_value source = value;
    return ref_store_through_storage(held, source, eng, eng->get_symbolizer());
}
} // namespace detail

} // namespace jai
