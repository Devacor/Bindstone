// Static type checker (docs/site/embedding/06-static-checking.html). Backend-neutral AST walk,
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
#include <jaiscript/detail/inline_variable.hpp>
#include <jaiscript/detail/ast.hpp>
#include <jaiscript/detail/method_value.hpp>
#include <jaiscript/detail/construction.hpp>
#include <jaiscript/detail/null_member_diagnostic.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_definition.hpp>
#include <jaiscript/core/class_registry.hpp>
#include <jaiscript/core/script_class.hpp>
#include <jaiscript/core/script_namespace.hpp>
#include <jaiscript/detail/environment.hpp>
#include <jaiscript/detail/builtin_methods.hpp>
#include <jaiscript/detail/builtin_api_arities.hpp>
#include <jaiscript/detail/math_intrinsics.hpp>
#include <jaiscript/detail/numeric_conversion.hpp>
#include <jaiscript/detail/parallel_transform.hpp>
#include <jaiscript/detail/ref_param_writes.hpp>
#include <jaiscript/detail/const_rules.hpp>
#include <jaiscript/detail/lvalue_shape.hpp>
#include <cctype>
#include <jaiscript/detail/return_flow.hpp>
#include <jaiscript/detail/ref_lvalue.hpp>

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
        enum_ref_k,    // an enum name (member access checks its values)
        utf8_k,
        math_k,        // a native math kind; cls names it ("math::vec3")
        coroutine_k    // the handle a coroutine call returns
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
		case ctype::utf8_k: return "utf8_string";
		case ctype::char_k: return "char";
		case ctype::bool_k: return "bool";
		case ctype::array_k: return t.ti ? t.ti->canonical_name() : "array";
		case ctype::map_k: return t.ti ? t.ti->canonical_name() : "map";
		case ctype::function_k: return "function";
		case ctype::weak_k: return t.ti ? t.ti->canonical_name() : "weak_ptr";
		case ctype::object_k:
			if (t.ti && t.ti->base_type == script_value_type::jai_shared_ptr_type) { return t.ti->canonical_name(); }
			return t.cls.empty() ? "object" : std::string(t.cls);
		case ctype::class_ref_k: return "class " + std::string(t.cls);
		case ctype::enum_ref_k: return "enum " + std::string(t.cls);
		case ctype::math_k: return std::string(t.cls);
		case ctype::coroutine_k: return "coroutine_handle";
		default: return "unknown";
	}
}

