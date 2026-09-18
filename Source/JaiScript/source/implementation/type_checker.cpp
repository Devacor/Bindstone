// Static type checker (docs/static_checking.md). Backend-neutral AST walk,
// run once per unique source at parse-cache-entry creation (engine.cpp hook).
//
// Guiding contract: NO FALSE POSITIVES. A program that runs clean and doesn't
// genuinely violate declared types must produce zero error diagnostics. Anywhere
// the static story is uncertain (var, unknown classes, includes, builtin methods,
// unknown element types, host object conversions) the checker stays lenient.
// Known declared containers follow their element boundary; scalar destinations
// follow detail::convert_for_typed_slot. Declared references, weak handles and
// callable slots additionally enforce the approved static contract.

#include <jaiscript/detail/type_checker.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/numeric_conversion.hpp>

#include <algorithm>
#include <optional>
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
        array_k, map_k, function_k, object_k, weak_k,
        class_ref_k,   // a class used as a value (Class::static access, constructor callee)
        enum_ref_k     // an enum name (member access checks its values)
    };
    kind_t kind = unknown;
    std::string_view cls{};   // object_k / class_ref_k / enum_ref_k
    // Proof pass only: the interned type_info this ctype came from (null = kind only), and
    // whether the kind was read off a runtime value tag rather than a declaration (engine
    // globals) - tentative kinds propagate through operators and are never proof sources.
    const type_info* ti = nullptr;
    bool tentative = false;
    // An element read from a declared, non-tentative typed container (array<int> x; x[i]): the kind is a
    // proof source for the operand class stamp (the typed rows guard it at runtime) while it stays
    // tentative for diagnostics, so no load-time text changes
    bool element_proof = false;
    // a reference alias of a declared typed container (var& x = G): its element reads may prove while
    // diagnostics still see a reference
    bool alias_proof = false;
    // A map key read through a range-for pair (`kv.first`, C++ `const Key`): copies freely,
    // never mutated or aliased (reference, shared_ptr, T& parameter, T& return, cast<T&>)
    bool const_key = false;
	const expression* origin = nullptr; // this use's expression, for literal-only checks

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
		case ctype::array_k: return t.ti ? t.ti->canonical_name() : "array";
		case ctype::map_k: return t.ti ? t.ti->canonical_name() : "map";
		case ctype::function_k: return "function";
		case ctype::weak_k: return t.ti ? t.ti->canonical_name() : "weak_ptr";
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
	ctype t;
	switch (ti->base_type) {
		case script_value_type::jai_any_type: return {};
		case script_value_type::jai_null_type: t = ctype::make(ctype::null_k); break;
		case script_value_type::jai_int_type: t = ctype::make(ctype::int_k); break;
		case script_value_type::jai_float_type: t = ctype::make(ctype::float_k); break;
		case script_value_type::jai_string_type: t = ctype::make(ctype::string_k); break;
		case script_value_type::jai_char_type: t = ctype::make(ctype::char_k); break;
		case script_value_type::jai_bool_type: t = ctype::make(ctype::bool_k); break;
		case script_value_type::jai_array_type: t = ctype::make(ctype::array_k); break;
		case script_value_type::jai_map_type: t = ctype::make(ctype::map_k); break;
		case script_value_type::jai_function_type: t = ctype::make(ctype::function_k); break;
		case script_value_type::jai_weak_ptr_type: t = ctype::make(ctype::weak_k); break;
		case script_value_type::jai_object_type:
		case script_value_type::jai_shared_ptr_type:
			// shared_ptr<T> carries T's name; member access resolves identically (ti keeps
			// the base_type, so the proof pass still tells value object from handle)
			if (ti->type_name.empty()) { return {}; }
			t = ctype::object(ti->type_name);
			break;
		case script_value_type::jai_reference_type:
			return ctype_from_type_info(ti->type_params.empty() ? nullptr : ti->type_params[0].get());
		default:
			return {};   // cpp_bound, invalid: lenient
	}
	t.ti = ti;
	return t;
}

// Function-variable spelling is checker metadata; the runtime slot remains auto.
ctype declared_variable_type(const variable_decl* decl) {
	return decl->function_spelling ? ctype::make(ctype::function_k) : ctype_from_type_info(decl->type.get());
}

std::string declared_variable_type_name(const variable_decl* decl) {
	return decl->function_spelling ? "function" : (decl->type ? decl->type->canonical_name() : "auto");
}

bool is_numeric(ctype::kind_t k) { return k == ctype::int_k || k == ctype::float_k; }
// Integral promotion (char_promotion.hpp): char acts as int64 0..255 under arithmetic/bitwise.
bool is_numeric_or_char(ctype::kind_t k) { return is_numeric(k) || k == ctype::char_k; }

// Result type of `l op r` (token base ops). Anything uncertain -> unknown.
ctype binary_result_kind(const ctype& l, token_type op, const ctype& r) {
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
            if (is_numeric_or_char(l.kind) && is_numeric_or_char(r.kind)) {
                return ctype::make((l.kind == ctype::float_k || r.kind == ctype::float_k)
                                       ? ctype::float_k : ctype::int_k);
            }
            return {};
        case token_type::percent:
        case token_type::ampersand:
        case token_type::pipe:
        case token_type::caret:
        case token_type::left_shift:
        case token_type::right_shift:
            return ((l.kind == ctype::int_k || l.kind == ctype::char_k) &&
                    (r.kind == ctype::int_k || r.kind == ctype::char_k)) ? ctype::make(ctype::int_k) : ctype{};
        case token_type::spaceship:
            return (is_numeric_or_char(l.kind) && is_numeric_or_char(r.kind)) ? ctype::make(ctype::int_k) : ctype{};
        default:
            return {};
    }
}

