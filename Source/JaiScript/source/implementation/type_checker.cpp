// Opt-in static type checker (docs/static_checking.md). Backend-neutral AST walk,
// run once per unique source at parse-cache-entry creation (engine.cpp hook).
//
// Guiding contract: NO FALSE POSITIVES. A program that runs clean and doesn't
// genuinely violate declared types must produce zero error diagnostics. Anywhere
// the static story is uncertain (var, unknown classes, includes, builtin methods,
// element types, host object conversions) the checker goes silent, mirroring the
// runtime conversion rules in interpreter::enforce_type_compatibility.

#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/detail/environment.hpp>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace jai {

std::string check_report::format(std::string_view source, std::string_view script_name) const {
    // Pre-split source lines for the caret display (empty source = no snippet).
    std::vector<std::string_view> lines;
    if (!source.empty()) {
        size_t start = 0;
        while (start <= source.size()) {
            size_t end = source.find('\n', start);
            if (end == std::string_view::npos) {
                lines.push_back(source.substr(start));
                break;
            }
            lines.push_back(source.substr(start, end - start));
            start = end + 1;
        }
    }

    std::string out;
    for (const auto& d : diagnostics) {
        out.append(script_name);
        out += ':';
        out += std::to_string(d.line);
        out += ':';
        out += std::to_string(d.column);
        out += (d.severity == check_diagnostic::level::error) ? ": error: " : ": warning: ";
        out += d.message;
        if (d.related_line != 0) {
            out += " (declared at " + std::to_string(d.related_line) + ":" + std::to_string(d.related_column) + ")";
        }
        out += '\n';
        if (d.line >= 1 && d.line <= lines.size()) {
            std::string_view line = lines[d.line - 1];
            out += "    ";
            out.append(line);
            out += "\n    ";
            for (size_t i = 0; i + 1 < d.column && i < line.size(); ++i) {
                out += (line[i] == '\t') ? '\t' : ' ';
            }
            out += "^\n";
        }
    }
    if (capped) {
        out += "(diagnostics capped at " + std::to_string(detail::k_check_diagnostic_cap) + ")\n";
    }
    return out;
}

namespace detail {
namespace {

using diag_level = check_diagnostic::level;

// ------------------------------------------------------------------ ctype

// The checker's static-type lattice. `unknown` is the gradual-typing opt-out
// (var, unresolvable expressions): it flows through everything and never errors.
struct ctype {
    enum kind_t : uint8_t {
        unknown, null_k, int_k, float_k, string_k, char_k, bool_k,
        array_k, map_k, function_k, object_k,
        class_ref_k,   // a class used as a value (Class::static access, constructor callee)
        enum_ref_k     // an enum name (member access checks its values)
    };
    kind_t kind = unknown;
    std::string_view cls{};   // object_k / class_ref_k / enum_ref_k

    bool is_unknown() const { return kind == unknown; }
    static ctype make(kind_t k) { return ctype{k, {}}; }
    static ctype object(std::string_view name) { return ctype{object_k, name}; }
};

std::string ctype_name(const ctype& t) {
    switch (t.kind) {
        case ctype::null_k: return "null";
        case ctype::int_k: return "int";
        case ctype::float_k: return "float";
        case ctype::string_k: return "string";
        case ctype::char_k: return "char";
        case ctype::bool_k: return "bool";
        case ctype::array_k: return "array";
        case ctype::map_k: return "map";
        case ctype::function_k: return "function";
        case ctype::object_k: return t.cls.empty() ? "object" : std::string(t.cls);
        case ctype::class_ref_k: return "class " + std::string(t.cls);
        case ctype::enum_ref_k: return "enum " + std::string(t.cls);
        default: return "unknown";
    }
}

ctype ctype_from_type_info(const type_info* ti) {
    if (!ti) {
        return {};
    }
    switch (ti->base_type) {
        case script_value_type::jai_any_type: return {};
        case script_value_type::jai_null_type: return ctype::make(ctype::null_k);
        case script_value_type::jai_int_type: return ctype::make(ctype::int_k);
        case script_value_type::jai_float_type: return ctype::make(ctype::float_k);
        case script_value_type::jai_string_type: return ctype::make(ctype::string_k);
        case script_value_type::jai_char_type: return ctype::make(ctype::char_k);
        case script_value_type::jai_bool_type: return ctype::make(ctype::bool_k);
        case script_value_type::jai_array_type: return ctype::make(ctype::array_k);
        case script_value_type::jai_map_type: return ctype::make(ctype::map_k);
        case script_value_type::jai_function_type: return ctype::make(ctype::function_k);
        case script_value_type::jai_object_type:
        case script_value_type::jai_shared_ptr_type:
            // shared_ptr<T> carries T's name; member access resolves identically
            return ti->type_name.empty() ? ctype{} : ctype::object(ti->type_name);
        case script_value_type::jai_reference_type:
            return ctype_from_type_info(ti->type_params.empty() ? nullptr : ti->type_params[0].get());
        default:
            return {};   // weak_ptr, cpp_bound, invalid: lenient
    }
}

bool is_numeric(ctype::kind_t k) { return k == ctype::int_k || k == ctype::float_k; }

// Result type of `l op r` (token base ops). Anything uncertain -> unknown.
ctype binary_result(const ctype& l, token_type op, const ctype& r) {
    switch (op) {
        case token_type::less:
        case token_type::greater:
        case token_type::less_equal:
        case token_type::greater_equal:
        case token_type::equal_equal:
        case token_type::bang_equal:
        case token_type::ampersand_ampersand:
        case token_type::pipe_pipe:
            return ctype::make(ctype::bool_k);   // guaranteed bool (binary_expr::returns_bool)
        case token_type::plus:
            if (l.kind == ctype::string_k || r.kind == ctype::string_k) {
                return ctype::make(ctype::string_k);   // concat, either side
            }
            [[fallthrough]];
        case token_type::minus:
        case token_type::star:
        case token_type::slash:
            if (is_numeric(l.kind) && is_numeric(r.kind)) {
                return ctype::make((l.kind == ctype::float_k || r.kind == ctype::float_k)
                                       ? ctype::float_k : ctype::int_k);
            }
            return {};
        case token_type::percent:
            return (l.kind == ctype::int_k && r.kind == ctype::int_k) ? ctype::make(ctype::int_k) : ctype{};
        case token_type::ampersand:
        case token_type::pipe:
        case token_type::caret:
        case token_type::left_shift:
        case token_type::right_shift:
            return (l.kind == ctype::int_k && r.kind == ctype::int_k) ? ctype::make(ctype::int_k) : ctype{};
        case token_type::spaceship:
            return (is_numeric(l.kind) && is_numeric(r.kind)) ? ctype::make(ctype::int_k) : ctype{};
        default:
            return {};
    }
}

token_type compound_base_op(token_type op) {
    switch (op) {
        case token_type::plus_equal: return token_type::plus;
        case token_type::minus_equal: return token_type::minus;
        case token_type::star_equal: return token_type::star;
        case token_type::slash_equal: return token_type::slash;
        case token_type::percent_equal: return token_type::percent;
        default: return token_type::equal;
    }
}

// ------------------------------------------------------------------ checker

class static_checker {
public:
    static_checker(engine& eng, const std::vector<declaration_ptr>& decls,
                   const std::vector<std::string>* extra_globals)
        : eng_(eng), decls_(decls) {
        if (extra_globals) {
            for (const auto& n : *extra_globals) {
                extra_globals_.insert(n);
            }
        }
    }

    check_report run() {
        collect_top_level();
        scopes_.emplace_back();
        for (const auto& d : decls_) {
            walk_decl(d.get());
        }
        scopes_.pop_back();

        check_report report;
        report.capped = capped_;
        report.diagnostics = std::move(diags_);
        std::stable_sort(report.diagnostics.begin(), report.diagnostics.end(),
            [](const check_diagnostic& a, const check_diagnostic& b) {
                if (a.line != b.line) { return a.line < b.line; }
                return a.column < b.column;
            });
        return report;
    }

private:
    // -------------------------------------------------------------- tables