ctype math_ctype(script_value_type kind) {
	return ctype{ctype::math_k, math_type_name(kind)};
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
		case script_value_type::jai_utf8_string_type: t = ctype::make(ctype::utf8_k); break;
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
		case script_value_type::jai_vec2_type:
		case script_value_type::jai_vec3_type:
		case script_value_type::jai_vec4_type:
		case script_value_type::jai_color_type:
		case script_value_type::jai_ivec2_type:
		case script_value_type::jai_ivec3_type:
			t = math_ctype(ti->base_type);
			break;
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
            if (l.kind == ctype::utf8_k || r.kind == ctype::utf8_k) {
                return ctype::make(ctype::utf8_k);   // a utf8_string side keeps the kind
            }
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
        case token_type::ampersand_equal: return token_type::ampersand;
        case token_type::pipe_equal: return token_type::pipe;
        case token_type::caret_equal: return token_type::caret;
        case token_type::left_shift_equal: return token_type::left_shift;
        case token_type::right_shift_equal: return token_type::right_shift;
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
                instance_names_.insert(n);
            }
        }
        program_names_ = opts.program_names;
        program_classes_ = opts.program_classes;
        program_functions_ = opts.program_functions;
        program_globals_ = opts.program_globals;
        program_enums_ = opts.program_enums;
        deferred_out_ = opts.deferred_classes;
        observer_ = opts.observer;
    }

    check_report run() {
        collect_top_level();
        scopes_.emplace_back();
        {
            mode_guard unit(mode_stack_, leading_mode(decls_), /*scoped=*/true);
            warn_duplicate_top_level_functions();   // under the unit's [[warn_mode]] / [[strict_mode]]
            for (const auto& d : decls_) {
                walk_decl(d.get());
            }
        }
        scopes_.pop_back();

        if (stats_out_) { counts_.deferred = deferred_ ? 1 : 0; *stats_out_ = counts_; }
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
        std::vector<const function_decl*> private_ctors;   // the ctors declared under private: / protected:
        std::unordered_map<std::string_view, uint8_t> nonpublic;   // private: / protected: members (member_hit::access)
        // Declared by another closure unit (program table): its locations belong to that unit's
        // source, and its member types seed proofs only once the engine has registered it.
        bool foreign = false;
        bool provable = true;
    };

    engine& eng_;
    const std::vector<declaration_ptr>& decls_;

    // Proof pass (internal/type_proofs_design.md): independent of diagnostics - a stamp never
    // depends on a diag() having fired, and diag() is a no-op when diagnostics are off.
    using proof_mode = check_options::proofs_t;
    proof_mode proofs_ = proof_mode::stamp;
    bool collect_diagnostics_ = true;
    proof_stats* stats_out_ = nullptr;
    proof_stats counts_{};
    check_observer* observer_ = nullptr;

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
    const program_function_map* program_functions_ = nullptr;
    const program_global_map* program_globals_ = nullptr;
    const program_enum_map* program_enums_ = nullptr;
    bool deferred_ = false;   // a program-table class met before the engine defined it: the unit's proofs re-stamp after the next import
    std::vector<std::string>* deferred_out_ = nullptr;   // their names, for the engine's re-stamp test
    std::unordered_map<std::string_view, sclass> program_sclasses_;
    std::unordered_map<std::string_view, std::vector<const function_decl*>> functions_;
    std::unordered_map<std::string_view, const enum_decl*> enums_;
    std::unordered_set<std::string_view> namespaces_;
    std::unordered_map<std::string, const variable_decl*> namespace_vars_;   // "path::name" -> its declaration (null: two spellings)
    // Every name declared ANYWHERE in the script (any depth). Flow-insensitive safety
    // net: execution order can make later declarations visible (nested functions,
    // conditional paths), so "undeclared" only fires for names absent from this set
    // AND the engine surface — typos, not ordering.
    std::unordered_set<std::string_view> all_names_;
    // names an assignment expression or a reference declaration targets, and each variable declaration's binary::layout(...)
    // initializer (null for any other initializer): a layout variable declared once and never assigned is statically known
    std::unordered_set<std::string_view> assigned_names_;
    std::unordered_map<std::string_view, std::vector<const expression*>> layout_inits_;
    std::unordered_set<std::string> extra_globals_;
    const std::unordered_set<std::string>* program_names_ = nullptr;   // the program's names, consulted beside extra_globals_ (never copied per check)
    bool extra_global(const std::string& name) const { return extra_globals_.count(name) || (program_names_ && program_names_->count(name)); }
    std::unordered_set<std::string> instance_names_;   // the host's per-call instance variables (a subset of extra_globals_)
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
        ctype::kind_t builtin_alias = ctype::unknown;   // a range-for reference over a typed array's elements: the element kind, for builtin receivers only
        const type_info* declared = nullptr;   // for message text; null for auto-inferred
        std::string display;              // declared-type text for messages
        size_t line = 0, column = 0;
        const lambda_expr* lambda = nullptr;   // a frame-slot local declared with a lambda literal (its calls' graft declaration)
        const enum_decl* enum_def = nullptr;   // an enum declared in a function or block: local to that scope
        bool is_const = false;            // const T x / const T& p / a const reference (auto& over a const) (internal/const.md)
    };
    std::vector<std::unordered_map<std::string_view, var_entry>> scopes_;
    std::vector<size_t> namespace_scopes_;   // indices into scopes_ that are namespace bodies (their enums are the namespace's)

    const enum_decl* find_enum(std::string_view name) {
        for (size_t i = scopes_.size(); i-- > 0;) {
            auto found = scopes_[i].find(name);
            if (found != scopes_[i].end() && found->second.enum_def) { return found->second.enum_def; }
        }
        auto it = enums_.find(name);
        return it != enums_.end() ? it->second : nullptr;
    }

    // An enum declared in a function or a block is local to that scope at run time (both backends): when the program
    // scope already names it, the declaration leaves that enum unchanged - say so.
    void warn_scoped_enum(const enum_decl* e) {
        const std::string name(e->name);
        auto top = enums_.find(e->name);
        if (top == enums_.end() && !extra_global(name) && !eng_.has_variable(name)) { return; }
        std::string where = "a block";
        if (!fn_stack_.empty()) {
            where = fn_stack_.back().name.empty() || fn_stack_.back().name == "<lambda>" ? std::string("a lambda") : "function '" + std::string(fn_stack_.back().name) + "'";
        }
        diag(diag_level::warning, e->location, "enum '" + name + "' declared in " + where +
             " is local to it: the program-scope '" + name + "' keeps its values",
             top != enums_.end() ? top->second->location.line : 0, top != enums_.end() ? top->second->location.column : 0);
    }

    // An include inside a function body is the function's own text, so a function it declares is a nested one: when the
    // program scope names it too, the include hides that function for the body and replaces nothing - the likely mistake
    void warn_spliced_function(const function_decl* fn) {
        if (fn_stack_.empty() || !fn_stack_.back().file || *fn_stack_.back().file == fn->location.filename) { return; }
        const std::string name(fn->name);
        auto top = functions_.find(fn->name);
        const bool program_has = top != functions_.end() || (program_functions_ && program_functions_->count(name)) || eng_.has_variable(name);
        if (!program_has) { return; }
        const std::string where = fn_stack_.back().name == "lambda" ? std::string("a lambda") : "function '" + std::string(fn_stack_.back().name) + "'";
        // the listing renders this unit's text: the warning stands at the enclosing body and names the included file's place
        source_location at = fn->location;
        at.line = fn_stack_.back().line;
        at.column = fn_stack_.back().column;
        diag(diag_level::warning, at, fn->location.to_string() + ": function '" + name + "' from the include in " + where +
             " is local to it and hides the program-scope '" + name + "' there without replacing it; import the file to replace program-scope functions",
             top != functions_.end() && !top->second.empty() ? top->second.front()->location.line : 0,
             top != functions_.end() && !top->second.empty() ? top->second.front()->location.column : 0);
    }

    // Live lambda bodies: a name declared below base is the lambda's own; above it the capture list must admit it
    struct capture_ctx {
        size_t base = 0;
        const lambda_expr* lambda = nullptr;
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
        uint8_t inferred_return = 0;      // auto: the kind every return so far certainly yields (1 int, 2 float, 3 bool)
        bool inferred_mixed = false;      // auto: a return of another kind, of an uncertain kind, or without a value
        const std::string* file = nullptr;   // the file the body is written in (a declaration from another one was spliced by an include)
    };
    std::vector<fn_ctx> fn_stack_;
    sclass* class_ctx_ = nullptr;
    std::string_view class_ctx_name_{};
    enum class cdtor_ctx : uint8_t { none, construction, destruction };
    cdtor_ctx cdtor_ctx_ = cdtor_ctx::none;   // code the class's constructor, field initializers or destructor run (not a lambda they create)
    std::string_view ov_class_{};   // the class whose methods infer_call_impl resolved the call to (empty: not a method call)

    static ref_write_callee ref_write_callee_of(const function_decl* f) {
        return ref_write_callee{&f->parameters, f->body.get(), f->name, false, f->is_coroutine};
    }
    // detail/ref_param_writes.hpp with the checker's names: this unit's function, then the program's, then the engine's;
    // an overloaded name is not seen (the backends' global lookup cannot see an overload set either)
    ref_param_write ref_writes(const function_decl* fn, size_t i) {
        ref_write_analysis analysis([this](const identifier_expr& id, std::vector<ref_write_callee>& out) {
            if (auto it = functions_.find(id.name); it != functions_.end()) {
                if (it->second.size() != 1) { return false; }
                out.push_back(ref_write_callee_of(it->second.front()));
                return true;
            }
            if (program_functions_) {
                if (auto pit = program_functions_->find(std::string(id.name)); pit != program_functions_->end()) {
                    if (pit->second.size() != 1 || pit->second.begin()->second.size() != 1) { return false; }
                    out.push_back(ref_write_callee_of(pit->second.begin()->second.front().get()));
                    return true;
                }
            }
            return resolve_global_ref_write_callee(eng_, id, out);
        });
        return analysis.analyze(ref_write_callee_of(fn), i);
    }
    // every method named `name` a class derived from `cls` declares (a call through a `cls` receiver may reach any)
    std::vector<const function_decl*> overrides_of(std::string_view cls, std::string_view name) {
        std::vector<const function_decl*> out;
        for (const auto* table : {&classes_, &program_sclasses_}) {
            for (const auto& [cname, sc] : *table) {
                if (cname == cls || class_relation(cname, cls) != 1) { continue; }
                if (auto m = sc.methods.find(name); m != sc.methods.end()) {
                    for (const auto* f : m->second) { out.push_back(f); }
                }
            }
        }
        return out;
    }

    // C++ [class.cdtor]: a constructor, field initializer or destructor reaches its own class's members only, so a call there to a
    // name only a derived class declares is the refusal the backends raise
    bool report_cdtor_call(std::string_view name, const source_location& loc) {
        if (cdtor_ctx_ == cdtor_ctx::none || !class_ctx_ || overrides_of(class_ctx_name_, name).empty()) { return false; }
        diag(diag_level::error, loc, format_error_message(cdtor_ctx_ == cdtor_ctx::construction ? detail::k_call_under_construction
                                                                                                 : detail::k_call_under_destruction,
                                                          name, class_ctx_name_));
        return true;
    }

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

    // One entry per signature: a same-signature redefinition (the error the redefinition pass reports, or an `override` that
    // replaces on purpose) takes the earlier one's place, so calls see one candidate
    static void add_overload(std::vector<const function_decl*>& set, const function_decl* fn) {
        for (auto& held : set) {
            if (detail::same_signature(held->parameters, fn->parameters)) { held = fn; return; }
        }
        set.push_back(fn);
    }

    // THE class model - same-unit classes and program-table classes are built by this one walk.
    static void model_class(const class_decl* cd, sclass& c) {
        c = sclass{};   // redefinition: last one wins, don't merge members
        c.decl = cd;
        for (const auto& m : cd->members) {
            if (!m.declaration) { continue; }
            const uint8_t access = m.visibility == class_decl::Private ? 1 : m.visibility == class_decl::Protected ? 2 : 0;
            if (m.declaration->get_type() == node_type::variable_decl) {
                auto* f = static_cast<const variable_decl*>(m.declaration.get());
                (f->is_static ? c.static_fields : c.fields).emplace(f->name, f);
                if (access) { c.nonpublic[f->name] = access; }
            } else if (m.declaration->get_type() == node_type::function_decl) {
                auto* fn = static_cast<const function_decl*>(m.declaration.get());
                if (access && fn->name != cd->name) { c.nonpublic[fn->name] = access; }
                if (fn->name == cd->name) {
                    add_overload(c.ctors, fn);
                    if (access) { c.private_ctors.push_back(fn); }
                } else if (fn->is_static) {
                    add_overload(c.static_methods[fn->name], fn);
                } else {
                    add_overload(c.methods[fn->name], fn);
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
        if (!c.provable) { deferred_ = true; if (deferred_out_) { deferred_out_->push_back(std::string(cd->name)); } }
        if (unit_redefines(cd)) { open_classes_.insert(cd->name); }
        return &c;
    }

    // The proof pass alone, stamping or clearing (never a diagnostics walk), types a top-level global exactly one other unit declares
    // once with a spelled non-reference type; a class-typed one needs its class defined, or the unit's proofs wait for the next import (deferred_)
    const variable_decl* program_global(const std::string& name) {
        if (!semantic_walk() || !program_globals_) { return nullptr; }
        auto it = program_globals_->find(name);
        if (it == program_globals_->end() || it->second.size() != 1 || it->second.begin()->second.size() != 1) { return nullptr; }
        const variable_decl* g = it->second.begin()->second.front().get();
        if (!g || !g->type || g->type->is_reference() || g->function_spelling || g->is_static) { return nullptr; }
        return g;
    }
    // The declared type a store to a program-scope name must respect when this unit does not declare it: the engine's cell
    // once a unit has run (typed off its tag), else the one spelled declaration another unit of the program makes
    ctype program_scope_store_type(const std::string& name) {
        if (instance_names_.count(name)) { return {}; }
        if (eng_.has_variable(name)) { return engine_global_type(name); }
        if (!program_globals_) { return {}; }
        auto it = program_globals_->find(name);
        if (it == program_globals_->end() || it->second.size() != 1 || it->second.begin()->second.size() != 1) { return {}; }
        const variable_decl* g = it->second.begin()->second.front().get();
        if (!g || !g->type || g->type->is_reference() || g->function_spelling || g->is_static) { return {}; }
        ctype t = ctype_from_type_info(g->type.get());
        if (t.kind == ctype::object_k && !find_sclass(t.cls) && !engine_class(t.cls)) { return {}; }
        t.tentative = true;
        return t;
    }
    // A bare name in a method resolves locals, then members, then program-scope (and namespace) variables: a member of the
    // class being walked hides a program-scope name, as at run time
    bool member_hides_program_name(std::string_view name, size_t scope_index) {
        if (!class_ctx_) { return false; }
        const bool program_scope = scope_index == 0 ||
            std::find(namespace_scopes_.begin(), namespace_scopes_.end(), scope_index) != namespace_scopes_.end();
        return program_scope && lookup_member(class_ctx_name_, name, /*want_static=*/false).found;
    }
    // ::name reaches the unit's program scope only: locals and fields never shadow it
    var_entry* find_named(const identifier_expr* id, size_t* scope_index = nullptr) {
        if (!id->global_qualified) {
            size_t at = 0;
            var_entry* local = find_local(id->name, &at);
            if (local && member_hides_program_name(id->name, at)) { return nullptr; }
            if (scope_index) { *scope_index = at; }
            return local;
        }
        if (scope_index) { *scope_index = 0; }
        if (scopes_.empty()) { return nullptr; }
        auto found = scopes_[0].find(id->name);
        return found != scopes_[0].end() ? &found->second : nullptr;
    }
    ctype program_global_type(const variable_decl* g) {
        ctype t = ctype_from_type_info(g->type.get());
        if (t.kind == ctype::object_k) { const sclass* c = find_sclass(t.cls); if (!c || !c->provable) { return {}; } }   // find_sclass noted a program class the engine lacks; a class outside the program types nothing
        return t;
    }

    void collect_scope_decls(const std::vector<declaration_ptr>& decls) {
        for (const auto& d : decls) {
            if (!d) { continue; }
            switch (d->get_type()) {
                case node_type::function_decl:
                    add_overload(functions_[static_cast<const function_decl*>(d.get())->name], static_cast<const function_decl*>(d.get()));
                    break;
                case node_type::variable_decl:
                    global_var_names_.insert(static_cast<const variable_decl*>(d.get())->name);
                    if (!collecting_namespace_ && static_cast<const variable_decl*>(d.get())->is_const) {
                        top_consts_[static_cast<const variable_decl*>(d.get())->name] = static_cast<const variable_decl*>(d.get());
                    }
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
                    const bool was_namespace = collecting_namespace_;
                    collecting_namespace_ = true;
                    collect_scope_decls(n->declarations);   // flattened: lenient by design
                    collecting_namespace_ = was_namespace;
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

    void collect_namespace_vars(const namespace_decl* n, const std::string& prefix) {
        const std::string path = prefix.empty() ? std::string(n->name) : prefix + "::" + std::string(n->name);
        for (const auto& d : n->declarations) {
            if (!d) { continue; }
            if (d->get_type() == node_type::variable_decl) {
                auto* v = static_cast<const variable_decl*>(d.get());
                auto [it, fresh] = namespace_vars_.try_emplace(path + "::" + std::string(v->name), v);
                if (!fresh && it->second && it->second->type.get() != v->type.get()) { it->second = nullptr; }   // a reopening that retypes it: lenient
            } else if (d->get_type() == node_type::namespace_decl) {
                collect_namespace_vars(static_cast<const namespace_decl*>(d.get()), path);
            }
        }
    }

    // The declaration a qualified store `ns::x` names when the unit declares that namespace variable
    const variable_decl* namespace_variable(const member_expr* m, std::string* qualified) const {
        std::vector<std::string_view> segments;
        const expression* e = m->object.get();
        while (e && e->get_type() == node_type::member_expr) {
            segments.push_back(static_cast<const member_expr*>(e)->member);
            e = static_cast<const member_expr*>(e)->object.get();
        }
        if (!e || e->get_type() != node_type::identifier_expr) { return nullptr; }
        std::string path(static_cast<const identifier_expr*>(e)->name);
        for (size_t i = segments.size(); i-- > 0;) { path += "::"; path += segments[i]; }
        if (!namespaces_.count(std::string_view(path))) { return nullptr; }
        auto it = namespace_vars_.find(path + "::" + std::string(m->member));
        if (it == namespace_vars_.end() || !it->second) { return nullptr; }
        *qualified = it->first;
        return it->second;
    }

    void collect_top_level() {
        collect_scope_decls(decls_);
        for (const auto& d : decls_) {
            if (d && d->get_type() == node_type::namespace_decl) { collect_namespace_vars(static_cast<const namespace_decl*>(d.get()), ""); }
        }
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
        size_t own_fields = 0;
        for (const auto& [id, _] : def->get_all_field_defaults()) {
            if (def->find_field_declaring_class(id) == def.get()) { ++own_fields; }
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

    // The general redefinition rule (internal/const.md 4.3): two definitions of one signature in the same unit
    // are an error - program-scope functions, methods, constructors and namespace functions, by their parameter types (T& and T
    // differ; const is not part of a signature). Declarations of other signatures are overloads. A
    // definition of the same signature from another unit is a redefinition, the hot-reload path: it replaces the earlier one, and
    // the checker warns when it sees the earlier definition.
    static std::string parameter_signature(const function_decl* fn) {
        std::string out = "(";
        for (size_t i = 0; i < fn->parameters.size(); ++i) {
            const parameter& p = fn->parameters[i];
            if (i) { out += ", "; }
            out += p.type ? p.type->canonical_name() : std::string("auto");
            if (p.is_reference && (!p.type || p.type->base_type != script_value_type::jai_reference_type)) { out += "&"; }
        }
        return out + ")";
    }

    // by_name: program-scope functions - a coroutine does not overload, so a function and a coroutine of one name replace each
    // other (a warning); one of the same signature is the redefinition error. An `override` declaration replaces on purpose; a
    // static and an instance method live in separate tables.
    void report_redefinitions(const std::vector<const function_decl*>& fns, bool by_name) {
        std::unordered_map<std::string, const function_decl*> first_seen;
        std::unordered_map<std::string_view, const function_decl*> first_name;
        for (const function_decl* fn : fns) {
            const std::string sig = parameter_signature(fn);
            const std::string key = std::string(fn->name) + (fn->is_static ? "#static" : "") + sig;
            auto [it, inserted] = first_seen.emplace(key, fn);
            if (inserted) {   // an earlier overload of the same name and table that differs only by reference
                for (const function_decl* earlier : fns) {
                    if (earlier == fn) { break; }
                    if (earlier->name != fn->name || earlier->is_static != fn->is_static ||
                        !detail::differs_only_by_reference(fn->parameters, earlier->parameters)) { continue; }
                    diag(diag_level::error, fn->location,
                         detail::reference_only_overload_text(fn->name, fn->parameters, earlier->parameters, earlier->location.line),
                         earlier->location.line, earlier->location.column);
                    break;
                }
            }
            if (!inserted && !fn->is_override) {
                diag(diag_level::error, fn->location,
                     "'" + std::string(fn->name) + "' is already defined" + (by_name ? std::string() : " with parameters " + sig) + " at " +
                     std::to_string(it->second->location.line) + ":" + std::to_string(it->second->location.column),
                     it->second->location.line, it->second->location.column);
                continue;
            }
            if (!by_name) { continue; }
            auto [nit, fresh] = first_name.emplace(fn->name, fn);
            if (!fresh && inserted && (fn->is_coroutine || nit->second->is_coroutine)) {
                diag(diag_level::warning, fn->location,
                     "duplicate definition of function '" + std::string(fn->name) +
                     "' replaces the earlier one (a coroutine does not overload)",
                     nit->second->location.line, nit->second->location.column);
            }
        }
    }

    // Every block of one namespace in the unit is one scope: a reopening's function collides like one in the first block
    void collect_namespace_functions(const namespace_decl* n, const std::string& prefix,
                                     std::map<std::string, std::vector<const function_decl*>>& by_namespace) {
        const std::string path = prefix.empty() ? std::string(n->name) : prefix + "::" + std::string(n->name);
        auto& fns = by_namespace[path];
        for (const auto& d : n->declarations) {
            if (!d) { continue; }
            if (d->get_type() == node_type::function_decl) { fns.push_back(static_cast<const function_decl*>(d.get())); }
            else if (d->get_type() == node_type::namespace_decl) { collect_namespace_functions(static_cast<const namespace_decl*>(d.get()), path, by_namespace); }
            else if (d->get_type() == node_type::class_decl) { report_class_redefinitions(static_cast<const class_decl*>(d.get())); }
        }
    }

    void report_class_redefinitions(const class_decl* cd) {
        std::vector<const function_decl*> fns;
        for (const auto& m : cd->members) {
            if (m.declaration && m.declaration->get_type() == node_type::function_decl) { fns.push_back(static_cast<const function_decl*>(m.declaration.get())); }
        }
        report_redefinitions(fns, /*by_name=*/false);
        report_override_markers(cd, fns);
    }

    // The base class whose chain declares the instance method `name` (script_class_definition::find_method's walk); a base
    // field's synthesized _get_/_set_ accessor counts only when `accessors` (an override may take it, a plain method is not
    // redefining it); found = false with certain = false when a base is out of the checker's sight
    struct base_method { bool found = false; bool certain = true; std::string owner; };
    base_method find_base_method(const class_decl* cd, std::string_view name, bool accessors, int depth = 0) {
        base_method result;
        if (depth > 32) { result.certain = false; return result; }
        for (auto base : cd->base_classes) {
            if (const sclass* b = find_sclass(base)) {
                if (b->methods.count(name)) { return {true, true, std::string(b->decl->name)}; }
                if (accessors && name.size() > 5 && (name.substr(0, 5) == "_get_" || name.substr(0, 5) == "_set_") && b->fields.count(name.substr(5))) {
                    return {true, true, std::string(b->decl->name)};
                }
                base_method deeper = find_base_method(b->decl, name, accessors, depth + 1);
                if (deeper.found) { return deeper; }
                result.certain = result.certain && deeper.certain;
                continue;
            }
            auto def = engine_class(base);
            if (!def) { result.certain = false; continue; }
            auto info = def->find_method(name);
            if (info.owner_class && (accessors || !info.owner_class->owns_auto_accessor(eng_.symbolize(name)))) {
                return {true, true, std::string(info.owner_class->get_name())};
            }
        }
        return result;
    }

    // A method redefining a base class method says 'override', and an 'override' redefines one (the run time raises both when
    // the class declaration runs)
    void report_override_markers(const class_decl* cd, const std::vector<const function_decl*>& fns) {
        for (const function_decl* fn : fns) {
            if (fn->is_static || fn->name == cd->name || (fn->name.size() > 1 && fn->name[0] == '~')) { continue; }
            const base_method base = cd->base_classes.empty() ? base_method{} : find_base_method(cd, fn->name, fn->is_override);
            std::string text;
            if (!fn->is_override && base.found) { text = detail::missing_override_text(fn->name, base.owner); }
            else if (fn->is_override && !base.found && base.certain) { text = detail::override_without_base_text(fn->name); }
            if (text.empty()) { continue; }
            text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
            diag(diag_level::error, fn->location, text);
        }
    }

    void warn_duplicate_top_level_functions() {
        std::vector<const function_decl*> fns;
        std::map<std::string, std::vector<const function_decl*>> namespace_fns;
        for (const auto& d : decls_) {
            if (!d) { continue; }
            if (d->get_type() == node_type::function_decl) { fns.push_back(static_cast<const function_decl*>(d.get())); }
            else if (d->get_type() == node_type::class_decl) { report_class_redefinitions(static_cast<const class_decl*>(d.get())); }
            else if (d->get_type() == node_type::namespace_decl) { collect_namespace_functions(static_cast<const namespace_decl*>(d.get()), {}, namespace_fns); }
        }
        for (const auto& [path, members] : namespace_fns) { report_redefinitions(members, /*by_name=*/false); }
        report_redefinitions(fns, /*by_name=*/true);
        if (!program_functions_) { return; }
        std::unordered_set<std::string> warned;
        for (const function_decl* fn : fns) {
            if (!warned.insert(std::string(fn->name) + parameter_signature(fn)).second) { continue; }
            auto pit = program_functions_->find(std::string(fn->name));
            if (pit == program_functions_->end()) { continue; }
            for (const auto& [unit, decls] : pit->second) {
                const function_decl* other = nullptr;   // the other unit's declaration of this signature, which this one replaces
                for (const auto& d : decls) {
                    if (d && parameter_signature(d.get()) == parameter_signature(fn)) { other = d.get(); break; }
                }
                if (!other || other->location.filename == fn->location.filename) { continue; }
                diag(diag_level::warning, fn->location, "'" + std::string(fn->name) + "' redefines the function defined in " +
                     other->location.filename + ":" + std::to_string(other->location.line));
                break;
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

    // A program-scope name (a global or a function), for the shadowing warnings; a bare name in a method resolves locals,
    // then members, then globals (docs/site/guide/07-classes.html), so a member hit is never degraded by one
    bool global_tier_name(std::string_view name) {
        if (global_var_names_.count(name) || functions_.count(name)) { return true; }
        std::string n(name);
        return extra_global(n) || eng_.has_variable(n) || eng_.has_function(n);
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
        if (name.find('#') != std::string_view::npos) { return; }   // a desugaring's own variable (parser::hidden_name): no script spells it
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
        if (v.is_cpp_bound() || v.is_reference() || (v.get_type_info() && v.get_type_info()->dynamic_slot)) {
            return {};   // write-through bindings convert on their own terms; a var holding a typed array is a var
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
        uint8_t access = 0;              // 0 public, 1 private, 2 protected
        std::string_view declarer;       // the class that declares a non-public member
    };

    // A registered class's non-public member: its declarer and level (class_definition::find_nonpublic_declarer)
    static void engine_member_access(const class_definition& def, uint64_t id, member_hit& h) {
        access_level level = access_level::public_access;
        if (const class_definition* declarer = def.find_nonpublic_declarer(id, level)) {
            h.access = level == access_level::private_access ? 1 : 2;
            h.declarer = declarer->get_name();
        }
    }

    // cls is base or derives from it, through the modeled chain and registered ancestors
    bool class_is_or_derives(std::string_view cls, std::string_view base, int depth = 0) {
        if (cls == base) { return true; }
        if (depth > 32) { return false; }
        if (const sclass* c = find_sclass(cls)) {
            for (auto b : c->decl->base_classes) {
                if (class_is_or_derives(b, base, depth + 1)) { return true; }
            }
            return false;
        }
        auto def = engine_class(cls);
        auto base_def = engine_class(base);
        return def && base_def && def->derives_from(base_def.get());
    }

    // Access control as the runtime applies it (detail::enforce_member_access): a private
    // (or protected, its synonym) member is reachable inside its declaring class and every class derived from it; the
    // same text as the raise
    bool access_denied(const member_hit& hit, std::string_view member, const source_location& loc) {
        if (!hit.found || hit.access == 0) { return false; }
        const std::string_view ctx = class_ctx_ ? class_ctx_name_ : std::string_view{};
        if (!ctx.empty() && class_is_or_derives(ctx, hit.declarer)) { return false; }
        diag(diag_level::error, loc,
             "Cannot access private member '" + std::string(member) + "' of class '" + std::string(hit.declarer) + "'");
        return true;
    }

    member_hit lookup_member(std::string_view cls, std::string_view member, bool want_static, int depth = 0) {
        member_hit miss;
        if (depth > 32) { miss.certain = false; return miss; }
        if (const sclass* c = find_sclass(cls)) {
            auto hit = [&](const variable_decl* f, const std::vector<const function_decl*>* m) {
                member_hit h{true, true, f, m};
                h.foreign = c->foreign;
                h.provable = c->provable;
                if (auto a = c->nonpublic.find(member); a != c->nonpublic.end()) {
                    h.access = a->second;
                    h.declarer = c->decl->name;
                }
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
                member_hit h{true, true, nullptr, nullptr};
                engine_member_access(*def, id, h);
                return h;
            }
            // a host static property: _get_x reads it, _set_x (when bound) stores it
            auto* sym = eng_.get_symbolizer();
            const bool static_getter = def->has_static_method(sym->get_getter_id_with_view(id).first);
            const bool static_setter = def->has_static_method(sym->get_setter_id_with_view(id).first);
            if (static_getter || static_setter) {
                member_hit h{true, true, nullptr, nullptr};
                h.read_only = closed && !static_setter;
                engine_member_access(*def, id, h);
                return h;
            }
        } else {
            uint64_t getter_id = eng_.symbolize("_get_" + std::string(member));
            if (engine_instance_member(*def, id, getter_id)) {
                member_hit h{true, true, nullptr, nullptr};
                if (closed && def->is_read_only_property(id)) {
                    h.read_only = !derived_opens_property(*def, id, eng_.symbolize("_set_" + std::string(member)));
                }
                engine_member_access(*def, id, h);
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
    // (internal/invariants.md 11). A `var`-typed map is dynamic and leaves it to the runtime.
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

    // -------------------------------------------------------------- const (internal/const.md)

    std::unordered_map<std::string_view, const variable_decl*> top_consts_;   // this unit's program-scope consts (a use may precede the declaration)
    bool collecting_namespace_ = false;

    struct const_hit {
        uint8_t verdict = const_none;   // const_raises: a write through the const, every step typed; const_runtime: through an untyped step
        std::string root;               // the const's name
        size_t line = 0, column = 0;    // its declaration
        // the path's last value (a call's receiver): its type when typed
        const type_info* final_ti = nullptr;
        std::string final_cls;
        bool final_known = false;
        bool final_library = false;   // a string, array or map: the builtin library's methods
    };

    static const type_info* const_referent(const type_info* t) {
        if (t && t->base_type == script_value_type::jai_reference_type) { return t->type_params.empty() ? nullptr : t->type_params[0].get(); }
        return t;
    }

    // the const program-scope declaration another unit of the program makes (one unit, once)
    const variable_decl* program_const_global(const std::string& name) {
        if (!program_globals_) { return nullptr; }
        auto it = program_globals_->find(name);
        if (it == program_globals_->end() || it->second.size() != 1 || it->second.begin()->second.size() != 1) { return nullptr; }
        const variable_decl* g = it->second.begin()->second.front().get();
        return g && g->is_const ? g : nullptr;
    }

    // Where a write lands, by C++'s top-level rule: through a const name and no handle, a write to the const (internal/const.md 2.2)
    const_hit const_path(const expression* e, bool value_too = false) {
        const_hit none;
        if (!e) { return none; }
        if (e->get_type() == node_type::ternary_expr) {   // a conditional of lvalues is const when either arm is (C++)
            auto* t = static_cast<const ternary_expr*>(e);
            const_hit a = const_path(t->then_expression.get(), value_too), b = const_path(t->else_expression.get(), value_too);
            if (a.verdict == const_raises) { return a; }
            if (b.verdict == const_raises) { return b; }
            return a.verdict ? a : b;
        }
        std::vector<const expression*> steps;
        const expression* base = e;
        while (base) {
            const node_type k = base->get_type();
            if (k == node_type::member_expr && !static_cast<const member_expr*>(base)->is_static) {
                steps.push_back(base);
                base = static_cast<const member_expr*>(base)->object.get();
            } else if (k == node_type::binary_expr && static_cast<const binary_expr*>(base)->op.type == token_type::left_bracket) {
                steps.push_back(base);
                base = static_cast<const binary_expr*>(base)->left.get();
            } else if (k == node_type::cast_expr && static_cast<const cast_expr*>(base)->implicit_ref) {
                base = static_cast<const cast_expr*>(base)->operand.get();
            } else if (k == node_type::ternary_expr) {
                return const_path(base, true).verdict ? const_hit{const_runtime, const_path(base, true).root} : none;
            } else {
                break;
            }
        }
        if (!base) { return none; }
        const_hit hit;
        const type_info* cur = nullptr;
        std::string cur_cls;
        bool known = false, handle = false, is_const = false, library = false;
        auto from_type = [&](const type_info* t) {
            t = const_referent(t);
            cur = t;
            known = t && t->base_type != script_value_type::jai_any_type;
            handle = const_handle_type(t);
            library = t && (t->base_type == script_value_type::jai_array_type || t->base_type == script_value_type::jai_map_type ||
                            t->base_type == script_value_type::jai_string_type || t->base_type == script_value_type::jai_utf8_string_type);
            if (t && t->base_type == script_value_type::jai_object_type) { cur_cls = t->type_name; }
        };
        if (base->get_type() == node_type::identifier_expr) {
            auto* id = static_cast<const identifier_expr*>(base);
            hit.root = std::string(id->name);
            if (var_entry* v = find_named(id)) {
                is_const = v->is_const;
                hit.line = v->line; hit.column = v->column;
                from_type(v->declared ? v->declared : v->type.ti);
                if (!v->declared && !v->type.ti && !v->dynamic) {
                    known = !v->type.is_unknown();
                    if (v->type.kind == ctype::object_k) { cur_cls = std::string(v->type.cls); }
                    library = v->type.kind == ctype::array_k || v->type.kind == ctype::map_k || v->type.kind == ctype::string_k || v->type.kind == ctype::utf8_k;
                }
                if (v->is_handle) { handle = true; known = true; }
                if (v->dynamic && !v->is_reference) { known = false; }
                if (v->map_pair) { known = true; handle = false; }   // a range-for pair is a value: its halves are the pair's
            } else if (class_ctx_ && !id->global_qualified && lookup_member(class_ctx_name_, id->name, false).found) {
                member_hit h = lookup_member(class_ctx_name_, id->name, false);
                if (!h.field) { return none; }
                is_const = h.field->is_const;
                if (!h.foreign) { hit.line = h.field->location.line; hit.column = h.field->location.column; }
                from_type(h.field->type.get());
            } else if (const enum_decl* en = find_enum(id->name)) {   // an enum's name: a const map<string, int> (internal/const.md 11)
                is_const = true;
                known = true; library = true;
                hit.line = en->location.line; hit.column = en->location.column;
            } else if (auto tc = top_consts_.find(id->name); tc != top_consts_.end() && !id->global_qualified) {
                is_const = true;
                hit.line = tc->second->location.line; hit.column = tc->second->location.column;
                from_type(tc->second->type.get());
            } else if (const variable_decl* g = program_const_global(std::string(id->name))) {
                is_const = true;
                from_type(g->type.get());
            } else if (eng_.is_const_global(std::string(id->name))) {
                is_const = true;
                known = false;
            } else {
                return none;
            }
        } else if (base->get_type() == node_type::member_expr) {   // C::s, ns::x
            auto* m = static_cast<const member_expr*>(base);
            hit.root = detail::const_spelled(m);
            std::string qualified;
            if (const variable_decl* nv = namespace_variable(m, &qualified)) {
                is_const = nv->is_const;
                hit.line = nv->location.line; hit.column = nv->location.column;
                from_type(nv->type.get());
            } else if (m->object && m->object->get_type() == node_type::identifier_expr) {
                const std::string_view cls = static_cast<const identifier_expr*>(m->object.get())->name;
                member_hit h = lookup_member(cls, m->member, true);
                if (h.found && h.field) {
                    is_const = h.field->is_const;
                    if (!h.foreign) { hit.line = h.field->location.line; hit.column = h.field->location.column; }
                    from_type(h.field->type.get());
                } else if (auto def = engine_class(cls); def && def->is_const_static(eng_.symbolize(m->member))) {
                    is_const = true;
                    known = false;
                }
            }
        } else {
            return none;
        }
        // a class constant reached through an instance (`obj.MAX = 1`, `this.MAX`) is the constant itself
        if (!is_const && !steps.empty() && steps.front()->get_type() == node_type::member_expr && !value_too) {
            auto* last = static_cast<const member_expr*>(steps.front());
            std::string owner;
            if (last->object && last->object->get_type() == node_type::this_expr && class_ctx_) { owner = std::string(class_ctx_name_); }
            else if (steps.size() == 1) { owner = cur_cls; }
            if (!owner.empty()) {
                member_hit h = lookup_member(owner, last->member, false);
                if (h.found && h.field && h.field->is_static && h.field->is_const) {
                    hit.root = std::string(last->member);
                    hit.verdict = const_raises;
                    if (!h.foreign) { hit.line = h.field->location.line; hit.column = h.field->location.column; }
                    return hit;
                }
            }
        }
        if (!is_const) {
            if (!steps.empty() && steps.front()->get_type() == node_type::member_expr && base->get_type() == node_type::this_expr && !value_too) {
                auto* last = static_cast<const member_expr*>(steps.front());
                if (steps.size() == 1 && class_ctx_) {
                    member_hit h = lookup_member(class_ctx_name_, last->member, false);
                    if (h.found && h.field && h.field->is_static && h.field->is_const) {
                        hit.root = std::string(last->member);
                        hit.verdict = const_raises;
                        return hit;
                    }
                }
            }
            return none;
        }
        if (steps.empty() && !value_too) { hit.verdict = const_raises; return hit; }   // the const itself
        uint8_t verdict = const_raises;
        for (size_t i = steps.size(); i-- > 0;) {
            const bool last = i == 0 && !value_too;
            if (known && handle) { return none; }
            if (!known) { verdict = const_runtime; break; }
            if (last) { break; }
            const expression* step = steps[i];
            if (step->get_type() == node_type::member_expr) {
                auto* m = static_cast<const member_expr*>(step);
                const std::string owner = cur_cls;
                cur = nullptr; cur_cls.clear(); known = false; handle = false; library = false;
                if (owner.empty()) { continue; }
                member_hit h = lookup_member(owner, m->member, false);
                if (h.found && h.field) {
                    if (h.field->is_static && h.field->is_const) { hit.root = std::string(m->member); hit.verdict = const_raises; return hit; }
                    from_type(h.field->type.get());
                }
            } else {
                const type_info* el = nullptr;
                if (cur && cur->base_type == script_value_type::jai_array_type) { el = cur->element_type().get(); }
                else if (cur && cur->base_type == script_value_type::jai_map_type) { el = cur->value_type().get(); }
                else if (cur && (cur->base_type == script_value_type::jai_string_type || cur->base_type == script_value_type::jai_utf8_string_type)) {
                    cur = nullptr; cur_cls.clear(); known = true; handle = false; library = false;   // a character: no further step
                    continue;
                }
                cur = nullptr; cur_cls.clear(); known = false; handle = false; library = false;
                from_type(el);
            }
        }
        if (value_too && verdict == const_raises) {
            if (known && handle) { return none; }
            if (!known) { verdict = const_runtime; }
        }
        hit.verdict = verdict;
        hit.final_ti = cur;
        hit.final_cls = cur_cls;
        hit.final_known = known;
        hit.final_library = library;
        return hit;
    }

    void stamp_const(const_site& site, uint8_t verdict, const std::function<std::string()>& text) {
        if (verdict == const_none) {
            if (site.verdict != const_none) { site = const_site{}; }
            return;
        }
        site.verdict = verdict;
        site.text = std::make_shared<const std::string>(text());
        site.text_id = eng_.symbolize(*site.text);   // the run time raises "{0}" with it: the text outlives every error object
    }

    // a store, a compound store or ++/-- through `target`
    void check_const_store(const_site& site, const expression* target, const source_location& at) {
        const const_hit h = const_path(target);
        stamp_const(site, h.verdict, [&] { return detail::const_store_text(target, h.root); });
        if (h.verdict == const_raises) { diag(diag_level::error, at, detail::const_store_diagnostic(target, h.root), h.line, h.column); }
    }

    // the checker's receiver-writes view: this unit's and the program's classes, then the engine's
    detail::receiver_view checker_receiver_view() {
        detail::receiver_view v;
        v.field_type = [this](std::string_view cls, std::string_view field, bool& is_field) -> const type_info* {
            member_hit h = lookup_member(cls, field, false);
            is_field = h.found && !h.methods;
            return h.field ? h.field->type.get() : nullptr;
        };
        v.methods = [this](std::string_view cls, std::string_view name, std::vector<const function_decl*>& out) -> bool {
            member_hit h = lookup_member(cls, name, false);
            if (!h.found) { return h.certain && !h.unresolved; }
            if (!h.methods) { return false; }
            for (const auto* f : *h.methods) { out.push_back(f); }
            return true;
        };
        v.functions = [this](const identifier_expr& id, std::vector<ref_write_callee>& out) {
            if (auto it = functions_.find(id.name); it != functions_.end()) {
                if (it->second.size() != 1) { return false; }
                out.push_back(ref_write_callee_of(it->second.front()));
                return true;
            }
            return resolve_global_ref_write_callee(eng_, id, out);
        };
        return v;
    }

    // a method call on a const-rooted receiver: an error when the method writes its receiver (internal/const.md 3.3)
    void check_const_call(call_expr* c) {
        if (!c->callee || c->callee->get_type() != node_type::member_expr) { stamp_const(c->const_check, const_none, {}); return; }
        auto* m = static_cast<member_expr*>(c->callee.get());
        if (m->is_static) { stamp_const(c->const_check, const_none, {}); return; }
        const const_hit h = const_path(m->object.get(), /*value_too=*/true);
        if (!h.verdict) { stamp_const(c->const_check, const_none, {}); return; }
        uint8_t verdict = h.verdict;
        bool builtin = false;
        size_t line = 0;
        if (h.final_known && h.final_library) {
            builtin = true;
            if (ref_write_reading_method(m->member)) { verdict = const_none; }
        } else if (h.final_known && !h.final_cls.empty()) {
            detail::receiver_write_analysis analysis(checker_receiver_view());
            detail::receiver_write w = analysis.analyze_methods(h.final_cls, m->member);
            if (!w.writes) {
                // an override in a derived class may write (a by-value slot can hold a subclass): the run time decides then
                bool any = false;
                for (const auto* f : overrides_of(h.final_cls, m->member)) {
                    if (analysis.analyze(f, h.final_cls).writes) { any = true; }
                }
                verdict = any ? const_runtime : const_none;
            } else {
                line = w.line;
                if (!w.certain) { verdict = const_runtime; }
            }
        } else if (h.final_known) {
            verdict = const_none;   // a primitive, a math value: its methods return values
        } else {
            verdict = const_runtime;
        }
        stamp_const(c->const_check, verdict, [&] { return detail::const_call_text(m->object.get(), h.root, m->member, builtin, line); });
        if (verdict == const_raises) {
            diag(diag_level::error, c->location, detail::const_lowercase(detail::const_call_text(m->object.get(), h.root, m->member, builtin, line)), h.line, h.column);
        }
    }

    // a T& / var& / shared_ptr<T> declaration bound to a const (an auto& or a const T& takes it as a const reference)
    void check_const_bind(variable_decl* decl, var_entry& entry) {
        if (decl->is_const) { entry.is_const = true; stamp_const(decl->const_check, const_none, {}); return; }
        if (!decl->initializer || !decl->type) { stamp_const(decl->const_check, const_none, {}); return; }
        const bool reference = decl->type->base_type == script_value_type::jai_reference_type;
        const bool handle = handle_type(decl->type.get());
        if (!reference && !handle) { stamp_const(decl->const_check, const_none, {}); return; }
        const const_hit h = const_path(decl->initializer.get(), /*value_too=*/handle);
        if (!h.verdict) { stamp_const(decl->const_check, const_none, {}); return; }
        const type_info* referent = reference ? const_referent(decl->type.get()) : nullptr;
        if (reference && !referent) {   // auto& deduces const T& (C++)
            entry.is_const = h.verdict == const_raises;
            stamp_const(decl->const_check, const_none, {});
            return;
        }
        if (handle && h.final_known && h.final_cls.empty()) { stamp_const(decl->const_check, const_none, {}); return; }
        const std::string declared = decl->type->canonical_name();
        auto text = [&] {
            return reference ? detail::const_bind_text(decl->name, declared, decl->initializer.get(), h.root, false)
                             : detail::const_share_text(decl->initializer.get(), h.root, declared);
        };
        stamp_const(decl->const_check, h.verdict, text);
        if (h.verdict == const_raises) { diag(diag_level::error, decl->location, detail::const_lowercase(text()), h.line, h.column); }
    }

    // cast<T&>(c) and cast<shared_ptr<T>>(c) name the const as writable (no const_cast)
    void check_const_cast(cast_expr* c) {
        if (c->implicit_ref || !(c->by_reference || handle_type(c->target.get()))) { stamp_const(c->const_check, const_none, {}); return; }
        const const_hit h = const_path(c->operand.get(), handle_type(c->target.get()));
        if (!h.verdict || (handle_type(c->target.get()) && h.final_known && h.final_cls.empty())) { stamp_const(c->const_check, const_none, {}); return; }
        const std::string target = c->target ? c->target->canonical_name() + (c->by_reference && c->target->base_type != script_value_type::jai_reference_type ? "&" : "") : std::string("T&");
        auto text = [&] { return handle_type(c->target.get()) ? detail::const_share_text(c->operand.get(), h.root, target) : detail::const_cast_text(c->operand.get(), h.root, target); };
        stamp_const(c->const_check, h.verdict, text);
        if (h.verdict == const_raises) { diag(diag_level::error, c->location, detail::const_lowercase(text()), h.line, h.column); }
    }

    // a const returned by reference
    void check_const_return(return_stmt* r) {
        if (!r->value || fn_stack_.empty()) { stamp_const(r->const_check, const_none, {}); return; }
        const fn_ctx& fn = fn_stack_.back();
        const bool by_reference = r->binds_reference || (fn.ret && fn.ret->base_type == script_value_type::jai_reference_type);
        if (!by_reference) { stamp_const(r->const_check, const_none, {}); return; }
        const const_hit h = const_path(r->value.get());
        const std::string declared = fn.ret ? fn.ret->canonical_name() : std::string("auto&");
        auto text = [&] { return detail::const_return_text(r->value.get(), h.root, fn.name, declared); };
        stamp_const(r->const_check, h.verdict, text);
        if (h.verdict == const_raises) { diag(diag_level::error, r->location, detail::const_lowercase(text()), h.line, h.column); }
    }

    // a const passed to a reference parameter the callee writes through (a const T& parameter takes it)
    void check_const_arguments(call_expr* c, const std::vector<const function_decl*>* ov, std::string_view ov_class, bool free_callee,
                               const std::vector<ctype>& args) {
        if (c->const_check.verdict != const_none || !ov) { return; }
        for (size_t i = 0; i < c->arguments.size() && i < args.size(); ++i) {
            const const_hit h = const_path(c->arguments[i].get());
            if (h.verdict != const_raises) { continue; }
            std::vector<const function_decl*> by_ref;
            bool all_ref = false;
            for (const auto* fn : *ov) {
                if (!script_fn_viable(fn, args, !free_callee && arity_rivals(*ov, args.size()))) { continue; }
                all_ref = !fn->is_coroutine && i < fn->parameters.size() && fn->parameters[i].is_reference && !fn->parameters[i].is_const;
                if (!all_ref) { break; }
                by_ref.push_back(fn);
            }
            if (!all_ref || by_ref.empty()) { continue; }
            if (!ov_class.empty()) {
                for (const auto* fn : overrides_of(ov_class, by_ref.front()->name)) { by_ref.push_back(fn); }
            }
            for (const auto* fn : by_ref) {
                if (i >= fn->parameters.size() || !fn->parameters[i].is_reference || fn->parameters[i].is_const || fn->is_coroutine) { continue; }
                const ref_param_write w = ref_writes(fn, i);
                if (!w.writes) { continue; }
                const std::string text = detail::const_argument_text(c->arguments[i].get(), h.root, fn->parameters[i].name, fn->name, w.line);
                stamp_const(c->const_check, const_raises, [&] { return text; });
                diag(diag_level::error, c->arguments[i]->location, "'" + std::string(fn->name) + "' writes through parameter '" + fn->parameters[i].name +
                     "' (line " + std::to_string(w.line) + "); '" + h.root + "' is const", h.line, h.column);
                return;
            }
        }
    }

    // ++/-- on a bare identifier (member targets desugar to += 1 and take the assignment rule)
    void check_incdec_target(unary_expr* u) {
        check_const_store(u->const_check, u->operand.get(), u->location);
        if (!class_ctx_ || u->operand->get_type() != node_type::identifier_expr) { return; }
        auto* id = static_cast<identifier_expr*>(u->operand.get());
        if (id->global_qualified || find_named(id)) { return; }
        member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
        if (hit.found && access_denied(hit, id->name, id->location)) { return; }
        if (hit.found && hit.read_only) { read_only_store(u->location, id->name); }
    }

    // -------------------------------------------------------------- type proofs
    // internal/type_proofs_design.md. Every rule writes its node field on EVERY visit (a stamp
    // or the no-proof default), so a re-check can add or drop proofs but never leaves a
    // stale one the pass no longer derives.

    bool stamping() const { return proofs_ == proof_mode::stamp; }
    bool semantic_walk() const { return !collect_diagnostics_ && proofs_ != proof_mode::leave; }

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
        for (auto n : k_no_proof_names) { if (member == n) { return std::nullopt; } }   // builtin-shadow names
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

    // a scalar (int, float, bool) as its base type, an array of scalars as jai_array_type | the element's base type << 8, else 0
    static uint16_t certain_kind(const type_info* t) {
        if (!t || t->is_reference()) return 0;
        auto scalar = [](script_value_type b) { return b == script_value_type::jai_int_type || b == script_value_type::jai_float_type || b == script_value_type::jai_bool_type; };
        if (scalar(t->base_type)) return static_cast<uint16_t>(t->base_type);
        if (t->base_type != script_value_type::jai_array_type) return 0;
        const type_info* el = t->element_type().get();
        if (!el || el->is_reference() || !scalar(el->base_type)) return 0;
        return static_cast<uint16_t>(static_cast<uint16_t>(script_value_type::jai_array_type) | (static_cast<uint16_t>(el->base_type) << 8));
    }
    // a declared type a proof may rest on: spelled, not a reference, not var, auto or a function
    static bool certain_type(const type_info* t) {
        if (!t || t->is_reference()) return false;
        switch (t->base_type) {
        case script_value_type::jai_any_type: case script_value_type::jai_function_type: case script_value_type::jai_null_type: return false;
        default: return t->type_name != "auto";
        }
    }
    static proof_global proof_of(uint64_t symbol, const type_info* t) {
        proof_global g; g.symbol = symbol;
        const uint16_t k = certain_kind(t);
        if (k != 0) { g.kind = k; } else { g.kind = proof_global::k_type; g.type = t; }
        return g;
    }
    // a program-scope name whose declared type changes only when its declaring unit runs again: this unit's own typed top-level
    // declaration, or the one spelled declaration another unit of the program makes, of any type a proof may rest on; a name a field
    // could shadow (inside a class) or a runtime include could declare stays uncertain. The name and its proof join deps; declared
    // receives the declared type
    bool certain_global(const identifier_expr* id, std::vector<proof_global>& deps, const type_info** declared = nullptr) {
        if (has_dynamic_includes_ || class_ctx_ || id->symbol_id == UINT64_MAX) return false;
        size_t scope = SIZE_MAX;
        const type_info* t = nullptr;
        if (var_entry* v = find_named(id, &scope)) {
            if (scope != 0 || v->dynamic || v->is_reference || v->is_handle) return false;
            t = v->declared;
        } else {
            if (functions_.count(id->name) || enums_.count(id->name)) return false;
            const variable_decl* g = program_global(std::string(id->name));
            if (!g) return false;
            t = g->type.get();
        }
        if (!certain_type(t)) return false;
        deps.push_back(proof_of(id->symbol_id, t));
        if (declared) { *declared = t; }
        return true;
    }
    // an argument whose static type an include cannot change: a literal, a local or a parameter (a frame slot), a certain program-scope
    // name (certain_global; its kind recorded in deps), an element of a local or certain array, and arithmetic, a unary, a ternary, a cast
    // or a math intrinsic over those
    bool certain_argument(const expression* e, std::vector<proof_global>& deps) {
        if (!e) return false;
        switch (e->get_type()) {
        case node_type::literal_expr: return true;
        case node_type::identifier_expr: {
            auto* id = static_cast<const identifier_expr*>(e);
            return id->slot_index != SIZE_MAX || certain_global(id, deps);
        }
        case node_type::binary_expr: {
            auto* b = static_cast<const binary_expr*>(e);
            if (b->op.type == token_type::left_bracket) {
                if (!b->left || b->left->get_type() != node_type::identifier_expr) return false;
                auto* base = static_cast<const identifier_expr*>(b->left.get());
                const type_info* declared = nullptr;
                if (base->slot_index == SIZE_MAX) {
                    if (!certain_global(base, deps, &declared)) return false;
                } else {
                    const var_entry* v = find_named(base);   // a local declared an array by value: a reference's declared type no bind checks
                    if (!v || v->dynamic || v->is_reference || !certain_type(v->declared)) return false;
                    declared = v->declared;
                }
                if (declared->base_type != script_value_type::jai_array_type) return false;
                return certain_argument(b->right.get(), deps);
            }
            return certain_argument(b->left.get(), deps) && certain_argument(b->right.get(), deps);
        }
        case node_type::unary_expr: return certain_argument(static_cast<const unary_expr*>(e)->operand.get(), deps);
        case node_type::ternary_expr: { auto* x = static_cast<const ternary_expr*>(e); return certain_argument(x->condition.get(), deps) && certain_argument(x->then_expression.get(), deps) && certain_argument(x->else_expression.get(), deps); }
        case node_type::cast_expr: return certain_argument(static_cast<const cast_expr*>(e)->operand.get(), deps);
        case node_type::call_expr: {
            auto* ce = static_cast<const call_expr*>(e);
            if (ce->callee && ce->callee->get_type() == node_type::identifier_expr) {
                // a free script function with a declared return: its result converts to that type whatever the arguments; the name and the
                // return join deps (proof_global::k_function), since a redefinition may declare another return type
                auto* id = static_cast<const identifier_expr*>(ce->callee.get());
                if (has_dynamic_includes_ || class_ctx_ || id->slot_index != SIZE_MAX || id->global_qualified || id->symbol_id == UINT64_MAX) return false;
                const auto fn = ce->proven_callee_decl.lock();
                if (!fn || fn->is_coroutine) return false;
                if (!certain_type(fn->return_type.get())) return false;
                proof_global g = proof_of(id->symbol_id, fn->return_type.get());
                if ((g.kind & 0xFF) == static_cast<uint16_t>(script_value_type::jai_array_type) && g.type == nullptr) { g.kind = proof_global::k_type; g.type = fn->return_type.get(); }
                g.kind = static_cast<uint16_t>(g.kind | proof_global::k_function);
                deps.push_back(g);
                return true;
            }
            if (!ce->callee || ce->callee->get_type() != node_type::member_expr || detail::math_intrinsic_for_call(static_cast<const member_expr*>(ce->callee.get())) == detail::math_fn::none) return false;
            for (const auto& a : ce->arguments) { if (!certain_argument(a.get(), deps)) return false; }
            return true;
        }
        default: return false;
        }
    }

    // C1: one statically resolved same-script callee, exact arity, no defaults in play, no
    // reference params. Per-argument same_type (an untyped/var parameter binds as identity).
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
        c->proven_callee_decl = fn && free_callee ? fn->weak_from_this() : std::weak_ptr<const function_decl>();
        if (!fn) {
            c->static_arg_proofs.clear();
            c->proof_globals.clear();
            c->proven_callee_body = nullptr;
            return;
        }
        c->static_arg_proofs.assign(n, proof_none);
        c->proof_globals.clear();
        std::vector<proof_global> deps;
        for (size_t i = 0; i < n; ++i) {
            const parameter& p = fn->parameters[i];
            if (p.is_reference) { continue; }   // a reference parameter binds, never converts: no proof, no count
            const bool untyped = !p.type || p.type->base_type == script_value_type::jai_any_type;
            deps.clear();
            if (!(certain_program || certain_argument(c->arguments[i].get(), deps))) { continue; }
            if (untyped || same_type(ctype_from_type_info(p.type.get()), args[i])) {
                c->static_arg_proofs[i] = proof_same_type;
                if (certain_program) { ++counts_.args; }
                for (const auto& d : deps) { if (std::find(c->proof_globals.begin(), c->proof_globals.end(), d) == c->proof_globals.end()) { c->proof_globals.push_back(d); } }
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

	static std::optional<script_int> literal_int(const expression* expr) {
		if (!expr || expr->get_type() != node_type::literal_expr) { return std::nullopt; }
		const auto* lit = static_cast<const literal_expr*>(expr);
		if (lit->value.is_int()) { return lit->value.unchecked_as_int(); }
		return std::nullopt;
	}

	// an int literal outside a uint8 / int8 element's range: the store raises, so the checker says so at load
	static std::string byte_literal_issue(const ctype& src, const type_info* target) {
		if (!target || !target->is_byte_element() || src.kind != ctype::int_k) { return {}; }
		const auto v = literal_int(src.origin);
		if (!v) { return {}; }
		const bool fits = target->is_signed ? (*v >= -128 && *v <= 127) : (*v >= 0 && *v <= 255);
		if (fits) { return {}; }
		return std::to_string(*v) + " is outside " + target->canonical_name() + " (" + (target->is_signed ? "-128..127" : "0..255") + ")";
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
		result.alias_proof = true;   // a typed reference: the binder holds it to its type, the typed rows guard the kind
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
		if (auto issue = byte_literal_issue(src, target); !issue.empty()) { return issue; }
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
			// A weak element takes null, a weak handle, or a shared handle of T or a subclass (C++: weak = shared, the declaration's row)
			accepted = src.kind == ctype::null_k || src.kind == ctype::weak_k ||
				(src.kind == ctype::object_k && (!src.ti || src.ti->base_type == script_value_type::jai_shared_ptr_type) &&
				 weak_initializer_compatible(src, dst));
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
			case ctype::utf8_k: return true;     // the string row
			case ctype::char_k: return src.kind == ctype::char_k || may_convert_via_method(src, "to_char");
			case ctype::array_k:
			case ctype::map_k: return container_issue(src, dst).empty();
			case ctype::function_k: return src.kind == ctype::function_k || src.kind == ctype::null_k;
			case ctype::weak_k: return weak_initializer_compatible(src, dst);
			case ctype::null_k: return src.kind == ctype::null_k;   // void returns
			case ctype::math_k: {
				// the same kind, or the other precision of the same dimensions (the typed-slot table's row)
				if (src.kind != ctype::math_k) { return false; }
				return detail::math::converts(math_type_from_name(src.cls), math_type_from_name(dst.cls));
			}
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
            // A defaulted-param ctor is NOT a converting ctor -
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
            case math_fn::floor_: case math_fn::ceil_: case math_fn::round_: case math_fn::abs_: case math_fn::min_:
            case math_fn::max_: case math_fn::clamp_: case math_fn::lerp_: case math_fn::mix_: case math_fn::unmix_:
            case math_fn::remap_: case math_fn::mix_in_: case math_fn::mix_out_: case math_fn::mix_in_out_:
            case math_fn::mix_out_in_: case math_fn::unmix_in_: case math_fn::unmix_out_: case math_fn::unmix_in_out_:
            case math_fn::unmix_out_in_: {
                // the component-wise family (math_intrinsics.hpp eval_vec_math): a vector argument makes a vector result
                // (a vec of its dimensions; abs/min/max/clamp keep ivec when every argument is integral), and an argument
                // of unknown type leaves the result unknown
                const ctype* vec = nullptr;
                bool unknown = false, integral = true;
                for (const auto& a : args) {
                    if (a.kind == ctype::math_k) {
                        if (!vec) { vec = &a; }
                        integral = integral && detail::math::integral(math_type_from_name(a.cls));
                    } else if (!is_numeric(a.kind)) {
                        unknown = true;
                    } else if (a.kind != ctype::int_k) {
                        integral = false;
                    }
                }
                if (vec) {
                    const script_value_type vk = math_type_from_name(vec->cls);
                    if (detail::math::is_color(vk)) { return unknown ? ctype{} : math_ctype(vk); }
                    const size_t dims = detail::math::dims(vk);
                    const bool exact = integral && (fn == math_fn::abs_ || fn == math_fn::min_ || fn == math_fn::max_ || fn == math_fn::clamp_);
                    return unknown ? ctype{} : math_ctype(detail::math::family_kind(exact, dims));
                }
                if (unknown) { return {}; }
                break;
            }
            case math_fn::dot_: case math_fn::cross_: case math_fn::length_: case math_fn::length_squared_:
            case math_fn::normalize_: case math_fn::reflect_: case math_fn::project_: case math_fn::angle_between_:
            case math_fn::perpendicular_: case math_fn::rotate_: case math_fn::distance: case math_fn::distance_squared: {
                // the geometry library (math_intrinsics.hpp eval_geometry): the vector arguments' kinds decide; the scalar
                // distance form (four numbers) stays float
                if (args.empty() || args[0].kind != ctype::math_k) {
                    if (fn == math_fn::distance || fn == math_fn::distance_squared) { break; }
                    return {};
                }
                const script_value_type vk = math_type_from_name(args[0].cls);
                if (detail::math::is_color(vk)) { return {}; }
                const size_t dims = detail::math::dims(vk);
                bool exact = detail::math::integral(vk);
                const bool two = fn != math_fn::length_ && fn != math_fn::length_squared_ && fn != math_fn::normalize_ &&
                                 fn != math_fn::perpendicular_ && !(fn == math_fn::rotate_ && args.size() == 2);
                if (two) {
                    if (args.size() < 2 || args[1].kind != ctype::math_k) { return {}; }
                    exact = exact && detail::math::integral(math_type_from_name(args[1].cls));
                }
                switch (fn) {
                    case math_fn::dot_: case math_fn::length_squared_: case math_fn::distance_squared:
                        return ctype::make(exact ? ctype::int_k : ctype::float_k);
                    case math_fn::cross_:
                        if (dims == 2) { return ctype::make(exact ? ctype::int_k : ctype::float_k); }
                        return math_ctype(exact ? script_value_type::jai_ivec3_type : script_value_type::jai_vec3_type);
                    case math_fn::perpendicular_:
                        return math_ctype(vk);
                    case math_fn::length_: case math_fn::angle_between_: case math_fn::distance:
                        return ctype::make(ctype::float_k);
                    default:
                        return math_ctype(detail::math::family_kind(false, dims));
                }
            }
            default:
                break;
        }
        switch (fn) {
            case math_fn::itrunc: case math_fn::ifloor:
            case math_fn::wrap_add_: case math_fn::wrap_sub_: case math_fn::wrap_mul_:
            case math_fn::i32_:
            case math_fn::i32_add_: case math_fn::i32_sub_: case math_fn::i32_mul_: case math_fn::i32_div_: case math_fn::i32_neg_:
                return ctype::make(ctype::int_k);
            case math_fn::cstore_: case math_fn::vcomp_:
                return {};   // the call site reads the kind from its literal code (math_step_result)
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
            case math_fn::vec2_: return math_ctype(script_value_type::jai_vec2_type);
            case math_fn::vec3_: return math_ctype(script_value_type::jai_vec3_type);
            case math_fn::ivec2_: return math_ctype(script_value_type::jai_ivec2_type);
            case math_fn::sign_: case math_fn::random_int_: return ctype::make(ctype::int_k);
            case math_fn::is_nan_: case math_fn::is_inf_: return ctype::make(ctype::bool_k);
            case math_fn::ivec3_: return math_ctype(script_value_type::jai_ivec3_type);
            case math_fn::vec4_: return math_ctype(script_value_type::jai_vec4_type);
            case math_fn::color_: case math_fn::mix_rgb_: case math_fn::mix_hsv_: return math_ctype(script_value_type::jai_color_type);
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

    // A host namespace entry with a declared signature (script_namespace_data::signatures): arity and argument kinds are
    // certainties the entry's own run-time check would raise; a unit that declares the namespace, or a script function of
    // the name, keeps the call lenient
    ctype check_namespace_entry(const member_expr* m, const std::vector<ctype>& args, const source_location& loc) {
        std::vector<std::string_view> segments;
        const expression* e = m->object.get();
        while (e && e->get_type() == node_type::member_expr) {
            segments.push_back(static_cast<const member_expr*>(e)->member);
            e = static_cast<const member_expr*>(e)->object.get();
        }
        if (!e || e->get_type() != node_type::identifier_expr) { return {}; }
        std::string path(static_cast<const identifier_expr*>(e)->name);
        for (size_t i = segments.size(); i-- > 0;) { path += "::"; path += segments[i]; }
        if (namespaces_.count(std::string_view(path))) { return {}; }
        auto& all = eng_.script_namespaces();
        auto nit = all.find(eng_.symbolize(path));
        if (nit == all.end() || !nit->second) { return {}; }
        const uint64_t member_id = eng_.symbolize(m->member);
        if (nit->second->functions.count(member_id)) { return {}; }
        auto sit = nit->second->signatures.find(member_id);
        if (sit == nit->second->signatures.end()) { return {}; }
        const host_entry_signature& sig = sit->second;
        const std::string qualified = path + "::" + std::string(m->member);
        if (args.size() < sig.min_args || args.size() > sig.max_args) {
            const std::string expected = sig.min_args == sig.max_args ? std::to_string(sig.min_args)
                : std::to_string(sig.min_args) + " to " + std::to_string(sig.max_args);
            diag(diag_level::error, loc, "'" + qualified + "' expects " + expected + " argument(s), got " + std::to_string(args.size()));
            return {};
        }
        for (size_t i = 0; i < args.size() && i < sig.params.size(); ++i) {
            if (!namespace_arg_accepts(args[i], sig.params[i])) {
                diag(diag_level::error, loc, "argument " + std::to_string(i + 1) + " of '" + qualified + "': cannot pass '" +
                     ctype_name(args[i]) + "' where " + namespace_param_spelling(sig.params[i]) + " is expected");
                return {};
            }
        }
        ctype t;
        if (sig.result_follows_first && !args.empty() && (args[0].kind == ctype::string_k || args[0].kind == ctype::utf8_k || args[0].kind == ctype::array_k)) { t = args[0]; }
        else if (sig.result == script_value_type::jai_array_type) { t = ctype::make(ctype::array_k); }
        else if (sig.result == script_value_type::jai_string_type) { t = ctype::make(ctype::string_k); }
        else if (sig.result == script_value_type::jai_bool_type) { t = ctype::make(ctype::bool_k); }
        else if (sig.result == script_value_type::jai_int_type) { t = ctype::make(ctype::int_k); }
        else if (sig.result == script_value_type::jai_float_type) { t = ctype::make(ctype::float_k); }
        else if (sig.result == script_value_type::jai_object_type && !sig.result_class.empty()) {
            const type_info* ti = eng_.get_type_info_object(sig.result_class);
            t = ctype::object(ti->type_name);
            t.ti = ti;
        }
        else { return {}; }
        t.tentative = path != "array";
        return t;
    }
    static bool namespace_arg_accepts(const ctype& arg, script_value_type p) {
        if (arg.is_unknown() || arg.kind == ctype::null_k || p == script_value_type::jai_null_type) { return true; }
        switch (p) {
            case script_value_type::jai_string_type: return arg.kind == ctype::string_k || arg.kind == ctype::utf8_k;
            case script_value_type::jai_int_type: return arg.kind == ctype::int_k;
            case script_value_type::jai_float_type: return arg.kind == ctype::int_k || arg.kind == ctype::float_k;
            case script_value_type::jai_bool_type: return arg.kind == ctype::bool_k;
            case script_value_type::jai_array_type: return arg.kind == ctype::array_k;
            case script_value_type::jai_map_type: return arg.kind == ctype::map_k;
            default: return true;
        }
    }
    static std::string namespace_param_spelling(script_value_type p) {
        switch (p) {
            case script_value_type::jai_string_type: return "a string";
            case script_value_type::jai_int_type: return "an int";
            case script_value_type::jai_float_type: return "a number";
            case script_value_type::jai_bool_type: return "a bool";
            case script_value_type::jai_array_type: return "an array";
            case script_value_type::jai_map_type: return "a map";
            default: return "a value";
        }
    }

    bool namespace_known(const std::string& path) {
        return namespaces_.count(std::string_view(path)) != 0 ||
               eng_.script_namespaces().count(eng_.symbolize(path)) != 0;
    }

    // A method call on a declared container or string: the name must be one the builtin registry dispatches (the runtime's
    // family-B text otherwise). A map answers a missing method through its key instead - `m.f(x)` calls the value stored
    // at "f" - so a map is certain only when that value cannot be called (its declared value type is a primitive, a string
    // or a container)
    void check_builtin_member(const ctype& recv, const member_expr* m) {
        int kind = -1;
        switch (recv.kind) {
            case ctype::array_k: kind = 0; break;
            case ctype::map_k: kind = 1; break;
            case ctype::string_k: kind = 2; break;
            case ctype::utf8_k: kind = 3; break;
            default: return;
        }
        if (kind == 1) {
            const type_info* value = recv.ti ? recv.ti->value_type().get() : nullptr;
            if (!value) { return; }
            switch (value->base_type) {
                case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
                case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_utf8_string_type:
                case script_value_type::jai_array_type: case script_value_type::jai_map_type:
                    break;
                default:
                    return;
            }
        }
        const auto& ids = eng_.builtin_method_ids(kind);
        if (ids.count(eng_.symbolize(m->member))) { return; }
        std::string best;
        size_t best_distance = 3;
        for (const uint64_t id : ids) {
            const std::string_view name = eng_.get_symbolizer()->get_string(id);
            const size_t d = edit_distance(m->member, name);
            if (d < best_distance || (d == best_distance && !best.empty() && name < best)) { best_distance = d; best = std::string(name); }
        }
        diag(diag_level::error, m->location, "no member '" + std::string(m->member) + "' on '" + ctype_name(recv) + "'" +
             (best.empty() ? std::string() : " (did you mean '" + best + "'?)"));
    }
	// A coroutine handle answers next() and done() only (the runtime's texts); resume is the retired spelling of next
	void coroutine_member_missing(const member_expr* a_member) {
		std::string best;
		if (a_member->member == "resume") {
			best = "next";
		} else {
			size_t bestDistance = 3;
			for (const std::string_view name : {std::string_view{"done"}, std::string_view{"next"}}) {
				const size_t distance = edit_distance(a_member->member, name);
				if (distance < bestDistance) { bestDistance = distance; best = std::string(name); }
			}
		}
		diag(diag_level::error, a_member->location, "no member '" + std::string(a_member->member) + "' on 'coroutine_handle'" +
			(best.empty() ? std::string() : " (did you mean '" + best + "'?)"));
	}
	ctype coroutine_method_call(const ctype& a_receiver, const member_expr* a_member, const call_expr* a_call, const std::vector<ctype>& a_args) {
		if (a_receiver.tentative) { return {}; }
		if (a_member->member == "done") {
			if (!a_args.empty()) { diag(diag_level::error, a_call->location, "done() takes no arguments"); }
			return ctype::make(ctype::bool_k);
		}
		if (a_member->member == "next") {
			if (a_args.size() > 1) {
				diag(diag_level::error, a_call->location, "next() takes 0 or 1 arguments ([count])");
			} else if (a_args.size() == 1 && !a_args[0].is_unknown() && !a_args[0].tentative && a_args[0].kind != ctype::int_k) {
				diag(diag_level::error, a_call->arguments[0]->location, "next() count must be an integer");
			}
			return {};
		}
		coroutine_member_missing(a_member);
		return {};
	}
	void checkBuiltinArity(const ctype& a_receiver, const member_expr* a_member, const call_expr* a_call) {
		uint8_t receiver;
		switch (a_receiver.kind) {
			case ctype::array_k: receiver = 0; break;
			case ctype::map_k: receiver = 1; break;
			case ctype::string_k: receiver = 2; break;
			case ctype::utf8_k: receiver = 3; break;
			default: return;
		}
		const auto* signature = findBuiltinArity(receiver, a_member->member);
		if (!signature) { return; }
		const size_t count = a_call->arguments.size();
		if (count >= signature->minimum && count <= signature->maximum) { return; }
		std::string expected = std::to_string(signature->minimum);
		if (signature->minimum != signature->maximum) { expected += " to " + std::to_string(signature->maximum); }
		diag(diag_level::error, a_call->location, "'" + std::string(a_member->member) + "' on '" +
			ctype_name(a_receiver) + "' expects " + expected + " arguments, got " + std::to_string(count));
	}

    // The return type a callback argument declares: a lambda's spelled one, or the one overload of a named script function
    const type_info* callback_declared_return(const expression* arg) const {
        if (!arg) { return nullptr; }
        if (arg->get_type() == node_type::lambda_expr) { return static_cast<const lambda_expr*>(arg)->return_type.get(); }
        if (arg->get_type() == node_type::identifier_expr) {
            auto it = functions_.find(static_cast<const identifier_expr*>(arg)->name);
            if (it != functions_.end() && it->second.size() == 1 && it->second[0]) { return it->second[0]->return_type.get(); }
        }
        return nullptr;
    }

    // The array helpers' results (the registry's map, reduce, for_each, find, find_index, some, every, concat, insert): map is array<T>
    // when its callback declares T, as the runtime builds it; find's element may be null, so it stays open
    bool array_helper_result(const ctype& recv, std::string_view member, const call_expr* c, const std::vector<ctype>& args, ctype& out) {
        if (member == "some" || member == "every") { out = ctype::make(ctype::bool_k); return true; }
        if (member == "find_index") { out = ctype::make(ctype::int_k); return true; }
        if (member == "for_each" || member == "insert") { out = ctype::make(ctype::null_k); return true; }
        if (member == "concat" && args.size() == 1) { out = recv; out.const_key = false; out.origin = nullptr; return true; }
        if (member == "map" && args.size() == 1) {
            out = ctype::make(ctype::array_k);   // an array; its element type is the runtime's (array<T> for a callback declaring T)
            return true;
        }
        if (member == "reduce" && (args.size() == 1 || args.size() == 2)) {
            // the callback's declared type when the initial value has it too (an empty array returns the initial value)
            const type_info* r = callback_declared_return(c->arguments[0].get());
            out = {};
            if (r && r->base_type != script_value_type::jai_reference_type && args.size() == 2) {
                const ctype declared = ctype_from_type_info(r);
                if (!declared.is_unknown() && declared.kind == args[1].kind && declared.cls == args[1].cls) { out = declared; }
            }
            return true;
        }
        return false;
    }

    // A builtin method of a declared container or string read without a call (the runtime's text, detail::method_value_text): a
    // builtin name wins over a map's key of the same name, so a map is certain too
    void builtin_method_value(const ctype& recv, const member_expr* m) {
        int kind = -1;
        const char* owner = nullptr;
        switch (recv.kind) {
            case ctype::array_k: kind = 0; owner = "array"; break;
            case ctype::map_k: kind = 1; owner = "map"; break;
            case ctype::string_k: kind = 2; owner = "string"; break;
            case ctype::utf8_k: kind = 3; owner = "utf8_string"; break;
            default: return;
        }
        if (!eng_.builtin_method_ids(kind).count(eng_.symbolize(m->member))) { return; }
        diag(diag_level::error, m->location, detail::method_value_text(m->member, owner, detail::method_value_receiver(m), true));
    }
    static size_t edit_distance(std::string_view a, std::string_view b) {
        std::vector<size_t> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) { row[j] = j; }
        for (size_t i = 1; i <= a.size(); ++i) {
            size_t diagonal = row[0];
            row[0] = i;
            for (size_t j = 1; j <= b.size(); ++j) {
                const size_t up = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
                diagonal = up;
            }
        }
        return row[b.size()];
    }

    // Handle builtins every object value answers (shared_ptr registry + the universal
    // same_as): a class method of the same name wins first, so this runs on misses only.
    static bool handle_builtin_result(std::string_view member, ctype& out) {
        if (member == "use_count" || member == "cpp_ref_count") { out = ctype::make(ctype::int_k); return true; }
        if (member == "unique" || member == "same_as") { out = ctype::make(ctype::bool_k); return true; }
        if (member == "reset") { out = {}; return true; }
        return false;
    }

    // -------------------------------------------------------------- binary data

    // The engine's binary:: library (detail/binary_data.hpp) and string::from_bytes, unless the program declares its own
    bool library_namespace(std::string_view ns) const { return namespaces_.count(ns) == 0; }
    static bool static_path(const member_expr* m, std::string_view ns) {
        return m && m->is_static && m->object && m->object->get_type() == node_type::identifier_expr &&
               static_cast<const identifier_expr*>(m->object.get())->name == ns;
    }
    static bool binary_width_name(std::string_view n) {
        return n == "int8" || n == "int16" || n == "int32" || n == "int64" || n == "uint8" || n == "uint16" || n == "uint32" ||
               n == "float32" || n == "float64";
    }
    bool binary_width_token(const member_expr* m) const {
        return static_path(m, "binary") && library_namespace("binary") && binary_width_name(m->member);
    }
    bool binary_call(const expression* e, std::string_view member) const {
        if (!e || e->get_type() != node_type::call_expr) { return false; }
        auto* c = static_cast<const call_expr*>(e);
        return c->callee && c->callee->get_type() == node_type::member_expr &&
               static_path(static_cast<const member_expr*>(c->callee.get()), "binary") &&
               static_cast<const member_expr*>(c->callee.get())->member == member && library_namespace("binary");
    }
    static bool is_class_kind(const ctype& t, std::string_view cls) { return t.kind == ctype::object_k && t.cls == cls; }

    // name, width pairs (binary::layout, reader.unpack): an odd count, a name that is not a string or a width that is not a
    // binary width token is certain at load; unknown kinds stay lenient
    void check_binary_pairs(const call_expr* c, const std::vector<ctype>& args, const char* who) {
        const size_t n = args.size();
        if (n == 0 || n % 2 != 0) {
            diag(diag_level::error, c->location, std::string(who) + " expects name, width pairs, got " + std::to_string(n) +
                 (n == 1 ? " argument" : " arguments"));
            return;
        }
        for (size_t i = 0; i < n; i += 2) {
            const ctype& name = args[i];
            if (!name.is_unknown() && name.kind != ctype::string_k && name.kind != ctype::utf8_k) {
                diag(diag_level::error, c->arguments[i]->location, std::string(who) + " argument " + std::to_string(i + 1) +
                     " must be a field name string, got " + ctype_name(name));
            }
            const ctype& w = args[i + 1];
            if (!w.is_unknown() && !is_class_kind(w, "binary::width")) {
                diag(diag_level::error, c->arguments[i + 1]->location, std::string(who) + " argument " + std::to_string(i + 2) +
                     " must be a binary width (binary::int32, binary::text(8), ...), got " + ctype_name(w));
            }
        }
    }

    // binary::layout(...), binary::text(n), binary::bytes(n), binary::reader(...), binary::writer(), string::from_bytes(...)
    bool library_namespace_call(const call_expr* c, const member_expr* m, const std::vector<ctype>& args, ctype& out) {
        if (static_path(m, "binary") && library_namespace("binary")) {
            if (m->member == "layout") { check_binary_pairs(c, args, "binary::layout"); out = object_of("binary::layout"); return true; }
            if (m->member == "text" || m->member == "bytes") { out = object_of("binary::width"); return true; }
            if (m->member == "reader") { out = object_of("binary::reader"); return true; }
            if (m->member == "writer") { out = object_of("binary::writer"); return true; }
            return false;
        }
        if (static_path(m, "string") && library_namespace("string") && m->member == "from_bytes") {
            out = ctype::make(ctype::string_k);
            return true;
        }
        return false;
    }

    // the layout a call argument statically is: an inline binary::layout(...) call, or a variable declared once with one and
    // never assigned again
    const call_expr* static_layout(const expression* e, int depth = 0) {
        if (!e || depth > 8) { return nullptr; }
        if (binary_call(e, "layout")) { return static_cast<const call_expr*>(e); }
        if (e->get_type() != node_type::identifier_expr) { return nullptr; }
        auto* id = static_cast<const identifier_expr*>(e);
        auto it = layout_inits_.find(id->name);
        if (it == layout_inits_.end() || it->second.size() != 1 || !it->second[0] || assigned_names_.count(id->name)) { return nullptr; }
        return static_layout(it->second[0], depth + 1);
    }

    // a statically known layout's field kinds: 1 integer, 2 float, 3 text, 4 bytes; empty when any width is not a token
    std::vector<uint8_t> layout_kinds(const call_expr* l) {
        std::vector<uint8_t> kinds;
        if (l->arguments.size() % 2 != 0) { return {}; }
        for (size_t i = 1; i < l->arguments.size(); i += 2) {
            const expression* w = l->arguments[i].get();
            if (w->get_type() == node_type::member_expr && binary_width_token(static_cast<const member_expr*>(w))) {
                const std::string_view n = static_cast<const member_expr*>(w)->member;
                kinds.push_back(n == "float32" || n == "float64" ? 2 : 1);
            } else if (binary_call(w, "text")) {
                kinds.push_back(3);
            } else if (binary_call(w, "bytes")) {
                kinds.push_back(4);
            } else {
                return {};
            }
        }
        return kinds;
    }

    // r.read(Class, layout) with a class this program declares and a statically known layout: the field counts and each
    // field's declared kind against its width (the runtime's check, at load)
    void check_binary_read(const call_expr* c) {
        if (c->arguments.size() != 2 || c->arguments[0]->get_type() != node_type::identifier_expr) { return; }
        const call_expr* l = static_layout(c->arguments[1].get());
        if (!l) { return; }
        const std::vector<uint8_t> kinds = layout_kinds(l);
        if (kinds.empty()) { return; }
        const std::string_view cname = static_cast<const identifier_expr*>(c->arguments[0].get())->name;
        std::vector<std::string_view> chain;
        for (std::string_view at = cname; !at.empty();) {
            const sclass* sc = find_sclass(at);
            if (!sc || chain.size() > 32 || sc->decl->base_classes.size() > 1) { return; }
            chain.push_back(at);
            at = sc->decl->base_classes.empty() ? std::string_view() : sc->decl->base_classes[0];
        }
        std::vector<const variable_decl*> fields;
        for (size_t i = chain.size(); i-- > 0;) {
            const sclass* sc = find_sclass(chain[i]);
            for (const auto& mem : sc->decl->members) {
                if (!mem.declaration || mem.declaration->get_type() != node_type::variable_decl) { continue; }
                auto* v = static_cast<const variable_decl*>(mem.declaration.get());
                if (sc->static_fields.count(v->name)) { continue; }
                fields.push_back(v);
            }
        }
        if (fields.size() != kinds.size()) {
            diag(diag_level::error, c->location, "Cannot read " + std::string(cname) + " from a layout of " + std::to_string(kinds.size()) +
                 (kinds.size() == 1 ? " field: " : " fields: ") + std::string(cname) + " has " + std::to_string(fields.size()) +
                 (fields.size() == 1 ? " field" : " fields"));
            return;
        }
        for (size_t i = 0; i < fields.size(); ++i) {
            const type_info* t = fields[i]->type.get();
            if (!t || t->base_type == script_value_type::jai_any_type || fields[i]->function_spelling) { continue; }
            const bool ok = (kinds[i] == 1 && t->base_type == script_value_type::jai_int_type) ||
                            (kinds[i] == 2 && t->base_type == script_value_type::jai_float_type) ||
                            (kinds[i] == 3 && (t->base_type == script_value_type::jai_string_type || t->base_type == script_value_type::jai_utf8_string_type)) ||
                            (kinds[i] == 4 && t->base_type == script_value_type::jai_array_type);
            if (!ok) {
                diag(diag_level::error, c->location, "Cannot read layout field " + std::to_string(i + 1) + " into " + std::string(cname) +
                     "." + std::string(fields[i]->name) + " (declared " + t->canonical_name() + ")");
            }
        }
    }

    // s.to_bytes() / s.reader() on a proven string, and the binary::reader / binary::writer members whose arguments the
    // bound signatures cannot describe (unpack's pairs, read's class and layout, pack's layout)
    bool binary_member_call(const call_expr* c, const member_expr* m, const ctype& recv, const std::vector<ctype>& args, ctype& out) {
        if (m->is_static) { return false; }
        if ((recv.kind == ctype::string_k || recv.kind == ctype::utf8_k) && (!recv.tentative || recv.element_proof)) {
            if (m->member == "to_bytes") { out = ctype_from_type_info(eng_.get_type_info_array(eng_.get_type_info_uint8())); return true; }
            if (m->member == "reader" && args.empty()) { out = object_of("binary::reader"); return true; }
            return false;
        }
        if (is_class_kind(recv, "binary::reader")) {
            if (m->member == "unpack") {
                if (!(args.size() == 1 && (args[0].is_unknown() || is_class_kind(args[0], "binary::layout")))) { check_binary_pairs(c, args, "unpack"); }
                out = ctype::make(ctype::map_k);
                return true;
            }
            if (m->member == "read" && args.size() == 2) {
                if (!args[1].is_unknown() && !is_class_kind(args[1], "binary::layout")) {
                    diag(diag_level::error, c->arguments[1]->location, "read expects a binary::layout, got " + ctype_name(args[1]));
                }
                check_binary_read(c);
                const expression* cls = c->arguments[0].get();
                out = cls->get_type() == node_type::identifier_expr && find_sclass(static_cast<const identifier_expr*>(cls)->name)
                    ? object_of(static_cast<const identifier_expr*>(cls)->name) : ctype{};
                return true;
            }
            return false;
        }
        if (is_class_kind(recv, "binary::writer") && m->member == "pack" && args.size() == 2) {
            if (!args[0].is_unknown() && !is_class_kind(args[0], "binary::layout")) {
                diag(diag_level::error, c->arguments[0]->location, "pack expects a binary::layout, got " + ctype_name(args[0]));
            }
            if (!args[1].is_unknown() && args[1].kind != ctype::map_k) {
                diag(diag_level::error, c->arguments[1]->location, "pack expects a map of field values, got " + ctype_name(args[1]));
            }
            out = object_of("binary::writer");
            return true;
        }
        return false;
    }

    // the result kind a strict host class's bound method declares (class_definition::strict_signatures)
    ctype host_result(const param_type_info& r) {
        switch (r.base_type) {
            case script_value_type::jai_int_type: return ctype::make(ctype::int_k);
            case script_value_type::jai_float_type: return ctype::make(ctype::float_k);
            case script_value_type::jai_bool_type: return ctype::make(ctype::bool_k);
            case script_value_type::jai_char_type: return ctype::make(ctype::char_k);
            case script_value_type::jai_string_type: return ctype::make(ctype::string_k);
            case script_value_type::jai_array_type: return ctype::make(ctype::array_k);
            case script_value_type::jai_map_type: return ctype::make(ctype::map_k);
            case script_value_type::jai_object_type:
                if (auto def = eng_.get_class_definition_by_type(r.cpp_type)) { return object_of(def->get_name()); }
                return {};
            default: return {};
        }
    }

    // a method call on a host class that opted into strict signatures: the arity and argument kinds its overloads accept, and
    // the result they agree on; other host classes stay lenient
    ctype check_strict_host_method(std::string_view cls, std::string_view member, const std::vector<ctype>& args, const source_location& loc) {
        auto def = engine_class(cls);
        if (!def || !def->is_cpp_class() || !def->strict_signatures() || def->extended_ever()) { return {}; }
        const auto& table = def->cpp_method_overload_table();
        auto it = table.find(eng_.symbolize(member));
        if (it == table.end()) { return {}; }
        const std::string fname = std::string(cls) + "." + std::string(member);
        auto bucket = it->second.find(args.size());
        if (bucket == it->second.end() || bucket->second.empty()) {
            std::vector<size_t> arities;
            for (const auto& [n, entries] : it->second) { if (!entries.empty()) { arities.push_back(n); } }
            std::sort(arities.begin(), arities.end());
            std::string text;
            for (size_t n : arities) { text += (text.empty() ? "" : " or ") + std::to_string(n); }
            diag(diag_level::error, loc, "'" + fname + "' expects " + text + " argument(s), got " + std::to_string(args.size()));
            return {};
        }
        std::optional<ctype> result;
        bool agree = true;
        bool viable = false;
        for (const auto& entry : bucket->second) {
            bool ok = true;
            for (size_t i = 0; i < args.size() && i < entry.param_types.size(); ++i) {
                if (!host_param_accepts(args[i], entry.param_types[i])) { ok = false; break; }
            }
            if (!ok) { continue; }
            viable = true;
            ctype r = host_result(entry.result);
            if (!result) { result = r; }
            else if (result->kind != r.kind || result->cls != r.cls) { agree = false; }
        }
        if (!viable) {
            diag(diag_level::error, loc, "no overload of '" + fname + "' accepts these argument types");
            return {};
        }
        return agree && result ? *result : ctype{};
    }

    // -------------------------------------------------------------- calls

    struct call_check {
        bool checked = false;   // a statically known callee was actually validated
        ctype result{};
    };

    // args already inferred by the caller; foreign = the overloads belong to another unit's source; scored = the runtime picks
    // among them by type (methods); a program's free functions of one name are redefinitions, the last one taking the call
    call_check check_script_overloads(std::string_view fname, const std::vector<const function_decl*>& overloads,
                                      const std::vector<ctype>& args, const source_location& loc, bool foreign = false,
                                      bool scored = true) {
        const function_decl* only_viable = nullptr;
        int viable_count = 0;
        for (const auto* fn : overloads) {
            if (script_fn_viable(fn, args, scored && arity_rivals(overloads, args.size()))) {
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
        if (viable_count == 1) {
            out.result = only_viable->is_coroutine ? ctype::make(ctype::coroutine_k) : ctype_from_type_info(only_viable->return_type.get());
        }
        if (viable_count > 1 && scored) { report_ambiguous_call(fname, overloads, args, loc); }
        return out;
    }

    // Steps from cls up its single-parent chain to base (0 = itself); SIZE_MAX when base is no ancestor or the chain is not modeled
    size_t class_distance(std::string_view cls, std::string_view base) {
        size_t steps = 0;
        std::string_view at = cls;
        while (steps <= 32) {
            if (at == base) { return steps; }
            const sclass* c = find_sclass(at);
            if (!c || c->decl->base_classes.size() != 1) { break; }
            at = c->decl->base_classes[0];
            ++steps;
        }
        auto def = engine_class(at);
        if (!def) { return SIZE_MAX; }
        const size_t runtime = detail_base_distance(def.get(), base);
        return runtime == SIZE_MAX ? SIZE_MAX : steps + runtime;
    }

    static size_t detail_base_distance(const class_definition* def, std::string_view name) { return base_distance(def, name); }

    // The runtime scorer's keys (core/overload_resolution.hpp score_overloads) over static argument types; nullopt when an argument
    // or a parameter is outside what the static view ranks exactly (an unknown kind, an object a parameter may convert, a container,
    // a handle). viable=false: a typed parameter the argument matches neither exactly nor by conversion, with no var slot
    struct static_rank {
        bool viable = true;
        size_t mismatches = 0, var_count = 0, defaults = 0, conversions = 0, distance = 0;
        int score = 0;
        auto key() const { return std::tie(mismatches, score, var_count, defaults, conversions, distance); }
    };
    std::optional<static_rank> static_overload_rank(const function_decl* fn, const std::vector<ctype>& args, bool in_set) {
        auto [min_a, max_a] = arity_range(fn);
        if (args.size() < min_a || args.size() > max_a) { return static_rank{false}; }
        static_rank r;
        r.defaults = fn->parameters.size() - args.size();
        bool all_exact = true, all_convertible = true, has_var = false, loose = false;
        for (size_t i = 0; i < args.size(); ++i) {
            const parameter& p = fn->parameters[i];
            if (!p.type || p.type->type_name.empty() || p.type->type_name == "any" || p.type->base_type == script_value_type::jai_any_type) {
                has_var = true;
                ++r.var_count;
                continue;
            }
            const ctype& a = args[i];
            if (a.is_unknown()) { return std::nullopt; }
            const ctype pt = ctype_from_type_info(p.type.get());
            auto cost = [&](size_t c) {
                all_exact = false;
                if (c) { r.conversions += c; loose = loose || c >= k_loose_conversion; } else { all_convertible = false; ++r.mismatches; }
            };
            const bool primitive_a = a.kind == ctype::int_k || a.kind == ctype::float_k || a.kind == ctype::bool_k || a.kind == ctype::char_k ||
                                     a.kind == ctype::string_k || a.kind == ctype::utf8_k;
            if (pt.kind == ctype::object_k) {
                if (a.kind != ctype::object_k) { return std::nullopt; }   // a converting constructor may take it
                if (a.cls == pt.cls) { continue; }
                const size_t d = class_distance(a.cls, pt.cls);
                if (d == SIZE_MAX) { return std::nullopt; }
                all_exact = false;
                ++r.conversions;
                r.distance += d;
                continue;
            }
            if (!primitive_a) { return std::nullopt; }
            if (a.kind == pt.kind) { continue; }
            const bool number_to = pt.kind == ctype::int_k || pt.kind == ctype::float_k;
            if (number_to && (a.kind == ctype::int_k || a.kind == ctype::float_k)) { cost(1); continue; }
            if (p.is_reference) { cost(0); continue; }
            switch (pt.kind) {
                case ctype::int_k: case ctype::float_k: cost(a.kind == ctype::bool_k ? 1 : 0); break;
                case ctype::bool_k: cost(in_set && (a.kind == ctype::string_k || a.kind == ctype::utf8_k) ? 0 : 2); break;
                case ctype::string_k: case ctype::utf8_k: cost(2); break;
                case ctype::char_k: cost(0); break;
                default: return std::nullopt;
            }
        }
        if (!all_convertible && !has_var) { r.viable = false; return r; }
        const bool all_typed = !has_var;
        if (all_typed && all_exact) { r.score = 0; }
        else if (loose) { r.score = static_cast<int>(2000 + r.conversions); }
        else if (all_typed) { r.score = static_cast<int>(1 + r.conversions); }
        else { r.score = 1000; }
        return r;
    }

    // Two overloads the runtime scorer could not tell apart on these argument types: the runtime's ambiguous-call text
    void report_ambiguous_call(std::string_view fname, const std::vector<const function_decl*>& overloads, const std::vector<ctype>& args,
                               const source_location& loc) {
        const bool in_set = arity_rivals(overloads, args.size());
        const function_decl* best = nullptr;
        const function_decl* rival = nullptr;
        std::optional<static_rank> best_rank;
        for (const auto* fn : overloads) {
            const auto rank = static_overload_rank(fn, args, in_set);
            if (!rank) { return; }
            if (!rank->viable) { continue; }
            if (!best_rank || rank->key() < best_rank->key()) { best = fn; best_rank = rank; rival = nullptr; }
            else if (rank->key() == best_rank->key() && !rival) { rival = fn; }
        }
        if (!best || !rival) { return; }
        std::string list = "(";
        for (size_t i = 0; i < args.size() && i < 6; ++i) { if (i) { list += ", "; } list += ctype_name(args[i]); }
        if (args.size() > 6) { list += ", ..."; }
        list += ")";
        const std::string name(fname);
        diag(diag_level::error, loc, "ambiguous call to '" + name + "' with " + list + ": '" + name + detail::parameter_signature(best->parameters) +
             "' and '" + name + detail::parameter_signature(rival->parameters) + "' match equally",
             best->location.line, best->location.column);
    }

    // A T& parameter's argument: a primitive of exactly that kind, a class T or a subclass; unknown and var& / auto& pass
    bool reference_parameter_binds(const parameter& p, const ctype& arg) {
        if (!p.type || arg.is_unknown()) { return true; }
        switch (p.type->base_type) {
            case script_value_type::jai_int_type: case script_value_type::jai_float_type: case script_value_type::jai_bool_type:
            case script_value_type::jai_char_type: case script_value_type::jai_string_type: case script_value_type::jai_utf8_string_type:
            case script_value_type::jai_object_type:
            case script_value_type::jai_vec2_type: case script_value_type::jai_vec3_type: case script_value_type::jai_vec4_type:
            case script_value_type::jai_ivec2_type: case script_value_type::jai_ivec3_type: case script_value_type::jai_color_type:
                return reference_type_compatible(arg, ctype_from_type_info(p.type.get()));
            case script_value_type::jai_array_type: case script_value_type::jai_map_type:
            case script_value_type::jai_shared_ptr_type: case script_value_type::jai_weak_ptr_type:
                if (!arg.ti || !concrete_reference_type(arg.ti) || !concrete_reference_type(p.type.get())) { return true; }
                return reference_type_compatible(arg, ctype_from_type_info(p.type.get()));
            default:
                return true;
        }
    }

    // Several overloads admit this argument count: the runtime scores them, else the one the arity admits binds and converts
    template <typename Overloads>
    static bool arity_rivals(const Overloads& overloads, size_t argc) {
        size_t admitted = 0;
        for (const auto& fn : overloads) {
            auto [min_a, max_a] = arity_range(&*fn);
            if (argc >= min_a && argc <= max_a && ++admitted > 1) { return true; }
        }
        return false;
    }

    // in_set: fn is one of several overloads, where a string's truthiness never makes a bool parameter viable (the
    // runtime scorer's rule); a lone function's bool slot still takes it by assignment
    bool script_fn_viable(const function_decl* fn, const std::vector<ctype>& args, bool in_set = false) {
        auto [min_a, max_a] = arity_range(fn);
        if (args.size() < min_a || args.size() > max_a) { return false; }
        for (size_t i = 0; i < args.size(); ++i) {
            const parameter& p = fn->parameters[i];
            if (p.is_reference) {   // a T& binds a T (a class: T or a subclass); var& and auto& bind any
                if (!reference_parameter_binds(p, args[i])) { return false; }
                continue;
            }
            if (in_set && p.type && p.type->base_type == script_value_type::jai_bool_type &&
                (args[i].kind == ctype::string_k || args[i].kind == ctype::utf8_k)) {
                return false;
            }
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
				if (p.is_reference) {
					if (!reference_parameter_binds(p, args[i])) {
						diag(diag_level::error, loc, "argument " + std::to_string(i + 1) + " of '" + std::string(fname) + "': cannot bind '" +
							 ctype_name(args[i]) + "' to '" + p.name + "' declared '" + p.type->canonical_name() + "&'", rl, rc);
						return;
					}
					continue;
				}
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

    // A host function's declared scalar result (every registration agrees): tentative, a diagnostics fact only
    ctype host_result_type(const std::string& name) {
        script_value_type k;
        if (!eng_.host_function_result(name, k)) { return {}; }
        ctype t;
        switch (k) {
            case script_value_type::jai_int_type: t = ctype::make(ctype::int_k); break;
            case script_value_type::jai_float_type: t = ctype::make(ctype::float_k); break;
            case script_value_type::jai_bool_type: t = ctype::make(ctype::bool_k); break;
            case script_value_type::jai_char_type: t = ctype::make(ctype::char_k); break;
            case script_value_type::jai_string_type: t = ctype::make(ctype::string_k); break;
            default: return {};
        }
        t.tentative = true;
        return t;
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
                return k == ctype::string_k || k == ctype::utf8_k;   // no builtin numeric->string conversion at the C++ boundary
            case script_value_type::jai_array_type:
                return k == ctype::array_k;
            case script_value_type::jai_map_type:
                return k == ctype::map_k;
            default:
                return true;   // objects/pointers: conversion registry territory, lenient
        }
    }

    // The runtime's aggregate rule (detail::collect_aggregate_fields): every class in the chain is a script class declaring
    // no constructor, no instance field is private or a reference; the instance fields in declaration order, bases first
    enum class aggregate_shape : uint8_t { aggregate, not_aggregate, unknown };
    aggregate_shape aggregate_fields(const sclass& c, std::vector<const variable_decl*>& out, int depth = 0) {
        if (depth > 64 || !c.decl) { return aggregate_shape::unknown; }
        if (!c.ctors.empty()) { return aggregate_shape::not_aggregate; }
        for (auto base : c.decl->base_classes) {
            const sclass* b = find_sclass(base);
            if (!b) {
                auto def = engine_class(base);
                return def && !def->is_script_class() ? aggregate_shape::not_aggregate : aggregate_shape::unknown;
            }
            const aggregate_shape shape = aggregate_fields(*b, out, depth + 1);
            if (shape != aggregate_shape::aggregate) { return shape; }
        }
        for (const auto& m : c.decl->members) {
            if (!m.declaration || m.declaration->get_type() != node_type::variable_decl) { continue; }
            auto* f = static_cast<const variable_decl*>(m.declaration.get());
            if (f->is_static) { continue; }
            if (c.nonpublic.count(f->name) || (f->type && f->type->base_type == script_value_type::jai_reference_type)) {
                return aggregate_shape::not_aggregate;
            }
            out.push_back(f);
        }
        return aggregate_shape::aggregate;
    }

    // The base an empty super() default-constructs (C++ implicit construction): a modeled script class whose constructors
    // all need arguments, reached through constructor-less script bases; empty when it can or a host class decides
    std::string_view base_without_default(std::string_view cls, int depth = 0) {
        if (depth > 32) { return {}; }
        const sclass* c = find_sclass(cls);
        if (!c || !c->decl) { return {}; }
        if (!c->ctors.empty()) {
            const std::vector<ctype> none;
            for (const auto* ctor : c->ctors) {
                if (script_fn_viable(ctor, none)) { return {}; }
            }
            return c->decl->name;
        }
        if (c->decl->base_classes.empty()) { return {}; }
        return base_without_default(c->decl->base_classes.front(), depth + 1);   // the base super() reaches
    }

    // cls default-constructs its bases (an empty super(), or the implicit default constructor)
    void report_base_without_default(std::string_view cls, const source_location& loc) {
        const sclass* c = find_sclass(cls);
        if (!c || !c->decl || c->decl->base_classes.empty()) { return; }
        if (auto missing = base_without_default(c->decl->base_classes.front()); !missing.empty()) {
            diag(diag_level::error, loc, "Base '" + std::string(missing) + "' has no default constructor; call super(...)");
        }
    }

    void check_ctor_call(std::string_view cls_name, const sclass& c, const std::vector<ctype>& args,
                         const source_location& loc) {
        if (c.ctors.empty() && args.empty()) { report_base_without_default(cls_name, loc); }
        if (c.ctors.empty()) {
            // implicit default + implicit copy, else aggregate initialization (detail/aggregate_init.hpp)
            bool copy_ok = args.size() == 1 &&
                (args[0].is_unknown() || (args[0].kind == ctype::object_k && class_relation(args[0].cls, cls_name) != 0));
            if (args.empty() || copy_ok) { return; }
            std::vector<const variable_decl*> fields;
            switch (aggregate_fields(c, fields)) {
                case aggregate_shape::unknown: return;
                case aggregate_shape::aggregate:
                    if (args.size() > fields.size()) {   // the runtime's text (detail::too_many_initializers)
                        diag(diag_level::error, loc, "too many initializers for '" + std::string(cls_name) + "': it has " +
                             std::to_string(fields.size()) + (fields.size() == 1 ? " field" : " fields") + ", got " + std::to_string(args.size()),
                             c.foreign ? 0 : c.decl->location.line, c.foreign ? 0 : c.decl->location.column);
                        return;
                    }
                    for (size_t i = 0; i < args.size(); ++i) {
                        const variable_decl* f = fields[i];
                        if (!f->type || f->type->base_type == script_value_type::jai_any_type) { continue; }
                        check_store(args[i], false, token_type::equal, ctype_from_type_info(f->type.get()), declared_variable_type_name(f),
                                    "field '" + std::string(f->name) + "'", loc, 0, 0);
                    }
                    return;
                case aggregate_shape::not_aggregate:
                    break;
            }
            {   // the runtime's text (detail::overload_miss)
                std::string list = "(";
                for (size_t i = 0; i < args.size() && i < 6; ++i) { if (i) { list += ", "; } list += ctype_name(args[i]); }
                if (args.size() > 6) { list += ", ..."; }
                diag(diag_level::error, loc, "no constructor of '" + std::string(cls_name) + "' takes " + list + ")",
                     c.foreign ? 0 : c.decl->location.line, c.foreign ? 0 : c.decl->location.column);
            }
            return;
        }
        size_t viable = 0;
        size_t viable_private = 0;
        for (const auto* ctor : c.ctors) {
            if (script_fn_viable(ctor, args, arity_rivals(c.ctors, args.size()))) {
                ++viable;
                if (std::find(c.private_ctors.begin(), c.private_ctors.end(), ctor) != c.private_ctors.end()) { ++viable_private; }
            }
        }
        if (viable > 1) { report_ambiguous_call(cls_name, c.ctors, args, loc); }
        if (viable) {
            // whichever overload the runtime picks is private: the raise detail::enforce_constructor_access gives
            const std::string_view ctx = class_ctx_ ? class_ctx_name_ : std::string_view{};
            if (viable_private == viable && (ctx.empty() || !class_is_or_derives(ctx, cls_name))) {
                diag(diag_level::error, loc, "Cannot access private constructor of class '" + std::string(cls_name) + "'");
            }
            return;
        }
        report_no_overload(cls_name, c.ctors, args, loc, c.foreign);
    }

    // -------------------------------------------------------------- expressions

	ctype infer(expression* e) {
		ctype result = infer_impl(e);
		result.origin = e;
		if (observer_ && e) { observe(e, result); }
		return result;
	}

	void observe(const expression* e, const ctype& t) {
		static_assert(static_cast<uint8_t>(check_expression_type::kind_t::utf8_k) == ctype::utf8_k);
		check_expression_type view;
		view.kind = static_cast<check_expression_type::kind_t>(t.kind);
		view.cls = t.cls;
		view.ti = t.ti;
		view.tentative = t.tentative;
		view.name = ctype_name(t);
		observer_->expression_type(e, view);
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
					const ctype cont = declared_container_receiver(b->left.get(), l);
					const ctype el = container_value_type(cont);
					const bool object_elements = el.kind == ctype::object_k && cont.kind == ctype::array_k && (!cont.tentative || cont.alias_proof);
					const bool vec_elements = el.kind == ctype::math_k && cont.kind == ctype::array_k && (!cont.tentative || cont.alias_proof);
					const uint8_t cls = object_elements ? operand_class_object_element
					                  : vec_elements ? operand_class_vec_element
					                  : !el.element_proof ? operand_class_unknown
					                  : el.kind == ctype::int_k ? operand_class_int_int
					                  : el.kind == ctype::float_k ? operand_class_float_float : operand_class_unknown;
					stamp(b->static_operand_class, cls, operand_class_unknown);
					const uint8_t receiver = cont.tentative ? 0 : cont.kind == ctype::string_k ? 1 : cont.kind == ctype::array_k ? 2 : cont.kind == ctype::map_k ? 3 : 0;
					stamp(b->subscript_receiver, receiver, uint8_t(0));
					return el;
				}
				if (b->op.type == token_type::question_question) {
					// the left value unless it is null, else the right: one type when both sides have it, the right's when the left is null
					if (l.kind == ctype::null_k) { return r; }
					if (l.kind == r.kind && l.cls == r.cls && !l.is_unknown()) {
						if (l.ti != r.ti) { l.ti = nullptr; }
						l.tentative = l.tentative || r.tentative;
						return l;
					}
					return {};
				}
				stamp(b->static_operand_class, operand_class_of(l, r), operand_class_unknown);   // annotation only
				if (l.kind == ctype::object_k) {
					if (auto user = class_binary_operator(l.cls, b->op.type, r)) { return *user; }
				}
				return binary_result(l, b->op.type, r);
			}
			case node_type::unary_expr: {
				auto* u = static_cast<unary_expr*>(e);
				ctype operand = infer(u->operand.get());
				if (operand.kind == ctype::object_k && (u->op.type == token_type::minus || u->op.type == token_type::plus ||
				                                        u->op.type == token_type::tilde)) {
					if (auto user = class_unary_operator(operand.cls, u->op.type)) { return *user; }
				}
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
			case node_type::import_expr: {
				for (const auto& a : static_cast<import_expr*>(e)->path_exprs) {
					const ctype t = infer(a.get());
					if (!t.is_unknown() && t.kind != ctype::string_k && t.kind != ctype::utf8_k && t.kind != ctype::array_k) {
						diag(diag_level::error, a->location, "an import path is a string or an array of strings, got '" + ctype_name(t) + "'");
					}
				}
				return ctype::make(ctype::bool_k);   // true when a module (re)ran
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
				if (!a.is_unknown() && !b.is_unknown() && a.kind != ctype::null_k && b.kind != ctype::null_k) {   // a T-or-null arm pair is a nullable value, never a type mix
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
				if (!capture_stack_.empty() && !fn_stack_.back().sees_fields &&
					capture_stack_.back().lambda->default_capture == lambda_expr::capture_default::none) {
					diag(diag_level::error, e->location, "lambda capture list does not capture 'this'; use [this], [=] or [&]");
				}
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
				if (c->implicit_ref) {   // a piece of an assignment target read as a reference: its operand's type
					ctype operand_t = infer(c->operand.get());
					operand_t.tentative = true;
					return operand_t;
				}
				ctype operand_t = infer(c->operand.get());
				check_const_cast(c);
				ctype t = ctype_from_type_info(c->target.get());
				if (t.kind == ctype::object_k && !known_class(t.cls) && !extra_global(std::string(t.cls))) {
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

    bool bare_this_field_ = false;
    ctype infer_identifier(identifier_expr* id) {
        std::optional<field_proof> fp;
        bare_this_field_ = false;
        ctype t = infer_identifier_impl(id, fp);
        stamp_field(id, fp);
        stamp(id->this_field, bare_this_field_, false);
        if (extended_names_.count(id->name)) { return {}; }
        return t;
    }

	ctype infer_identifier_impl(identifier_expr* id, std::optional<field_proof>& fp) {
		size_t scope_index = 0;
		if (var_entry* local = find_named(id, &scope_index)) {
			if (scope_index == 0) { note_global_use(id->name); }   // top-level var read
			for (const auto& cap : capture_stack_) {
				const bool program_scope = scope_index == 0 ||
					std::find(namespace_scopes_.begin(), namespace_scopes_.end(), scope_index) != namespace_scopes_.end();
				if (program_scope || scope_index >= cap.base || cap.lambda->default_capture != lambda_expr::capture_default::none) { continue; }
				bool listed = std::any_of(cap.lambda->captures.begin(), cap.lambda->captures.end(),
					[&](const auto& c) { return c.name == id->name; });
				if (!listed) {
					diag(diag_level::error, id->location, "lambda capture list does not capture enclosing local '" +
						std::string(id->name) + "'; name it or use [=] or [&]", local->line, local->column);
				}
			}
			return local->dynamic ? (local->alias.kind != ctype::unknown ? local->alias : ctype{}) : local->type;
		}
		if (class_ctx_ && !id->global_qualified) {
			member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
			if (hit.found) {
				if (!capture_stack_.empty() && !fn_stack_.back().sees_fields &&
					capture_stack_.back().lambda->default_capture == lambda_expr::capture_default::none) {
					diag(diag_level::error, id->location, "lambda capture list does not capture 'this' for member '" + std::string(id->name) + "'; use [this], [=] or [&]");
					return {};
				}
				if (access_denied(hit, id->name, id->location)) { return {}; }
				if (bare_field_context()) { fp = resolve_field_proof(class_ctx_name_, id->name, hit); }
				if (hit.methods) {
					if (!hit.methods->empty() && !hit.methods->front()->is_static && bare_field_context()) {   // a method is called or bound, never read bare
						diag(diag_level::error, id->location, detail::bare_method_value_text(id->name, class_ctx_name_));
					}
					return ctype::make(ctype::function_k);
				}
				if (hit.field && !hit.field->is_static && !hit.methods && bare_field_context() && !chain_has_user_accessor(class_ctx_name_, id->name)) { bare_this_field_ = true; }
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
		if (const variable_decl* g = program_global(name)) { note_global_use(id->name); return program_global_type(g); }   // the proof pass alone types a global another unit declares
		if (extra_global(name)) { note_global_use(id->name); return {}; }
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

    // an enum's value by the enum's own name (this unit's enum, or the one enum of that name another unit of the program declares):
    // no local, field or variable of the program takes the name and no runtime include could
    bool enum_value_read(const member_expr* m, const ctype& recv, const ctype& t) {
        if (m->is_static || m->null_safe || !m->object || m->object->get_type() != node_type::identifier_expr || has_dynamic_includes_) return false;
        auto* id = static_cast<const identifier_expr*>(m->object.get());
        const std::string name(id->name);
        if (program_global(name)) return false;
        if (recv.kind == ctype::enum_ref_k) return t.kind == ctype::int_k;
        if (!recv.is_unknown() || class_ctx_ || id->slot_index != SIZE_MAX || id->global_qualified || !program_enums_) return false;
        if (find_named(id) || functions_.count(id->name) || enums_.count(id->name)) return false;
        const auto it = program_enums_->find(name);
        if (it == program_enums_->end() || it->second.size() != 1 || !it->second.begin()->second) return false;
        for (const auto& [vname, _] : it->second.begin()->second->values) { if (vname == m->member) return true; }
        return false;
    }

    // a member read through a reference local declared of a class (`Body& b = bodies[i]; b.x`): the binding admits that class or a
    // subclass, so a scalar field's declared kind is a proof source the typed rows guard, tentative for diagnostics (an element read's standing)
    ctype reference_field_read(const member_expr* m) {
        if (m->is_static || m->null_safe || !m->object || m->object->get_type() != node_type::identifier_expr) { return {}; }
        const auto* local = find_local(static_cast<const identifier_expr*>(m->object.get())->name);
        if (!local || !local->is_reference || !local->declared) { return {}; }
        const ctype declared = ctype_from_type_info(local->declared);
        if (declared.kind != ctype::object_k) { return {}; }
        const member_hit h = lookup_member(declared.cls, m->member, false);
        if (!h.found || h.methods || !h.field || h.field->is_static || !h.field->type || chain_has_user_accessor(declared.cls, m->member)) { return {}; }
        ctype t = ctype_from_type_info(h.field->type.get());
        if (t.kind != ctype::int_k && t.kind != ctype::float_k) { return {}; }
        t.tentative = true;
        t.element_proof = true;
        return t;
    }

    ctype infer_member(member_expr* m, bool writing, const variable_decl** typed_field_out = nullptr,
                       ctype* recv_out = nullptr, member_hit* hit_out = nullptr,
                       std::optional<field_proof>* proof_out = nullptr) {
        std::optional<field_proof> fp;
        ctype recv;
        ctype t = infer_member_impl(m, writing, typed_field_out, &recv, hit_out, fp);
        if (recv_out) { *recv_out = recv; }
        stamp_field(m, fp);
        stamp(m->enum_constant, !writing && enum_value_read(m, recv, t), false);
        if (proof_out) { *proof_out = fp; }
        return t;
    }

	ctype infer_member_impl(member_expr* m, bool writing, const variable_decl** typed_field_out,
							ctype* recv_out, member_hit* hit_out, std::optional<field_proof>& fp) {
		if (m->is_static && binary_width_token(m)) { return object_of("binary::width"); }
		if (m->is_static && namespace_path_receiver(m)) { return {}; }
		ctype recv = infer(m->object.get());
		if (recv_out) { *recv_out = recv; }
		if (recv.kind == ctype::math_k) {
			// a component (x / width, y / height, z / depth): an int or a float for diagnostics, never a proof source (the
			// VM's component rows are a later phase); anything else is the runtime's no-member text, statically
			const script_value_type kind = math_type_from_name(recv.cls);
			if (detail::math::component_of(kind, m->member) < 0) {
				if (detail::vec_method_fn(kind, m->member) != math_fn::none) { return ctype::make(ctype::function_k); }
				diag(diag_level::error, m->location, std::string(recv.cls) + " has no member '" + std::string(m->member) + "'");
				return {};
			}
			ctype c = ctype::make(detail::math::integral(kind) ? ctype::int_k : ctype::float_k);
			c.tentative = true;
			return c;
		}
		if (recv.kind == ctype::coroutine_k) {
			if (!recv.tentative && m->member != "next" && m->member != "done") { coroutine_member_missing(m); }
			return {};
		}
		if (recv.is_unknown() && !writing) {
			if (ctype t = reference_field_read(m); !t.is_unknown()) { return t; }
		}
		if (recv.is_unknown() || recv.kind == ctype::map_k || recv.kind == ctype::array_k ||
			recv.kind == ctype::string_k || recv.kind == ctype::utf8_k || recv.kind == ctype::function_k || recv.kind == ctype::weak_k || recv.kind == ctype::null_k ||
			recv.kind == ctype::int_k || recv.kind == ctype::float_k || recv.kind == ctype::char_k ||
			recv.kind == ctype::bool_k) {
			if (!writing && !recv.tentative && !m->is_static) { builtin_method_value(recv, m); }
			return {};   // builtin methods / map keys / dynamic: lenient
		}
		if (extended_members_.count(m->member)) { return {}; }
		if (class_dot_receiver(m, recv)) { return {}; }
		if (recv.kind == ctype::enum_ref_k) {
			if (const enum_decl* e = find_enum(recv.cls)) {
				for (const auto& [vname, _] : e->values) {
					if (vname == m->member) { return ctype::make(ctype::int_k); }
				}
				diag(diag_level::error, m->location, "no value '" + std::string(m->member) +
					 "' in enum '" + std::string(recv.cls) + "'",
					 e->location.line, e->location.column);
			}
			return {};
		}
		bool want_static = (recv.kind == ctype::class_ref_k);
		std::string_view cls = recv.cls;
		member_hit hit = lookup_member(cls, m->member, want_static);
		if (access_denied(hit, m->member, m->location)) { return {}; }
		if (hit_out) { *hit_out = hit; }
		if (!want_static && chain_has_user_accessor(cls, m->member)) { return accessor_read_type(cls, m->member); }   // a user _get_/_set_ answers: known member, typed only by a single declared getter, never a proof
		if (hit.found) {
			if (typed_field_out && hit.field && (hit.field->type || hit.field->function_spelling)) { *typed_field_out = hit.field; }
			if (recv.kind == ctype::object_k && !m->null_safe && !m->is_static &&
				m->object->get_type() != node_type::super_expr) {
				fp = resolve_field_proof(cls, m->member, hit);
			}
			if (hit.methods) {
				if (!writing && !want_static) {   // a method is called or bound, never read bare
					diag(diag_level::error, m->location, detail::method_value_text(m->member, cls, detail::method_value_receiver(m), false));
				}
				return ctype::make(ctype::function_k);
			}
			if (hit.field && (hit.field->type || hit.field->function_spelling)) { return member_type(hit); }
			return {};
		}
		if (!hit.certain) { return {}; }
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
        ov_class_ = {};
        ctype t = infer_call_impl(c, args, ov);
        const std::string_view ov_class = ov ? ov_class_ : std::string_view{};
        bool free_callee = false;   // resolved through the program's free functions (a method by its bare name, a constructor: never inlined)
        if (ov && c->callee && c->callee->get_type() == node_type::identifier_expr) {
            auto it = functions_.find(static_cast<identifier_expr*>(c->callee.get())->name);
            free_callee = it != functions_.end() && ov == &it->second;
        }
        check_const_call(c);
        check_const_arguments(c, ov, ov_class, free_callee, args);
        // a free function another unit of the program declares (one unit, once): the inliner's decl and the argument proofs; the
        // unit's own diagnostics keep their view (ov stays the unit's)
        std::vector<const function_decl*> program_ov;
        if (!ov && program_functions_ && c->callee && c->callee->get_type() == node_type::identifier_expr) {
            const std::string_view name = static_cast<identifier_expr*>(c->callee.get())->name;
            if (functions_.find(name) == functions_.end()) {
                auto pit = program_functions_->find(std::string(name));
                if (pit != program_functions_->end() && pit->second.size() == 1 && pit->second.begin()->second.size() == 1) { program_ov.push_back(pit->second.begin()->second[0].get()); }
            }
            // the proof pass alone types the call by that declaration's scalar return (the consumers guard it; a graft re-checks the
            // callee's declared return per definition epoch, call_expr::k_proof_function)
            if (t.is_unknown() && program_ov.size() == 1 && semantic_walk()) {
                const uint16_t k = certain_kind(program_ov[0]->return_type.get());
                if (k != 0 && (k & 0xFF) != static_cast<uint16_t>(script_value_type::jai_array_type)) { t = ctype_from_type_info(program_ov[0]->return_type.get()); }
            }
        }
        // a local declared with a lambda literal: the lambda as a one-overload declaration (the guard checks the binding at run time)
        const std::vector<const function_decl*>* lambda_ov = nullptr;
        if (!ov && program_ov.empty() && c->callee && c->callee->get_type() == node_type::identifier_expr) {
            auto* id = static_cast<identifier_expr*>(c->callee.get());
            const var_entry* local = id->slot_index != SIZE_MAX && !id->global_qualified ? find_local(id->name) : nullptr;
            if (local && local->lambda && !lambda_copies_at_creation(local->lambda)) {
                auto* lam = const_cast<lambda_expr*>(local->lambda);
                if (!lam->graft_decl && lam->body && lam->body->get_type() == node_type::block_stmt) {
                    auto d = std::make_shared<function_decl>(lam->location, std::string_view("<lambda>"));
                    d->parameters = lam->parameters; d->return_type = lam->return_type;
                    d->body = std::static_pointer_cast<block_stmt>(lam->body); d->local_count = lam->local_count;
                    lam->graft_decl = d;
                    lam->graft_overloads.assign(1, d.get());
                }
                if (lam->graft_decl) {
                    // an untyped lambda whose returns are one certain kind declares that kind with the engine's canonical stamp
                    const uint8_t k = lam->return_type ? 0 : lam->inferred_return_kind;
                    lam->graft_decl->graft_return_kind = k;
                    lam->graft_decl->return_type = k == 1 ? type_info_ptr{eng_.get_type_info_int()} : k == 2 ? type_info_ptr{eng_.get_type_info_float()}
                                                 : k == 3 ? type_info_ptr{eng_.get_type_info_bool()} : lam->return_type;
                    lambda_ov = &lam->graft_overloads;
                }
            }
        }
        stamp_call_proofs(c, lambda_ov ? lambda_ov : program_ov.empty() ? ov : &program_ov, args, free_callee || !program_ov.empty() || lambda_ov != nullptr);
        {
            const function_decl* md = nullptr;
            if (stamping() && ov && ov->size() == 1 && c->callee && c->callee->get_type() == node_type::member_expr) {
                auto* m = static_cast<member_expr*>(c->callee.get());
                auto* recv = m->object && m->object->get_type() == node_type::identifier_expr ? static_cast<identifier_expr*>(m->object.get()) : nullptr;
                const function_decl* fn = (*ov)[0];
                auto [lo, hi] = arity_range(fn);
                if (!m->is_static && !m->null_safe && recv && recv->slot_index != SIZE_MAX && !recv->global_qualified &&
                    fn->body && !fn->is_coroutine && !fn->is_static && lo == c->arguments.size() && hi == c->arguments.size()) { md = fn; }
            }
            if (proofs_ == proof_mode::stamp) { c->proven_method_decl = md ? md->weak_from_this() : std::weak_ptr<const function_decl>(); }
            else if (proofs_ == proof_mode::clear) { c->proven_method_decl.reset(); }
        }
        if (ov) {
            // a map key passed where every viable overload binds the parameter by reference
            // or as a shared_ptr handle (a by-value parameter copies)
            for (size_t i = 0; i < c->arguments.size() && i < args.size(); ++i) {
                if (!args[i].const_key) { continue; }
                bool aliasing = false, viable = false;
                for (const auto* fn : *ov) {
                    if (!script_fn_viable(fn, args, !free_callee && arity_rivals(*ov, args.size()))) { continue; }
                    viable = true;
                    aliasing = i < fn->parameters.size() &&
                               (fn->parameters[i].is_reference || handle_type(fn->parameters[i].type.get()));
                    if (!aliasing) { break; }
                }
                if (viable && aliasing) { map_key_alias(c->arguments[i]->location, c->arguments[i].get()); }
            }
            // a literal where every viable overload binds the parameter by reference: a string literal binds a writable
            // temporary for a string& overload; otherwise it binds a temporary when the callee (every override of a method)
            // never writes through the parameter, else the load reports the write
            for (size_t i = 0; i < c->arguments.size() && i < args.size(); ++i) {
                const node_type k = c->arguments[i]->get_type();
                if (k != node_type::literal_expr && k != node_type::array_literal_expr && k != node_type::map_literal_expr) { continue; }
                std::vector<const function_decl*> by_ref;
                bool all_ref = false;
                for (const auto* fn : *ov) {
                    if (!script_fn_viable(fn, args, !free_callee && arity_rivals(*ov, args.size()))) { continue; }
                    all_ref = !fn->is_coroutine && i < fn->parameters.size() && fn->parameters[i].is_reference;   // a coroutine binds a copy
                    if (!all_ref) { break; }
                    by_ref.push_back(fn);
                }
                if (!all_ref || by_ref.empty()) { continue; }
                if (args[i].kind == ctype::string_k && std::any_of(by_ref.begin(), by_ref.end(), [i](const function_decl* fn) {
                        return fn->parameters[i].type && fn->parameters[i].type->base_type == script_value_type::jai_string_type;
                    })) {
                    continue;
                }
                if (!ov_class.empty()) {
                    for (const auto* fn : overrides_of(ov_class, by_ref.front()->name)) { by_ref.push_back(fn); }
                }
                for (const auto* fn : by_ref) {
                    if (i >= fn->parameters.size() || !fn->parameters[i].is_reference || fn->parameters[i].is_const || fn->is_coroutine) { continue; }
                    const ref_param_write w = ref_writes(fn, i);
                    if (w.writes) {
                        diag(diag_level::error, c->arguments[i]->location, "'" + std::string(fn->name) + "' writes through parameter '" +
                             fn->parameters[i].name + "' (line " + std::to_string(w.line) + "); pass a variable");
                        break;
                    }
                }
            }
        }
        return t;
    }

	// Top-level code runs in order: a call there to a function this file declares only further down finds nothing yet
	void report_call_before_declaration(std::string_view name, const std::vector<const function_decl*>& decls, const source_location& at) {
		const function_decl* first = nullptr;
		for (const auto* fn : decls) {
			if (fn->location.filename != at.filename) { return; }
			if (fn->location.line < at.line || (fn->location.line == at.line && fn->location.column < at.column)) { return; }
			if (!first || fn->location.line < first->location.line) { first = fn; }
		}
		if (!first || eng_.has_function(std::string(name))) { return; }
		diag(diag_level::error, at, "'" + std::string(name) + "' is called before its declaration (line " + std::to_string(first->location.line) +
		                            "): top-level code runs in order, so declare it above the call or make the call after it");
	}

	ctype infer_call_impl(call_expr* c, const std::vector<ctype>& args, const std::vector<const function_decl*>*& ov) {
		expression* callee = c->callee.get();
		if (callee->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<identifier_expr*>(callee);
			if (id->name == "weak_from_this" || id->name == "shared_from_this") { return {}; }   // call-shape builtins, resolved before any lookup
			size_t scope_index = 0;
			if (find_local(id->name, &scope_index) && (id->global_qualified || !member_hides_program_name(id->name, scope_index))) {
				if (scope_index == 0) { note_global_use(id->name); }
				return {};   // callable local: signature unknown
			}
			if (class_ctx_ && !id->global_qualified) {
				member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
				if (hit.found) {
					if (access_denied(hit, id->name, id->location)) { return {}; }
					if (hit.methods) {
						if (hit.provable) { ov = hit.methods; ov_class_ = class_ctx_name_; }
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
				if (fn_stack_.empty() && !class_ctx_) { report_call_before_declaration(id->name, it->second, c->location); }
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
			if (extra_global(name)) { note_global_use(id->name); return {}; }
			if (eng_.is_type_name(name) || eng_.get_class_registry().find_script_class(name)) {
				return object_of(id->name);   // registered class ctor: args lenient
			}
			if (eng_.has_function(name)) {
				note_global_use(id->name);
				check_host_call(name, args, c->location);
				// a stdlib math alias yields the builtin's float wherever its name binds the builtin: a proof every consumer guards at run time
				if (args.size() == 1 && eng_.math_alias_fn(id->symbol_id) < engine::k_math_alias_rows) { return ctype::make(ctype::float_k); }
				return host_result_type(name);
			}
			if (eng_.has_variable(name)) { note_global_use(id->name); return {}; }
			if (engine_knows_name(name)) { return {}; }
			if (!id->global_qualified && report_cdtor_call(id->name, id->location)) { return {}; }
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
				if ((fn == math_fn::cstore_ || fn == math_fn::vcomp_) && !c->arguments.empty()) {
					// the lowering's steps: the component type follows from the literal kind (cstore$) or code (vcomp$)
					const expression* lit = c->arguments[fn == math_fn::cstore_ ? c->arguments.size() - 1 : 0].get();
					if (lit->get_type() == node_type::literal_expr && static_cast<const literal_expr*>(lit)->value.raw_storage_index() == script_value::TYPEID_INT) {
						const script_int code = static_cast<const literal_expr*>(lit)->value.unchecked_as_int();
						const bool integral = fn == math_fn::cstore_ ? detail::math::integral(static_cast<script_value_type>(code)) : (code & detail::k_vcomp_exact) != 0;
						return ctype::make(integral ? ctype::int_k : ctype::float_k);
					}
				}
				if (fn != math_fn::none && !detail::math_arity_accepts(fn, args.size())) { return {}; }   // the kernel raises the table's text
				if (fn != math_fn::none && args.size() <= 8) { return math_intrinsic_result(fn, args); }
				if (ctype t; library_namespace_call(c, m, args, t)) { return t; }
				if (namespace_path_receiver(m)) { return check_namespace_entry(m, args, c->location); }
			}
			ctype recv = declared_container_receiver(m->object.get(), infer(m->object.get()));
			if (recv.kind == ctype::math_k && !m->is_static) {
				// a vector's method: the free function with the receiver first (detail::vec_method_fn); anything else is the
				// runtime's no-member text
				const script_value_type kind = math_type_from_name(recv.cls);
				const math_fn fn = detail::vec_method_fn(kind, m->member);
				if (fn == math_fn::none) {
					if (!recv.tentative) { diag(diag_level::error, m->location, std::string(recv.cls) + " has no member '" + std::string(m->member) + "'"); }
					return {};
				}
				if (args.size() != detail::vec_method_arity(fn, kind)) {
					if (!recv.tentative) { diag(diag_level::error, c->location, detail::vec_method_arity_text(fn, kind)); }
					return {};
				}
				std::vector<ctype> full;
				full.reserve(args.size() + 1);
				full.push_back(recv);
				full.insert(full.end(), args.begin(), args.end());
				return math_intrinsic_result(fn, full);
			}
			if (recv.kind == ctype::coroutine_k && !m->is_static) { return coroutine_method_call(recv, m, c, args); }
			if (ctype t; binary_member_call(c, m, recv, args, t)) { return t; }
			if (recv.kind == ctype::array_k && recv.ti && m->member == "push" && args.size() == 1) {
				const auto issue = container_element_issue(args[0], recv.ti->element_type().get());
				if (!issue.empty()) {
					diag(diag_level::error, c->location, "cannot push into '" + ctype_name(recv) + "': " + issue);
				}
			}
			if (recv.const_key && string_mutator_name(m->member)) { map_key_mutation(m->location, m->object.get()); }
			if (class_dot_receiver(m, recv)) { return {}; }
			if (recv.kind == ctype::object_k || recv.kind == ctype::class_ref_k) {
				bool want_static = (recv.kind == ctype::class_ref_k);
				member_hit hit = lookup_member(recv.cls, m->member, want_static);
				if (!hit.found && !want_static && chain_has_user_accessor(recv.cls, m->member)) { return {}; }   // calling what a user _get_ returns
				if (hit.found) {
					if (hit.methods) {
						if (hit.provable) { ov = hit.methods; ov_class_ = recv.cls; }
						ctype r = check_script_overloads(m->member, *hit.methods, args, c->location, hit.foreign).result;
						if (!hit.provable) { r.tentative = true; }
						return r;
					}
					if (!want_static) { return check_strict_host_method(recv.cls, m->member, args, c->location); }
					return {};
				}
				if (!want_static) {
					if (ctype t; handle_builtin_result(m->member, t)) { return t; }
				}
				if (stale_shape_receiver(recv, m->object.get())) { return {}; }
				if (hit.certain && !want_static && m->object->get_type() == node_type::this_expr && recv.cls == class_ctx_name_ &&
					report_cdtor_call(m->member, m->location)) {
					return {};
				}
				if (hit.certain) {
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
			uint8_t proven = 0;
			if (!recv.tentative || recv.element_proof) {
				proven = recv.kind == ctype::string_k ? 1 : recv.kind == ctype::array_k ? 2 : recv.kind == ctype::map_k ? 3 : 0;
			}
			if (proven == 0 && recv.is_unknown() && m->object->get_type() == node_type::identifier_expr) {
				if (const var_entry* v = find_local(static_cast<const identifier_expr*>(m->object.get())->name)) {
					proven = v->builtin_alias == ctype::string_k ? 1 : v->builtin_alias == ctype::array_k ? 2 : v->builtin_alias == ctype::map_k ? 3 : 0;
				}
			}
			if (!recv.tentative && !m->is_static) {
				check_builtin_member(recv, m);
				checkBuiltinArity(recv, m, c);
			}
			stamp(c->builtin_receiver, proven, uint8_t(0));
			stamp(c->builtin_receiver_open, uint8_t(!proven && (recv.kind == ctype::unknown || recv.kind == ctype::string_k || recv.kind == ctype::array_k || recv.kind == ctype::map_k)), uint8_t(0));
			{
				std::vector<proof_global> deps;
				auto* rid = m->object && m->object->get_type() == node_type::identifier_expr ? static_cast<identifier_expr*>(m->object.get()) : nullptr;
				const bool certain = stamping() && proven && rid && rid->slot_index == SIZE_MAX && certain_global(rid, deps);
				c->receiver_global = certain ? deps.back() : proof_global{};
			}
			if (proven && c->arguments.empty() && (m->member == "size" || m->member == "length")) { return ctype::make(ctype::int_k); }   // every kind's size is its count
			if (recv.kind == ctype::array_k && !recv.tentative) {
				if (ctype t; array_helper_result(recv, m->member, c, args, t)) { return t; }
			}
			if (proven) {
				for (auto& a : c->arguments) {   // a builtin reads its arguments as values: a subscript argument needs no element reference
					if (a->get_type() == node_type::binary_expr && static_cast<binary_expr*>(a.get())->op.type == token_type::left_bracket) {
						stamp(static_cast<binary_expr*>(a.get())->transient_element_read, true, false);
					}
				}
			}
			return {};   // builtin container/string methods etc.
		}
		infer(callee);
		return {};
	}

    // -------------------------------------------------------------- assignment

    ctype infer_assignment(assignment_expr* a) {
        uint8_t store_proof = proof_none;
        check_const_store(a->const_check, a->target.get(), a->location);
        ctype t = infer_assignment_impl(a, store_proof);
        stamp(a->static_store_proof, store_proof, proof_none);
        if (store_proof != proof_none && stamping()) { ++counts_.stores; }
        return t;
    }

	ctype infer_assignment_impl(assignment_expr* a, uint8_t& store_proof) {
		ctype rhs = infer(a->value.get());
		struct incdec_scope { const char*& slot; const char* prev; ~incdec_scope() { slot = prev; } } incdec_guard{incdec_op_, incdec_op_};
		incdec_op_ = !a->incdec_desugar ? nullptr : a->op.type == token_type::plus_equal ? "++" : "--";
		bool compound = (a->op.type != token_type::equal);
		token_type base_op = compound_base_op(a->op.type);

		expression* target = a->target.get();
		if (target->get_type() == node_type::identifier_expr) {
			auto* id = static_cast<identifier_expr*>(target);
			std::optional<field_proof> fp;
			bool this_field = false;
			auto done = [&](ctype t) { stamp_field(id, fp); stamp(id->this_field, this_field, false); return t; };
			size_t scope_index = 0;
			if (var_entry* local = find_named(id, &scope_index)) {
				if (scope_index == 0) { note_global_use(id->name); }   // top-level var write
				if (rhs.const_key && local->is_handle && !compound) { map_key_alias(a->location, a->value.get()); return done(rhs); }
				if (local->dynamic) { return done(rhs); }
				check_store(rhs, compound, base_op, local->type, local->display,
							"'" + std::string(id->name) + "'", a->location, local->line, local->column);
				if (!compound && same_type(local->type, rhs)) { store_proof = proof_same_type; }
				if (compound) { stamp(a->static_operand_class, operand_class_of(local->type, rhs), operand_class_unknown); }   // the fused compound's typed row
				return done(local->type);
			}
			if (class_ctx_ && !id->global_qualified) {
				member_hit hit = lookup_member(class_ctx_name_, id->name, /*want_static=*/false);
				if (hit.found) {
					if (access_denied(hit, id->name, id->location)) { return done({}); }
					if (hit.read_only) { read_only_store(a->location, id->name); return done({}); }
					if (bare_field_context()) { fp = resolve_field_proof(class_ctx_name_, id->name, hit); }
					if (hit.field && !hit.field->is_static && !hit.methods && bare_field_context() && !chain_has_user_accessor(class_ctx_name_, id->name)) { this_field = true; }
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
			if (extra_global(name)) {
				note_global_use(id->name);
				ctype gt = program_scope_store_type(name);
				if (gt.is_unknown()) { return done(rhs); }
				check_store(rhs, compound, base_op, gt, ctype_name(gt), "'" + name + "'", a->location, 0, 0);
				return done(gt);
			}
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
			// a shared_ptr<T> namespace variable admits a handle by its static pointee at load; other namespace stores
			// stay run-time checks under strict (spec: a_typed_namespace_store_is_checked_at_run_time_under_strict)
			std::string qualified;
			if (!typed_field && m->is_static) {
				const variable_decl* nv = namespace_variable(m, &qualified);
				if (nv && nv->type && nv->type->base_type == script_value_type::jai_shared_ptr_type) {
					const ctype nt = program_global_type(nv);
					check_store(rhs, compound, base_op, nt, declared_variable_type_name(nv), "'" + qualified + "'", a->location,
								nv->location.line, nv->location.column);
					if (!nt.is_unknown()) { return nt; }
				}
			}
			// An assignment expression's value is the destination's stored value:
			// the member's type when the checker knows it; for a member it cannot
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
				if (recv.kind == ctype::object_k && !recv.tentative && subscript_returns_by_value(recv.cls)) {
					std::string text(detail::by_value_subscript_store_text);
					text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
					diag(diag_level::error, a->location, text);
					return rhs;
				}
				if (recv.kind == ctype::string_k || recv.kind == ctype::utf8_k) {
					// a string character takes a char or a string (its text replaces the character)
					const ctype character = ctype::make(recv.kind == ctype::string_k ? ctype::char_k : ctype::utf8_k);
					const ctype stored = compound ? binary_result(character, base_op, rhs) : rhs;
					if (!stored.is_unknown() && stored.kind != ctype::char_k && stored.kind != ctype::string_k && stored.kind != ctype::utf8_k) {
						diag(diag_level::error, a->location, "cannot store into '" + ctype_name(recv) +
							 "': a string character takes a char or a string, got '" + ctype_name(stored) + "'");
					}
					return stored;
				}
				const ctype element = container_value_type(recv);
				const ctype stored = compound ? binary_result(element, base_op, rhs) : rhs;
				const auto issue = container_element_issue(stored, element.ti);
				if (!issue.empty()) {
					diag(diag_level::error, a->location, "cannot store into '" + ctype_name(recv) + "': " + issue);
				}
				return element.is_unknown() ? stored : element;
			}
		}
		if (detail::reference_target(target)) {
			// cast<T&>(e) (its operand already is a T), f() returning T&, a conditional of lvalues, an assignment: the store
			// checks against the referent's type; a call the checker resolves to by-value overloads only names no storage
			if (target->get_type() == node_type::call_expr && call_returns_by_value(static_cast<call_expr*>(target))) {
				non_lvalue_target(a->location, target);
				infer(target);
				return rhs;
			}
			ctype t = infer(target);
			if (t.is_unknown()) { return rhs; }
			t.tentative = false;
			const std::string name = ctype_name(t);
			const bool spelled_cast = target->get_type() == node_type::cast_expr && !static_cast<cast_expr*>(target)->implicit_ref;
			check_store(rhs, compound, base_op, t, name, spelled_cast ? "a cast<" + name + "&> target" : std::string("the reference"), a->location, 0, 0);
			return t;
		}
		non_lvalue_target(a->location, target);
		infer(target);
		return rhs;
	}

    void non_lvalue_target(const source_location& loc, const expression* target) {
        std::string text = detail::non_lvalue_target_text(target);
        if (!text.empty()) { text[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0]))); }
        diag(diag_level::error, loc, text);
    }

    // `C.s` on a class name (the runtime's text, class_dot_member_text): reported, true
    bool class_dot_receiver(const member_expr* m, const ctype& recv) {
        if (recv.kind != ctype::class_ref_k || m->is_static || !m->object || m->object->get_type() != node_type::identifier_expr) { return false; }
        diag(diag_level::error, m->location, class_dot_member_text(recv.cls, m->member));
        return true;
    }

    // The class's operator[] (its chain's, and every override a derived receiver could reach) returns by value only
    bool subscript_returns_by_value(std::string_view cls) {
        member_hit hit = lookup_member(cls, "[]", /*want_static=*/false);
        if (!hit.found || !hit.methods || hit.methods->empty()) { return false; }
        const auto by_value = [](const function_decl* fn) { return !fn->return_type || !fn->return_type->is_reference(); };
        for (const auto* fn : *hit.methods) { if (!by_value(fn)) { return false; } }
        for (const auto* fn : overrides_of(cls, "[]")) { if (!by_value(fn)) { return false; } }
        return true;
    }

    // A call whose callee resolves to the program's free functions or a class's constructors, none returning a reference
    bool call_returns_by_value(const call_expr* c) {
        if (!c->callee || c->callee->get_type() != node_type::identifier_expr) { return false; }
        auto* id = static_cast<const identifier_expr*>(c->callee.get());
        if (id->global_qualified || find_local(id->name) || class_ctx_) { return false; }
        if (auto it = functions_.find(id->name); it != functions_.end()) {
            if (it->second.empty()) { return false; }
            for (const auto* fn : it->second) {
                if (fn->return_type && fn->return_type->is_reference()) { return false; }
            }
            return true;
        }
        return find_sclass(id->name) != nullptr;
    }

    // Compound assign on a typed target behaves like C++ —
    // `x op= rhs` is `x = T(x op rhs)` — so it checks exactly like plain assignment
    // of the operation's result. var targets keep the dynamic behavior (unchecked).
	const char* incdec_op_ = nullptr;   // the ++/-- spelling while checking its += 1 / -= 1 desugar
	void check_store(const ctype& rhs, bool compound, token_type base_op, const ctype& target,
					 const std::string& target_display, const std::string& what,
					 const source_location& loc, size_t decl_line, size_t decl_col) {
		if (target.is_unknown()) { return; }
		if (compound && incdec_op_ && target.kind == ctype::string_k) {
			diag(diag_level::error, loc, std::string("cannot apply '") + incdec_op_ + "' to 'string'", decl_line, decl_col);
			return;
		}
		if (compound && base_op == token_type::plus && target.kind == ctype::string_k && !rhs.is_unknown() &&
			rhs.kind != ctype::string_k && rhs.kind != ctype::char_k && rhs.kind != ctype::utf8_k && !eng_.has_function("+")) {
			// a string target appends text only (a char is text; string += int raises, unlike binary +)
			diag(diag_level::error, loc, "cannot apply '+=' to 'string' and '" + ctype_name(rhs) + "'", decl_line, decl_col);
			return;
		}
		if (compound && !rhs.is_unknown() && target.kind != ctype::object_k) {   // a class target's operator decides
			const char* bitwise = base_op == token_type::ampersand ? "&=" : base_op == token_type::pipe ? "|=" : base_op == token_type::caret ? "^="
			                    : base_op == token_type::left_shift ? "<<=" : base_op == token_type::right_shift ? ">>=" : nullptr;
			const auto integral = [](const ctype& t) { return t.kind == ctype::int_k || t.kind == ctype::char_k; };
			if (bitwise && (!integral(target) || !integral(rhs))) {
				diag(diag_level::error, loc, std::string("cannot apply '") + bitwise + "' to '" + ctype_name(target) + "' and '" + ctype_name(rhs) + "'",
				     decl_line, decl_col);
				return;
			}
		}
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

    // A class operand's unary - + ~ runs the chain's overload taking no argument (both backends): its declared result, or
    // unknown; nullopt when the chain declares none (the builtin rule and its text apply)
    std::optional<ctype> class_unary_operator(std::string_view cls, token_type op) {
        const std::string_view name = op == token_type::minus ? "-" : op == token_type::plus ? "+" : "~";
        member_hit hit = lookup_member(cls, name, /*want_static=*/false);
        if (!hit.found) { return std::nullopt; }
        if (hit.methods) {
            for (const auto* fn : *hit.methods) {
                if (fn->parameters.empty() || fn->parameters.front().default_value) {
                    return fn->return_type && !fn->is_coroutine ? ctype_from_type_info(fn->return_type.get()) : ctype{};
                }
            }
        }
        return ctype{};
    }

    // A class operand's binary arithmetic, bitwise or <=> operator runs when an overload of the chain's nearest set takes the
    // right operand (both backends): its declared result, or unknown; nullopt when none takes it (the builtin rule applies)
    std::optional<ctype> class_binary_operator(std::string_view cls, token_type op, const ctype& r) {
        std::string_view name;
        switch (op) {
            case token_type::plus: name = "+"; break;
            case token_type::minus: name = "-"; break;
            case token_type::star: name = "*"; break;
            case token_type::slash: name = "/"; break;
            case token_type::percent: name = "%"; break;
            case token_type::ampersand: name = "&"; break;
            case token_type::pipe: name = "|"; break;
            case token_type::caret: name = "^"; break;
            case token_type::left_shift: name = "<<"; break;
            case token_type::right_shift: name = ">>"; break;
            case token_type::spaceship: name = "<=>"; break;
            default: return std::nullopt;
        }
        member_hit hit = lookup_member(cls, name, /*want_static=*/false);
        if (!hit.found) { return std::nullopt; }
        if (!hit.methods) { return ctype{}; }
        const std::vector<ctype> one{r};
        const function_decl* taken = nullptr;
        size_t takers = 0;
        for (const auto* fn : *hit.methods) {
            if (script_fn_viable(fn, one)) { taken = fn; ++takers; }
        }
        if (takers == 0) { return std::nullopt; }
        if (takers > 1 || !taken->return_type || taken->is_coroutine) { return ctype{}; }
        return ctype_from_type_info(taken->return_type.get());
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
                if (f->variable_name == "entry#" && !container.tentative && !container.is_unknown() && container.kind != ctype::map_k && container.kind != ctype::coroutine_k) {   // for (auto [k, v] : x)
                    diag(diag_level::error, f->container->location, "a structured binding takes a map's entries, its key and value: cannot bind '" +
                         ctype_name(container) + "' to [k, v]");
                }
                // the elements' static type: an array's element type, a string's char; a var container, an array<var> or anything
                // else has none
                ctype elem{};
                if (container.kind == ctype::array_k && container.ti && container.ti->element_type() &&
                    container.ti->element_type()->base_type != script_value_type::jai_any_type) {
                    elem = ctype_from_type_info(container.ti->element_type());
                } else if (container.kind == ctype::string_k) {
                    elem = ctype::make(ctype::char_k);
                }
                if (!f->element_type) { f->auto_spelled = true; }
                if (f->auto_spelled && !f->auto_typed && semantic_walk() && container.kind != ctype::map_k) {
                    f->auto_typed = true;   // an auto over elements without a static type is a var, decided once
                    if (elem.is_unknown()) { f->element_type = eng_.get_type_info_any(); }
                }
                const bool declared = f->element_type && f->element_type->base_type != script_value_type::jai_any_type && !f->auto_spelled;
                stamp(f->element_conversion_free, !declared || (!elem.is_unknown() && same_type(ctype_from_type_info(f->element_type.get()), elem)), false);
                if (declared && f->is_reference && !elem.is_unknown() && !reference_type_compatible(elem, ctype_from_type_info(f->element_type.get()))) {
                    diag(diag_level::error, f->location, "cannot bind '" + ctype_name(elem) + "' to '" + std::string(f->variable_name) +
                         "' declared '" + f->element_type->canonical_name() + "&'");
                }
                // over the elements of a const: auto& binds const references (C++ deduction), T& / var& are errors
                bool loop_const = f->is_const;
                {
                    const const_hit h = f->is_reference && !f->is_const ? const_path(f->container.get(), /*value_too=*/true) : const_hit{};
                    const bool typed_ref = f->is_reference && !f->auto_spelled && f->element_type && f->variable_name != "entry#";
                    if (h.verdict && !typed_ref) { loop_const = loop_const || h.verdict == const_raises; }
                    const uint8_t verdict = typed_ref ? h.verdict : uint8_t(const_none);
                    const std::string declared = f->element_type ? f->element_type->canonical_name() + "&" : std::string("var&");
                    stamp_const(f->const_check, verdict, [&] { return detail::const_bind_text(f->variable_name, declared, f->container.get(), h.root, true); });
                    if (verdict == const_raises) {
                        diag(diag_level::error, f->location, detail::const_lowercase(detail::const_bind_text(f->variable_name, declared, f->container.get(), h.root, true)), h.line, h.column);
                    }
                }
                scopes_.emplace_back();
                var_entry entry;
                entry.is_const = loop_const;
                entry.type = ctype_from_type_info(f->element_type.get());
                entry.dynamic = !f->element_type || f->element_type->base_type == script_value_type::jai_any_type ||
                                f->is_reference;
                entry.is_reference = f->is_reference;
                entry.map_pair = container.kind == ctype::map_k;
                if (f->is_reference && container.kind != ctype::map_k) {
                    // an alias of a typed buffer's element holds the element's kind (the buffer converts every store): the builtin receiver
                    // proof reads it (the call checks the receiver's kind at run time)
                    const ctype held = container.is_unknown() ? declared_container_receiver(f->container.get(), container) : container;
                    if (held.kind == ctype::array_k && (!held.tentative || held.alias_proof) && held.ti && held.ti->element_type()) {
                        entry.builtin_alias = ctype_from_type_info(held.ti->element_type().get()).kind;
                    }
                }
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
                entry.is_const = f->loop_param.is_const;
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
                    more.is_const = extra.is_const;
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
        check_const_return(r);
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
		if (!fn.is_coroutine && !fn.ret) {
			fn_ctx& open = fn_stack_.back();
			const uint8_t k = !r->value || r->binds_reference || value.tentative ? 0
			                : value.kind == ctype::int_k ? 1 : value.kind == ctype::float_k ? 2 : value.kind == ctype::bool_k ? 3 : 0;
			if (k == 0 || (open.inferred_return != 0 && open.inferred_return != k)) { open.inferred_mixed = true; }
			else { open.inferred_return = k; }
		}
		if (fn.is_coroutine || !fn.ret) { return; }   // coroutine returns/yields + auto: lenient
		if (!r->value && requires_return_value(fn.ret)) {
			diag(diag_level::error, r->location, "'" + std::string(fn.name) + "' declared to return '" +
				 fn.ret->canonical_name() + "' returns no value here", fn.line, fn.column);
			return;
		}
		if (fn.ret->base_type == script_value_type::jai_reference_type && r->value) {
			const type_info* referent = fn.ret->element_type().get();
			if (referent && !reference_type_compatible(value, ctype_from_type_info(referent))) {
				diag(diag_level::error, r->location, "cannot return reference to '" + ctype_name(value) +
					"' from '" + std::string(fn.name) + "' declared to return '" + fn.ret->canonical_name() + "'");
			}
			return;
		}
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
			// a nullable result (a class, a handle, a container) returns null here: warning severity, like falling off the end
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
                    warn_spliced_function(fn);
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
                namespace_scopes_.push_back(scopes_.size() - 1);
                for (auto& inner : n->declarations) { walk_decl(inner.get()); }
                namespace_scopes_.pop_back();
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
                    entry.is_const = dd->is_const;
                    entry.line = dd->location.line; entry.column = dd->location.column;
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
                entry.is_const = x->is_const;
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
            case node_type::enum_decl: {
                auto* e = static_cast<enum_decl*>(d);
                if (scopes_.size() > 1 && (namespace_scopes_.empty() || namespace_scopes_.back() != scopes_.size() - 1)) {
                    warn_scoped_enum(e);
                    var_entry entry;
                    entry.type = ctype{ctype::enum_ref_k, e->name};
                    entry.enum_def = e;
                    entry.line = e->location.line;
                    entry.column = e->location.column;
                    scopes_.back().insert_or_assign(e->name, std::move(entry));
                }
                break;
            }
            default:
                break;
        }
    }

	// An auto declaration takes its initializer's static type: var or unknown makes the slot a
	// var; any other type keeps auto, which the runtime locks to the value. Semantic: the unit's first proof pass
	// decides it in every mode (stamp or clear see the same program), and it never changes under a running unit.
	void type_auto_slot(variable_decl* decl, const ctype& init) {
		if (!decl->auto_spelled || decl->auto_typed || !semantic_walk()) { return; }
		decl->auto_typed = true;
		if (init.is_unknown() || init.kind == ctype::coroutine_k) {
			decl->type = eng_.get_type_info_any();
			decl->decl_fast_flags = variable_decl::classify_decl_fast(decl->type);
		} else if (init.kind == ctype::float_k && decl->initializer && decl->initializer->get_type() == node_type::ternary_expr &&
		           last_mixed_ternary_.node == decl->initializer.get()) {
			decl->type = eng_.get_type_info_float();   // an int/float ternary is float (C++ common type): the chosen arm converts
			decl->decl_fast_flags = variable_decl::classify_decl_fast(decl->type);
		}
	}

	// An auto initializer's static type: a ternary whose arms are int and float has C++'s common type, float;
	// every other mixed ternary has none
	ctype numeric_ternary_type(const expression* init_expr, const ctype& init) {
		if (!init.is_unknown() || last_mixed_ternary_.node != init_expr) { return init; }
		const auto a = last_mixed_ternary_.then_type.kind, b = last_mixed_ternary_.else_type.kind;
		if ((a == ctype::int_k && b == ctype::float_k) || (a == ctype::float_k && b == ctype::int_k)) { return ctype::make(ctype::float_k); }
		return init;
	}

	// Inline and plain declarations of one program-scope name never mix across units: the runtime
	// refuses whichever runs second, so the closure reports the pair once, at the plain declaration; this unit's own mix
	// turns the cell in place
	void check_inline_redefinition(const variable_decl* decl) {
		if (decl->is_inline || scopes_.size() != 1 || !program_globals_) { return; }
		auto it = program_globals_->find(std::string(decl->name));
		if (it == program_globals_->end()) { return; }
		for (const auto& [unit, globals] : it->second) {
			for (const auto& g : globals) {
				if (g && g->is_inline && !unit_declares(g.get())) {
					diag(diag_level::error, decl->location, detail::inline_redefinition_text(decl, decl->name));
					return;
				}
			}
		}
	}
	bool unit_declares(const variable_decl* g) const {
		for (const auto& d : decls_) { if (d.get() == g) { return true; } }
		return false;
	}

	void walk_variable_decl(variable_decl* decl) {
		if (!decl->type && !decl->function_spelling) { decl->auto_spelled = true; }
		check_inline_redefinition(decl);
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
		// (runtime errors identically)
		if (decl->type && decl->type->base_type == script_value_type::jai_shared_ptr_type &&
			!decl->type->element_type()) {
			if (!has_init || init.kind == ctype::null_k) {
				diag(diag_level::error, decl->location, "cannot infer shared_ptr<auto> pointee for '" +
					 std::string(decl->name) + "' without a non-null initializer (use var, or an explicit shared_ptr<T>)");
				entry.dynamic = true;   // recovery: no cascades
			} else if (!init.is_unknown() && init.kind != ctype::object_k) {
				// runtime: family D's declaration shape (Cannot initialize 'p' (declared shared_ptr<auto>) with int)
				diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
					 "' declared 'shared_ptr<auto>' with '" + ctype_name(init) + "'");
				entry.dynamic = true;   // recovery: no cascades
			} else {
				entry.type = init;      // object(cls), or unknown -> lenient
				entry.dynamic = init.is_unknown();
				entry.is_handle = true;
				entry.display = "shared_ptr<" + ctype_name(init) + ">";
			}
			check_const_bind(decl, entry);
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
		} else if (decl->type && !decl->auto_spelled) {
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
			stamp(decl->reference_admission_proven, is_reference_decl && has_init && init_ok && !binding_source.is_unknown() &&
			      !binding_source.tentative && binding_source.kind != ctype::null_k &&
			      binding_source.kind != ctype::object_k, false);   // a class binding checks at run time (a reload may change the chain)
			if (has_init && (!entry.dynamic || is_reference_decl) && !init_ok) {
				diag(diag_level::error, decl->location, "cannot initialize '" + std::string(decl->name) +
				     "' declared '" + entry.display + "' with '" + ctype_name(binding_source) + "'" + assignment_detail(binding_source, entry.type));
				// Recovery keeps the declared type; reference use remains conservative.
			}
		} else if (has_init) {
			// auto with initializer: the initializer's static type, then enforced as that type; a var or unknown
			// static type makes the slot a var. null carries no type: the runtime locks at the first non-null
			// store, so a null init stays dynamic.
			init = numeric_ternary_type(decl->initializer.get(), init);
			entry.type = init;
			entry.dynamic = init.is_unknown() || init.kind == ctype::null_k;
			entry.display = ctype_name(init);
			type_auto_slot(decl, init);
			// A mixed-branch ternary has no single static type, so the auto is a var: say so where it reads like a typed slot
			if (init.is_unknown() && last_mixed_ternary_.node == decl->initializer.get()) {
				diag(diag_level::warning, decl->location, "auto '" + std::string(decl->name) +
					 "' is initialized from a ternary with branch types '" +
					 ctype_name(last_mixed_ternary_.then_type) + "' and '" +
					 ctype_name(last_mixed_ternary_.else_type) +
					 "' - it has no single static type, so '" + std::string(decl->name) +
					 "' is a var; declare the type explicitly, or make both branches the same type");
			}
		} else {
			// auto without initializer: locked by the first runtime assignment — the
			// checker can't see branch order, so it stays dynamic (documented leniency)
			entry.dynamic = true;
		}
		stamp(decl->static_init_proof, init_proof, proof_none);
		if (init_proof != proof_none && stamping()) { ++counts_.decls; }
		if (has_init && decl->slot_index != SIZE_MAX && decl->initializer->get_type() == node_type::lambda_expr && !is_reference_decl) {
			entry.lambda = static_cast<const lambda_expr*>(decl->initializer.get());
		}
		check_const_bind(decl, entry);
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
        ctx.file = &fn->location.filename;
        // Constructor params shadowing the field they initialize (C(int hp) { this.hp = hp; })
        // is the established idiom - no field-shadow warnings on ctor params.
        bool is_ctor = class_ctx_ && fn->name == class_ctx_name_;
        const cdtor_ctx enclosing_cdtor = cdtor_ctx_;
        cdtor_ctx_ = is_ctor ? cdtor_ctx::construction
                   : (class_ctx_ && !fn->is_static && fn->name.size() > 1 && fn->name[0] == '~') ? cdtor_ctx::destruction : cdtor_ctx::none;
        fn_stack_.push_back(std::move(ctx));
        mode_guard enclosing(mode_stack_, scoped_region_mode(), /*scoped=*/true, /*always=*/true);
        define_params(fn->parameters, fn->location, /*warn_field_shadows=*/!is_ctor, /*binds_copies=*/fn->is_coroutine);
        fn_stack_.back().in_ctor_initializers = true;
        for (const auto& init : fn->initializers) {
            for (const auto& arg : init.arguments) { infer(arg.get()); }
            if (is_ctor && init.target == "super" && init.arguments.empty()) { report_base_without_default(class_ctx_name_, fn->location); }
        }
        // a constructor naming no initializer default-constructs its base (C++ implicit `: super()`)
        if (is_ctor && fn->initializers.empty()) { report_base_without_default(class_ctx_name_, fn->location); }
        fn_stack_.back().in_ctor_initializers = false;
        if (fn->body) {
            mode_guard body(mode_stack_, leading_mode(fn->body->declarations), /*scoped=*/true);
            for (auto& d : fn->body->declarations) { walk_decl(d.get()); }
        }
        check_falls_off_end(fn);
        fn_stack_.pop_back();
        scopes_.pop_back();
        capture_stack_.swap(saved_captures);
        cdtor_ctx_ = enclosing_cdtor;
    }

    // A default no evaluation can turn into storage (a literal, a construction, a lambda, an operator's result); a conditional, a call
    // or a cast may yield a reference, and the bind judges those
    static bool default_never_names_storage(const expression* e) {
        switch (e->get_type()) {
        case node_type::literal_expr: case node_type::array_literal_expr: case node_type::map_literal_expr:
        case node_type::lambda_expr: case node_type::new_expr:
            return true;
        case node_type::unary_expr: {
            const token_type op = static_cast<const unary_expr*>(e)->op.type;
            return op != token_type::plus_plus && op != token_type::minus_minus;
        }
        case node_type::binary_expr: {
            const token_type op = static_cast<const binary_expr*>(e)->op.type;
            return op != token_type::left_bracket && op != token_type::question_question;
        }
        default:
            return false;
        }
    }

    void define_params(const std::vector<parameter>& params, const source_location& fn_loc,
                       bool warn_field_shadows = true, bool binds_copies = false) {
        for (const auto& p : params) {
            ctype pt = ctype_from_type_info(p.type.get());
            if (p.default_value) {
                ctype dv = infer(p.default_value.get());
                if (!p.is_reference && !pt.is_unknown() && !assignable(dv, pt)) {
                    diag(diag_level::error, p.default_value->location,
                         "default value of '" + p.name + "' declared '" +
                         (p.type ? p.type->canonical_name() : "auto") + "' has type '" + ctype_name(dv) + "'");
                }
                if (p.is_reference && !p.is_const && !binds_copies && default_never_names_storage(p.default_value.get())) {   // a coroutine binds a copy
                    diag(diag_level::error, p.default_value->location, "cannot bind non-lvalue default to reference parameter '" + p.name + "'");
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
            entry.is_const = p.is_const;
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

	// A base class no unit, include, extern or host can bring is the definition's error, reported at load in the run time's
	// words so a module that would stop half-defined applies nothing
	void check_class_bases(const class_decl* cd) {
		if (!namespace_scopes_.empty() || has_includes_) { return; }
		for (std::string_view base : cd->base_classes) {
			if (find_sclass(base) || engine_class(base) || base.find("::") != std::string_view::npos || global_var_names_.count(base) ||
			    engine_knows_name(std::string(base))) { continue; }
			diag(diag_level::error, cd->location, "Base class '" + std::string(base) + "' not found");
			return;
		}
	}

	void walk_class(const class_decl* cd) {
		check_class_bases(cd);
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
				if (!f->type && !f->function_spelling) { f->auto_spelled = true; }
				if (f->initializer) {
					const cdtor_ctx enclosing_cdtor = std::exchange(cdtor_ctx_, f->is_static ? cdtor_ctx::none : cdtor_ctx::construction);
					ctype init = infer(f->initializer.get());
					cdtor_ctx_ = enclosing_cdtor;
					if (f->auto_spelled) { type_auto_slot(f, numeric_ternary_type(f->initializer.get(), init)); }   // an auto field: its initializer's static type
					if ((f->type && !f->auto_spelled) || f->function_spelling) {
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
				// the builtin - point at the now-unreachable builtin.
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

    // An init-capture, or an explicit by-value capture of a program-scope name, holds a copy taken at creation:
    // the call reads that copy, never the name, so the body is not grafted at the call site
    bool lambda_copies_at_creation(const lambda_expr* l) {
        for (const auto& cap : l->captures) {
            if (cap.initializer) { return true; }
            if (!cap.by_reference && cap.name != "this" && !find_local(cap.name)) { return true; }
        }
        return false;
    }

    void walk_lambda(lambda_expr* l) {
        for (const auto& cap : l->captures) {
            if (cap.initializer) { infer(cap.initializer.get()); continue; }   // an init-capture: its expression, in the enclosing scope
            if (cap.name == "this") { continue; }   // capture-list entries are not identifier uses
            if (find_local(cap.name)) { continue; }
            if (class_ctx_) {
                member_hit hit = lookup_member(class_ctx_name_, cap.name, false);
                if (hit.found || hit.unresolved) { continue; }
            }
            if (functions_.count(cap.name) || classes_.count(cap.name) || enums_.count(cap.name) ||
                all_names_.count(cap.name)) { continue; }
            std::string name(cap.name);
            if (extra_global(name) || engine_knows_name(name)) { continue; }
            undeclared(l->location, cap.name);
        }
        capture_stack_.push_back(capture_ctx{scopes_.size(), l});
        // the body may run after the constructor or destructor that creates the lambda has returned
        const cdtor_ctx enclosing_cdtor = std::exchange(cdtor_ctx_, cdtor_ctx::none);
        struct restore_cdtor { cdtor_ctx& slot; cdtor_ctx was; ~restore_cdtor() { slot = was; } } restore{cdtor_ctx_, enclosing_cdtor};

        scopes_.emplace_back();
        fn_ctx ctx{l->return_type.get(), /*is_coroutine=*/false, "lambda",
                   l->location.line, l->location.column};
        ctx.file = &l->location.filename;
        // Only a [this] capture reaches fields from a lambda body (bare [=]/[&] do not)
        for (const auto& cap : l->captures) {
            if (cap.name == "this") { ctx.sees_fields = class_ctx_ != nullptr; break; }
        }
        fn_stack_.push_back(std::move(ctx));
        mode_guard enclosing(mode_stack_, scoped_region_mode(), /*scoped=*/true, /*always=*/true);
        define_params(l->parameters, l->location);
        for (const auto& cap : l->captures) {
            if (!cap.initializer) {   // a named capture is the lambda's own copy of the variable, typed as it
                if (var_entry* original = find_local(cap.name)) { scopes_.back().emplace(cap.name, *original); }
                continue;
            }
            var_entry entry;
            entry.dynamic = true;   // the lambda's own copy, typed by the value it holds
            entry.line = l->location.line; entry.column = l->location.column;
            scopes_.back().emplace(cap.name, std::move(entry));
        }
        walk_stmt(l->body.get());
        check_falls_off_end(l->return_type.get(), l->body.get(), l->location, "lambda");
        {
            const fn_ctx& done = fn_stack_.back();
            const uint8_t kind = !l->return_type && !done.inferred_mixed && !has_includes_ ? done.inferred_return : uint8_t(0);
            stamp(l->inferred_return_kind, kind, uint8_t(0));
        }
        fn_stack_.pop_back();
        scopes_.pop_back();
        capture_stack_.pop_back();
    }

    // ---------------------------------------------------- falling off the end

    void check_falls_off_end(const function_decl* fn) {
        if (fn->is_coroutine) { return; }
        check_falls_off_end(fn->return_type.get(), fn->body.get(), fn->location, fn->name);
    }

    // A result that holds no null is a load error when a path reaches the end of the body; the end reached only through
    // a catch the analysis cannot prove runs, or a nullable result, stays a warning
    void check_falls_off_end(const type_info* return_type, const statement* body, const source_location& location, std::string_view name) {
        if (!return_type || !body) { return; }
        ctype ret = ctype_from_type_info(return_type);
        if ((ret.is_unknown() && !requires_return_value(return_type)) || ret.kind == ctype::null_k) { return; }
        const auto flow = return_flow(return_type).analyze(body);
        if (!(flow.paths & (return_flow::normal | return_flow::maybe_normal))) { return; }
        const bool required = requires_return_value(return_type) && (flow.paths & return_flow::normal);
        diag(required ? diag_level::error : diag_level::warning, location, "'" + std::string(name) + "' declared to return '" +
             return_type->canonical_name() + "' may reach the end of its body without returning" + (required ? "" : " (returns null)"));
    }

};

// Safety-net sweep: every declared name at any depth (see all_names_ comment).
void static_checker::collect_names(const statement* node) {
    if (!node) { return; }
    switch (node->get_type()) {
        case node_type::variable_decl: {
            auto* v = static_cast<const variable_decl*>(node);
            all_names_.insert(v->name);
            layout_inits_[v->name].push_back(binary_call(v->initializer.get(), "layout") ? v->initializer.get() : nullptr);
            if (v->type && v->type->base_type == script_value_type::jai_reference_type && v->initializer &&
                v->initializer->get_type() == node_type::identifier_expr) {
                assigned_names_.insert(static_cast<const identifier_expr*>(v->initializer.get())->name);
            }
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
            has_includes_ = true;
            has_dynamic_includes_ = true;   // nested: a runtime include, beyond the closure
            break;
        case node_type::import_decl:
            has_includes_ = true;
            if (static_cast<const import_decl*>(node)->path_expr) { has_dynamic_includes_ = true; }   // a literal one is a closure unit (collect_unit_imports)
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
            for (const auto& cap : l->captures) { if (cap.initializer) { all_names_.insert(cap.name); collect_names_expr(cap.initializer.get()); } }
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
            if (a->target && a->target->get_type() == node_type::identifier_expr) {
                assigned_names_.insert(static_cast<const identifier_expr*>(a->target.get())->name);
            }
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
        case node_type::import_expr: {
            auto* imp = static_cast<const import_expr*>(e);
            has_includes_ = true;
            if (!imp->path_exprs.empty()) { has_dynamic_includes_ = true; }   // literal paths are closure units (collect_unit_imports)
            for (const auto& a : imp->path_exprs) { collect_names_expr(a.get()); }
            break;
        }
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

// Every literal import below the top level: an import runs at program scope wherever it is written, so its module is a unit
// of the program like a top-level one (lambda bodies included: they are statements too)
namespace {
struct import_path_walker {
    std::vector<std::string>& out;
    void stmt(const statement* node) {
        if (!node) { return; }
        switch (node->get_type()) {
            case node_type::import_decl: {
                auto* imp = static_cast<const import_decl*>(node);
                if (!imp->path_expr) { out.push_back(imp->path); }
                break;
            }
            case node_type::variable_decl: expr(static_cast<const variable_decl*>(node)->initializer.get()); break;
            case node_type::function_decl: stmt(static_cast<const function_decl*>(node)->body.get()); break;
            case node_type::class_decl:
                for (const auto& m : static_cast<const class_decl*>(node)->members) { stmt(m.declaration.get()); }
                break;
            case node_type::namespace_decl:
                for (const auto& d : static_cast<const namespace_decl*>(node)->declarations) { stmt(d.get()); }
                break;
            case node_type::destructuring_decl: expr(static_cast<const destructuring_decl*>(node)->initializer.get()); break;
            case node_type::expression_stmt: expr(static_cast<const expression_stmt*>(node)->expression.get()); break;
            case node_type::expression_decl: expr(static_cast<const expression_decl*>(node)->expression.get()); break;
            case node_type::statement_decl: stmt(static_cast<const statement_decl*>(node)->statement.get()); break;
            case node_type::block_stmt:
                for (const auto& d : static_cast<const block_stmt*>(node)->declarations) { stmt(d.get()); }
                break;
            case node_type::if_stmt: {
                auto* i = static_cast<const if_stmt*>(node);
                expr(i->condition.get());
                stmt(i->then_statement.get());
                stmt(i->else_statement.get());
                break;
            }
            case node_type::while_stmt: expr(static_cast<const while_stmt*>(node)->condition.get()); stmt(static_cast<const while_stmt*>(node)->body.get()); break;
            case node_type::for_stmt: {
                auto* f = static_cast<const for_stmt*>(node);
                stmt(f->initializer.get());
                expr(f->condition.get());
                expr(f->update.get());
                stmt(f->body.get());
                break;
            }
            case node_type::range_for_stmt: expr(static_cast<const range_for_stmt*>(node)->container.get()); stmt(static_cast<const range_for_stmt*>(node)->body.get()); break;
            case node_type::parallel_for_stmt: stmt(static_cast<const parallel_for_stmt*>(node)->body.get()); break;
            case node_type::try_stmt: stmt(static_cast<const try_stmt*>(node)->try_block.get()); stmt(static_cast<const try_stmt*>(node)->catch_block.get()); break;
            case node_type::switch_stmt: {
                auto* sw = static_cast<const switch_stmt*>(node);
                for (const auto& cs : sw->cases) { for (const auto& st : cs->body) { stmt(st.get()); } }
                if (sw->default_case) { for (const auto& st : sw->default_case->body) { stmt(st.get()); } }
                break;
            }
            case node_type::return_stmt: expr(static_cast<const return_stmt*>(node)->value.get()); break;
            default: break;
        }
    }
    void expr(const expression* e) {
        if (!e) { return; }
        switch (e->get_type()) {
            case node_type::lambda_expr: stmt(static_cast<const lambda_expr*>(e)->body.get()); break;
            case node_type::binary_expr: expr(static_cast<const binary_expr*>(e)->left.get()); expr(static_cast<const binary_expr*>(e)->right.get()); break;
            case node_type::unary_expr: expr(static_cast<const unary_expr*>(e)->operand.get()); break;
            case node_type::assignment_expr: expr(static_cast<const assignment_expr*>(e)->target.get()); expr(static_cast<const assignment_expr*>(e)->value.get()); break;
            case node_type::call_expr: {
                auto* c = static_cast<const call_expr*>(e);
                expr(c->callee.get());
                for (const auto& a : c->arguments) { expr(a.get()); }
                break;
            }
            case node_type::member_expr: expr(static_cast<const member_expr*>(e)->object.get()); break;
            case node_type::new_expr: for (const auto& a : static_cast<const new_expr*>(e)->arguments) { expr(a.get()); } break;
            case node_type::ternary_expr: {
                auto* t = static_cast<const ternary_expr*>(e);
                expr(t->condition.get());
                expr(t->then_expression.get());
                expr(t->else_expression.get());
                break;
            }
            case node_type::array_literal_expr: for (const auto& el : static_cast<const array_literal_expr*>(e)->elements) { expr(el.get()); } break;
            case node_type::map_literal_expr:
                for (const auto& [k, v] : static_cast<const map_literal_expr*>(e)->entries) { expr(k.get()); expr(v.get()); }
                break;
            case node_type::throw_expr: expr(static_cast<const throw_expr*>(e)->value.get()); break;
            case node_type::cast_expr: expr(static_cast<const cast_expr*>(e)->operand.get()); break;
            case node_type::yield_expr: expr(static_cast<const yield_expr*>(e)->value.get()); break;
            case node_type::import_expr:
                for (const auto& p : static_cast<const import_expr*>(e)->paths) { out.push_back(p); }
                break;
            default: break;
        }
    }
};
} // namespace

void collect_unit_imports(const std::vector<declaration_ptr>& decls, std::vector<std::string>& out) {
    import_path_walker w{out};
    for (const auto& d : decls) {
        if (d && d->get_type() != node_type::import_decl) { w.stmt(d.get()); }
    }
}

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

void collect_unit_functions(const std::vector<declaration_ptr>& decls, const std::string& unit_path, program_function_map& out) {
    for (const auto& d : decls) {   // the unit's earlier entries go first: a re-gather replaces, never accumulates
        if (d && d->get_type() == node_type::function_decl) { out[std::string(static_cast<const function_decl*>(d.get())->name)][unit_path].clear(); }
    }
    for (const auto& d : decls) {
        if (d && d->get_type() == node_type::function_decl) {
            auto fd = std::static_pointer_cast<const function_decl>(d);
            out[std::string(fd->name)][unit_path].push_back(std::move(fd));
        }
    }
}

void collect_unit_globals(const std::vector<declaration_ptr>& decls, const std::string& unit_path, program_global_map& out) {
    for (const auto& d : decls) {   // a re-gather replaces the unit's own entries, never accumulates
        if (d && d->get_type() == node_type::variable_decl) { out[std::string(static_cast<const variable_decl*>(d.get())->name)][unit_path].clear(); }
    }
    for (const auto& d : decls) {   // every spelling counts toward "declared once": the lookup asks the one declaration for its type
        if (d && d->get_type() == node_type::variable_decl) {
            auto vd = std::static_pointer_cast<const variable_decl>(d);
            out[std::string(vd->name)][unit_path].push_back(std::move(vd));
        }
    }
}

void collect_unit_enums(const std::vector<declaration_ptr>& decls, const std::string& unit_path, program_enum_map& out) {
    for (auto& [name, units] : out) { units.erase(unit_path); }   // a re-gather replaces the unit's own entries
    for (const auto& d : decls) {
        if (d && d->get_type() == node_type::enum_decl) {
            auto ed = std::static_pointer_cast<const enum_decl>(d);
            out[std::string(ed->name)][unit_path] = std::move(ed);
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