ctype binary_result(const ctype& l, token_type op, const ctype& r) {
    ctype out = binary_result_kind(l, op, r);
    out.tentative = l.tentative || r.tentative;
    out.element_proof = (l.tentative || r.tentative) && (!l.tentative || l.element_proof) && (!r.tentative || r.element_proof);
    return out;
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
                   const std::vector<std::string>* extra_globals, const check_options& opts)
        : eng_(eng), decls_(decls), proofs_(opts.proofs), collect_diagnostics_(opts.diagnostics), stats_out_(opts.stats) {
        if (extra_globals) {
            for (const auto& n : *extra_globals) {
                extra_globals_.insert(n);
            }
        }
        if (opts.program_names) {
            extra_globals_.insert(opts.program_names->begin(), opts.program_names->end());
        }
        program_classes_ = opts.program_classes;
    }

    check_report run() {
        collect_top_level();
        warn_duplicate_top_level_functions();
        scopes_.emplace_back();
        {
            mode_guard unit(mode_stack_, leading_mode(decls_), /*scoped=*/true);
            for (const auto& d : decls_) {
                walk_decl(d.get());
            }
        }
        scopes_.pop_back();

        if (stats_out_) { *stats_out_ = counts_; }
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
        // Declared by another closure unit (program table): its locations belong to that unit's
        // source, and its member types seed proofs only once the engine has registered it.
        bool foreign = false;
        bool provable = true;
    };

    engine& eng_;
    const std::vector<declaration_ptr>& decls_;

    // Proof pass (docs/type_proofs_design.md): independent of diagnostics - a stamp never
    // depends on a diag() having fired, and diag() is a no-op when diagnostics are off.
    using proof_mode = check_options::proofs_t;
    proof_mode proofs_ = proof_mode::stamp;
    bool collect_diagnostics_ = true;
    proof_stats* stats_out_ = nullptr;
    proof_stats counts_{};

    std::vector<check_diagnostic> diags_;
    std::unordered_set<std::string> seen_;   // "line:col:message" dedupe keys
    bool capped_ = false;
    bool has_includes_ = false;           // include/import present (any form): proof gates stay shut
    bool has_dynamic_includes_ = false;   // a path the closure cannot read: undeclared identifiers downgrade to warnings
    // [[warn_mode]] / [[strict_mode]] regions, innermost on top. A statement-level frame stops
    // at nested function/lambda bodies (they re-push the nearest scoped mode); a block/unit
    // frame flows into them.
    struct mode_frame { uint8_t mode; bool scoped; };
    std::vector<mode_frame> mode_stack_;
    struct mode_guard {
        std::vector<mode_frame>& stack;
        bool pushed;
        mode_guard(std::vector<mode_frame>& s, uint8_t mode, bool scoped, bool always = false)
            : stack(s), pushed(always || mode != check_mode_attr_none) {
            if (pushed) { stack.push_back({mode, scoped}); }
        }
        ~mode_guard() { if (pushed) { stack.pop_back(); } }
    };
    uint8_t region_mode() const { return mode_stack_.empty() ? check_mode_attr_none : mode_stack_.back().mode; }
    uint8_t scoped_region_mode() const {
        for (auto it = mode_stack_.rbegin(); it != mode_stack_.rend(); ++it) {
            if (it->scoped) { return it->mode; }
        }
        return check_mode_attr_none;
    }
    static uint8_t leading_mode(const std::vector<declaration_ptr>& decls) {
        return decls.empty() || !decls.front() ? check_mode_attr_none : decls.front()->check_mode_attr;
    }

    std::unordered_map<std::string_view, sclass> classes_;
    // Program-closure classes, modeled on first use from the engine's table (a same-unit class
    // always wins over its table entry).
    const program_class_map* program_classes_ = nullptr;
    std::unordered_map<std::string_view, sclass> program_sclasses_;
    std::unordered_map<std::string_view, std::vector<const function_decl*>> functions_;
    std::unordered_map<std::string_view, const enum_decl*> enums_;
    std::unordered_set<std::string_view> namespaces_;
    // Every name declared ANYWHERE in the script (any depth). Flow-insensitive safety
    // net: execution order can make later declarations visible (nested functions,
    // conditional paths), so "undeclared" only fires for names absent from this set
    // AND the engine surface — typos, not ordering.
    std::unordered_set<std::string_view> all_names_;
    std::unordered_set<std::string> extra_globals_;
    // Top-level (and namespace-flattened) variable names. Runtime resolution reads the
    // scope chain THROUGH the globals before falling back to this-fields, so a name that
    // is both a global and a field is flow-order-ambiguous to a static walk: member hits
    // on these names degrade to unknown instead of trusting the field's declared type.
    std::unordered_set<std::string_view> global_var_names_;
    // Classes this unit declares that the engine already holds with a different field set:
    // a hot-reload redefinition. Instances made under the old shape are live while the unit
    // runs (migrate hooks read retired fields, probes expect the runtime error), so member
    // misses on those receivers stay silent (stale_shape_receiver).
    std::unordered_set<std::string_view> open_classes_;
    // extend(x, Class) re-classes x IN PLACE: every name passed to extend anywhere in the unit
    // reads as unknown from then on (flow-insensitive, by name).
    std::unordered_set<std::string_view> extended_names_;
    std::unordered_set<std::string_view> extended_members_;

    // Mixed-branch ternary stash: infer()'s ternary case records the two known-but-
    // different branch types so walk_variable_decl can warn when an auto decl would
    // lock its type from whichever branch RUNTIME takes. Stashed rather than re-inferred
    // at the decl site - re-running infer() on the arms would duplicate sub-diagnostics.
    struct mixed_ternary_note {
        const expression* node = nullptr;
        ctype then_type{};
        ctype else_type{};
    } last_mixed_ternary_;

    struct var_entry {
        ctype type;
        bool dynamic = false;             // var (or untyped) — assignments never checked
        bool is_reference = false;        // T&/auto&/var& decl, & parameter, auto& loop variable
        bool is_handle = false;           // declared shared_ptr<T> / shared_ptr<auto>
        bool map_pair = false;            // range-for variable over a map: .first is the read-only key
        ctype alias;                      // a reference alias of a declared typed container: the type its element reads may prove
        const type_info* declared = nullptr;   // for message text; null for auto-inferred
        std::string display;              // declared-type text for messages
        size_t line = 0, column = 0;
    };
    std::vector<std::unordered_map<std::string_view, var_entry>> scopes_;

    // Live lambda contexts: a bare [] lambda whose body reaches an enclosing-function local
    // captured it implicitly — warn once per lambda, nudging toward explicit [=]/[&].
    struct capture_ctx {
        size_t base = 0;
        bool bare = false;
        bool warned = false;
        source_location location;
    };
    std::vector<capture_ctx> capture_stack_;

    struct fn_ctx {
        const type_info* ret = nullptr;   // null = auto
        bool is_coroutine = false;
        std::string_view name;
        size_t line = 0, column = 0;
        // Bare names in this body can reach the enclosing class's fields (method/ctor,
        // or [this]-capturing lambda). Plain [=]/[&] lambdas cannot - no field-shadow
        // warnings inside them.
        bool sees_fields = false;
        bool is_static_method = false;
        bool in_ctor_initializers = false;   // : super(...)/this(...) args run before fields exist
        // Names this body resolved through the global tier so far (shadow shape D:
        // "declaration shadows a global used earlier in the same scope chain").
        std::unordered_set<std::string_view> globals_used;
    };
    std::vector<fn_ctx> fn_stack_;
    sclass* class_ctx_ = nullptr;
    std::string_view class_ctx_name_{};

    // -------------------------------------------------------------- diagnostics

    void diag(diag_level severity, const source_location& loc, std::string message,
              size_t related_line = 0, size_t related_column = 0) {
        if (!collect_diagnostics_) { return; }
        const uint8_t mode = region_mode();
        if (mode == check_mode_attr_warn) { severity = diag_level::warning; }
        if (diags_.size() >= k_check_diagnostic_cap) {
            capped_ = true;
            return;
        }
        std::string key = std::to_string(loc.line) + ":" + std::to_string(loc.column) + ":" + message;
        if (!seen_.insert(std::move(key)).second) {
            return;
        }
        diags_.push_back({severity, loc.line, loc.column, std::move(message), related_line, related_column,
                          mode == check_mode_attr_strict});
    }

    void undeclared(const source_location& loc, std::string_view name) {
        // Literal imports are covered by the program closure's names; a dynamic path can
        // define anything - keep the signal, drop the teeth.
        diag(has_dynamic_includes_ ? diag_level::warning : diag_level::error, loc,
             "use of undeclared identifier '" + std::string(name) + "'");
    }

    // -------------------------------------------------------------- collection

    void collect_names(const statement* node);        // safety-net name sweep (recursive)
    void collect_names_expr(const expression* e);     // lambdas declare names inside expressions

    // THE class model - same-unit classes and program-table classes are built by this one walk.
    static void model_class(const class_decl* cd, sclass& c) {
        c = sclass{};   // redefinition: last one wins, don't merge members
        c.decl = cd;
        for (const auto& m : cd->members) {
            if (!m.declaration) { continue; }
            if (m.declaration->get_type() == node_type::variable_decl) {
                auto* f = static_cast<const variable_decl*>(m.declaration.get());
                (f->is_static ? c.static_fields : c.fields).emplace(f->name, f);
            } else if (m.declaration->get_type() == node_type::function_decl) {
                auto* fn = static_cast<const function_decl*>(m.declaration.get());
                if (fn->name == cd->name) {
                    c.ctors.push_back(fn);
                } else if (fn->is_static) {
                    c.static_methods[fn->name].push_back(fn);
                } else {
                    c.methods[fn->name].push_back(fn);
                }
            }
        }
    }

    void collect_class(const class_decl* cd) {
        sclass& c = classes_[cd->name];
        model_class(cd, c);
        for (const auto& [n, _] : c.fields) { all_names_.insert(n); }
        for (const auto& [n, _] : c.static_fields) { all_names_.insert(n); }
        for (const auto& [n, _] : c.methods) { all_names_.insert(n); }
        for (const auto& [n, _] : c.static_methods) { all_names_.insert(n); }
    }

    // The unit's own class first, then the program table (modeled once per check; a table
    // class the engine holds under a different shape is open like a same-unit redefinition).
    sclass* find_sclass(std::string_view cls) {
        if (auto it = classes_.find(cls); it != classes_.end()) { return &it->second; }
        if (auto it = program_sclasses_.find(cls); it != program_sclasses_.end()) { return &it->second; }
        if (!program_classes_) { return nullptr; }
        auto pit = program_classes_->find(std::string(cls));
        if (pit == program_classes_->end() || !pit->second) { return nullptr; }
        const class_decl* cd = pit->second.get();
        sclass& c = program_sclasses_[cd->name];
        model_class(cd, c);
        c.foreign = true;
        c.provable = engine_class(cd->name) != nullptr;
        if (unit_redefines(cd)) { open_classes_.insert(cd->name); }
        return &c;
    }

    void collect_scope_decls(const std::vector<declaration_ptr>& decls) {
        for (const auto& d : decls) {
            if (!d) { continue; }
            switch (d->get_type()) {
                case node_type::function_decl:
                    functions_[static_cast<const function_decl*>(d.get())->name]
                        .push_back(static_cast<const function_decl*>(d.get()));
                    break;
                case node_type::variable_decl:
                    global_var_names_.insert(static_cast<const variable_decl*>(d.get())->name);
                    break;
                case node_type::extern_decl:
                    global_var_names_.insert(static_cast<const extern_decl*>(d.get())->name);
                    break;
                case node_type::destructuring_decl:
                    for (const auto& [name, _] : static_cast<const destructuring_decl*>(d.get())->names) {
                        global_var_names_.insert(name);
                    }
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
                    // `namespace a::b::c` is ONE flat name; every prefix is a path root too
                    namespaces_.insert(n->name);
                    for (size_t pos = n->name.find("::"); pos != std::string_view::npos; pos = n->name.find("::", pos + 2)) {
                        namespaces_.insert(n->name.substr(0, pos));
                    }
                    collect_scope_decls(n->declarations);   // flattened: lenient by design
                    break;
                }
                default:
                    break;
            }
        }
    }

    static bool literal_unit_path(const statement* d) {
        return d->get_type() == node_type::include_decl
            ? !static_cast<const include_decl*>(d)->path_expr
            : !static_cast<const import_decl*>(d)->path_expr;
    }

    void collect_top_level() {
        collect_scope_decls(decls_);
        for (const auto& d : decls_) {
            if (d && (d->get_type() == node_type::include_decl || d->get_type() == node_type::import_decl)) {
                has_includes_ = true;
                if (!literal_unit_path(d.get())) { has_dynamic_includes_ = true; }
                continue;
            }
            collect_names(d.get());
        }
        for (const auto& [name, c] : classes_) {
            if (unit_redefines(c.decl)) { open_classes_.insert(name); }
        }
    }

    // The engine already holds a script class of this name with a different instance/static
    // field set: this unit hot-reloads it (an identical re-execution keeps the same set).
    bool unit_redefines(const class_decl* cd) {
        auto def = eng_.get_class_registry().find_script_class(std::string(cd->name));
        if (!def) { return false; }
        const uint64_t cpp_object_id = eng_.symbolize(class_constants::CPP_OBJECT_FIELD);
        size_t own_fields = 0;
        for (const auto& [id, _] : def->get_all_field_defaults()) {
            if (id != cpp_object_id && def->find_field_declaring_class(id) == def.get()) { ++own_fields; }
        }
        size_t fields = 0, statics = 0;
        for (const auto& m : cd->members) {
            if (!m.declaration || m.declaration->get_type() != node_type::variable_decl) { continue; }
            auto* f = static_cast<const variable_decl*>(m.declaration.get());
            const uint64_t id = eng_.symbolize(f->name);
            if (f->is_static) {
                ++statics;
                if (!def->has_static_field(id) && !def->is_static_field(id)) { return true; }
            } else {
                ++fields;
                if (def->find_field_declaring_class(id) != def.get()) { return true; }
            }
        }
        return fields != own_fields || statics != def->get_static_field_names().size();
    }

    // Duplicate same-name TOP-LEVEL function definitions inside ONE textual parse
    // unit: the later definition silently replaces the earlier at execution (top
    // level does not overload), so surface it — warning severity always (Dev
    // ruling 2026-07). Include/import composition stays silent by construction:
    // included files parse and check as their own units at execution time, and
    // this scan never crosses into namespace/class members (those overload
    // legitimately with their own runtime collision rules).
    void warn_duplicate_top_level_functions() {
        std::unordered_map<std::string_view, const function_decl*> first_seen;
        for (const auto& d : decls_) {
            if (!d || d->get_type() != node_type::function_decl) { continue; }
            auto* fn = static_cast<const function_decl*>(d.get());
            auto [it, inserted] = first_seen.emplace(fn->name, fn);
            if (!inserted) {
                diag(diag_level::warning, fn->location,
                     "duplicate definition of function '" + std::string(fn->name) +
                     "' replaces the earlier one (top-level functions do not overload)",
                     it->second->location.line, it->second->location.column);
            }
        }
    }

    // -------------------------------------------------------------- name resolution

    var_entry* find_local(std::string_view name, size_t* scope_index = nullptr) {
        for (size_t i = scopes_.size(); i-- > 0;) {
            auto found = scopes_[i].find(name);
            if (found != scopes_[i].end()) {
                if (scope_index) { *scope_index = i; }
                return &found->second;
            }
        }
        return nullptr;
    }

    // The name resolves through the global tier at runtime (scope chain reaches the
    // globals BEFORE the this-field/static fallbacks - see the audited resolution
    // order in docs/site/guide/07-classes.html). Used both to degrade ambiguous
    // member hits to unknown and for the shadowing warnings.
    bool global_tier_name(std::string_view name) {
        if (global_var_names_.count(name) || functions_.count(name)) { return true; }
        std::string n(name);
        return extra_globals_.count(n) || eng_.has_variable(n) || eng_.has_function(n);
    }

    void note_global_use(std::string_view name) {
        if (!fn_stack_.empty()) {
            fn_stack_.back().globals_used.insert(name);
        }
    }

    // ---------------------------------------------------- shadowing warnings
    // Always warning severity - shadowing is legal; the checker just points at it.

    struct field_shadow_hit {
        std::string_view cls;
        const variable_decl* decl = nullptr;
        bool is_static = false;
    };

    // In-script hierarchy only: engine-registered ancestors are unknowable -> silent.
    std::optional<field_shadow_hit> find_shadowed_field(std::string_view cls, std::string_view name, int depth = 0) {
        if (depth > 32) { return std::nullopt; }
        auto it = classes_.find(cls);
        if (it == classes_.end()) { return std::nullopt; }
        if (auto f = it->second.fields.find(name); f != it->second.fields.end()) {
            return field_shadow_hit{cls, f->second, false};
        }
        if (auto f = it->second.static_fields.find(name); f != it->second.static_fields.end()) {
            return field_shadow_hit{cls, f->second, true};
        }
        for (auto base : it->second.decl->base_classes) {
            if (auto hit = find_shadowed_field(base, name, depth + 1)) { return hit; }
        }
        return std::nullopt;
    }

    void warn_shadow(std::string_view kind, std::string_view name, const source_location& loc,
                     bool allow_field_shape = true) {
        if (fn_stack_.empty()) { return; }   // top-level decls ARE the globals
        // The dangerous shape: shadows a field of the enclosing class (only where the
        // body can actually reach fields - methods/ctors and [this] lambdas)
        if (allow_field_shape && class_ctx_ && fn_stack_.back().sees_fields) {
            if (auto hit = find_shadowed_field(class_ctx_name_, name)) {
                std::string msg = std::string(kind) + " '" + std::string(name) + "' shadows " +
                    (hit->is_static ? "static field '" : "field '") + std::string(name) +
                    "' of class '" + std::string(hit->cls) + "'";
                if (hit->decl && hit->decl->location.line != 0) {
                    msg += " (field declared at " + std::to_string(hit->decl->location.line) + ":" +
                           std::to_string(hit->decl->location.column) + ")";
                }
                diag(diag_level::warning, loc, std::move(msg));
                return;
            }
        }
        // Shadows an outer local/param (any enclosing non-global scope)
        for (size_t i = scopes_.size() - 1; i-- > 1;) {
            auto found = scopes_[i].find(name);
            if (found != scopes_[i].end()) {
                std::string msg = std::string(kind) + " '" + std::string(name) +
                    "' shadows a declaration in an enclosing scope";
                if (found->second.line != 0) {
                    msg += " (declared at " + std::to_string(found->second.line) + ":" +
                           std::to_string(found->second.column) + ")";
                }
                diag(diag_level::warning, loc, std::move(msg));
                return;
            }
        }
        // Shadows a global this body already READ through the global tier: from here on
        // the same spelling silently means the local (declaration-point scoping).
        bool used_earlier = false;
        for (const auto& f : fn_stack_) {
            if (f.globals_used.count(name)) { used_earlier = true; break; }
        }
        if (used_earlier && (global_tier_name(name) || scopes_[0].count(name))) {
            diag(diag_level::warning, loc, std::string(kind) + " '" + std::string(name) +
                 "' shadows global '" + std::string(name) + "' used earlier in this function");
        }
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
        ctype t = ctype_from_type_info(v.get_type_info());
        t.tentative = true;   // value-tag derived (a later redeclaration retags): never a proof source
        return t;
    }

    // -------------------------------------------------------------- class lookups

    // 1 = compatible (same or src derived from dst), 0 = definitely unrelated, 2 = unknown
    int class_relation(std::string_view src, std::string_view dst, int depth = 0) {
        if (src == dst) { return 1; }
        if (depth > 32) { return 2; }
        if (const sclass* c = find_sclass(src)) {
            bool saw_unknown = false;
            for (auto base : c->decl->base_classes) {
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

    // Member lookup across the in-script hierarchy (this unit, then the program table),
    // falling back to engine-registered ancestors. `certain` comes back false when any
    // ancestor is unresolvable — the caller must stay silent then; `unresolved` says an
    // ancestor is nowhere at all (bare names in the class's methods can then mean anything).
    struct member_hit {
        bool found = false;
        bool certain = true;
        const variable_decl* field = nullptr;              // typed in-script field
        const std::vector<const function_decl*>* methods = nullptr;
        bool foreign = false;      // declared by another closure unit: no related location in this source
        bool provable = true;      // the declaring class is this unit's or registered: its types may seed proofs
        bool unresolved = false;
        bool read_only = false;    // a getter-only binder property on a closed class: every store is the runtime raise
    };

    member_hit lookup_member(std::string_view cls, std::string_view member, bool want_static, int depth = 0) {
        member_hit miss;
        if (depth > 32) { miss.certain = false; return miss; }
        if (const sclass* c = find_sclass(cls)) {
            auto hit = [&](const variable_decl* f, const std::vector<const function_decl*>* m) {
                member_hit h{true, true, f, m};
                h.foreign = c->foreign;
                h.provable = c->provable;
                return h;
            };
            if (want_static) {
                if (auto f = c->static_fields.find(member); f != c->static_fields.end()) { return hit(f->second, nullptr); }
                if (auto m = c->static_methods.find(member); m != c->static_methods.end()) { return hit(nullptr, &m->second); }
            } else {
                if (auto f = c->fields.find(member); f != c->fields.end()) { return hit(f->second, nullptr); }
                if (auto m = c->methods.find(member); m != c->methods.end()) { return hit(nullptr, &m->second); }
                // instance access reaches statics too (Cat().count, count++ in methods)
                if (auto f = c->static_fields.find(member); f != c->static_fields.end()) { return hit(f->second, nullptr); }
                if (auto m = c->static_methods.find(member); m != c->static_methods.end()) { return hit(nullptr, &m->second); }
            }
            member_hit result;
            for (auto base : c->decl->base_classes) {
                member_hit h = lookup_member(base, member, want_static, depth + 1);
                if (h.found) {
                    if (h.read_only && c->methods.count("_set_" + std::string(member))) { h.read_only = false; }   // a script _set_ override takes the stores
                    return h;
                }
                result.certain = result.certain && h.certain;
                result.unresolved = result.unresolved || h.unresolved;
            }
            return result;
        }
        auto def = engine_class(cls);
        if (!def) {
            miss.certain = false;
            miss.unresolved = true;
            return miss;
        }
        uint64_t id = eng_.symbolize(member);
        // Script/vm classes from previous executes don't expose a uniform member surface
        // across backends: a hit is trusted, a miss is not. Only a cpp_class is closed, and
        // only while no script layer was ever extended over it.
        bool closed = def->is_cpp_class() && !def->extended_ever();
        if (want_static) {
            if (def->has_static_method(id) || def->is_static_field(id) || def->has_static_field(id)) {
                return member_hit{true, true, nullptr, nullptr};
            }
        } else {
            uint64_t getter_id = eng_.symbolize("_get_" + std::string(member));
            if (engine_instance_member(*def, id, getter_id)) {
                member_hit h{true, true, nullptr, nullptr};
                if (closed && def->is_read_only_property(id)) {
                    h.read_only = !derived_opens_property(*def, id, eng_.symbolize("_set_" + std::string(member)));
                }
                return h;
            }
            // A binder surface is a lower bound on the C++ declaration: a virtual bound only
            // on registered subclasses still dispatches through a base-typed handle.
            if (closed && derived_defines_member(*def, id, getter_id)) { closed = false; }
        }
        miss.certain = closed;   // cpp classes: member definitely absent; others stay open
        return miss;
    }

    // A user _get_/_set_ for `member` anywhere in cls's chain: modeled classes through their
    // method maps, registered ancestors through the engine's definitions. Such a member is
    // known, its static type is the accessor's business (unknown), and it is never a proof.
    bool chain_has_user_accessor(std::string_view cls, std::string_view member, int depth = 0) {
        if (depth > 32) { return false; }
        if (const sclass* c = find_sclass(cls)) {
            const std::string getter = "_get_" + std::string(member), setter = "_set_" + std::string(member);
            if (c->methods.count(getter) || c->methods.count(setter)) { return true; }
            for (auto base : c->decl->base_classes) {
                if (chain_has_user_accessor(base, member, depth + 1)) { return true; }
            }
            return false;
        }
        auto def = engine_class(cls);
        if (!def) { return false; }
        const uint64_t id = eng_.symbolize(member);
        auto* sym = eng_.get_symbolizer();
        return def->user_accessor(sym->get_getter_id_with_view(id).first) != nullptr ||
               def->user_accessor(sym->get_setter_id_with_view(id).first) != nullptr;
    }

    // The ONE user accessor `name` (_get_x / _set_x) across cls's modeled chain: null when the
    // chain declares none or several, or reaches a registered ancestor carrying one (its
    // declared signature is not visible). Diagnostics only - never consulted for a proof.
    const function_decl* single_user_accessor(std::string_view cls, std::string_view name) {
        const function_decl* found = nullptr;
        int count = 0;
        if (!count_user_accessors(cls, name, found, count, 0) || count != 1) { return nullptr; }
        return found;
    }

    bool count_user_accessors(std::string_view cls, std::string_view name, const function_decl*& found, int& count, int depth) {
        if (depth > 32) { return false; }
        if (const sclass* c = find_sclass(cls)) {
            if (auto m = c->methods.find(name); m != c->methods.end()) {
                for (const auto* fn : m->second) { found = fn; ++count; }
            }
            for (auto base : c->decl->base_classes) {
                if (!count_user_accessors(base, name, found, count, depth + 1)) { return false; }
            }
            return true;
        }
        auto def = engine_class(cls);
        if (!def) { return true; }
        return def->user_accessor(eng_.symbolize(std::string(name))) == nullptr;
    }

    // An accessor-bearing member reads as the getter's declared return type when the chain has
    // exactly one user _get_x, parameterless, non-coroutine, declaring a non-var type; tentative,
    // so nothing downstream is proven from it.
    ctype accessor_read_type(std::string_view cls, std::string_view member) {
        const std::string getter = "_get_" + std::string(member);
        const function_decl* fn = single_user_accessor(cls, getter);
        if (!fn || fn->is_coroutine || !fn->parameters.empty() || !fn->return_type) { return {}; }
        ctype t = ctype_from_type_info(fn->return_type.get());
        t.tentative = true;
        return t;
    }

    // Stores through exactly one user _set_x taking one by-value parameter check against that
    // parameter's declared type; a compound store needs the getter's type too (x op= v is x = T(x op v)).
    void check_accessor_store(std::string_view cls, std::string_view member, const ctype& rhs, bool compound,
                              token_type base_op, const source_location& loc) {
        const std::string setter = "_set_" + std::string(member);
        const function_decl* fn = single_user_accessor(cls, setter);
        if (!fn || fn->parameters.size() != 1 || fn->parameters[0].is_reference || !fn->parameters[0].type) { return; }
        const type_info* pt = fn->parameters[0].type.get();
        ctype target = ctype_from_type_info(pt);
        if (target.is_unknown()) { return; }
        ctype stored = rhs;
        if (compound) {
            ctype gt = accessor_read_type(cls, member);
            if (gt.is_unknown()) { return; }
            stored = binary_result(gt, base_op, rhs);
        }
        check_store(stored, /*compound=*/false, base_op, target, pt->canonical_name(),
                    "property '" + std::string(member) + "'", loc, 0, 0);
    }

    bool chain_open(std::string_view cls, int depth = 0) {
        if (depth > 32) { return true; }
        const sclass* c = find_sclass(cls);   // models a table class (marking it open) before the test
        if (open_classes_.count(cls)) { return true; }
        if (!c) { return false; }
        for (auto base : c->decl->base_classes) {
            if (chain_open(base, depth + 1)) { return true; }
        }
        return false;
    }

    // A field's declared type as the checker types it: certain from this unit or a registered
    // class, tentative (never a proof source) while it is known from the program table alone.
	static ctype member_type(const member_hit& hit) {
		ctype t = declared_variable_type(hit.field);
		if (!hit.provable) { t.tentative = true; }
		return t;
	}

    // Instances shaped by an EARLIER definition of a class this unit redefines: values
    // that predate the unit (engine globals, value-tag typed) and `this` inside the
    // class's own methods (migration hooks run on the old shape). A fresh instance of the
    // new definition is checked against it.
    bool stale_shape_receiver(const ctype& recv, const expression* object) {
        if (recv.kind != ctype::object_k || !chain_open(recv.cls)) { return false; }
        return recv.tentative || (object && object->get_type() == node_type::this_expr);
    }

    bool stale_field_context() {
        return class_ctx_ && !fn_stack_.empty() && fn_stack_.back().sees_fields && chain_open(class_ctx_name_);
    }

    static bool engine_instance_member(const class_definition& def, uint64_t id, uint64_t getter_id) {
        return !def.get_method(id, false).is_invalid() || def.find_field_declaring_class(id) != nullptr ||
               !def.get_method(getter_id, false).is_invalid() ||
               def.is_static_field(id) || def.has_static_field(id) || def.has_static_method(id);
    }

    static bool derived_defines_member(const class_definition& def, uint64_t id, uint64_t getter_id, int depth = 0) {
        if (depth > 32) { return true; }
        for (const auto& weak : def.derived_classes()) {
            auto derived = weak.lock();
            if (!derived) { continue; }
            if (engine_instance_member(*derived, id, getter_id) || derived_defines_member(*derived, id, getter_id, depth + 1)) {
                return true;
            }
        }
        return false;
    }

    // A registered subclass owning a _set_ for id that is not its own read-only rejection (a
    // writable re-registration, a script override, a shadowing field's synthesized setter):
    // the store dispatches there through a base-typed handle, so the base's read-only answer
    // is not certain.
    static bool derived_opens_property(const class_definition& def, uint64_t id, uint64_t setter_id, int depth = 0) {
        if (depth > 32) { return true; }
        for (const auto& weak : def.derived_classes()) {
            auto derived = weak.lock();
            if (!derived) { continue; }
            if ((derived->has_method(setter_id) && !derived->owns_read_only_property(id)) ||
                derived_opens_property(*derived, id, setter_id, depth + 1)) {
                return true;
            }
        }
        return false;
    }

    void read_only_store(const source_location& loc, std::string_view name) {
        diag(diag_level::error, loc, "cannot assign to read-only property '" + std::string(name) + "'");
    }

    // `kv.first` of a range-for over a statically known map: the pair variable's name, or
    // empty. Map keys are read-only (C++ `const Key`, the ordering invariant); mutating the
    // pair's key copy is a load error here and a copy-on-write no-op at runtime
    // (docs/invariants.md 11). A `var`-typed map is dynamic and leaves it to the runtime.
    std::string_view map_key_operand(const expression* e) {
        if (!e || e->get_type() != node_type::member_expr) { return {}; }
        const auto* m = static_cast<const member_expr*>(e);
        if (m->is_static || m->member != "first" || !m->object || m->object->get_type() != node_type::identifier_expr) { return {}; }
        const auto* id = static_cast<const identifier_expr*>(m->object.get());
        const var_entry* local = find_local(id->name);
        return local && local->map_pair ? id->name : std::string_view{};
    }

    void map_key_mutation(const source_location& loc, std::string_view pair_name) {
        diag(diag_level::error, loc, "cannot mutate '" + std::string(pair_name) + ".first': map keys are read-only");
    }

    // `kv.first` spelled directly, or any expression whose inferred type carries const_key
    // (a ternary over keys); the text names the spelling when there is one
    std::string map_key_text(const expression* e) {
        std::string_view pair_name = map_key_operand(e);
        return pair_name.empty() ? std::string("a map key") : "'" + std::string(pair_name) + ".first'";
    }

    void map_key_mutation(const source_location& loc, const expression* e) {
        diag(diag_level::error, loc, "cannot mutate " + map_key_text(e) + ": map keys are read-only");
    }

    // Every mutable-alias spelling: T&/auto&/var& decl, shared_ptr<T> decl or store, cast<T&>,
    // cast<shared_ptr<T>>, a T&/shared_ptr<T> parameter or return. Copying is the way out.
    void map_key_alias(const source_location& loc, const expression* e) {
        std::string_view pair_name = map_key_operand(e);
        std::string copy_hint = pair_name.empty() ? std::string("var k = kv.first") : "var k = " + std::string(pair_name) + ".first";
        diag(diag_level::error, loc, "cannot alias " + map_key_text(e) + ": map keys are read-only (copy it instead: " + copy_hint + ")");
    }

    static bool handle_type(const type_info* t) {
        return t && t->base_type == script_value_type::jai_shared_ptr_type;
    }

    static bool string_mutator_name(std::string_view name) {
        for (std::string_view m : k_string_mutator_method_names) {
            if (m == name) { return true; }
        }
        return false;
    }

    // ++/-- on a bare identifier (member targets desugar to += 1 and take the assignment rule)
    void check_incdec_target(unary_expr* u) {
        if (!class_ctx_ || u->operand->get_type() != node_type::identifier_expr) { return; }
        auto* id = static_cast<identifier_expr*>(u->operand.get());
        if (find_local(id->name)) { return; }
        member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
        if (hit.found && hit.read_only && !global_tier_name(id->name)) { read_only_store(u->location, id->name); }
    }

    // -------------------------------------------------------------- type proofs
    // docs/type_proofs_design.md. Every rule writes its node field on EVERY visit (a stamp
    // or the no-proof default), so a re-check can add or drop proofs but never leaves a
    // stale one the pass no longer derives.

    bool stamping() const { return proofs_ == proof_mode::stamp; }

    template <class F, class V>
    void stamp(F& field, V value, V def) {
        if (proofs_ == proof_mode::stamp) { field = static_cast<F>(value); }
        else if (proofs_ == proof_mode::clear) { field = static_cast<F>(def); }
    }

    // Proof certainty: the payload guard (detail::same_type_payload) is TOTAL for these kinds -
    // every dynamic value the site can hold is either the identity case or falls to the ladder.
    bool known_class(std::string_view name) { return classes_.count(name) || engine_class(name) != nullptr; }

    // cast<C>(h) on a handle or reference operand deep-copies (C++ value semantics - the
    // runtime is right); the warning names the aliasing spellings. Warning severity: never gates.
    enum class cast_source : uint8_t { plain, handle, reference };
    var_entry* cast_operand_local(const expression* operand) {
        if (!operand || operand->get_type() != node_type::identifier_expr) { return nullptr; }
        auto* id = static_cast<const identifier_expr*>(operand);
        return id->global_qualified ? nullptr : find_local(id->name);
    }
    cast_source cast_operand_source(const expression* operand, const ctype& t) {
        if (t.ti && t.ti->base_type == script_value_type::jai_shared_ptr_type) { return cast_source::handle; }
        var_entry* local = cast_operand_local(operand);
        if (!local || (!local->type.is_unknown() && local->type.kind != ctype::object_k)) { return cast_source::plain; }
        if (local->is_reference) { return cast_source::reference; }
        return local->is_handle ? cast_source::handle : cast_source::plain;
    }
    static std::string cast_operand_spelling(const expression* e) {
        std::string tail;
        while (e && e->get_type() == node_type::member_expr) {
            auto* m = static_cast<const member_expr*>(e);
            if (m->is_static) { return "..."; }
            tail = std::string(m->null_safe ? "?." : m->is_arrow ? "->" : ".") + std::string(m->member) + tail;
            e = m->object.get();
        }
        if (e && e->get_type() == node_type::identifier_expr) {
            auto* id = static_cast<const identifier_expr*>(e);
            return (id->global_qualified ? "::" : "") + std::string(id->name) + tail;
        }
        if (e && e->get_type() == node_type::this_expr) { return "this" + tail; }
        return "...";
    }
    void warn_cast_copy(const cast_expr* c, const ctype& operand_t) {
        const expression* operand = c->operand.get();
        if (cast_operand_source(operand, operand_t) == cast_source::plain) { return; }
        size_t related_line = 0, related_column = 0;
        if (var_entry* local = cast_operand_local(operand)) {
            related_line = local->line;
            related_column = local->column;
        }
        const std::string cls = c->target->canonical_name();
        const std::string src = cast_operand_spelling(operand);
        diag(diag_level::warning, c->location,
             "cast<" + cls + ">(" + src + ") copies the instance - use cast<" + cls + "&>(" + src +
             ") to alias it or cast<shared_ptr<" + cls + ">>(" + src + ") to share it",
             related_line, related_column);
    }
    static bool certain_scalar(const ctype& t) {
        return (!t.tentative || t.element_proof) && (t.kind == ctype::int_k || t.kind == ctype::float_k || t.kind == ctype::bool_k ||
                                t.kind == ctype::char_k || t.kind == ctype::string_k);
    }
    bool certain_object(const ctype& t) {
        return !t.tentative && t.kind == ctype::object_k && t.ti &&
               t.ti->base_type == script_value_type::jai_object_type && !t.ti->type_name.empty() && known_class(t.cls);
    }
    bool certain(const ctype& t) { return certain_scalar(t) || certain_object(t); }
    // same_type: certain on both sides, identical kind, and for objects the SAME interned spelling.
    bool same_type(const ctype& target, const ctype& rhs) {
        if (!certain(target) || !certain(rhs) || target.kind != rhs.kind) { return false; }
        return target.kind != ctype::object_k || target.ti == rhs.ti;
    }
    // Constructor-call result: the parser's interned object spelling for `name`.
    ctype object_of(std::string_view name) {
        ctype t = ctype::object(name);
        t.ti = eng_.get_type_info_object(std::string(name));
        return t;
    }
    static uint8_t operand_class_of(const ctype& l, const ctype& r) {
        if ((l.tentative && !l.element_proof) || (r.tentative && !r.element_proof)) { return operand_class_unknown; }
        const bool li = l.kind == ctype::int_k, lf = l.kind == ctype::float_k;
        const bool ri = r.kind == ctype::int_k, rf = r.kind == ctype::float_k;
        if (li && ri) { return operand_class_int_int; }
        if (lf && rf) { return operand_class_float_float; }
        if (li && rf) { return operand_class_int_float; }
        if (lf && ri) { return operand_class_float_int; }
        if (l.kind == ctype::string_k && r.kind == ctype::string_k) { return operand_class_string_string; }
        if (l.kind == ctype::bool_k && r.kind == ctype::bool_k) { return operand_class_bool_bool; }
        return operand_class_unknown;
    }

    // Bare names reach this-fields here: method/ctor body or [this] lambda, not a static
    // method, not a ctor initializer list, no includes (which can define anything).
    bool bare_field_context() const {
        return class_ctx_ && !fn_stack_.empty() && fn_stack_.back().sees_fields &&
               !fn_stack_.back().is_static_method && !fn_stack_.back().in_ctor_initializers && !has_includes_;
    }

    struct field_proof { class_definition* cd; uint32_t slot; uint64_t epoch; type_info* declared; bool write_ok; };
    static constexpr std::string_view k_no_proof_names[] = {"same_as", "reset", "use_count", "unique", "cpp_ref_count"};

    // Chain closed = every ancestor is a script class visible to the engine, none carries a C++ base.
    bool script_chain_closed(const class_definition& cd, int depth = 0) {
        if (depth > 32 || !cd.is_script_class() || cd.find_cpp_base_class() || cd.extended_ever()) { return false; }
        for (const auto& p : cd.get_parent_classes()) {
            if (!p || !script_chain_closed(*p, depth + 1)) { return false; }
        }
        return true;
    }

    std::optional<field_proof> resolve_field_proof(std::string_view cls, std::string_view member, const member_hit& hit) {
        if (!stamping() || !hit.found || hit.methods) { return std::nullopt; }
        if (hit.field && hit.field->is_static) { return std::nullopt; }                 // statics live in class storage
        if (member == class_constants::CPP_OBJECT_FIELD) { return std::nullopt; }
        for (auto n : k_no_proof_names) { if (member == n) { return std::nullopt; } }   // builtin-shadow names
        if (global_tier_name(member)) { return std::nullopt; }
        auto def = engine_class(cls);                                                   // null on a same-unit class's FIRST check
        if (!def || def->chain_has_nonpublic() || !script_chain_closed(*def)) { return std::nullopt; }
        const uint64_t id = eng_.symbolize(member);
        if (!def->get_method(id, false).is_invalid()) { return std::nullopt; }         // method with the field's name: ladder decides
        if (def->find_field_declaring_class(id) == nullptr) { return std::nullopt; }
        const uint32_t slot = def->field_slot(id);
        if (slot == class_definition::k_no_field_slot) { return std::nullopt; }
        const uint64_t getter_id = eng_.get_symbolizer()->get_getter_id_with_view(id).first;
        const uint64_t setter_id = eng_.get_symbolizer()->get_setter_id_with_view(id).first;
        if (def->user_accessor(getter_id) || def->user_accessor(setter_id)) { return std::nullopt; }   // a user _get_/_set_ in the chain owns the member: off the lanes, reads and writes
        type_info* declared = def->get_field_declared_type(id).get();
        if (hit.field && hit.field->type.get() != declared) { return std::nullopt; }   // class also declared in THIS unit: spellings must agree (interned pointers)
        bool write_ok = true;
        if (declared) {
            switch (declared->base_type) {
                case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
                case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_object_type:
                case script_value_type::jai_any_type: break;
                default: write_ok = false;   // array/map/shared_ptr/weak_ptr/reference/function fields: reads only
            }
        }
        return field_proof{def.get(), slot, def->field_shape_epoch(), declared, write_ok};
    }

    template <class N> void stamp_field(N* n, const std::optional<field_proof>& p) {
        stamp(n->proven_class, p ? p->cd : nullptr, static_cast<class_definition*>(nullptr));
        stamp(n->proven_slot, p ? p->slot : uint32_t(UINT32_MAX), uint32_t(UINT32_MAX));
        stamp(n->proven_epoch, p ? p->epoch : uint64_t(0), uint64_t(0));
        stamp(n->proven_field_type, p ? p->declared : nullptr, static_cast<type_info*>(nullptr));
        stamp(n->proven_field_write, p ? p->write_ok : false, false);
        if (p && stamping()) { ++counts_.members; }
    }

    // Field store: plain `=` whose RHS static type IS the field's declared type.
    bool field_store_same_type(const std::optional<field_proof>& p, token_type op, const ctype& rhs) {
        return op == token_type::equal && p && p->declared && same_type(ctype_from_type_info(p->declared), rhs);
    }

    // C1: one statically resolved same-script callee, exact arity, no defaults in play, no
    // reference params. Per-argument same_type (an untyped/var parameter binds as identity).
    // an argument whose static type an include cannot change: a literal, a local or a parameter (a frame slot), and arithmetic, a
    // unary, a ternary, a cast or a math intrinsic over those; a global or a member may be redeclared by an included file
    static bool certain_under_includes(const expression* e) {
        if (!e) return false;
        switch (e->get_type()) {
        case node_type::literal_expr: return true;
        case node_type::identifier_expr: return static_cast<const identifier_expr*>(e)->slot_index != SIZE_MAX;
        case node_type::binary_expr: { auto* b = static_cast<const binary_expr*>(e); return b->op.type != token_type::left_bracket && certain_under_includes(b->left.get()) && certain_under_includes(b->right.get()); }
        case node_type::unary_expr: return certain_under_includes(static_cast<const unary_expr*>(e)->operand.get());
        case node_type::ternary_expr: { auto* x = static_cast<const ternary_expr*>(e); return certain_under_includes(x->condition.get()) && certain_under_includes(x->then_expression.get()) && certain_under_includes(x->else_expression.get()); }
        case node_type::cast_expr: return certain_under_includes(static_cast<const cast_expr*>(e)->operand.get());
        case node_type::call_expr: {
            auto* ce = static_cast<const call_expr*>(e);
            if (!ce->callee || ce->callee->get_type() != node_type::member_expr || detail::math_intrinsic_for_call(static_cast<const member_expr*>(ce->callee.get())) == detail::math_fn::none) return false;
            for (const auto& a : ce->arguments) { if (!certain_under_includes(a.get())) return false; }
            return true;
        }
        default: return false;
        }
    }

    void stamp_call_proofs(call_expr* c, const std::vector<const function_decl*>* ov, const std::vector<ctype>& args, bool free_callee) {
        if (proofs_ == proof_mode::leave) { return; }
        const size_t n = c->arguments.size();
        const function_decl* fn = nullptr;
        bool by_value = true;   // the identity bind's proof asks for no reference parameter; the inliner takes the decl and the per-argument proofs either way
        const bool certain_program = !has_includes_;   // the identity bind's proof and the statistics keep the includes-free gate; the inliner's decl and its argument proofs go by the argument
        if (stamping() && ov && ov->size() == 1) {
            fn = (*ov)[0];
            auto [lo, hi] = arity_range(fn);
            bool ok = fn->body && !fn->is_coroutine && lo == n && hi == n && args.size() == n;
            for (const auto& p : fn->parameters) { if (p.is_reference) { by_value = false; } }
            if (!ok) { fn = nullptr; }
        }
        c->proven_callee_decl = fn && free_callee ? fn : nullptr;
        if (!fn) {
            c->static_arg_proofs.clear();
            c->proven_callee_body = nullptr;
            return;
        }
        c->static_arg_proofs.assign(n, proof_none);
        for (size_t i = 0; i < n; ++i) {
            const parameter& p = fn->parameters[i];
            if (p.is_reference) { continue; }   // a reference parameter binds, never converts: no proof, no count
            const bool untyped = !p.type || p.type->base_type == script_value_type::jai_any_type;
            if (!(certain_program || certain_under_includes(c->arguments[i].get()))) { continue; }
            if (untyped || same_type(ctype_from_type_info(p.type.get()), args[i])) {
                c->static_arg_proofs[i] = proof_same_type;
                if (certain_program) { ++counts_.args; }
            }
        }
        c->proven_callee_body = by_value && certain_program ? fn->body.get() : nullptr;
        if (by_value && certain_program) { ++counts_.calls; }
    }

    // -------------------------------------------------------------- assignability

    // Mirrors detail::convert_for_typed_slot (the one runtime table for declarations,
    // assignment, parameters, returns and fields): what a store into a `dst`-typed slot
    // accepts (with conversion). Unknown on either side accepts.
	// References alias storage: conversions that create a value cannot bind T&.
	bool reference_type_compatible(const ctype& src, const ctype& dst) {
		if (src.is_unknown() || dst.is_unknown()) { return true; }
		if (src.kind != dst.kind) { return false; }
		if (src.ti && dst.ti) {
			if (src.ti->base_type != dst.ti->base_type) { return false; }
			if (src.ti->base_type == script_value_type::jai_object_type) {
				return class_relation(src.cls, dst.cls) != 0;
			}
			return src.ti->equals(*dst.ti);
		}
		if (src.kind == ctype::object_k) { return class_relation(src.cls, dst.cls) != 0; }
		return true;   // same scalar kind, or unavailable container parameters
	}

	bool weak_initializer_compatible(const ctype& src, const ctype& dst) {
		if (src.is_unknown() || src.kind == ctype::null_k) { return true; }
		if (!src.ti) {
			// An object spelling can lose its value/handle distinction at a mixed
			// conditional. Do not reject a handle whose wrapper is unknown.
			return src.kind == ctype::weak_k || src.kind == ctype::object_k;
		}
		if (src.ti->base_type != script_value_type::jai_shared_ptr_type &&
		    src.ti->base_type != script_value_type::jai_weak_ptr_type) {
			return false;
		}
		const type_info* from = src.ti->element_type();
		const type_info* to = dst.ti ? dst.ti->element_type().get() : nullptr;
		return reference_type_compatible(ctype_from_type_info(from), ctype_from_type_info(to));
	}

	// The origin is the expression at this use, never a variable's old initializer.
	// Only literal elements need a saved ctype; revisiting their AST could duplicate
	// lambda walks, diagnostics and proof stamps.
	std::unordered_map<const expression*, ctype> literal_element_types_;

	static std::optional<script_float> literal_float(const expression* expr) {
		if (!expr) { return std::nullopt; }
		if (expr->get_type() == node_type::literal_expr) {
			const auto* lit = static_cast<const literal_expr*>(expr);
			if (lit->value.is_float()) { return lit->value.unchecked_as_float(); }
			return std::nullopt;
		}
		if (expr->get_type() == node_type::unary_expr) {
			const auto* unary = static_cast<const unary_expr*>(expr);
			if (unary->op.type == token_type::plus || unary->op.type == token_type::minus) {
				if (auto value = literal_float(unary->operand.get())) {
					return unary->op.type == token_type::minus ? -*value : *value;
				}
			}
		}
		return std::nullopt;
	}

	static bool literal_int_overflow(const ctype& src, const ctype& dst) {
		if (dst.kind != ctype::int_k || src.kind != ctype::float_k) { return false; }
		const auto value = literal_float(src.origin);
		return value && !float_fits_script_int(*value);
	}

	ctype declared_container_receiver(const expression* expr, ctype inferred) {
		if (!inferred.is_unknown() || !expr || expr->get_type() != node_type::identifier_expr) {
			return inferred;
		}
		const auto* id = static_cast<const identifier_expr*>(expr);
		const auto* local = find_local(id->name);
		if (!local || !local->is_reference || !local->declared) { return inferred; }
		ctype result = ctype_from_type_info(local->declared);
		if (result.kind != ctype::array_k && result.kind != ctype::map_k) { return inferred; }
		result.tentative = true;
		return result;
	}

	static ctype container_value_type(const ctype& container) {
		if (!container.ti) { return {}; }
		const type_info* element = container.kind == ctype::array_k
			? container.ti->element_type().get()
			: (container.kind == ctype::map_k ? container.ti->value_type().get() : nullptr);
		ctype result = ctype_from_type_info(element);
		// Tentative for diagnostics; a scalar element of a declared, non-tentative typed container is a
		// proof source for the operand class (the typed rows guard it at runtime)
		result.tentative = true;
		result.element_proof = (!container.tentative || container.alias_proof) && (result.kind == ctype::int_k || result.kind == ctype::float_k);
		return result;
	}

	ctype recorded_literal_element(const expression* expr) const {
		const auto found = literal_element_types_.find(expr);
		return found == literal_element_types_.end() ? ctype{} : found->second;
	}

	std::string container_element_issue(const ctype& src, const type_info* target, int depth = 0) {
		const ctype dst = ctype_from_type_info(target);
		if (src.is_unknown() || dst.is_unknown() || depth > 512) { return {}; }
		if (literal_int_overflow(src, dst)) { return "float literal is outside the int range"; }
		if (dst.kind == ctype::array_k || dst.kind == ctype::map_k) {
			return container_issue(src, dst, depth + 1);
		}
		bool accepted = src.kind == dst.kind;
		if (is_numeric(src.kind) && is_numeric(dst.kind)) { accepted = true; }
		if ((src.kind == ctype::bool_k && dst.kind == ctype::int_k) ||
			(src.kind == ctype::int_k && dst.kind == ctype::bool_k)) { accepted = true; }
		if (target->base_type == script_value_type::jai_object_type) {
			accepted = src.kind == ctype::object_k && class_relation(src.cls, dst.cls) != 0;
		} else if (target->base_type == script_value_type::jai_shared_ptr_type) {
			// A shared-handle element takes null or a same/derived live instance,
			// including an instance already carried by a shared handle.
			accepted = src.kind == ctype::null_k ||
				(src.kind == ctype::object_k && class_relation(src.cls, dst.cls) != 0);
		} else if (target->base_type == script_value_type::jai_weak_ptr_type) {
			// The weak-element kernel accepts null or an existing weak handle.
			// Shared-to-weak conversion at an element boundary is not a runtime row.
			accepted = src.kind == ctype::null_k || src.kind == ctype::weak_k;
		}
		if (accepted) { return {}; }
		return "expected '" + target->canonical_name() + "', got '" + ctype_name(src) + "'";
	}

	std::string map_key_issue(const ctype& src, const type_info* target) {
		const ctype dst = ctype_from_type_info(target);
		if (src.is_unknown() || dst.is_unknown()) { return {}; }
		// Keys never coerce: a numeric conversion could merge distinct entries.
		// The runtime key boundary checks the kind, not a pointee or element type.
		const bool accepted = src.kind == dst.kind &&
			(!src.ti || src.ti->base_type == target->base_type);
		if (accepted) { return {}; }
		return "expected '" + target->canonical_name() + "', got '" + ctype_name(src) + "'";
	}

	std::string container_issue(const ctype& src, const ctype& dst, int depth = 0) {
		if (src.is_unknown() || dst.is_unknown() || depth > 512) { return {}; }
		if (src.kind != dst.kind) {
			return "expected '" + ctype_name(dst) + "', got '" + ctype_name(src) + "'";
		}
		if (!dst.ti) { return {}; }
		if (dst.kind == ctype::array_k) {
			const auto* target = dst.ti->element_type().get();
			if (src.origin && src.origin->get_type() == node_type::array_literal_expr) {
				const auto* literal = static_cast<const array_literal_expr*>(src.origin);
				for (size_t i = 0; i < literal->elements.size(); ++i) {
					auto issue = container_element_issue(recorded_literal_element(literal->elements[i].get()), target, depth + 1);
					if (!issue.empty()) { return "array element " + std::to_string(i) + ": " + issue; }
				}
			} else if (src.ti) {
				auto issue = container_element_issue(ctype_from_type_info(src.ti->element_type().get()), target, depth + 1);
				if (!issue.empty()) { return "array element type: " + issue; }
			}
			return {};
		}
		if (dst.kind == ctype::map_k) {
			const auto* key = dst.ti->key_type().get();
			const auto* value = dst.ti->value_type().get();
			if (src.origin && src.origin->get_type() == node_type::map_literal_expr) {
				const auto* literal = static_cast<const map_literal_expr*>(src.origin);
				for (size_t i = 0; i < literal->entries.size(); ++i) {
					const auto& entry = literal->entries[i];
					auto issue = map_key_issue(recorded_literal_element(entry.first.get()), key);
					if (!issue.empty()) { return "map key " + std::to_string(i) + ": " + issue; }
					issue = container_element_issue(recorded_literal_element(entry.second.get()), value, depth + 1);
					if (!issue.empty()) { return "map value " + std::to_string(i) + ": " + issue; }
				}
			} else if (src.ti) {
				auto issue = map_key_issue(ctype_from_type_info(src.ti->key_type().get()), key);
				if (!issue.empty()) { return "map key type: " + issue; }
				issue = container_element_issue(ctype_from_type_info(src.ti->value_type().get()), value, depth + 1);
				if (!issue.empty()) { return "map value type: " + issue; }
			}
		}
		return {};
	}

	std::string assignment_detail(const ctype& src, const ctype& dst) {
		if (literal_int_overflow(src, dst)) { return ": float literal is outside the int range"; }
		if (dst.kind == ctype::array_k || dst.kind == ctype::map_k) {
			auto issue = container_issue(src, dst);
			if (!issue.empty()) { return ": " + issue; }
		}
		return {};
	}

	bool assignable(const ctype& src, const ctype& dst) {
		if (dst.is_unknown() || src.is_unknown()) { return true; }
		if (literal_int_overflow(src, dst)) { return false; }
		if (dst.kind == ctype::class_ref_k || dst.kind == ctype::enum_ref_k ||
			src.kind == ctype::class_ref_k || src.kind == ctype::enum_ref_k) {
			return true;   // exotic; stay lenient
		}
		switch (dst.kind) {
			case ctype::int_k:
				return src.kind == ctype::int_k || src.kind == ctype::float_k || src.kind == ctype::bool_k ||
					   may_convert_via_method(src, "to_int");
			case ctype::float_k:
				return src.kind == ctype::float_k || src.kind == ctype::int_k || src.kind == ctype::bool_k ||
					   may_convert_via_method(src, "to_float");
			case ctype::bool_k: return true;     // truthy conversion accepts anything
			case ctype::string_k: return true;   // to_string conversion accepts anything
			case ctype::char_k: return src.kind == ctype::char_k || may_convert_via_method(src, "to_char");
			case ctype::array_k:
			case ctype::map_k: return container_issue(src, dst).empty();
			case ctype::function_k: return src.kind == ctype::function_k || src.kind == ctype::null_k;
			case ctype::weak_k: return weak_initializer_compatible(src, dst);
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
        const sclass* c = find_sclass(target_cls);
        if (!c) {
            return engine_class(target_cls) != nullptr;   // registered: lenient
        }
        for (const auto* ctor : c->ctors) {
            // Dev ruling (2026-07): a defaulted-param ctor is NOT a converting ctor -
            // only TRUE one-parameter constructors convert (matches the runtime gate)
            if (ctor->parameters.size() != 1) { continue; }
            const parameter& p = ctor->parameters[0];
            if (ctor_param_takes(src, ctype_from_type_info(p.type.get()))) {
                return true;
            }
        }
        return false;
    }

    // The runtime's single-step gate: the converting ctor's declared parameter takes the
    // source DIRECTLY - untyped, its own class or a base of it, the same primitive base or
    // int<->float. Never another conversion (no chaining, no to_X() on the way in).
    bool ctor_param_takes(const ctype& src, const ctype& param) {
        if (param.is_unknown() || src.is_unknown()) { return true; }
        if (param.kind == ctype::object_k) {
            return src.kind == ctype::object_k && class_relation(src.cls, param.cls) != 0;
        }
        if (src.kind == ctype::object_k) { return false; }
        if (param.kind == src.kind) { return true; }
        return (param.kind == ctype::int_k && src.kind == ctype::float_k) ||
               (param.kind == ctype::float_k && src.kind == ctype::int_k);
    }

    // Object -> primitive through a script to_int/to_float/to_char (the table's to_X() row):
    // checkable for in-script sources; engine-registered classes stay lenient.
    bool may_convert_via_method(const ctype& src, std::string_view method) {
        if (src.kind != ctype::object_k) { return false; }
        if (!find_sclass(src.cls)) {
            return engine_class(src.cls) != nullptr;
        }
        member_hit hit = lookup_member(src.cls, method, false);
        return !hit.certain || (hit.found && hit.methods != nullptr);
    }

    static std::pair<size_t, size_t> arity_range(const function_decl* fn) {
        size_t min_a = 0;
        for (const auto& p : fn->parameters) {
            if (p.default_value) { break; }
            ++min_a;
        }
        return {min_a, fn->parameters.size()};
    }

    // -------------------------------------------------------------- language builtins

    // Result kind of a math:: intrinsic (detail/math_intrinsics.hpp kernel): int for the
    // integer family, float for the transcendental/interpolation family, operand-typed for
    // abs/min/max/clamp/random (uniform int -> int, uniform float -> float, else unknown).
    static ctype math_intrinsic_result(math_fn fn, const std::vector<ctype>& args) {
        auto all = [&](ctype::kind_t k) {
            for (const auto& a : args) { if (a.kind != k) { return false; } }
            return !args.empty();
        };
        switch (fn) {
            case math_fn::itrunc: case math_fn::ifloor:
            case math_fn::wrap_add_: case math_fn::wrap_sub_: case math_fn::wrap_mul_:
                return ctype::make(ctype::int_k);
            case math_fn::abs_: case math_fn::min_: case math_fn::max_: case math_fn::clamp_:
                if (all(ctype::int_k)) { return ctype::make(ctype::int_k); }
                if (all(ctype::float_k)) { return ctype::make(ctype::float_k); }
                return {};
            case math_fn::random_: case math_fn::random_range:
                if (args.empty()) { return ctype::make(ctype::float_k); }
                if (all(ctype::int_k)) { return ctype::make(ctype::int_k); }
                for (const auto& a : args) { if (!is_numeric(a.kind)) { return {}; } }
                return ctype::make(ctype::float_k);
            case math_fn::random_seed:
                return {};
            default:
                return ctype::make(ctype::float_k);
        }
    }

    // `a::b::c` chains resolve through namespaces registered under their FULL path ("a::b"),
    // so a static receiver is a namespace path when any prefix of it names one (this unit's
    // declarations or the engine's).
    bool namespace_path_receiver(const member_expr* m) {
        std::vector<std::string_view> segments;
        const expression* e = m->object.get();
        while (e && e->get_type() == node_type::member_expr) {
            auto* inner = static_cast<const member_expr*>(e);
            if (!inner->is_static) { return false; }
            segments.push_back(inner->member);
            e = inner->object.get();
        }
        if (!e || e->get_type() != node_type::identifier_expr) { return false; }
        std::string path(static_cast<const identifier_expr*>(e)->name);
        if (namespace_known(path)) { return true; }
        for (size_t i = segments.size(); i-- > 0;) {
            path += "::";
            path += segments[i];
            if (namespace_known(path)) { return true; }
        }
        return false;
    }

    bool namespace_known(const std::string& path) {
        return namespaces_.count(std::string_view(path)) != 0 ||
               eng_.script_namespaces().count(eng_.symbolize(path)) != 0;
    }

    // Handle builtins every object value answers (shared_ptr registry + the universal
    // same_as): a class method of the same name wins first, so this runs on misses only.
    static bool handle_builtin_result(std::string_view member, ctype& out) {
        if (member == "use_count" || member == "cpp_ref_count") { out = ctype::make(ctype::int_k); return true; }
        if (member == "unique" || member == "same_as") { out = ctype::make(ctype::bool_k); return true; }
        if (member == "reset") { out = {}; return true; }
        return false;
    }

    // -------------------------------------------------------------- calls

    struct call_check {
        bool checked = false;   // a statically known callee was actually validated
        ctype result{};
    };

    // args already inferred by the caller; foreign = the overloads belong to another unit's source
    call_check check_script_overloads(std::string_view fname, const std::vector<const function_decl*>& overloads,
                                      const std::vector<ctype>& args, const source_location& loc, bool foreign = false) {
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
            report_no_overload(fname, overloads, args, loc, foreign);
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
							const std::vector<ctype>& args, const source_location& loc, bool foreign = false) {
		size_t rl = (overloads.empty() || foreign) ? 0 : overloads[0]->location.line;
		size_t rc = (overloads.empty() || foreign) ? 0 : overloads[0]->location.column;
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
						 p.name + "' declared '" + (p.type ? p.type->canonical_name() : "auto") + "'" + assignment_detail(args[i], pt),
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
                     c.foreign ? 0 : c.decl->location.line, c.foreign ? 0 : c.decl->location.column);
            }
            return;
        }
        for (const auto* ctor : c.ctors) {
            if (script_fn_viable(ctor, args)) { return; }
        }
        report_no_overload(cls_name, c.ctors, args, loc, c.foreign);
    }

    // -------------------------------------------------------------- expressions

	ctype infer(expression* e) {
		ctype result = infer_impl(e);
		result.origin = e;
		return result;
	}

	ctype infer_impl(expression* e) {
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
					// the proven element kind rides the node's class (int_int / float_float) for the compiler's temps
					const ctype el = container_value_type(declared_container_receiver(b->left.get(), l));					const uint8_t cls = !el.element_proof ? operand_class_unknown
					                  : el.kind == ctype::int_k ? operand_class_int_int
					                  : el.kind == ctype::float_k ? operand_class_float_float : operand_class_unknown;
					stamp(b->static_operand_class, cls, operand_class_unknown);
					return el;
				}
				stamp(b->static_operand_class, operand_class_of(l, r), operand_class_unknown);   // annotation only
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
						check_incdec_target(u);
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
			case node_type::member_expr: {
				auto* m = static_cast<member_expr*>(e);
				ctype t = infer_member(m, /*writing=*/false);
				if (!map_key_operand(m).empty()) { t.const_key = true; }
				return t;
			}
			case node_type::lambda_expr:
				walk_lambda(static_cast<lambda_expr*>(e));
				return ctype::make(ctype::function_k);
			case node_type::new_expr: {
				auto* n = static_cast<new_expr*>(e);
				std::vector<ctype> args;
				for (auto& a : n->arguments) { args.push_back(infer(a.get())); }
				ctype t = ctype_from_type_info(n->type.get());
				if (t.kind == ctype::object_k) {
					if (const sclass* c = find_sclass(t.cls)) {
						check_ctor_call(t.cls, *c, args, n->location);
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
				if (a.kind == b.kind && a.cls == b.cls) {
					if (a.ti != b.ti) { a.ti = nullptr; }   // two spellings (value vs handle) are not one certain type
					a.tentative = a.tentative || b.tentative;
					a.const_key = a.const_key || b.const_key;
					return a;
				}
				if (!a.is_unknown() && !b.is_unknown()) {
					last_mixed_ternary_ = {e, a, b};
				}
				ctype mixed{};
				mixed.const_key = a.const_key || b.const_key;
				return mixed;
			}
			case node_type::array_literal_expr: {
				auto* a = static_cast<array_literal_expr*>(e);
				for (auto& el : a->elements) { literal_element_types_[el.get()] = infer(el.get()); }
				return ctype::make(ctype::array_k);
			}
			case node_type::map_literal_expr: {
				auto* m = static_cast<map_literal_expr*>(e);
				for (auto& [k, v] : m->entries) {
					literal_element_types_[k.get()] = infer(k.get());
					literal_element_types_[v.get()] = infer(v.get());
				}
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
			case node_type::cast_expr: {
				// cast<T>(e): certain T (proofs flow); cast<T&>(e): T for member lookup, never a
				// store proof (tentative, like every reference); cast<shared_ptr<T>>(e): the handle
				// spelling. The operand is the explicit typed boundary - never checked against T.
				auto* c = static_cast<cast_expr*>(e);
				ctype operand_t = infer(c->operand.get());
				ctype t = ctype_from_type_info(c->target.get());
				if (t.kind == ctype::object_k && !known_class(t.cls) && !extra_globals_.count(std::string(t.cls))) {
					diag(diag_level::error, c->location, "unknown type in cast: '" + std::string(t.cls) + "'");
					return {};
				}
				if (collect_diagnostics_ && !c->by_reference && c->target->base_type == script_value_type::jai_object_type) {
					warn_cast_copy(c, operand_t);
				}
				if (operand_t.const_key && (c->by_reference || handle_type(c->target.get()))) {
					map_key_alias(c->location, c->operand.get());
				}
				if (c->by_reference) { t.tentative = true; }
				return t;
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
        std::optional<field_proof> fp;
        ctype t = infer_identifier_impl(id, fp);
        stamp_field(id, fp);
        if (extended_names_.count(id->name)) { return {}; }
        return t;
    }

	ctype infer_identifier_impl(identifier_expr* id, std::optional<field_proof>& fp) {
		size_t scope_index = 0;
		if (var_entry* local = find_local(id->name, &scope_index)) {
			if (scope_index == 0) { note_global_use(id->name); }   // top-level var read
			if (!capture_stack_.empty()) {
				auto& cap = capture_stack_.back();
				if (cap.bare && !cap.warned && scope_index != 0 && scope_index < cap.base) {
					cap.warned = true;
					diag(diag_level::warning, cap.location,
						 "bare [] implicitly captured local '" + std::string(id->name) +
						 "' by value — be explicit with [=] (snapshot) or [&] (share updates)",
						 local->line, local->column);
				}
			}
			return local->dynamic ? (local->alias.kind != ctype::unknown ? local->alias : ctype{}) : local->type;
		}
		if (class_ctx_) {
			member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
			if (hit.found) {
				// Runtime resolution reaches GLOBALS before this-fields; a name that is
				// both is flow-order-ambiguous to a static walk (a global declared later
				// in the source steals the read at call time) - stay unknown.
				if (global_tier_name(id->name)) { return {}; }
				if (bare_field_context()) { fp = resolve_field_proof(class_ctx_name_, id->name, hit); }
				if (hit.methods) { return ctype::make(ctype::function_k); }
				if (hit.field && (hit.field->type || hit.field->function_spelling)) { return member_type(hit); }
				return {};
			}
			if (hit.unresolved) { return {}; }   // an ancestor nowhere to be found can define anything
		}
		if (functions_.count(id->name)) { note_global_use(id->name); return ctype::make(ctype::function_k); }
		if (find_sclass(id->name)) { return ctype{ctype::class_ref_k, id->name}; }
		if (enums_.count(id->name)) { return ctype{ctype::enum_ref_k, id->name}; }
		if (namespaces_.count(id->name)) { return {}; }
		std::string name(id->name);
		if (extra_globals_.count(name)) { note_global_use(id->name); return {}; }
		if (eng_.is_type_name(name) || eng_.get_class_registry().find_script_class(name)) {
			return ctype{ctype::class_ref_k, id->name};
		}
		if (eng_.has_function(name)) { note_global_use(id->name); return ctype::make(ctype::function_k); }
		if (eng_.has_variable(name)) { note_global_use(id->name); return engine_global_type(name); }
		if (engine_knows_name(name)) { return {}; }
		if (all_names_.count(id->name)) { return {}; }
		if (stale_field_context()) { return {}; }   // bare old-shape field in a redefined class's method
		undeclared(id->location, id->name);
		return {};
	}

    ctype infer_member(member_expr* m, bool writing, const variable_decl** typed_field_out = nullptr,
                       ctype* recv_out = nullptr, member_hit* hit_out = nullptr,
                       std::optional<field_proof>* proof_out = nullptr) {
        std::optional<field_proof> fp;
        ctype t = infer_member_impl(m, writing, typed_field_out, recv_out, hit_out, fp);
        stamp_field(m, fp);
        if (proof_out) { *proof_out = fp; }
        return t;
    }

	ctype infer_member_impl(member_expr* m, bool writing, const variable_decl** typed_field_out,
							ctype* recv_out, member_hit* hit_out, std::optional<field_proof>& fp) {
		if (m->is_static && namespace_path_receiver(m)) { return {}; }
		ctype recv = infer(m->object.get());
		if (recv_out) { *recv_out = recv; }
		if (recv.is_unknown() || recv.kind == ctype::map_k || recv.kind == ctype::array_k ||
			recv.kind == ctype::string_k || recv.kind == ctype::function_k || recv.kind == ctype::weak_k || recv.kind == ctype::null_k ||
			recv.kind == ctype::int_k || recv.kind == ctype::float_k || recv.kind == ctype::char_k ||
			recv.kind == ctype::bool_k) {
			return {};   // builtin methods / map keys / dynamic: lenient
		}
		if (extended_members_.count(m->member)) { return {}; }
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
		if (hit_out) { *hit_out = hit; }
		if (!want_static && chain_has_user_accessor(cls, m->member)) { return accessor_read_type(cls, m->member); }   // a user _get_/_set_ answers: known member, typed only by a single declared getter, never a proof
		if (hit.found) {
			if (typed_field_out && hit.field && (hit.field->type || hit.field->function_spelling)) { *typed_field_out = hit.field; }
			if (recv.kind == ctype::object_k && !m->null_safe && !m->is_static &&
				m->object->get_type() != node_type::super_expr) {
				fp = resolve_field_proof(cls, m->member, hit);
			}
			if (hit.methods) { return ctype::make(ctype::function_k); }
			if (hit.field && (hit.field->type || hit.field->function_spelling)) { return member_type(hit); }
			return {};
		}
		if (!hit.certain) { return {}; }
		if (m->member == class_constants::CPP_OBJECT_FIELD) { return {}; }
		if (stale_shape_receiver(recv, m->object.get())) { return {}; }
		if (!want_static) {
			if (ctype t; handle_builtin_result(m->member, t)) { return {}; }   // builtin as a value: lenient
		}
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
        const std::vector<const function_decl*>* ov = nullptr;   // the statically resolved same-script overload set
        ctype t = infer_call_impl(c, args, ov);
        bool free_callee = false;   // resolved through the program's free functions (a method by its bare name, a constructor: never inlined)
        if (ov && c->callee && c->callee->get_type() == node_type::identifier_expr) {
            auto it = functions_.find(static_cast<identifier_expr*>(c->callee.get())->name);
            free_callee = it != functions_.end() && ov == &it->second;
        }
        stamp_call_proofs(c, ov, args, free_callee);
        if (ov) {
            // a map key passed where every viable overload binds the parameter by reference
            // or as a shared_ptr handle (a by-value parameter copies)
            for (size_t i = 0; i < c->arguments.size() && i < args.size(); ++i) {
                if (!args[i].const_key) { continue; }
                bool aliasing = false, viable = false;
                for (const auto* fn : *ov) {
                    if (!script_fn_viable(fn, args)) { continue; }
                    viable = true;
                    aliasing = i < fn->parameters.size() &&
                               (fn->parameters[i].is_reference || handle_type(fn->parameters[i].type.get()));
                    if (!aliasing) { break; }
                }
                if (viable && aliasing) { map_key_alias(c->arguments[i]->location, c->arguments[i].get()); }
            }
        }
        return t;
    }

	ctype infer_call_impl(call_expr* c, const std::vector<ctype>& args, const std::vector<const function_decl*>*& ov) {
		expression* callee = c->callee.get();
		if (callee->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<identifier_expr*>(callee);
			if (id->name == "weak_from_this" || id->name == "shared_from_this") { return {}; }   // call-shape builtins, resolved before any lookup
			size_t scope_index = 0;
			if (find_local(id->name, &scope_index)) {
				if (scope_index == 0) { note_global_use(id->name); }
				return {};   // callable local: signature unknown
			}
			if (class_ctx_) {
				member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
				if (hit.found) {
					// Bare calls prefer the GLOBAL function when one exists (audited
					// resolution order); flow order decides which one runs - lenient.
					if (global_tier_name(id->name)) { return {}; }
					if (hit.methods) {
						if (hit.provable) { ov = hit.methods; }
						ctype r = check_script_overloads(id->name, *hit.methods, args, c->location, hit.foreign).result;
						if (!hit.provable) { r.tentative = true; }
						return r;
					}
					return {};
				}
				if (hit.unresolved) { return {}; }
			}
			if (auto it = functions_.find(id->name); it != functions_.end()) {
				note_global_use(id->name);
				ov = &it->second;
				return check_script_overloads(id->name, it->second, args, c->location).result;
			}
			if (const sclass* sc = find_sclass(id->name)) {
				check_ctor_call(id->name, *sc, args, c->location);
				if (sc->provable) { ov = &sc->ctors; }
				return object_of(id->name);
			}
			if (enums_.count(id->name)) { return {}; }
			std::string name(id->name);
			if (extra_globals_.count(name)) { note_global_use(id->name); return {}; }
			if (eng_.is_type_name(name) || eng_.get_class_registry().find_script_class(name)) {
				return object_of(id->name);   // registered class ctor: args lenient
			}
			if (eng_.has_function(name)) {
				note_global_use(id->name);
				check_host_call(name, args, c->location);
				return {};
			}
			if (eng_.has_variable(name)) { note_global_use(id->name); return {}; }
			if (engine_knows_name(name)) { return {}; }
			if (all_names_.count(id->name)) { return {}; }
			if (stale_field_context()) { return {}; }
			undeclared(id->location, id->name);
			return {};
		}
		if (callee->get_type() == node_type::member_expr) {
			auto* m = static_cast<member_expr*>(callee);
			if (m->is_static) {
				// math:: intrinsics take the kernel lane (same shape gate as both backends)
				const math_fn fn = math_intrinsic_for_call(m);
				if (fn != math_fn::none && args.size() <= 8) { return math_intrinsic_result(fn, args); }
				if (namespace_path_receiver(m)) { return {}; }
			}
			ctype recv = declared_container_receiver(m->object.get(), infer(m->object.get()));
			if (recv.kind == ctype::array_k && recv.ti && m->member == "push" && args.size() == 1) {
				const auto issue = container_element_issue(args[0], recv.ti->element_type().get());
				if (!issue.empty()) {
					diag(diag_level::error, c->location, "cannot push into '" + ctype_name(recv) + "': " + issue);
				}
			}
			if (recv.const_key && string_mutator_name(m->member)) { map_key_mutation(m->location, m->object.get()); }
			if (recv.kind == ctype::object_k || recv.kind == ctype::class_ref_k) {
				bool want_static = (recv.kind == ctype::class_ref_k);
				member_hit hit = lookup_member(recv.cls, m->member, want_static);
				if (!hit.found && !want_static && chain_has_user_accessor(recv.cls, m->member)) { return {}; }   // calling what a user _get_ returns
				if (hit.found) {
					if (hit.methods) {
						if (hit.provable) { ov = hit.methods; }
						ctype r = check_script_overloads(m->member, *hit.methods, args, c->location, hit.foreign).result;
						if (!hit.provable) { r.tentative = true; }
						return r;
					}
					return {};
				}
				if (!want_static) {
					if (ctype t; handle_builtin_result(m->member, t)) { return t; }
				}
				if (stale_shape_receiver(recv, m->object.get())) { return {}; }
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
        uint8_t store_proof = proof_none;
        ctype t = infer_assignment_impl(a, store_proof);
        stamp(a->static_store_proof, store_proof, proof_none);
        if (store_proof != proof_none && stamping()) { ++counts_.stores; }
        return t;
    }

	ctype infer_assignment_impl(assignment_expr* a, uint8_t& store_proof) {
		ctype rhs = infer(a->value.get());
		bool compound = (a->op.type != token_type::equal);
		token_type base_op = compound_base_op(a->op.type);

		expression* target = a->target.get();
		if (target->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<identifier_expr*>(target);
			std::optional<field_proof> fp;
			auto done = [&](ctype t) { stamp_field(id, fp); return t; };
			size_t scope_index = 0;
			if (var_entry* local = find_local(id->name, &scope_index)) {
				if (scope_index == 0) { note_global_use(id->name); }   // top-level var write
				if (rhs.const_key && local->is_handle && !compound) { map_key_alias(a->location, a->value.get()); return done(rhs); }
				if (local->dynamic) { return done(rhs); }
				check_store(rhs, compound, base_op, local->type, local->display,
							"'" + std::string(id->name) + "'", a->location, local->line, local->column);
				if (!compound && same_type(local->type, rhs)) { store_proof = proof_same_type; }
				if (compound) { stamp(a->static_operand_class, operand_class_of(local->type, rhs), operand_class_unknown); }   // the fused compound's typed row
				return done(local->type);
			}
			if (class_ctx_) {
				member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
				if (hit.found) {
					// Writes resolve through the global tier before fields too (audited
					// order) - a name that is both is flow-order-ambiguous: stay lenient.
					if (global_tier_name(id->name)) { return done(rhs); }
					if (hit.read_only) { read_only_store(a->location, id->name); return done({}); }
					if (bare_field_context()) { fp = resolve_field_proof(class_ctx_name_, id->name, hit); }
					if (field_store_same_type(fp, a->op.type, rhs)) { store_proof = proof_same_type; }
					if (hit.field && (hit.field->type || hit.field->function_spelling)) {
						ctype ft = member_type(hit);
						if (!ft.is_unknown()) {
							check_store(rhs, compound, base_op, ft, declared_variable_type_name(hit.field),
										"field '" + std::string(id->name) + "'", a->location,
										hit.foreign ? 0 : hit.field->location.line, hit.foreign ? 0 : hit.field->location.column);
						}
						return done(ft);
					}
					return done({});
				}
				if (hit.unresolved) { return done(rhs); }
			}
			std::string name(id->name);
			if (extra_globals_.count(name)) { note_global_use(id->name); return done(rhs); }
			if (functions_.count(id->name) || find_sclass(id->name) || enums_.count(id->name) ||
				namespaces_.count(id->name)) {
				return done(rhs);
			}
			if (eng_.has_variable(name)) {
				note_global_use(id->name);
				ctype gt = engine_global_type(name);
				if (!gt.is_unknown()) {
					check_store(rhs, compound, base_op, gt, ctype_name(gt),
								"'" + std::string(id->name) + "'", a->location, 0, 0);
				}
				return done(gt);   // engine globals: never proven (S3)
			}
			if (eng_.has_function(name) || engine_knows_name(name) || all_names_.count(id->name) || stale_field_context()) {
				return done(rhs);
			}
			undeclared(id->location, id->name);
			return done(rhs);
		}
		if (target->get_type() == node_type::member_expr) {
			const variable_decl* typed_field = nullptr;
			std::optional<field_proof> fp;
			member_hit hit;
			auto* m = static_cast<member_expr*>(target);
			if (std::string_view pair_name = map_key_operand(m); !pair_name.empty()) {
				map_key_mutation(a->location, pair_name);
				return rhs;
			}
			ctype recv;
			const ctype read_type = infer_member(m, /*writing=*/true, &typed_field, &recv, &hit, &fp);
			// An assignment expression's value is the destination's stored value (Dev ruling
			// 2026-09-08): the member's type when the checker knows it; for a member it cannot
			// read (no field, no getter) the runtime hands back the rhs or the compound's result.
			// Keep assignment results' existing non-key provenance.
			ctype result_type = !read_type.is_unknown() ? read_type : compound ? binary_result(read_type, base_op, rhs) : rhs;
			result_type.const_key = false;
			if (hit.found && hit.read_only && recv.kind == ctype::object_k && !m->is_static) {
				read_only_store(a->location, m->member);
				return rhs;
			}
			if (field_store_same_type(fp, a->op.type, rhs)) { store_proof = proof_same_type; }
			if (typed_field && (typed_field->type || typed_field->function_spelling)) {
				ctype ft = member_type(hit);
				if (!ft.is_unknown()) {
					check_store(rhs, compound, base_op, ft, declared_variable_type_name(typed_field),
								"field '" + std::string(typed_field->name) + "'", a->location,
								hit.foreign ? 0 : typed_field->location.line, hit.foreign ? 0 : typed_field->location.column);
				}
				return ft;
			}
			if (recv.kind == ctype::object_k && !m->is_static) { check_accessor_store(recv.cls, m->member, rhs, compound, base_op, a->location); }
			return result_type;
		}
		if (target->get_type() == node_type::binary_expr) {
			auto* subscript = static_cast<binary_expr*>(target);
			if (subscript->op.type == token_type::left_bracket) {
				const ctype recv = declared_container_receiver(subscript->left.get(), infer(subscript->left.get()));
				infer(subscript->right.get());
				stamp(subscript->static_operand_class, operand_class_unknown, operand_class_unknown);
				const ctype element = container_value_type(recv);
				const ctype stored = compound ? binary_result(element, base_op, rhs) : rhs;
				const auto issue = container_element_issue(stored, element.ti);
				if (!issue.empty()) {
					diag(diag_level::error, a->location, "cannot store into '" + ctype_name(recv) + "': " + issue);
				}
				return element.is_unknown() ? stored : element;
			}
		}
		infer(target);
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
		if (!compound && target.kind == ctype::object_k && assign_operator_accepts(target.cls, stored)) { return; }
		diag(diag_level::error, loc, "cannot assign '" + ctype_name(stored) + "' to " + what +
			 " declared '" + target_display + "'" + assignment_detail(stored, target), decl_line, decl_col);
	}

    // `target = rhs` on a class-typed target runs a user operator=(rhs) ahead of the
    // conversion ladder (both backends); a registered ancestor's operator= stays lenient.
    bool assign_operator_accepts(std::string_view cls, const ctype& src, int depth = 0) {
        if (depth > 32) { return true; }
        const sclass* c = find_sclass(cls);
        if (!c) {
            auto def = engine_class(cls);
            return !def || !def->get_method(eng_.symbolize("="), false).is_invalid();
        }
        if (auto m = c->methods.find("="); m != c->methods.end()) {
            const std::vector<ctype> one{src};
            for (const auto* fn : m->second) {
                if (script_fn_viable(fn, one)) { return true; }
            }
        }
        for (auto base : c->decl->base_classes) {
            if (assign_operator_accepts(base, src, depth + 1)) { return true; }
        }
        return false;
    }

    // -------------------------------------------------------------- statements

    void walk_stmt(statement* s) {
        if (!s) { return; }
        mode_guard region(mode_stack_, s->check_mode_attr, /*scoped=*/false);
        switch (s->get_type()) {
            case node_type::expression_stmt:
                infer(static_cast<expression_stmt*>(s)->expression.get());
                break;
            case node_type::block_stmt: {
                auto* b = static_cast<block_stmt*>(s);
                mode_guard block(mode_stack_, leading_mode(b->declarations), /*scoped=*/true);
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
                const ctype container = infer(f->container.get());
                scopes_.emplace_back();
                var_entry entry;
                entry.type = ctype_from_type_info(f->element_type.get());
                entry.dynamic = !f->element_type || f->element_type->base_type == script_value_type::jai_any_type ||
                                f->is_reference;
                entry.is_reference = f->is_reference;
                entry.map_pair = container.kind == ctype::map_k;
                entry.is_handle = f->element_type && f->element_type->base_type == script_value_type::jai_shared_ptr_type;
                entry.declared = f->element_type.get();
                entry.display = f->element_type ? f->element_type->canonical_name() : "auto";
                entry.line = f->location.line;
                entry.column = f->location.column;
                warn_shadow("range-for variable", f->variable_name, f->location);
                scopes_.back().emplace(f->variable_name, std::move(entry));
                walk_stmt(f->body.get());
                scopes_.pop_back();
                break;
            }
            case node_type::parallel_for_stmt: {
                auto* f = static_cast<parallel_for_stmt*>(s);
                infer(f->container.get());
                scopes_.emplace_back();
                var_entry entry;
                entry.type = ctype_from_type_info(f->loop_param.type.get());
                entry.dynamic = true;   // by-ref element: dynamic like ref range-for
                entry.is_reference = f->loop_param.is_reference;
                entry.declared = f->loop_param.type.get();
                entry.display = f->loop_param.type ? f->loop_param.type->canonical_name() : "auto";
                entry.line = f->location.line;
                entry.column = f->location.column;
                warn_shadow("parallel_for variable", f->loop_param.name, f->location);
                scopes_.back().emplace(std::string_view(f->loop_param.name), std::move(entry));
                for (const auto& extra : f->extra_params) {
                    var_entry more;
                    more.type = ctype_from_type_info(extra.type.get());
                    more.dynamic = !extra.type || extra.type->base_type == script_value_type::jai_any_type || extra.is_reference;
                    more.is_reference = extra.is_reference;
                    more.declared = extra.type.get();
                    more.display = extra.type ? extra.type->canonical_name() : "auto";
                    more.line = f->location.line;
                    more.column = f->location.column;
                    warn_shadow("parallel_for variable", extra.name, f->location);
                    scopes_.back().emplace(std::string_view(extra.name), std::move(more));
                }
                for (const auto& c : f->extra_containers) { infer(c.get()); }
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
                    warn_shadow("catch variable", t->catch_var, t->location);
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
        uint8_t proof = proof_none;
        check_return_impl(r, proof);
        stamp(r->static_return_proof, proof, proof_none);
        if (proof != proof_none && stamping()) { ++counts_.returns; }
    }

	void check_return_impl(return_stmt* r, uint8_t& proof) {
		ctype value = r->value ? infer(r->value.get()) : ctype::make(ctype::null_k);
		if (fn_stack_.empty()) { return; }
		const fn_ctx& fn = fn_stack_.back();
		if (r->value && value.const_key && (r->binds_reference || handle_type(fn.ret))) {
			map_key_alias(r->location, r->value.get());
		}
		if (fn.is_coroutine || !fn.ret) { return; }   // coroutine returns/yields + auto: lenient
		ctype ret = ctype_from_type_info(fn.ret);
		if (ret.is_unknown()) { return; }
		if (r->value && !r->binds_reference && ret.kind != ctype::null_k && same_type(ret, value)) { proof = proof_same_type; }
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
				 std::string(fn.name) + "' declared to return '" + fn.ret->canonical_name() + "'" + assignment_detail(value, ret),
				 fn.line, fn.column);
		}
	}

    // -------------------------------------------------------------- declarations

    void walk_decl(declaration* d) {
        if (!d) { return; }
        mode_guard region(mode_stack_, d->check_mode_attr, /*scoped=*/false);
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
                    warn_shadow("local", name, dd->location);
                    scopes_.back().emplace(name, std::move(entry));
                }
                break;
            }
            case node_type::extern_decl: {
                auto* x = static_cast<extern_decl*>(d);
                var_entry entry;
                entry.line = x->location.line;
                entry.column = x->location.column;
                if (x->is_function) {
                    entry.type = ctype::make(ctype::function_k);
                    entry.dynamic = true;
                } else if (x->type) {
                    entry.declared = x->type.get();
                    entry.display = x->type->canonical_name();
                    entry.type = ctype_from_type_info(x->type.get());
                    entry.dynamic = entry.type.is_unknown();
                } else {
                    entry.dynamic = true;
                }
                scopes_.back().emplace(x->name, std::move(entry));
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
		bool is_handle_decl = handle_type(decl->type.get());
		if (has_init) {
			init = infer(decl->initializer.get());
			if (init.const_key && (is_reference_decl || is_handle_decl)) {
				map_key_alias(decl->location, decl->initializer.get());
			}
		}

		var_entry entry;
		entry.line = decl->location.line;
		entry.column = decl->location.column;
		// `var p = kv;` / `auto& p = kv;`: the copy or alias of a map pair still holds the key
		if (has_init && decl->initializer->get_type() == node_type::identifier_expr) {
			const var_entry* src = find_local(static_cast<const identifier_expr*>(decl->initializer.get())->name);
			entry.map_pair = src && src->map_pair;
		}
		// shared_ptr<auto>: pointee INFERRED from the initializer's class then enforced
		// like the explicit spelling; null/missing initializer has nothing to infer
		// (runtime errors identically - Dev ruling 2026-07)
		if (decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type &&
			!decl->type->element_type()) {
			if (!has_init || init.kind == ctype::null_k) {
				diag(diag_level::error, decl->location, "cannot infer shared_ptr<auto> pointee for '" +
					 std::string(decl->name) + "' without a non-null initializer (use var, or an explicit shared_ptr<T>)");
				entry.dynamic = true;   // recovery: no cascades
			} else if (!init.is_unknown() && init.kind != ctype::object_k) {
				// runtime: "Cannot initialize shared_ptr with this type"
				diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
					 "' declared 'shared_ptr<auto>' with '" + ctype_name(init) + "'");
				entry.dynamic = true;   // recovery: no cascades
			} else {
				entry.type = init;      // object(cls), or unknown -> lenient
				entry.dynamic = init.is_unknown();
				entry.is_handle = true;
				entry.display = "shared_ptr<" + ctype_name(init) + ">";
			}
			warn_shadow("local", decl->name, decl->location);
			scopes_.back().emplace(decl->name, std::move(entry));
			stamp(decl->static_init_proof, proof_none, proof_none);
			return;
		}
		uint8_t init_proof = proof_none;
		if (decl->function_spelling) {
			entry.type = ctype::make(ctype::function_k);
			entry.display = "function";
			entry.dynamic = false;
			if (has_init && !assignable(init, entry.type)) {
				diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
					 "' declared 'function' with '" + ctype_name(init) + "'");
			}
		} else if (decl->type) {
			entry.declared = decl->type.get();
			entry.display = decl->type->canonical_name();
			entry.type = ctype_from_type_info(decl->type.get());
			entry.dynamic = decl->type->base_type == script_value_type::jai_any_type || is_reference_decl;
			if (is_reference_decl && has_init && decl->initializer->get_type() == node_type::identifier_expr &&
			    !init.tentative && init.ti && (init.kind == ctype::array_k || init.kind == ctype::map_k)) {
				// `var& pix = PIX;` with PIX a declared typed container: the alias's element reads may prove
				entry.alias = init;
				entry.alias.tentative = true;
				entry.alias.alias_proof = true;
			}
			entry.is_reference = is_reference_decl;
			entry.is_handle = decl->type->base_type == script_value_type::jai_shared_ptr_type;
			const bool plain_kind = decl->type->base_type != script_value_type::jai_shared_ptr_type &&
									decl->type->base_type != script_value_type::jai_weak_ptr_type &&
									decl->type->base_type != script_value_type::jai_reference_type &&
									decl->type->base_type != script_value_type::jai_any_type;
			if (has_init && !entry.dynamic && !is_reference_decl && plain_kind && same_type(entry.type, init)) {
				init_proof = proof_same_type;
			}
			ctype binding_source = init;
			if (is_reference_decl && decl->initializer &&
			    decl->initializer->get_type() == node_type::identifier_expr) {
				const auto* id = static_cast<const identifier_expr*>(decl->initializer.get());
				if (const auto* source = find_local(id->name); source && source->is_reference && source->declared) {
					binding_source = ctype_from_type_info(source->declared);
				}
			}
			const bool init_ok = is_reference_decl
				? reference_type_compatible(binding_source, entry.type)
				: assignable(init, entry.type);
			if (has_init && (!entry.dynamic || is_reference_decl) && !init_ok) {
				diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
				     "' declared '" + entry.display + "' with '" + ctype_name(binding_source) + "'" + assignment_detail(binding_source, entry.type));
				// Recovery keeps the declared type; reference use remains conservative.
			}
		} else if (has_init) {
			// auto with initializer: infer then enforce as that type. null carries no type:
			// the runtime locks at the first non-null store, so a null init stays dynamic.
			entry.type = init;
			entry.dynamic = init.is_unknown() || init.kind == ctype::null_k;
			entry.display = ctype_name(init);
			// The one place inference is VALUE-dependent and silent: mixed-branch
			// ternaries lock auto to whichever branch runtime takes (int-locked then
			// truncates later float assignments; other-shape mixes turn later
			// assignments into runtime type mismatches on only one path).
			if (init.is_unknown() && last_mixed_ternary_.node == decl->initializer.get()) {
				diag(diag_level::warning, decl->location, "auto '" + std::string(decl->name) +
					 "' is initialized from a ternary with branch types '" +
					 ctype_name(last_mixed_ternary_.then_type) + "' and '" +
					 ctype_name(last_mixed_ternary_.else_type) +
					 "' - the locked type follows the branch taken at runtime; declare the type "
					 "explicitly, use 'var', or make both branches the same type");
			}
		} else {
			// auto without initializer: locked by the first runtime assignment — the
			// checker can't see branch order, so it stays dynamic (documented leniency)
			entry.dynamic = true;
		}
		stamp(decl->static_init_proof, init_proof, proof_none);
		if (init_proof != proof_none && stamping()) { ++counts_.decls; }
		warn_shadow("local", decl->name, decl->location);
		scopes_.back().emplace(decl->name, std::move(entry));
	}

    void walk_function(const function_decl* fn) {
        // Named functions capture nothing: lambda capture contexts must not leak in.
        std::vector<capture_ctx> saved_captures;
        saved_captures.swap(capture_stack_);
        scopes_.emplace_back();
        fn_ctx ctx{fn->return_type.get(), fn->is_coroutine, fn->name,
                   fn->location.line, fn->location.column};
        ctx.sees_fields = class_ctx_ != nullptr;
        ctx.is_static_method = fn->is_static;
        // Constructor params shadowing the field they initialize (C(int hp) { this.hp = hp; })
        // is the established idiom - no field-shadow warnings on ctor params.
        bool is_ctor = class_ctx_ && fn->name == class_ctx_name_;
        fn_stack_.push_back(std::move(ctx));
        mode_guard enclosing(mode_stack_, scoped_region_mode(), /*scoped=*/true, /*always=*/true);
        define_params(fn->parameters, fn->location, /*warn_field_shadows=*/!is_ctor);
        fn_stack_.back().in_ctor_initializers = true;
        for (const auto& init : fn->initializers) {
            for (const auto& arg : init.arguments) { infer(arg.get()); }
        }
        fn_stack_.back().in_ctor_initializers = false;
        if (fn->body) {
            mode_guard body(mode_stack_, leading_mode(fn->body->declarations), /*scoped=*/true);
            for (auto& d : fn->body->declarations) { walk_decl(d.get()); }
        }
        check_falls_off_end(fn);
        fn_stack_.pop_back();
        scopes_.pop_back();
        capture_stack_.swap(saved_captures);
    }

    void define_params(const std::vector<parameter>& params, const source_location& fn_loc,
                       bool warn_field_shadows = true) {
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
            warn_shadow("parameter", p.name, fn_loc, warn_field_shadows);
            var_entry entry;
            entry.type = pt;
            entry.dynamic = pt.is_unknown() || p.is_reference;
            entry.is_reference = p.is_reference;
            entry.is_handle = p.type && p.type->base_type == script_value_type::jai_shared_ptr_type;
            entry.declared = p.type.get();
            entry.display = p.type ? p.type->canonical_name() : "auto";
            entry.line = fn_loc.line;
            entry.column = fn_loc.column;
            scopes_.back().emplace(std::string_view(p.name), std::move(entry));
        }
    }

    // Member-name / global-name collision (same source unit only - engine-registered
    // names like to_string stay quiet: shadowing them is the established
    // print-integration idiom). Warning severity like every shadowing diagnostic:
    // bare 'name' in this class's methods resolves to the MEMBER (C++ order),
    // ::name is the spelling that still reaches the global.
    void warn_member_shadows_global(std::string_view kind, std::string_view name,
                                    std::string_view cls, const source_location& loc) {
        if (global_var_names_.count(name) == 0 && functions_.count(name) == 0) { return; }
        diag(diag_level::warning, loc,
             std::string(kind) + " '" + std::string(name) + "' of class '" + std::string(cls) +
             "' shadows the global '" + std::string(name) + "' - bare '" + std::string(name) +
             "' in methods means the member; spell ::" + std::string(name) + " for the global");
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
				warn_member_shadows_global(f->is_static ? "static field" : "field",
										   f->name, cd->name, f->location);
				if (f->initializer) {
					ctype init = infer(f->initializer.get());
					if (f->type || f->function_spelling) {
						ctype ft = declared_variable_type(f);
						if (!ft.is_unknown() && !assignable(init, ft)) {
							diag(diag_level::error, f->location, "cannot initialize field '" +
								 std::string(f->name) + "' declared '" + declared_variable_type_name(f) +
								 "' with '" + ctype_name(init) + "'" + assignment_detail(init, ft));
						}
					}
				}
			} else if (m.declaration->get_type() == node_type::function_decl) {
				auto* fn = static_cast<function_decl*>(m.declaration.get());
				if (fn->name != cd->name) {   // ctors carry the class name by construction
					warn_member_shadows_global(fn->is_static ? "static method" : "method",
											   fn->name, cd->name, fn->location);
				}
				// A class method that shadows a builtin shared_ptr handle method WINS over
				// the builtin (Dev ruling 2026-07) - point at the now-unreachable builtin.
				for (std::string_view b : k_shadowable_handle_method_names) {
					if (fn->name == b) {
						std::string m2(b);
						diag(diag_level::warning, fn->location,
							 "class method '" + m2 + "' shadows the builtin shared_ptr " + m2 +
							 "() - the builtin is unreachable on " + std::string(cd->name) +
							 " instances; rename the class method to reach it");
						break;
					}
				}
				walk_function(fn);
			} else {
				walk_decl(m.declaration.get());
			}
		}
		class_ctx_ = prev_class;
		class_ctx_name_ = prev_name;
	}

    void walk_lambda(lambda_expr* l) {
        for (const auto& cap : l->captures) {
            if (cap.name == "this") { continue; }   // capture-list entries are not identifier uses
            if (find_local(cap.name)) { continue; }
            if (class_ctx_) {
                member_hit hit = lookup_member(class_ctx_name_, cap.name, false);
                if (hit.found || hit.unresolved) { continue; }
            }
            if (functions_.count(cap.name) || classes_.count(cap.name) || enums_.count(cap.name) ||
                all_names_.count(cap.name)) { continue; }
            std::string name(cap.name);
            if (extra_globals_.count(name) || engine_knows_name(name)) { continue; }
            undeclared(l->location, cap.name);
        }
        capture_stack_.push_back(capture_ctx{
            scopes_.size(),
            l->default_capture == lambda_expr::capture_default::none && l->captures.empty(),
            false, l->location});

        scopes_.emplace_back();
        fn_ctx ctx{l->return_type.get(), /*is_coroutine=*/false, "lambda",
                   l->location.line, l->location.column};
        // Only a [this] capture reaches fields from a lambda body (bare [=]/[&] do not)
        for (const auto& cap : l->captures) {
            if (cap.name == "this") { ctx.sees_fields = class_ctx_ != nullptr; break; }
        }
        fn_stack_.push_back(std::move(ctx));
        mode_guard enclosing(mode_stack_, scoped_region_mode(), /*scoped=*/true, /*always=*/true);
        define_params(l->parameters, l->location);
        walk_stmt(l->body.get());
        fn_stack_.pop_back();
        scopes_.pop_back();
        capture_stack_.pop_back();
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
        case node_type::extern_decl:
            all_names_.insert(static_cast<const extern_decl*>(node)->name);
            break;
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
            has_dynamic_includes_ = true;   // nested: beyond the closure's top-level walk
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
        case node_type::parallel_for_stmt: {
            auto* f = static_cast<const parallel_for_stmt*>(node);
            all_names_.insert(std::string_view(f->loop_param.name));
            for (const auto& extra : f->extra_params) { all_names_.insert(std::string_view(extra.name)); }
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
            const bool is_extend = c->callee->get_type() == node_type::identifier_expr &&
                                   static_cast<const identifier_expr*>(c->callee.get())->name == "extend";
            for (const auto& arg : c->arguments) {
                collect_names_expr(arg.get());
                if (!is_extend || !arg) { continue; }
                if (arg->get_type() == node_type::identifier_expr) {
                    extended_names_.insert(static_cast<const identifier_expr*>(arg.get())->name);
                } else if (arg->get_type() == node_type::member_expr) {
                    extended_members_.insert(static_cast<const member_expr*>(arg.get())->member);
                }
            }
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
        case node_type::cast_expr:
            collect_names_expr(static_cast<const cast_expr*>(e)->operand.get());
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
                              const std::vector<std::string>* extra_globals, check_options opts) {
    return static_checker(eng, decls, extra_globals, opts).run();
}

// KEEP-PARALLEL with static_checker::collect_scope_decls (same categories, flat spelling).
void collect_unit_names(const std::vector<declaration_ptr>& decls, std::unordered_set<std::string>& out) {
    for (const auto& d : decls) {
        if (!d) { continue; }
        switch (d->get_type()) {
            case node_type::function_decl:
                out.emplace(static_cast<const function_decl*>(d.get())->name);
                break;
            case node_type::variable_decl:
                out.emplace(static_cast<const variable_decl*>(d.get())->name);
                break;
            case node_type::extern_decl:
                out.emplace(static_cast<const extern_decl*>(d.get())->name);
                break;
            case node_type::destructuring_decl:
                for (const auto& [name, _] : static_cast<const destructuring_decl*>(d.get())->names) { out.emplace(name); }
                break;
            case node_type::class_decl:
                out.emplace(static_cast<const class_decl*>(d.get())->name);
                break;
            case node_type::enum_decl: {
                auto* e = static_cast<const enum_decl*>(d.get());
                out.emplace(e->name);
                for (const auto& [vname, _] : e->values) { out.emplace(vname); }
                break;
            }
            case node_type::namespace_decl: {
                auto* n = static_cast<const namespace_decl*>(d.get());
                out.emplace(n->name);
                for (size_t pos = n->name.find("::"); pos != std::string_view::npos; pos = n->name.find("::", pos + 2)) {
                    out.emplace(n->name.substr(0, pos));
                }
                collect_unit_names(n->declarations, out);
                break;
            }
            default:
                break;
        }
    }
}

void collect_unit_classes(const std::vector<declaration_ptr>& decls, program_class_map& out) {
    for (const auto& d : decls) {
        if (!d) { continue; }
        if (d->get_type() == node_type::class_decl) {
            auto cd = std::static_pointer_cast<class_decl>(d);
            out[std::string(cd->name)] = std::move(cd);
        } else if (d->get_type() == node_type::namespace_decl) {
            collect_unit_classes(static_cast<const namespace_decl*>(d.get())->declarations, out);
        }
    }
}

} // namespace detail
} // namespace jai