    struct sclass {
        const class_decl* decl = nullptr;
        std::unordered_map<std::string_view, const variable_decl*> fields;
        std::unordered_map<std::string_view, const variable_decl*> static_fields;
        std::unordered_map<std::string_view, std::vector<const function_decl*>> methods;
        std::unordered_map<std::string_view, std::vector<const function_decl*>> static_methods;
        std::vector<const function_decl*> ctors;
    };

    engine& eng_;
    const std::vector<declaration_ptr>& decls_;

    std::vector<check_diagnostic> diags_;
    std::unordered_set<std::string> seen_;   // "line:col:message" dedupe keys
    bool capped_ = false;
    bool has_includes_ = false;   // include/import present: undefined identifiers downgrade to warnings

    std::unordered_map<std::string_view, sclass> classes_;
    std::unordered_map<std::string_view, std::vector<const function_decl*>> functions_;
    std::unordered_map<std::string_view, const enum_decl*> enums_;
    std::unordered_set<std::string_view> namespaces_;
    // Every name declared ANYWHERE in the script (any depth). Flow-insensitive safety
    // net: execution order can make later declarations visible (nested functions,
    // conditional paths), so "undeclared" only fires for names absent from this set
    // AND the engine surface — typos, not ordering.
    std::unordered_set<std::string_view> all_names_;
    std::unordered_set<std::string> extra_globals_;

    struct var_entry {
        ctype type;
        bool dynamic = false;             // var (or untyped) — assignments never checked
        const type_info* declared = nullptr;   // for message text; null for auto-inferred
        std::string display;              // declared-type text for messages
        size_t line = 0, column = 0;
    };
    std::vector<std::unordered_map<std::string_view, var_entry>> scopes_;

    struct fn_ctx {
        const type_info* ret = nullptr;   // null = auto
        bool is_coroutine = false;
        std::string_view name;
        size_t line = 0, column = 0;
    };
    std::vector<fn_ctx> fn_stack_;
    sclass* class_ctx_ = nullptr;
    std::string_view class_ctx_name_{};

    // -------------------------------------------------------------- diagnostics

    void diag(diag_level severity, const source_location& loc, std::string message,
              size_t related_line = 0, size_t related_column = 0) {
        if (diags_.size() >= k_check_diagnostic_cap) {
            capped_ = true;
            return;
        }
        std::string key = std::to_string(loc.line) + ":" + std::to_string(loc.column) + ":" + message;
        if (!seen_.insert(std::move(key)).second) {
            return;
        }
        diags_.push_back({severity, loc.line, loc.column, std::move(message), related_line, related_column});
    }

    void undeclared(const source_location& loc, std::string_view name) {
        // Included/imported files can define anything: keep the signal, drop the teeth.
        diag(has_includes_ ? diag_level::warning : diag_level::error, loc,
             "use of undeclared identifier '" + std::string(name) + "'");
    }

    // -------------------------------------------------------------- collection

    void collect_names(const statement* node);        // safety-net name sweep (recursive)
    void collect_names_expr(const expression* e);     // lambdas declare names inside expressions

    void collect_class(const class_decl* cd) {
        sclass& c = classes_[cd->name];
        c = sclass{};   // same-source redefinition: last one wins, don't merge members
        c.decl = cd;
        for (const auto& m : cd->members) {
            if (!m.declaration) { continue; }
            if (m.declaration->get_type() == node_type::variable_decl) {
                auto* f = static_cast<const variable_decl*>(m.declaration.get());
                (f->is_static ? c.static_fields : c.fields).emplace(f->name, f);
                all_names_.insert(f->name);
            } else if (m.declaration->get_type() == node_type::function_decl) {
                auto* fn = static_cast<const function_decl*>(m.declaration.get());
                if (fn->name == cd->name) {
                    c.ctors.push_back(fn);
                } else if (fn->is_static) {
                    c.static_methods[fn->name].push_back(fn);
                } else {
                    c.methods[fn->name].push_back(fn);
                }
                all_names_.insert(fn->name);
            }
        }
    }

    void collect_scope_decls(const std::vector<declaration_ptr>& decls) {
        for (const auto& d : decls) {
            if (!d) { continue; }
            switch (d->get_type()) {
                case node_type::function_decl:
                    functions_[static_cast<const function_decl*>(d.get())->name]
                        .push_back(static_cast<const function_decl*>(d.get()));
                    break;
                case node_type::class_decl:
                    collect_class(static_cast<const class_decl*>(d.get()));
                    break;
                case node_type::enum_decl: {
                    auto* e = static_cast<const enum_decl*>(d.get());
                    enums_[e->name] = e;
                    for (const auto& [vname, _] : e->values) {
                        all_names_.insert(vname);
                    }
                    break;
                }
                case node_type::namespace_decl: {
                    auto* n = static_cast<const namespace_decl*>(d.get());
                    namespaces_.insert(n->name);
                    collect_scope_decls(n->declarations);   // flattened: lenient by design
                    break;
                }
                default:
                    break;
            }
        }
    }

    void collect_top_level() {
        collect_scope_decls(decls_);
        for (const auto& d : decls_) {
            collect_names(d.get());
        }
    }

    // -------------------------------------------------------------- name resolution

    var_entry* find_local(std::string_view name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    bool engine_knows_name(const std::string& name) {
        if (eng_.has_function(name) || eng_.has_variable(name) || eng_.is_type_name(name)) {
            return true;
        }
        if (eng_.get_class_registry().find_script_class(name)) {
            return true;
        }
        auto templates = eng_.get_registered_template_types();
        if (templates.count(name)) {
            return true;
        }
        return eng_.script_namespaces().count(eng_.symbolize(name)) != 0;
    }

    // Static type of an engine global, trusted only when unambiguous.
    ctype engine_global_type(const std::string& name) {
        auto env = eng_.get_global_environment();
        if (!env) { return {}; }
        auto result = env->get(name);
        if (!result) { return {}; }
        const script_value& v = result.value();
        if (v.is_cpp_bound() || v.is_reference()) {
            return {};   // write-through bindings convert on their own terms
        }
        return ctype_from_type_info(v.get_type_info());
    }

    // -------------------------------------------------------------- class lookups

    // 1 = compatible (same or src derived from dst), 0 = definitely unrelated, 2 = unknown
    int class_relation(std::string_view src, std::string_view dst, int depth = 0) {
        if (src == dst) { return 1; }
        if (depth > 32) { return 2; }
        auto it = classes_.find(src);
        if (it != classes_.end()) {
            bool saw_unknown = false;
            for (auto base : it->second.decl->base_classes) {
                int r = class_relation(base, dst, depth + 1);
                if (r == 1) { return 1; }
                if (r == 2) { saw_unknown = true; }
            }
            return saw_unknown ? 2 : 0;
        }
        auto def = engine_class(src);
        if (def) {
            return def->is_subtype_of(std::string(dst)) ? 1 : 0;
        }
        return 2;   // src class not visible: relationship unknowable
    }

    std::shared_ptr<class_definition> engine_class(std::string_view name) {
        std::string n(name);
        auto def = eng_.get_class_definition(n);
        if (def) { return def; }
        auto script_def = eng_.get_class_registry().find_script_class(n);
        return script_def ? std::static_pointer_cast<class_definition>(script_def) : nullptr;
    }

    // Member lookup across the in-script hierarchy, falling back to engine-registered
    // ancestors. `certain` comes back false when any ancestor is unresolvable — the
    // caller must stay silent then.
    struct member_hit {
        bool found = false;
        bool certain = true;
        const variable_decl* field = nullptr;              // typed in-script field
        const std::vector<const function_decl*>* methods = nullptr;
    };

    member_hit lookup_member(std::string_view cls, std::string_view member, bool want_static, int depth = 0) {
        member_hit miss;
        if (depth > 32) { miss.certain = false; return miss; }
        auto it = classes_.find(cls);
        if (it != classes_.end()) {
            sclass& c = it->second;
            if (want_static) {
                if (auto f = c.static_fields.find(member); f != c.static_fields.end()) {
                    return member_hit{true, true, f->second, nullptr};
                }
                if (auto m = c.static_methods.find(member); m != c.static_methods.end()) {
                    return member_hit{true, true, nullptr, &m->second};
                }
            } else {
                if (auto f = c.fields.find(member); f != c.fields.end()) {
                    return member_hit{true, true, f->second, nullptr};
                }
                if (auto m = c.methods.find(member); m != c.methods.end()) {
                    return member_hit{true, true, nullptr, &m->second};
                }
                // instance access reaches statics too (Cat().count, count++ in methods)
                if (auto f = c.static_fields.find(member); f != c.static_fields.end()) {
                    return member_hit{true, true, f->second, nullptr};
                }
                if (auto m = c.static_methods.find(member); m != c.static_methods.end()) {
                    return member_hit{true, true, nullptr, &m->second};
                }
            }
            bool certain = true;
            for (auto base : c.decl->base_classes) {
                member_hit hit = lookup_member(base, member, want_static, depth + 1);
                if (hit.found) { return hit; }
                certain = certain && hit.certain;
            }
            member_hit result;
            result.certain = certain;
            return result;
        }
        auto def = engine_class(cls);
        if (!def) {
            miss.certain = false;
            return miss;
        }
        uint64_t id = eng_.symbolize(member);
        // Script/vm classes from previous executes don't expose a uniform member surface
        // across backends: a hit is trusted, a miss is not (only cpp_class is closed).
        bool closed = def->is_cpp_class();
        if (want_static) {
            if (def->has_static_method(id) || def->is_static_field(id) || def->has_static_field(id)) {
                return member_hit{true, true, nullptr, nullptr};
            }
        } else {
            if (!def->get_method(id, false).is_invalid()) {
                return member_hit{true, true, nullptr, nullptr};
            }
            if (def->find_field_declaring_class(id) != nullptr) {
                return member_hit{true, true, nullptr, nullptr};
            }
            uint64_t getter_id = eng_.symbolize("_get_" + std::string(member));
            if (!def->get_method(getter_id, false).is_invalid()) {
                return member_hit{true, true, nullptr, nullptr};
            }
            if (def->is_static_field(id) || def->has_static_field(id) || def->has_static_method(id)) {
                return member_hit{true, true, nullptr, nullptr};
            }
        }
        miss.certain = closed;   // cpp classes: member definitely absent; others stay open
        return miss;
    }

    // -------------------------------------------------------------- assignability

    // Mirrors interpreter::enforce_type_compatibility: what a store into a `dst`-typed
    // slot accepts (with conversion). Unknown on either side accepts.
    bool assignable(const ctype& src, const ctype& dst) {
        if (dst.is_unknown() || src.is_unknown()) { return true; }
        if (dst.kind == ctype::class_ref_k || dst.kind == ctype::enum_ref_k ||
            src.kind == ctype::class_ref_k || src.kind == ctype::enum_ref_k) {
            return true;   // exotic; stay lenient
        }
        switch (dst.kind) {
            case ctype::int_k:
                return src.kind == ctype::int_k || src.kind == ctype::float_k || src.kind == ctype::bool_k;
            case ctype::float_k:
                return src.kind == ctype::float_k || src.kind == ctype::int_k || src.kind == ctype::bool_k;
            case ctype::bool_k: return true;     // truthy conversion accepts anything
            case ctype::string_k: return true;   // to_string conversion accepts anything
            case ctype::char_k: return src.kind == ctype::char_k;
            case ctype::array_k: return src.kind == ctype::array_k;
            case ctype::map_k: return src.kind == ctype::map_k;
            case ctype::function_k: return true;   // function slots: lenient in v1
            case ctype::null_k: return src.kind == ctype::null_k;   // void returns
            case ctype::object_k: {
                if (src.kind == ctype::null_k) { return true; }
                if (src.kind == ctype::object_k) {
                    int rel = class_relation(src.cls, dst.cls);
                    if (rel != 0) { return true; }
                    return may_construct_from(dst.cls, src);
                }
                if (src.kind == ctype::int_k || src.kind == ctype::float_k || src.kind == ctype::string_k ||
                    src.kind == ctype::bool_k || src.kind == ctype::char_k) {
                    return may_construct_from(dst.cls, src);   // primitive-to-object ctor conversion
                }
                return false;   // array/map/function into an object slot: always a runtime error
            }
            default:
                return true;
        }
    }

    // Constructor-conversion fallback Target(src): checkable only for in-script classes;
    // engine-registered targets stay lenient.
    bool may_construct_from(std::string_view target_cls, const ctype& src) {
        auto it = classes_.find(target_cls);
        if (it == classes_.end()) {
            return engine_class(target_cls) != nullptr;   // registered: lenient
        }
        for (const auto* ctor : it->second.ctors) {
            auto [min_a, max_a] = arity_range(ctor);
            if (min_a > 1 || max_a < 1) { continue; }
            const parameter& p = ctor->parameters[0];
            if (assignable(src, ctype_from_type_info(p.type.get()))) {
                return true;
            }
        }
        return false;
    }

    static std::pair<size_t, size_t> arity_range(const function_decl* fn) {
        size_t min_a = 0;
        for (const auto& p : fn->parameters) {
            if (p.default_value) { break; }
            ++min_a;
        }
        return {min_a, fn->parameters.size()};
    }

    // -------------------------------------------------------------- calls

    struct call_check {
        bool checked = false;   // a statically known callee was actually validated
        ctype result{};
    };

    // args already inferred by the caller
    call_check check_script_overloads(std::string_view fname, const std::vector<const function_decl*>& overloads,
                                      const std::vector<ctype>& args, const source_location& loc) {
        const function_decl* only_viable = nullptr;
        int viable_count = 0;
        for (const auto* fn : overloads) {
            if (script_fn_viable(fn, args)) {
                ++viable_count;
                only_viable = fn;
            }
        }
        call_check out;
        out.checked = true;
        if (viable_count == 0) {
            report_no_overload(fname, overloads, args, loc);
            // recovery: assume the first overload's return type
            if (!overloads.empty() && overloads[0]->return_type && !overloads[0]->is_coroutine) {
                out.result = ctype_from_type_info(overloads[0]->return_type.get());
            }
            return out;
        }
        if (viable_count == 1 && !only_viable->is_coroutine) {
            out.result = ctype_from_type_info(only_viable->return_type.get());
        }
        return out;
    }

    bool script_fn_viable(const function_decl* fn, const std::vector<ctype>& args) {
        auto [min_a, max_a] = arity_range(fn);
        if (args.size() < min_a || args.size() > max_a) { return false; }
        for (size_t i = 0; i < args.size(); ++i) {
            const parameter& p = fn->parameters[i];
            if (p.is_reference) { continue; }   // ref binding rules are lvalue-shaped: lenient
            if (!assignable(args[i], ctype_from_type_info(p.type.get()))) {
                return false;
            }
        }
        return true;
    }

    void report_no_overload(std::string_view fname, const std::vector<const function_decl*>& overloads,
                            const std::vector<ctype>& args, const source_location& loc) {
        size_t rl = overloads.empty() ? 0 : overloads[0]->location.line;
        size_t rc = overloads.empty() ? 0 : overloads[0]->location.column;
        if (overloads.size() == 1) {
            const function_decl* fn = overloads[0];
            auto [min_a, max_a] = arity_range(fn);
            if (args.size() < min_a || args.size() > max_a) {
                std::string expected = (min_a == max_a)
                    ? std::to_string(min_a)
                    : std::to_string(min_a) + " to " + std::to_string(max_a);
                diag(diag_level::error, loc, "'" + std::string(fname) + "' expects " + expected +
                     " argument(s), got " + std::to_string(args.size()), rl, rc);
                return;
            }
            for (size_t i = 0; i < args.size(); ++i) {
                const parameter& p = fn->parameters[i];
                if (p.is_reference) { continue; }
                ctype pt = ctype_from_type_info(p.type.get());
                if (!assignable(args[i], pt)) {
                    diag(diag_level::error, loc, "argument " + std::to_string(i + 1) + " of '" +
                         std::string(fname) + "': cannot pass '" + ctype_name(args[i]) + "' to '" +
                         p.name + "' declared '" + (p.type ? p.type->canonical_name() : "auto") + "'",
                         rl, rc);
                    return;
                }
            }
            return;
        }
        diag(diag_level::error, loc, "no overload of '" + std::string(fname) + "' matches this call (" +
             std::to_string(args.size()) + " argument(s))", rl, rc);
    }

    // Host (C++) function surface: reject only certainties — arity misses across every
    // overload, or a known-primitive argument no builtin conversion reaches.
    void check_host_call(const std::string& fname, const std::vector<ctype>& args, const source_location& loc) {
        std::vector<engine::host_overload_signature> sigs;
        if (!eng_.host_function_signatures(fname, sigs) || sigs.empty()) {
            return;
        }
        bool arity_ok = false;
        bool viable = false;
        for (const auto& sig : sigs) {
            if (sig.arity != SIZE_MAX && sig.arity != args.size()) { continue; }
            arity_ok = true;
            if (host_overload_viable(sig, args)) { viable = true; break; }
        }
        if (!arity_ok) {
            std::string arities;
            for (const auto& sig : sigs) {
                if (sig.arity == SIZE_MAX) { return; }   // variadic somewhere: anything goes
                if (!arities.empty()) { arities += " or "; }
                arities += std::to_string(sig.arity);
            }
            diag(diag_level::error, loc, "'" + fname + "' expects " + arities +
                 " argument(s), got " + std::to_string(args.size()));
            return;
        }
        if (!viable) {
            diag(diag_level::error, loc, "no overload of '" + fname + "' accepts these argument types");
        }
    }

    bool host_overload_viable(const engine::host_overload_signature& sig, const std::vector<ctype>& args) {
        if (sig.param_types.empty()) { return true; }   // untyped legacy binding
        if (sig.param_types.size() != args.size()) { return true; }   // defensive: don't guess
        for (size_t i = 0; i < args.size(); ++i) {
            if (!host_param_accepts(args[i], sig.param_types[i])) {
                return false;
            }
        }
        return true;
    }

    static bool host_param_accepts(const ctype& arg, const param_type_info& p) {
        if (p.base_type == script_value_type::jai_null_type) { return true; }   // script_value wildcard
        if (arg.is_unknown() || arg.kind == ctype::null_k) { return true; }
        auto k = arg.kind;
        switch (p.base_type) {
            case script_value_type::jai_int_type:
            case script_value_type::jai_float_type:
                return k == ctype::int_k || k == ctype::float_k || k == ctype::bool_k || k == ctype::char_k;
            case script_value_type::jai_bool_type:
                return k == ctype::bool_k || k == ctype::int_k || k == ctype::float_k;
            case script_value_type::jai_char_type:
                return k == ctype::char_k || k == ctype::int_k || k == ctype::float_k;
            case script_value_type::jai_string_type:
                return k == ctype::string_k;   // no builtin numeric->string conversion at the C++ boundary
            case script_value_type::jai_array_type:
                return k == ctype::array_k;
            case script_value_type::jai_map_type:
                return k == ctype::map_k;
            default:
                return true;   // objects/pointers: conversion registry territory, lenient
        }
    }

    void check_ctor_call(std::string_view cls_name, const sclass& c, const std::vector<ctype>& args,
                         const source_location& loc) {
        if (c.ctors.empty()) {
            // implicit default + implicit copy
            bool copy_ok = args.size() == 1 &&
                (args[0].is_unknown() || (args[0].kind == ctype::object_k && class_relation(args[0].cls, cls_name) != 0));
            if (!args.empty() && !copy_ok) {
                diag(diag_level::error, loc, "no constructor of '" + std::string(cls_name) + "' takes " +
                     std::to_string(args.size()) + " argument(s)",
                     c.decl->location.line, c.decl->location.column);
            }
            return;
        }
        for (const auto* ctor : c.ctors) {
            if (script_fn_viable(ctor, args)) { return; }
        }
        report_no_overload(cls_name, c.ctors, args, loc);
    }

    // -------------------------------------------------------------- expressions

    ctype infer(expression* e) {
        if (!e) { return {}; }
        switch (e->get_type()) {
            case node_type::literal_expr: {
                auto* lit = static_cast<literal_expr*>(e);
                switch (lit->value.type()) {
                    case script_value_type::jai_null_type: return ctype::make(ctype::null_k);
                    case script_value_type::jai_int_type: return ctype::make(ctype::int_k);
                    case script_value_type::jai_float_type: return ctype::make(ctype::float_k);
                    case script_value_type::jai_string_type: return ctype::make(ctype::string_k);
                    case script_value_type::jai_char_type: return ctype::make(ctype::char_k);
                    case script_value_type::jai_bool_type: return ctype::make(ctype::bool_k);
                    default: return {};
                }
            }
            case node_type::identifier_expr:
                return infer_identifier(static_cast<identifier_expr*>(e));
            case node_type::binary_expr: {
                auto* b = static_cast<binary_expr*>(e);
                ctype l = infer(b->left.get());
                ctype r = infer(b->right.get());
                if (b->op.type == token_type::left_bracket) {
                    return {};   // subscript: element types stay lenient
                }
                return binary_result(l, b->op.type, r);
            }
            case node_type::unary_expr: {
                auto* u = static_cast<unary_expr*>(e);
                ctype operand = infer(u->operand.get());
                switch (u->op.type) {
                    case token_type::bang: return ctype::make(ctype::bool_k);
                    case token_type::minus:
                    case token_type::plus:
                        return is_numeric(operand.kind) ? operand : ctype{};
                    case token_type::plus_plus:
                    case token_type::minus_minus:
                        return is_numeric(operand.kind) ? operand : ctype{};
                    case token_type::tilde:
                        return operand.kind == ctype::int_k ? operand : ctype{};
                    default: return {};
                }
            }
            case node_type::assignment_expr:
                return infer_assignment(static_cast<assignment_expr*>(e));
            case node_type::call_expr:
                return infer_call(static_cast<call_expr*>(e));
            case node_type::member_expr:
                return infer_member(static_cast<member_expr*>(e), /*writing=*/false);
            case node_type::lambda_expr:
                walk_lambda(static_cast<lambda_expr*>(e));
                return ctype::make(ctype::function_k);
            case node_type::new_expr: {
                auto* n = static_cast<new_expr*>(e);
                std::vector<ctype> args;
                for (auto& a : n->arguments) { args.push_back(infer(a.get())); }
                ctype t = ctype_from_type_info(n->type.get());
                if (t.kind == ctype::object_k) {
                    auto it = classes_.find(t.cls);
                    if (it != classes_.end()) {
                        check_ctor_call(t.cls, it->second, args, n->location);
                    }
                }
                return t;
            }
            case node_type::include_expr: {
                auto* inc = static_cast<include_expr*>(e);
                if (inc->path_expr) { infer(inc->path_expr.get()); }
                return ctype::make(ctype::unknown);   // include's value is the file's result
            }
            case node_type::ternary_expr: {
                auto* t = static_cast<ternary_expr*>(e);
                infer(t->condition.get());
                ctype a = infer(t->then_expression.get());
                ctype b = infer(t->else_expression.get());
                return (a.kind == b.kind && a.cls == b.cls) ? a : ctype{};
            }
            case node_type::array_literal_expr: {
                auto* a = static_cast<array_literal_expr*>(e);
                for (auto& el : a->elements) { infer(el.get()); }
                return ctype::make(ctype::array_k);
            }
            case node_type::map_literal_expr: {
                auto* m = static_cast<map_literal_expr*>(e);
                for (auto& [k, v] : m->entries) { infer(k.get()); infer(v.get()); }
                return ctype::make(ctype::map_k);
            }
            case node_type::this_expr:
                return class_ctx_ ? ctype::object(class_ctx_name_) : ctype{};
            case node_type::super_expr:
                if (class_ctx_ && !class_ctx_->decl->base_classes.empty()) {
                    return ctype::object(class_ctx_->decl->base_classes[0]);
                }
                return {};
            case node_type::throw_expr: {
                auto* t = static_cast<throw_expr*>(e);
                if (t->value) { infer(t->value.get()); }
                return {};
            }
            case node_type::yield_expr: {
                auto* y = static_cast<yield_expr*>(e);
                if (y->value) { infer(y->value.get()); }
                return {};
            }
            default:
                return {};
        }
    }

    ctype infer_identifier(identifier_expr* id) {
        if (var_entry* local = find_local(id->name)) {
            return local->dynamic ? ctype{} : local->type;
        }
        if (class_ctx_) {
            member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
            if (hit.found) {
                if (hit.methods) { return ctype::make(ctype::function_k); }
                if (hit.field && hit.field->type) { return ctype_from_type_info(hit.field->type.get()); }
                return {};
            }
        }
        if (functions_.count(id->name)) { return ctype::make(ctype::function_k); }
        if (classes_.count(id->name)) { return ctype{ctype::class_ref_k, id->name}; }
        if (enums_.count(id->name)) { return ctype{ctype::enum_ref_k, id->name}; }
        if (namespaces_.count(id->name)) { return {}; }
        std::string name(id->name);
        if (extra_globals_.count(name)) { return {}; }
        if (eng_.is_type_name(name) || eng_.get_class_registry().find_script_class(name)) {
            return ctype{ctype::class_ref_k, id->name};
        }
        if (eng_.has_function(name)) { return ctype::make(ctype::function_k); }
        if (eng_.has_variable(name)) { return engine_global_type(name); }
        if (engine_knows_name(name)) { return {}; }
        if (all_names_.count(id->name)) { return {}; }
        undeclared(id->location, id->name);
        return {};
    }

    ctype infer_member(member_expr* m, bool writing, const variable_decl** typed_field_out = nullptr) {
        ctype recv = infer(m->object.get());
        if (recv.is_unknown() || recv.kind == ctype::map_k || recv.kind == ctype::array_k ||
            recv.kind == ctype::string_k || recv.kind == ctype::function_k || recv.kind == ctype::null_k ||
            recv.kind == ctype::int_k || recv.kind == ctype::float_k || recv.kind == ctype::char_k ||
            recv.kind == ctype::bool_k) {
            return {};   // builtin methods / map keys / dynamic: lenient
        }
        if (recv.kind == ctype::enum_ref_k) {
            auto it = enums_.find(recv.cls);
            if (it != enums_.end()) {
                for (const auto& [vname, _] : it->second->values) {
                    if (vname == m->member) { return ctype::make(ctype::int_k); }
                }
                diag(diag_level::error, m->location, "no value '" + std::string(m->member) +
                     "' in enum '" + std::string(recv.cls) + "'",
                     it->second->location.line, it->second->location.column);
            }
            return {};
        }
        bool want_static = (recv.kind == ctype::class_ref_k);
        std::string_view cls = recv.cls;
        member_hit hit = lookup_member(cls, m->member, want_static);
        if (hit.found) {
            if (typed_field_out && hit.field && hit.field->type) { *typed_field_out = hit.field; }
            if (hit.methods) { return ctype::make(ctype::function_k); }
            if (hit.field && hit.field->type) { return ctype_from_type_info(hit.field->type.get()); }
            return {};
        }
        if (!hit.certain) { return {}; }
        if (m->member == class_constants::CPP_OBJECT_FIELD) { return {}; }
        size_t rl = 0, rc = 0;
        if (auto it = classes_.find(cls); it != classes_.end()) {
            rl = it->second.decl->location.line;
            rc = it->second.decl->location.column;
        }
        diag(diag_level::error, m->location,
             std::string(want_static ? "no static member '" : "no member '") + std::string(m->member) +
             "' in class '" + std::string(cls) + "'", rl, rc);
        (void)writing;
        return {};
    }

    ctype infer_call(call_expr* c) {
        std::vector<ctype> args;
        args.reserve(c->arguments.size());
        for (auto& a : c->arguments) { args.push_back(infer(a.get())); }

        expression* callee = c->callee.get();
        if (callee->get_type() == node_type::identifier_expr) {
            auto* id = static_cast<identifier_expr*>(callee);
            if (find_local(id->name)) { return {}; }   // callable local: signature unknown
            if (class_ctx_) {
                member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
                if (hit.found) {
                    if (hit.methods) { return check_script_overloads(id->name, *hit.methods, args, c->location).result; }
                    return {};
                }
            }
            if (auto it = functions_.find(id->name); it != functions_.end()) {
                return check_script_overloads(id->name, it->second, args, c->location).result;
            }
            if (auto it = classes_.find(id->name); it != classes_.end()) {
                check_ctor_call(id->name, it->second, args, c->location);
                return ctype::object(id->name);
            }
            if (enums_.count(id->name)) { return {}; }
            std::string name(id->name);
            if (extra_globals_.count(name)) { return {}; }
            if (eng_.is_type_name(name) || eng_.get_class_registry().find_script_class(name)) {
                return ctype::object(id->name);   // registered class ctor: args lenient
            }
            if (eng_.has_function(name)) {
                check_host_call(name, args, c->location);
                return {};
            }
            if (eng_.has_variable(name) || engine_knows_name(name)) { return {}; }
            if (all_names_.count(id->name)) { return {}; }
            undeclared(id->location, id->name);
            return {};
        }
        if (callee->get_type() == node_type::member_expr) {
            auto* m = static_cast<member_expr*>(callee);
            ctype recv = infer(m->object.get());
            if (recv.kind == ctype::object_k || recv.kind == ctype::class_ref_k) {
                bool want_static = (recv.kind == ctype::class_ref_k);
                member_hit hit = lookup_member(recv.cls, m->member, want_static);
                if (hit.found) {
                    if (hit.methods) {
                        return check_script_overloads(m->member, *hit.methods, args, c->location).result;
                    }
                    return {};
                }
                if (hit.certain && m->member != class_constants::CPP_OBJECT_FIELD) {
                    size_t rl = 0, rc = 0;
                    if (auto it = classes_.find(recv.cls); it != classes_.end()) {
                        rl = it->second.decl->location.line;
                        rc = it->second.decl->location.column;
                    }
                    diag(diag_level::error, m->location,
                         std::string(want_static ? "no static member '" : "no member '") + std::string(m->member) +
                         "' in class '" + std::string(recv.cls) + "'", rl, rc);
                }
                return {};
            }
            if (recv.kind == ctype::enum_ref_k) {
                return {};
            }
            return {};   // builtin container/string methods etc.
        }
        infer(callee);
        return {};
    }

    // -------------------------------------------------------------- assignment

    ctype infer_assignment(assignment_expr* a) {
        ctype rhs = infer(a->value.get());
        bool compound = (a->op.type != token_type::equal);
        token_type base_op = compound_base_op(a->op.type);

        expression* target = a->target.get();
        if (target->get_type() == node_type::identifier_expr) {
            auto* id = static_cast<identifier_expr*>(target);
            if (var_entry* local = find_local(id->name)) {
                if (local->dynamic) { return rhs; }
                check_store(rhs, compound, base_op, local->type, local->display,
                            "'" + std::string(id->name) + "'", a->location, local->line, local->column);
                return local->type;
            }
            if (class_ctx_) {
                member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
                if (hit.found) {
                    if (hit.field && hit.field->type) {
                        ctype ft = ctype_from_type_info(hit.field->type.get());
                        if (!ft.is_unknown()) {
                            check_store(rhs, compound, base_op, ft, hit.field->type->canonical_name(),
                                        "field '" + std::string(id->name) + "'", a->location,
                                        hit.field->location.line, hit.field->location.column);
                        }
                        return ft;
                    }
                    return {};
                }
            }
            std::string name(id->name);
            if (functions_.count(id->name) || classes_.count(id->name) || enums_.count(id->name) ||
                namespaces_.count(id->name) || extra_globals_.count(name)) {
                return rhs;
            }
            if (eng_.has_variable(name)) {
                ctype gt = engine_global_type(name);
                if (!gt.is_unknown()) {
                    check_store(rhs, compound, base_op, gt, ctype_name(gt),
                                "'" + std::string(id->name) + "'", a->location, 0, 0);
                }
                return gt;
            }
            if (eng_.has_function(name) || engine_knows_name(name) || all_names_.count(id->name)) {
                return rhs;
            }
            undeclared(id->location, id->name);
            return rhs;
        }
        if (target->get_type() == node_type::member_expr) {
            const variable_decl* typed_field = nullptr;
            infer_member(static_cast<member_expr*>(target), /*writing=*/true, &typed_field);
            if (typed_field && typed_field->type) {
                ctype ft = ctype_from_type_info(typed_field->type.get());
                if (!ft.is_unknown()) {
                    check_store(rhs, compound, base_op, ft, typed_field->type->canonical_name(),
                                "field '" + std::string(typed_field->name) + "'", a->location,
                                typed_field->location.line, typed_field->location.column);
                }
                return ft;
            }
            return rhs;
        }
        infer(target);   // subscripts and other lvalues: element types stay lenient
        return rhs;
    }

    // Dev ruling 2026-07: compound assign on a typed target behaves like C++ —
    // `x op= rhs` is `x = T(x op rhs)` — so it checks exactly like plain assignment
    // of the operation's result. var targets keep the old dynamic behavior (unchecked).
    void check_store(const ctype& rhs, bool compound, token_type base_op, const ctype& target,
                     const std::string& target_display, const std::string& what,
                     const source_location& loc, size_t decl_line, size_t decl_col) {
        if (target.is_unknown()) { return; }
        ctype stored = compound ? binary_result(target, base_op, rhs) : rhs;
        if (assignable(stored, target)) { return; }
        diag(diag_level::error, loc, "cannot assign '" + ctype_name(stored) + "' to " + what +
             " declared '" + target_display + "'", decl_line, decl_col);
    }

    // -------------------------------------------------------------- statements

    void walk_stmt(statement* s) {
        if (!s) { return; }
        switch (s->get_type()) {
            case node_type::expression_stmt:
                infer(static_cast<expression_stmt*>(s)->expression.get());
                break;
            case node_type::block_stmt: {
                auto* b = static_cast<block_stmt*>(s);
                scopes_.emplace_back();
                for (auto& d : b->declarations) { walk_decl(d.get()); }
                scopes_.pop_back();
                break;
            }
            case node_type::if_stmt: {
                auto* i = static_cast<if_stmt*>(s);
                infer(i->condition.get());
                walk_stmt(i->then_statement.get());
                walk_stmt(i->else_statement.get());
                break;
            }
            case node_type::while_stmt: {
                auto* w = static_cast<while_stmt*>(s);
                infer(w->condition.get());
                walk_stmt(w->body.get());
                break;
            }
            case node_type::for_stmt: {
                auto* f = static_cast<for_stmt*>(s);
                scopes_.emplace_back();
                if (f->initializer) { walk_decl(f->initializer.get()); }
                if (f->condition) { infer(f->condition.get()); }
                if (f->update) { infer(f->update.get()); }
                walk_stmt(f->body.get());
                scopes_.pop_back();
                break;
            }
            case node_type::range_for_stmt: {
                auto* f = static_cast<range_for_stmt*>(s);
                infer(f->container.get());
                scopes_.emplace_back();
                var_entry entry;
                entry.type = ctype_from_type_info(f->element_type.get());
                entry.dynamic = !f->element_type || f->element_type->base_type == script_value_type::jai_any_type ||
                                f->is_reference;
                entry.declared = f->element_type.get();
                entry.display = f->element_type ? f->element_type->canonical_name() : "auto";
                entry.line = f->location.line;
                entry.column = f->location.column;
                scopes_.back().emplace(f->variable_name, std::move(entry));
                walk_stmt(f->body.get());
                scopes_.pop_back();
                break;
            }
            case node_type::return_stmt:
                check_return(static_cast<return_stmt*>(s));
                break;
            case node_type::try_stmt: {
                auto* t = static_cast<try_stmt*>(s);
                walk_stmt(t->try_block.get());
                scopes_.emplace_back();
                if (!t->catch_var.empty()) {
                    var_entry entry;
                    entry.dynamic = true;
                    scopes_.back().emplace(std::string_view(t->catch_var), std::move(entry));
                }
                walk_stmt(t->catch_block.get());
                scopes_.pop_back();
                break;
            }
            case node_type::switch_stmt: {
                auto* sw = static_cast<switch_stmt*>(s);
                infer(sw->condition.get());
                for (auto& cs : sw->cases) {
                    infer(cs->value.get());
                    scopes_.emplace_back();
                    for (auto& st : cs->body) { walk_stmt(st.get()); }
                    scopes_.pop_back();
                }
                if (sw->default_case) {
                    scopes_.emplace_back();
                    for (auto& st : sw->default_case->body) { walk_stmt(st.get()); }
                    scopes_.pop_back();
                }
                break;
            }
            case node_type::break_stmt:
            case node_type::continue_stmt:
            case node_type::fallthrough_stmt:
                break;
            default:
                if (s->get_type() >= node_type::variable_decl) {
                    walk_decl(static_cast<declaration*>(s));
                }
                break;
        }
    }

    void check_return(return_stmt* r) {
        ctype value = r->value ? infer(r->value.get()) : ctype::make(ctype::null_k);
        if (fn_stack_.empty()) { return; }
        const fn_ctx& fn = fn_stack_.back();
        if (fn.is_coroutine || !fn.ret) { return; }   // coroutine returns/yields + auto: lenient
        ctype ret = ctype_from_type_info(fn.ret);
        if (ret.is_unknown()) { return; }
        if (ret.kind == ctype::null_k) {
            if (r->value && value.kind != ctype::null_k && !value.is_unknown()) {
                diag(diag_level::error, r->location, "cannot return a value from '" + std::string(fn.name) +
                     "' declared 'void'", fn.line, fn.column);
            }
            return;
        }
        if (!r->value) {
            // Runtime returns null here (typed-return conversion skipped for bare returns
            // per HEAD behavior): warning severity, matching the fall-off-the-end rule.
            diag(diag_level::warning, r->location, "'" + std::string(fn.name) + "' declared to return '" +
                 fn.ret->canonical_name() + "' returns no value here (returns null)", fn.line, fn.column);
            return;
        }
        if (!assignable(value, ret)) {
            diag(diag_level::error, r->location, "cannot return '" + ctype_name(value) + "' from '" +
                 std::string(fn.name) + "' declared to return '" + fn.ret->canonical_name() + "'",
                 fn.line, fn.column);
        }
    }

    // -------------------------------------------------------------- declarations

    void walk_decl(declaration* d) {
        if (!d) { return; }
        switch (d->get_type()) {
            case node_type::variable_decl:
                walk_variable_decl(static_cast<variable_decl*>(d));
                break;
            case node_type::function_decl: {
                auto* fn = static_cast<function_decl*>(d);
                if (scopes_.size() > 1) {
                    // nested function: visible from here on (top-level ones were pre-collected)
                    var_entry entry;
                    entry.type = ctype::make(ctype::function_k);
                    entry.dynamic = true;
                    scopes_.back().emplace(fn->name, std::move(entry));
                }
                walk_function(fn);
                break;
            }
            case node_type::class_decl: {
                auto* cd = static_cast<class_decl*>(d);
                if (!classes_.count(cd->name)) { collect_class(cd); }
                walk_class(cd);
                break;
            }
            case node_type::namespace_decl: {
                auto* n = static_cast<namespace_decl*>(d);
                scopes_.emplace_back();
                for (auto& inner : n->declarations) { walk_decl(inner.get()); }
                scopes_.pop_back();
                break;
            }
            case node_type::expression_decl:
                infer(static_cast<expression_decl*>(d)->expression.get());
                break;
            case node_type::statement_decl:
                walk_stmt(static_cast<statement_decl*>(d)->statement.get());
                break;
            case node_type::destructuring_decl: {
                auto* dd = static_cast<destructuring_decl*>(d);
                infer(dd->initializer.get());
                for (const auto& [name, _] : dd->names) {
                    var_entry entry;
                    entry.dynamic = true;
                    scopes_.back().emplace(name, std::move(entry));
                }
                break;
            }
            case node_type::include_decl:
            case node_type::import_decl: {
                // path expressions still get identifier checks
                if (d->get_type() == node_type::include_decl) {
                    auto* inc = static_cast<include_decl*>(d);
                    if (inc->path_expr) { infer(inc->path_expr.get()); }
                } else {
                    auto* imp = static_cast<import_decl*>(d);
                    if (imp->path_expr) { infer(imp->path_expr.get()); }
                }
                break;
            }
            case node_type::enum_decl:
                break;
            default:
                break;
        }
    }

    void walk_variable_decl(variable_decl* decl) {
        ctype init{};
        bool has_init = decl->initializer != nullptr;
        bool is_reference_decl = decl->type && decl->type->base_type == script_value_type::jai_reference_type;
        if (has_init) {
            init = infer(decl->initializer.get());
        }

        var_entry entry;
        entry.line = decl->location.line;
        entry.column = decl->location.column;
        if (decl->type) {
            entry.declared = decl->type.get();
            entry.display = decl->type->canonical_name();
            entry.type = ctype_from_type_info(decl->type.get());
            entry.dynamic = decl->type->base_type == script_value_type::jai_any_type || is_reference_decl;
            if (has_init && !entry.dynamic && !entry.type.is_unknown() && !assignable(init, entry.type)) {
                diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
                     "' declared '" + entry.display + "' with '" + ctype_name(init) + "'");
                // recovery: the variable keeps its declared type; no cascades
            }
        } else if (has_init) {
            // auto with initializer: infer then enforce as that type
            entry.type = init;
            entry.dynamic = init.is_unknown();
            entry.display = ctype_name(init);
        } else {
            // auto without initializer: locked by the first runtime assignment — the
            // checker can't see branch order, so it stays dynamic (documented leniency)
            entry.dynamic = true;
        }
        scopes_.back().emplace(decl->name, std::move(entry));
    }

    void walk_function(const function_decl* fn) {
        scopes_.emplace_back();
        define_params(fn->parameters);
        for (const auto& init : fn->initializers) {
            for (const auto& arg : init.arguments) { infer(arg.get()); }
        }
        fn_stack_.push_back({fn->return_type.get(), fn->is_coroutine, fn->name,
                             fn->location.line, fn->location.column});
        if (fn->body) {
            for (auto& d : fn->body->declarations) { walk_decl(d.get()); }
        }
        check_falls_off_end(fn);
        fn_stack_.pop_back();
        scopes_.pop_back();
    }

    void define_params(const std::vector<parameter>& params) {
        for (const auto& p : params) {
            ctype pt = ctype_from_type_info(p.type.get());
            if (p.default_value) {
                ctype dv = infer(p.default_value.get());
                if (!p.is_reference && !pt.is_unknown() && !assignable(dv, pt)) {
                    diag(diag_level::error, p.default_value->location,
                         "default value of '" + p.name + "' declared '" +
                         (p.type ? p.type->canonical_name() : "auto") + "' has type '" + ctype_name(dv) + "'");
                }
            }
            var_entry entry;
            entry.type = pt;
            entry.dynamic = pt.is_unknown() || p.is_reference;
            entry.declared = p.type.get();
            entry.display = p.type ? p.type->canonical_name() : "auto";
            scopes_.back().emplace(std::string_view(p.name), std::move(entry));
        }
    }

    void walk_class(const class_decl* cd) {
        auto it = classes_.find(cd->name);
        if (it == classes_.end()) { return; }
        sclass* prev_class = class_ctx_;
        std::string_view prev_name = class_ctx_name_;
        class_ctx_ = &it->second;
        class_ctx_name_ = cd->name;
        for (const auto& m : cd->members) {
            if (!m.declaration) { continue; }
            if (m.declaration->get_type() == node_type::variable_decl) {
                auto* f = static_cast<variable_decl*>(m.declaration.get());
                if (f->initializer) {
                    ctype init = infer(f->initializer.get());
                    if (f->type) {
                        ctype ft = ctype_from_type_info(f->type.get());
                        if (!ft.is_unknown() && !assignable(init, ft)) {
                            diag(diag_level::error, f->location, "cannot initialize field '" +
                                 std::string(f->name) + "' declared '" + f->type->canonical_name() +
                                 "' with '" + ctype_name(init) + "'");
                        }
                    }
                }
            } else if (m.declaration->get_type() == node_type::function_decl) {
                walk_function(static_cast<function_decl*>(m.declaration.get()));
            } else {
                walk_decl(m.declaration.get());
            }
        }
        class_ctx_ = prev_class;
        class_ctx_name_ = prev_name;
    }

    void walk_lambda(lambda_expr* l) {
        for (const auto& cap : l->captures) {
            if (find_local(cap.name)) { continue; }
            if (class_ctx_ && lookup_member(class_ctx_name_, cap.name, false).found) { continue; }
            if (functions_.count(cap.name) || classes_.count(cap.name) || enums_.count(cap.name) ||
                all_names_.count(cap.name)) { continue; }
            std::string name(cap.name);
            if (extra_globals_.count(name) || engine_knows_name(name)) { continue; }
            undeclared(l->location, cap.name);
        }
        scopes_.emplace_back();
        define_params(l->parameters);
        fn_stack_.push_back({l->return_type.get(), /*is_coroutine=*/false, "lambda",
                             l->location.line, l->location.column});
        walk_stmt(l->body.get());
        fn_stack_.pop_back();
        scopes_.pop_back();
    }

    // ---------------------------------------------------- fall-off-the-end warning

    void check_falls_off_end(const function_decl* fn) {
        if (!fn->return_type || fn->is_coroutine || !fn->body) { return; }
        ctype ret = ctype_from_type_info(fn->return_type.get());
        if (ret.is_unknown() || ret.kind == ctype::null_k) { return; }
        if (guarantees_exit_block(fn->body->declarations)) { return; }
        diag(diag_level::warning, fn->location, "'" + std::string(fn->name) + "' declared to return '" +
             fn->return_type->canonical_name() + "' may reach the end of its body without returning (returns null)");
    }

    bool guarantees_exit_block(const std::vector<declaration_ptr>& decls) {
        for (const auto& d : decls) {
            if (d && guarantees_exit(d.get())) { return true; }
        }
        return false;
    }

    bool guarantees_exit(const statement* s) {
        if (!s) { return false; }
        switch (s->get_type()) {
            case node_type::return_stmt: return true;
            case node_type::expression_stmt: {
                auto* e = static_cast<const expression_stmt*>(s)->expression.get();
                return e && e->get_type() == node_type::throw_expr;
            }
            case node_type::expression_decl: {
                auto* e = static_cast<const expression_decl*>(s)->expression.get();
                return e && e->get_type() == node_type::throw_expr;
            }
            case node_type::block_stmt:
                return guarantees_exit_block(static_cast<const block_stmt*>(s)->declarations);
            case node_type::statement_decl:
                return guarantees_exit(static_cast<const statement_decl*>(s)->statement.get());
            case node_type::if_stmt: {
                auto* i = static_cast<const if_stmt*>(s);
                return i->else_statement && guarantees_exit(i->then_statement.get()) &&
                       guarantees_exit(i->else_statement.get());
            }
            case node_type::try_stmt: {
                auto* t = static_cast<const try_stmt*>(s);
                return guarantees_exit(t->try_block.get()) && guarantees_exit(t->catch_block.get());
            }
            case node_type::while_stmt: {
                // while(true) without a break can only leave via return/throw
                auto* w = static_cast<const while_stmt*>(s);
                if (!w->condition || w->condition->get_type() != node_type::literal_expr) { return false; }
                auto* lit = static_cast<const literal_expr*>(w->condition.get());
                return lit->value.is_bool() && lit->value.unchecked_as_bool() && !contains_break(w->body.get());
            }
            case node_type::switch_stmt: {
                auto* sw = static_cast<const switch_stmt*>(s);
                if (!sw->default_case) { return false; }
                for (const auto& cs : sw->cases) {
                    bool exits = false;
                    for (const auto& st : cs->body) {
                        if (guarantees_exit(st.get())) { exits = true; break; }
                    }
                    if (!exits) { return false; }
                }
                for (const auto& st : sw->default_case->body) {
                    if (guarantees_exit(st.get())) { return true; }
                }
                return false;
            }
            default:
                return false;
        }
    }

    // break at this loop's own level (nested loops swallow their breaks)
    bool contains_break(const statement* s) {
        if (!s) { return false; }
        switch (s->get_type()) {
            case node_type::break_stmt: return true;
            case node_type::block_stmt:
                for (const auto& d : static_cast<const block_stmt*>(s)->declarations) {
                    if (contains_break(d.get())) { return true; }
                }
                return false;
            case node_type::statement_decl:
                return contains_break(static_cast<const statement_decl*>(s)->statement.get());
            case node_type::if_stmt: {
                auto* i = static_cast<const if_stmt*>(s);
                return contains_break(i->then_statement.get()) || contains_break(i->else_statement.get());
            }
            case node_type::try_stmt: {
                auto* t = static_cast<const try_stmt*>(s);
                return contains_break(t->try_block.get()) || contains_break(t->catch_block.get());
            }
            default:
                return false;   // nested loops/switches own their breaks
        }
    }
};

// Safety-net sweep: every declared name at any depth (see all_names_ comment).
void static_checker::collect_names(const statement* node) {
    if (!node) { return; }
    switch (node->get_type()) {
        case node_type::variable_decl: {
            auto* v = static_cast<const variable_decl*>(node);
            all_names_.insert(v->name);
            if (v->initializer) { collect_names_expr(v->initializer.get()); }
            break;
        }
        case node_type::function_decl: {
            auto* f = static_cast<const function_decl*>(node);
            all_names_.insert(f->name);
            for (const auto& p : f->parameters) { all_names_.insert(std::string_view(p.name)); }
            if (f->body) { collect_names(f->body.get()); }
            break;
        }
        case node_type::class_decl: {
            auto* c = static_cast<const class_decl*>(node);
            all_names_.insert(c->name);
            for (const auto& m : c->members) { collect_names(m.declaration.get()); }
            break;
        }
        case node_type::namespace_decl: {
            auto* n = static_cast<const namespace_decl*>(node);
            all_names_.insert(n->name);
            for (const auto& d : n->declarations) { collect_names(d.get()); }
            break;
        }
        case node_type::enum_decl: {
            auto* e = static_cast<const enum_decl*>(node);
            all_names_.insert(e->name);
            for (const auto& [vname, _] : e->values) { all_names_.insert(vname); }
            break;
        }
        case node_type::destructuring_decl: {
            auto* dd = static_cast<const destructuring_decl*>(node);
            for (const auto& [name, _] : dd->names) { all_names_.insert(name); }
            if (dd->initializer) { collect_names_expr(dd->initializer.get()); }
            break;
        }
        case node_type::include_decl:
        case node_type::import_decl:
            has_includes_ = true;
            break;
        case node_type::expression_stmt:
            collect_names_expr(static_cast<const expression_stmt*>(node)->expression.get());
            break;
        case node_type::expression_decl:
            collect_names_expr(static_cast<const expression_decl*>(node)->expression.get());
            break;
        case node_type::statement_decl:
            collect_names(static_cast<const statement_decl*>(node)->statement.get());
            break;
        case node_type::block_stmt:
            for (const auto& d : static_cast<const block_stmt*>(node)->declarations) { collect_names(d.get()); }
            break;
        case node_type::if_stmt: {
            auto* i = static_cast<const if_stmt*>(node);
            collect_names_expr(i->condition.get());
            collect_names(i->then_statement.get());
            collect_names(i->else_statement.get());
            break;
        }
        case node_type::while_stmt: {
            auto* w = static_cast<const while_stmt*>(node);
            collect_names_expr(w->condition.get());
            collect_names(w->body.get());
            break;
        }
        case node_type::for_stmt: {
            auto* f = static_cast<const for_stmt*>(node);
            collect_names(f->initializer.get());
            collect_names_expr(f->condition.get());
            collect_names_expr(f->update.get());
            collect_names(f->body.get());
            break;
        }
        case node_type::range_for_stmt: {
            auto* f = static_cast<const range_for_stmt*>(node);
            all_names_.insert(f->variable_name);
            collect_names_expr(f->container.get());
            collect_names(f->body.get());
            break;
        }
        case node_type::try_stmt: {
            auto* t = static_cast<const try_stmt*>(node);
            collect_names(t->try_block.get());
            collect_names(t->catch_block.get());
            break;
        }
        case node_type::switch_stmt: {
            auto* sw = static_cast<const switch_stmt*>(node);
            collect_names_expr(sw->condition.get());
            for (const auto& cs : sw->cases) {
                for (const auto& st : cs->body) { collect_names(st.get()); }
            }
            if (sw->default_case) {
                for (const auto& st : sw->default_case->body) { collect_names(st.get()); }
            }
            break;
        }
        case node_type::return_stmt:
            collect_names_expr(static_cast<const return_stmt*>(node)->value.get());
            break;
        default:
            break;
    }
}

void static_checker::collect_names_expr(const expression* e) {
    if (!e) { return; }
    switch (e->get_type()) {
        case node_type::lambda_expr: {
            auto* l = static_cast<const lambda_expr*>(e);
            for (const auto& p : l->parameters) { all_names_.insert(std::string_view(p.name)); }
            collect_names(l->body.get());
            break;
        }
        case node_type::binary_expr: {
            auto* b = static_cast<const binary_expr*>(e);
            collect_names_expr(b->left.get());
            collect_names_expr(b->right.get());
            break;
        }
        case node_type::unary_expr:
            collect_names_expr(static_cast<const unary_expr*>(e)->operand.get());
            break;
        case node_type::assignment_expr: {
            auto* a = static_cast<const assignment_expr*>(e);
            collect_names_expr(a->target.get());
            collect_names_expr(a->value.get());
            break;
        }
        case node_type::call_expr: {
            auto* c = static_cast<const call_expr*>(e);
            collect_names_expr(c->callee.get());
            for (const auto& arg : c->arguments) { collect_names_expr(arg.get()); }
            break;
        }
        case node_type::member_expr:
            collect_names_expr(static_cast<const member_expr*>(e)->object.get());
            break;
        case node_type::new_expr:
            for (const auto& arg : static_cast<const new_expr*>(e)->arguments) { collect_names_expr(arg.get()); }
            break;
        case node_type::include_expr:
            collect_names_expr(static_cast<const include_expr*>(e)->path_expr.get());
            break;
        case node_type::ternary_expr: {
            auto* t = static_cast<const ternary_expr*>(e);
            collect_names_expr(t->condition.get());
            collect_names_expr(t->then_expression.get());
            collect_names_expr(t->else_expression.get());
            break;
        }
        case node_type::array_literal_expr:
            for (const auto& el : static_cast<const array_literal_expr*>(e)->elements) { collect_names_expr(el.get()); }
            break;
        case node_type::map_literal_expr:
            for (const auto& [k, v] : static_cast<const map_literal_expr*>(e)->entries) {
                collect_names_expr(k.get());
                collect_names_expr(v.get());
            }
            break;
        case node_type::throw_expr:
            collect_names_expr(static_cast<const throw_expr*>(e)->value.get());
            break;
        case node_type::yield_expr:
            collect_names_expr(static_cast<const yield_expr*>(e)->value.get());
            break;
        default:
            break;
    }
}

} // namespace

check_report run_static_check(engine& eng, const std::vector<declaration_ptr>& decls,
                              const std::vector<std::string>* extra_globals) {
    return static_checker(eng, decls, extra_globals).run();
}

} // namespace detail
} // namespace jai
