#pragma once

// Grammar-driven JaiScript program synthesis. Builds random-but-mostly-valid
// source text from a weighted grammar, biased toward semantically spicy
// constructs: classes (inheritance / ctor delegation / statics / operator
// methods), lambdas with captures, coroutines, ref params (incl. field and
// element lvalue args), compound assigns, ++/-- in conditions, try/catch/throw,
// switch with fallthrough, the int/auto/var type ladder, builtin string/array/
// map methods, int64 overflow edges, hot-reload shapes (multi-segment programs
// on one engine), nested functions, and small recursion.
//
// Occasional runtime errors are deliberate: matching error text on both
// backends is agreement; only mismatches are findings.

#include "fuzz_harness.hpp"

#include <string>
#include <vector>

namespace jai::fuzz {

	enum class vt { i, f, b, s, arr, map, obj, any };

	struct var_info {
		std::string name;
		vt type = vt::any;
		int class_idx = -1; // when type == obj
		bool is_var_decl = false; // declared with `var` (retype allowed)
	};

	struct param_info {
		std::string name;
		std::string type_text; // "int", "int&", "var", "var&", "auto", "string", "C0", "C0&"
		vt type = vt::any;
		bool is_ref = false;
	};

	struct func_info {
		std::string name;
		vt ret = vt::any;
		std::vector<param_info> params;
		bool is_coroutine = false;
	};

	struct method_info {
		std::string name;
		vt ret = vt::i;
		int arity = 0;
	};

	struct class_info {
		std::string name;
		int base_idx = -1;
		std::vector<std::pair<std::string, vt>> fields;   // own + inherited (flattened for lookup)
		std::vector<method_info> methods;                 // own + inherited
		std::vector<int> ctor_arities;                    // int params only
		std::vector<std::string> statics;                 // int statics, accessed Class::name
		bool has_op_plus = false;
		bool has_op_lt = false;
		std::string coroutine_method;                     // own or inherited (empty = none)
		std::string typed_int_field;                      // declared-int field (typed writes convert)
	};

	class generator {
	public:
		generator(uint64_t seed, int max_expr_depth = 4)
			: rng_(mix64(seed + 0xD1B54A32D192ED03ULL)), max_depth_(max_expr_depth), seed_(seed) {}

		program generate() {
			program p;
			p.seed = seed_;
			classes_.clear();
			funcs_.clear();
			scopes_.clear();
			names_ = 0;
			lines_.clear();
			scopes_.push_back({});

			int n_classes = pick_weighted({55, 30, 15}); // 0,1,2
			for (int i = 0; i < n_classes; ++i) emit_class();
			int n_funcs = pick_weighted({25, 35, 25, 15}); // 0..3
			for (int i = 0; i < n_funcs; ++i) emit_function();

			int n_stmts = static_cast<int>(rng_.range(4, 12));
			for (int i = 0; i < n_stmts; ++i) emit_stmt(0);

			// Final expression: the program's result value.
			lines_.push_back(expr(random_value_type(), max_depth_) + ";");
			p.segments.push_back(join());

			// Hot-reload shape: a second execute() on the same engine that
			// redefines a function or class, then uses surviving state.
			if ((n_classes > 0 || n_funcs > 0) && rng_.chance(22)) {
				lines_.clear();
				if (n_classes > 0 && rng_.chance(60)) emit_class_redefinition();
				else if (n_funcs > 0) emit_function_redefinition();
				int extra = static_cast<int>(rng_.range(1, 4));
				for (int i = 0; i < extra; ++i) emit_stmt(0);
				lines_.push_back(expr(random_value_type(), max_depth_) + ";");
				p.segments.push_back(join());
			}
			return p;
		}

	private:
		fuzz_rng rng_;
		int max_depth_;
		uint64_t seed_;
		int names_ = 0;
		std::vector<class_info> classes_;
		std::vector<func_info> funcs_;
		std::vector<std::vector<var_info>> scopes_;
		std::vector<std::string> lines_;
		int loop_depth_ = 0;
		int stmt_nesting_ = 0;
		bool in_function_ = false;
		vt current_ret_ = vt::any;

		// ---------- helpers ----------
		std::string fresh(const char* prefix) { return std::string(prefix) + std::to_string(names_++); }
		std::string join() const {
			std::string out;
			for (const auto& l : lines_) { out += l; out += '\n'; }
			return out;
		}
		int pick_weighted(const std::vector<uint32_t>& weights) {
			uint32_t total = 0;
			for (auto w : weights) total += w;
			uint32_t roll = rng_.below(total);
			for (size_t i = 0; i < weights.size(); ++i) {
				if (roll < weights[i]) return static_cast<int>(i);
				roll -= weights[i];
			}
			return 0;
		}

		vt random_value_type() {
			switch (pick_weighted({30, 12, 12, 18, 12, 8, 8})) {
				case 0: return vt::i;
				case 1: return vt::f;
				case 2: return vt::b;
				case 3: return vt::s;
				case 4: return vt::arr;
				case 5: return vt::map;
				default: return classes_.empty() ? vt::i : vt::obj;
			}
		}

		std::vector<const var_info*> vars_of(vt t) const {
			std::vector<const var_info*> out;
			for (const auto& scope : scopes_)
				for (const auto& v : scope)
					if (v.type == t) out.push_back(&v);
			return out;
		}
		const var_info* random_var(vt t) {
			auto vs = vars_of(t);
			if (vs.empty()) return nullptr;
			return vs[rng_.below(static_cast<uint32_t>(vs.size()))];
		}
		void declare(const std::string& name, vt t, int class_idx = -1, bool is_var = false) {
			scopes_.back().push_back({name, t, class_idx, is_var});
		}

		// ---------- literals ----------
		std::string int_literal() {
			switch (pick_weighted({55, 18, 10, 7, 5, 5})) {
				case 0: return std::to_string(rng_.range(0, 12));
				case 1: return std::to_string(rng_.range(-20, 120));
				case 2: return std::to_string(rng_.range(-100000, 100000));
				case 3: return "9223372036854775807";           // INT64_MAX
				case 4: return "(-9223372036854775807 - 1)";    // INT64_MIN without literal-overflow
				case 5: return std::to_string(rng_.range(0, 1) ? 4611686018427387904LL : -4611686018427387904LL);
				default: return "1";
			}
		}
		std::string float_literal() {
			static constexpr const char* const lits[] = {"0.0", "1.0", "-1.5", "2.5", "3.25", "0.5", "100.125", "-0.25", "1000000.0", "0.001"};
			return lits[rng_.below(10)];
		}
		std::string string_literal() {
			static constexpr const char* const lits[] = {"\"a\"", "\"b\"", "\"xy\"", "\"hello\"", "\"\"", "\"one,two,three\"", "\"  pad  \"", "\"zZz\""};
			return lits[rng_.below(8)];
		}
		std::string bool_literal() { return rng_.chance(50) ? "true" : "false"; }

		// ---------- expressions ----------
		std::string expr(vt want, int depth) {
			if (want == vt::any) {
				switch (pick_weighted({35, 15, 12, 20, 10, 8})) {
					case 0: want = vt::i; break;
					case 1: want = vt::f; break;
					case 2: want = vt::b; break;
					case 3: want = vt::s; break;
					case 4: want = vt::arr; break;
					default: want = vt::map; break;
				}
			}
			switch (want) {
				case vt::i: return int_expr(depth);
				case vt::f: return float_expr(depth);
				case vt::b: return bool_expr(depth);
				case vt::s: return string_expr(depth);
				case vt::arr: return array_expr(depth);
				case vt::map: return map_expr(depth);
				case vt::obj: return obj_expr(depth);
				default: return int_expr(depth);
			}
		}

		std::string int_expr(int depth) {
			if (depth <= 0) {
				if (rng_.chance(45)) {
					if (const var_info* v = random_var(vt::i)) return v->name;
				}
				return int_literal();
			}
			switch (pick_weighted({28, 14, 8, 7, 6, 8, 6, 6, 5, 4, 4, 4})) {
				case 0: { // arithmetic
					static constexpr const char* const ops[] = {"+", "-", "*", "/", "%"};
					const char* op = ops[rng_.below(5)];
					std::string rhs = int_expr(depth - 1);
					if ((op[0] == '/' || op[0] == '%') && rng_.chance(85))
						rhs = "(" + rhs + " + " + std::to_string(rng_.range(1, 7)) + ")"; // usually nonzero
					return "(" + int_expr(depth - 1) + " " + op + " " + rhs + ")";
				}
				case 1: return int_leaf_or_var();
				case 2: return "(-(" + int_expr(depth - 1) + "))";
				case 3: { // bitwise / shifts (small shift amounts)
					static constexpr const char* const ops[] = {"&", "|", "^", "<<", ">>"};
					const char* op = ops[rng_.below(5)];
					std::string rhs = (op[0] == '<' || op[0] == '>') ? std::to_string(rng_.range(0, 8)) : int_expr(depth - 1);
					return "(" + int_expr(depth - 1) + " " + op + " " + rhs + ")";
				}
				case 4: return "(" + bool_expr(depth - 1) + " ? " + int_expr(depth - 1) + " : " + int_expr(depth - 1) + ")";
				case 5: { // container/string size
					if (const var_info* a = random_var(vt::arr)) return a->name + (rng_.chance(50) ? ".size()" : ".length()");
					if (const var_info* s = random_var(vt::s)) return s->name + ".size()";
					return int_leaf_or_var();
				}
				case 6: { // array element
					if (const var_info* a = random_var(vt::arr)) return a->name + "[" + std::to_string(rng_.range(0, 2)) + "]";
					return int_leaf_or_var();
				}
				case 7: { // int-returning call
					std::string c = call_returning(vt::i);
					return c.empty() ? int_leaf_or_var() : c;
				}
				case 8: { // object int field / method
					if (const var_info* o = random_var(vt::obj)) {
						const class_info& ci = classes_[o->class_idx];
						std::vector<std::string> opts;
						for (const auto& fld : ci.fields) if (fld.second == vt::i) opts.push_back(o->name + "." + fld.first);
						for (const auto& m : ci.methods) if (m.ret == vt::i && m.arity == 0) opts.push_back(o->name + "." + m.name + "()");
						if (!opts.empty()) return opts[rng_.below(static_cast<uint32_t>(opts.size()))];
					}
					return int_leaf_or_var();
				}
				case 9: { // static member read
					for (const auto& ci : classes_)
						if (!ci.statics.empty())
							return ci.name + "::" + ci.statics[rng_.below(static_cast<uint32_t>(ci.statics.size()))];
					return int_leaf_or_var();
				}
				case 10: return "(" + int_expr(depth - 1) + " <=> " + int_expr(depth - 1) + ")";
				case 11: { // string method returning int
					if (const var_info* s = random_var(vt::s)) {
						switch (rng_.below(3)) {
							case 0: return "(" + s->name + ".find(" + string_literal() + "))";
							case 1: return "(" + s->name + ".count(" + string_literal() + "))";
							default: return s->name + ".length()";
						}
					}
					return int_leaf_or_var();
				}
				default: return int_leaf_or_var();
			}
		}
		std::string int_leaf_or_var() {
			if (rng_.chance(50)) {
				if (const var_info* v = random_var(vt::i)) return v->name;
			}
			return int_literal();
		}

		std::string float_expr(int depth) {
			if (depth <= 0) {
				if (rng_.chance(40)) {
					if (const var_info* v = random_var(vt::f)) return v->name;
				}
				return float_literal();
			}
			switch (pick_weighted({35, 20, 15, 12, 10, 8})) {
				case 0: {
					static constexpr const char* const ops[] = {"+", "-", "*", "/"};
					return "(" + float_expr(depth - 1) + " " + ops[rng_.below(4)] + " " + float_expr(depth - 1) + ")";
				}
				case 1: { const var_info* v = random_var(vt::f); return v ? v->name : float_literal(); }
				case 2: return "(" + int_expr(depth - 1) + " * " + float_literal() + ")"; // int/float mixing
				case 3: return "(" + bool_expr(depth - 1) + " ? " + float_expr(depth - 1) + " : " + float_expr(depth - 1) + ")";
				case 4: return "(-(" + float_expr(depth - 1) + "))";
				default: {
					std::string c = call_returning(vt::f);
					return c.empty() ? float_literal() : c;
				}
			}
		}

		std::string bool_expr(int depth) {
			if (depth <= 0) {
				if (rng_.chance(35)) {
					if (const var_info* v = random_var(vt::b)) return v->name;
				}
				return bool_literal();
			}
			switch (pick_weighted({30, 18, 12, 10, 10, 8, 6, 6})) {
				case 0: {
					static constexpr const char* const ops[] = {"<", ">", "<=", ">=", "==", "!="};
					return "(" + int_expr(depth - 1) + " " + ops[rng_.below(6)] + " " + int_expr(depth - 1) + ")";
				}
				case 1: {
					static constexpr const char* const ops[] = {"&&", "||"};
					return "(" + bool_expr(depth - 1) + " " + ops[rng_.below(2)] + " " + bool_expr(depth - 1) + ")";
				}
				case 2: return "(!" + bool_expr(depth - 1) + ")";
				case 3: {
					static constexpr const char* const ops[] = {"==", "!=", "<"};
					return "(" + string_expr(depth - 1) + " " + ops[rng_.below(3)] + " " + string_expr(depth - 1) + ")";
				}
				case 4: {
					static constexpr const char* const ops[] = {"<", ">", "==", "!="};
					return "(" + float_expr(depth - 1) + " " + ops[rng_.below(4)] + " " + float_expr(depth - 1) + ")";
				}
				case 5: { // container predicates
					if (const var_info* a = random_var(vt::arr)) {
						switch (rng_.below(4)) {
							case 0: return a->name + ".empty()";
							case 1: return "(" + a->name + ".contains(" + int_literal() + "))";
							case 2:
								// bare element read as truthiness (element reads reach
								// conditions as reference wrappers - is_truthy must deref)
								return "(" + a->name + ".size() > 0 && " + a->name + "[0])";
							default: return "(" + a->name + ".has(" + int_literal() + "))";
						}
					}
					if (const var_info* m = random_var(vt::map)) return "(" + m->name + ".has(" + string_literal() + "))";
					return bool_literal();
				}
				case 6: { // string predicates
					if (const var_info* s = random_var(vt::s)) {
						switch (rng_.below(3)) {
							case 0: return "(" + s->name + ".contains(" + string_literal() + "))";
							case 1: return "(" + s->name + ".starts_with(" + string_literal() + "))";
							default: return s->name + ".empty()";
						}
					}
					return bool_literal();
				}
				default: { // object ordering via operator<
					if (const var_info* o = random_var(vt::obj)) {
						const class_info& ci = classes_[o->class_idx];
						if (ci.has_op_lt) {
							if (const var_info* o2 = random_var(vt::obj)) {
								if (o2->class_idx == o->class_idx)
									return "(" + o->name + " < " + o2->name + ")";
							}
						}
					}
					return bool_literal();
				}
			}
		}

		std::string string_expr(int depth) {
			if (depth <= 0) {
				if (rng_.chance(40)) {
					if (const var_info* v = random_var(vt::s)) return v->name;
				}
				return string_literal();
			}
			switch (pick_weighted({30, 20, 15, 15, 12, 8})) {
				case 0: return "(" + string_expr(depth - 1) + " + " + string_expr(depth - 1) + ")";
				case 1: return "(" + string_expr(depth - 1) + " + to_string(" + int_expr(depth - 1) + "))";
				case 2: { const var_info* v = random_var(vt::s); return v ? v->name : string_literal(); }
				case 3: { // builtin string methods
					std::string base = random_var(vt::s) ? random_var(vt::s)->name : string_literal();
					switch (rng_.below(8)) {
						case 0: return base + ".to_upper()";
						case 1: return base + ".to_lower()";
						case 2: return base + ".trim()";
						case 3: return base + ".reverse()";
						case 4: return base + ".substr(0, " + std::to_string(rng_.range(0, 3)) + ")";
						case 5: return base + ".repeat(" + std::to_string(rng_.range(0, 3)) + ")";
						case 6: return base + ".replace_all(" + string_literal() + ", " + string_literal() + ")";
						default: return base + ".pad_left(" + std::to_string(rng_.range(0, 6)) + ", \"*\")";
					}
				}
				case 4: return "to_string(" + expr(rng_.chance(50) ? vt::i : vt::f, depth - 1) + ")";
				default: {
					std::string c = call_returning(vt::s);
					return c.empty() ? string_literal() : c;
				}
			}
		}

		std::string array_expr(int depth) {
			if (depth <= 0 || rng_.chance(40)) {
				if (const var_info* v = random_var(vt::arr)) if (rng_.chance(70)) return v->name;
				int n = static_cast<int>(rng_.range(1, 4));
				std::string out = "[";
				vt elem = rng_.chance(70) ? vt::i : (rng_.chance(50) ? vt::s : vt::f);
				for (int i = 0; i < n; ++i) {
					if (i) out += ", ";
					out += expr(elem, 0);
				}
				return out + "]";
			}
			if (const var_info* a = random_var(vt::arr)) {
				switch (rng_.below(3)) {
					case 0: return a->name + ".slice(0, " + std::to_string(rng_.range(0, 2)) + ")";
					case 1: return a->name + ".reverse()";
					default: return a->name;
				}
			}
			if (const var_info* s = random_var(vt::s)) return s->name + ".split(\",\")";
			return array_expr(0);
		}

		std::string map_expr(int depth) {
			if (const var_info* v = random_var(vt::map)) if (rng_.chance(50)) return v->name;
			(void)depth;
			int n = static_cast<int>(rng_.range(1, 3));
			std::string out = "{";
			for (int i = 0; i < n; ++i) {
				if (i) out += ", ";
				out += "\"k" + std::to_string(i) + "\": " + expr(rng_.chance(70) ? vt::i : vt::s, 0);
			}
			return out + "}";
		}

		std::string obj_expr(int depth) {
			(void)depth;
			if (classes_.empty()) return int_literal();
			if (rng_.chance(55)) {
				if (const var_info* v = random_var(vt::obj)) return v->name;
			}
			const class_info& ci = classes_[rng_.below(static_cast<uint32_t>(classes_.size()))];
			int arity = ci.ctor_arities.empty() ? 0 : ci.ctor_arities[rng_.below(static_cast<uint32_t>(ci.ctor_arities.size()))];
			std::string out = ci.name + "(";
			for (int i = 0; i < arity; ++i) {
				if (i) out += ", ";
				out += int_expr(1);
			}
			return out + ")";
		}

		// Call a previously declared function returning `want` ("" when none fits).
		std::string call_returning(vt want) {
			std::vector<const func_info*> fits;
			for (const auto& f : funcs_)
				if (!f.is_coroutine && (f.ret == want || (f.ret == vt::any && want == vt::i)) && !has_ref_param(f))
					fits.push_back(&f);
			if (fits.empty()) return {};
			const func_info* f = fits[rng_.below(static_cast<uint32_t>(fits.size()))];
			return call_text(*f);
		}
		static bool has_ref_param(const func_info& f) {
			for (const auto& p : f.params) if (p.is_ref) return true;
			return false;
		}
		std::string call_text(const func_info& f) {
			std::string out = f.name + "(";
			for (size_t i = 0; i < f.params.size(); ++i) {
				if (i) out += ", ";
				out += expr(f.params[i].type, 1);
			}
			return out + ")";
		}

		// An int lvalue (plain var / obj int field / array element); declares a
		// temp when nothing suitable is in scope. Used for ref args and ++/--.
		std::string int_lvalue() {
			std::vector<std::string> opts;
			if (const var_info* v = random_var(vt::i)) opts.push_back(v->name);
			if (const var_info* o = random_var(vt::obj)) {
				const class_info& ci = classes_[o->class_idx];
				for (const auto& fld : ci.fields)
					if (fld.second == vt::i) { opts.push_back(o->name + "." + fld.first); break; }
			}
			if (const var_info* a = random_var(vt::arr))
				opts.push_back(a->name + "[" + std::to_string(rng_.range(0, 2)) + "]");
			if (!opts.empty()) return opts[rng_.below(static_cast<uint32_t>(opts.size()))];
			std::string t = fresh("t");
			lines_.push_back("int " + t + " = " + int_literal() + ";");
			declare(t, vt::i);
			return t;
		}

		// ---------- classes ----------
		void emit_class() {
			class_info ci;
			ci.name = fresh("C");
			int base = -1;
			if (!classes_.empty() && rng_.chance(45)) base = static_cast<int>(rng_.below(static_cast<uint32_t>(classes_.size())));
			ci.base_idx = base;

			std::string header = "class " + ci.name;
			if (base >= 0) header += " : " + classes_[base].name;
			lines_.push_back(header + " {");

			if (base >= 0) { // inherit flattened field/method view
				ci.fields = classes_[base].fields;
				ci.methods = classes_[base].methods;
				ci.coroutine_method = classes_[base].coroutine_method;
				ci.typed_int_field = classes_[base].typed_int_field;
			}

			int n_fields = static_cast<int>(rng_.range(1, 3));
			std::string first_int_field;
			for (int i = 0; i < n_fields; ++i) {
				std::string fname = fresh("m");
				switch (pick_weighted({55, 15, 15, 15})) {
					case 0:
						lines_.push_back("int " + fname + " = " + std::to_string(rng_.range(0, 20)) + ";");
						ci.fields.push_back({fname, vt::i});
						if (first_int_field.empty()) first_int_field = fname;
						if (ci.typed_int_field.empty()) ci.typed_int_field = fname;
						break;
					case 1:
						lines_.push_back("string " + fname + " = " + string_literal() + ";");
						ci.fields.push_back({fname, vt::s});
						break;
					case 2:
						lines_.push_back("var " + fname + " = " + std::to_string(rng_.range(0, 9)) + ";");
						ci.fields.push_back({fname, vt::i});
						break;
					default:
						lines_.push_back("auto " + fname + " = " + float_literal() + ";");
						ci.fields.push_back({fname, vt::f});
						break;
				}
			}
			if (first_int_field.empty()) {
				first_int_field = fresh("m");
				lines_.push_back("int " + first_int_field + " = 1;");
				ci.fields.push_back({first_int_field, vt::i});
				if (ci.typed_int_field.empty()) ci.typed_int_field = first_int_field;
			}

			if (rng_.chance(35)) {
				std::string sname = fresh("s");
				lines_.push_back("static int " + sname + " = " + std::to_string(rng_.range(0, 100)) + ";");
				ci.statics.push_back(sname);
			}

			// Constructors
			bool base_needs_super = base >= 0 && !classes_[base].ctor_arities.empty() &&
				classes_[base].ctor_arities[0] != 0;
			auto super_call = [&]() -> std::string {
				if (base < 0) return {};
				const auto& arities = classes_[base].ctor_arities;
				int a = arities.empty() ? 0 : arities[rng_.below(static_cast<uint32_t>(arities.size()))];
				std::string s = " : super(";
				for (int i = 0; i < a; ++i) { if (i) s += ", "; s += std::to_string(rng_.range(0, 9)); }
				return s + ")";
			};
			int ctor_kind = pick_weighted({30, 30, 25, 15}); // none / (int) / both / delegating
			if (ctor_kind == 0 && !base_needs_super) {
				ci.ctor_arities.push_back(0); // implicit default ctor
			} else {
				bool used_super = base >= 0 && rng_.chance(70);
				std::string sup = used_super ? super_call() : std::string();
				if (ctor_kind == 3) { // delegation: C() : this(k) {}  +  C(int a)
					lines_.push_back(ci.name + "(int a)" + sup + " { " + first_int_field + " = a; }");
					lines_.push_back(ci.name + "() : this(" + std::to_string(rng_.range(1, 9)) + ") { }");
					ci.ctor_arities.push_back(0);
					ci.ctor_arities.push_back(1);
				} else if (ctor_kind == 2) {
					lines_.push_back(ci.name + "()" + sup + " { }");
					lines_.push_back(ci.name + "(int a)" + (used_super ? super_call() : std::string()) + " { " + first_int_field + " = a; }");
					ci.ctor_arities.push_back(0);
					ci.ctor_arities.push_back(1);
				} else {
					lines_.push_back(ci.name + "(int a)" + sup + " { " + first_int_field + " = a; }");
					ci.ctor_arities.push_back(1); // explicit ctor: no implicit default
				}
				if (rng_.chance(20)) { // ctor that throws
					lines_.push_back(ci.name + "(string boom) { throw \"ctor-boom-" + ci.name + "\"; }");
				}
			}

			// Methods
			{
				std::string g = fresh("get");
				lines_.push_back("int " + g + "() { return " + first_int_field + "; }");
				ci.methods.push_back({g, vt::i, 0});
			}
			if (rng_.chance(60)) {
				std::string a = fresh("add");
				lines_.push_back("void " + a + "(int n) { " + first_int_field + " = " + first_int_field + " + n; }");
				ci.methods.push_back({a, vt::any, 1});
			}
			if (rng_.chance(30)) {
				std::string d = fresh("desc");
				lines_.push_back("string " + d + "() { return \"<\" + to_string(" + first_int_field + ") + \">\"; }");
				ci.methods.push_back({d, vt::s, 0});
			}

			// Coroutine method: yields this-fields across suspensions, sometimes with a
			// float final return (typed conversion) or a field mutation mid-flight
			if (rng_.chance(30)) {
				std::string cm = fresh("cog");
				ci.coroutine_method = cm;
				lines_.push_back("coroutine int " + cm + "() {");
				lines_.push_back("yield " + first_int_field + ";");
				if (rng_.chance(50)) lines_.push_back(first_int_field + " = " + first_int_field + " + 1;");
				lines_.push_back("yield " + first_int_field + " + " + std::to_string(rng_.range(1, 5)) + ";");
				if (rng_.chance(30)) lines_.push_back("return " + float_literal() + ";");
				else lines_.push_back("return " + first_int_field + ";");
				lines_.push_back("}");
			}

			// Operator methods (throwing variants exercise the operator-throw surface)
			if (rng_.chance(35)) {
				ci.has_op_plus = true;
				if (rng_.chance(20))
					lines_.push_back("auto operator+(" + ci.name + " other) { throw \"op-plus-boom\"; }");
				else
					lines_.push_back("auto operator+(" + ci.name + " other) { return " + first_int_field + " + other." + first_int_field + "; }");
			}
			if (rng_.chance(30)) {
				ci.has_op_lt = true;
				if (rng_.chance(20))
					lines_.push_back("bool operator<(" + ci.name + " other) { throw \"op-lt-boom\"; }");
				else
					lines_.push_back("bool operator<(" + ci.name + " other) { return " + first_int_field + " < other." + first_int_field + "; }");
			}

			lines_.push_back("}");
			classes_.push_back(std::move(ci));
		}

		void emit_class_redefinition() {
			// Redefine an existing class with a compatible-but-different shape;
			// instances created in segment 1 must migrate.
			const class_info& old = classes_[rng_.below(static_cast<uint32_t>(classes_.size()))];
			std::string keep_field, keep_method;
			for (const auto& f : old.fields) if (f.second == vt::i) { keep_field = f.first; break; }
			for (const auto& m : old.methods) if (m.ret == vt::i && m.arity == 0) { keep_method = m.name; break; }
			lines_.push_back("class " + old.name + " {");
			if (!keep_field.empty())
				lines_.push_back("int " + keep_field + " = " + std::to_string(rng_.range(0, 9)) + ";");
			std::string newf = fresh("m");
			lines_.push_back("int " + newf + " = " + std::to_string(rng_.range(50, 99)) + ";");
			if (!keep_method.empty() && !keep_field.empty())
				lines_.push_back("int " + keep_method + "() { return " + keep_field + " + " + newf + "; }");
			lines_.push_back("}");
		}

		void emit_function_redefinition() {
			// Redefine an existing non-coroutine function with the same signature shape.
			std::vector<const func_info*> fits;
			for (const auto& f : funcs_) if (!f.is_coroutine) fits.push_back(&f);
			if (fits.empty()) return;
			const func_info& f = *fits[rng_.below(static_cast<uint32_t>(fits.size()))];
			std::string header = "function " + f.name + "(";
			for (size_t i = 0; i < f.params.size(); ++i) {
				if (i) header += ", ";
				header += f.params[i].type_text + " " + f.params[i].name;
			}
			header += ")";
			lines_.push_back(header + " {");
			if (f.ret == vt::s) lines_.push_back("return \"reloaded\";");
			else if (f.ret == vt::f) lines_.push_back("return 9.5;");
			else lines_.push_back("return " + std::to_string(rng_.range(500, 999)) + ";");
			lines_.push_back("}");
		}

		// ---------- functions ----------
		void emit_function() {
			switch (pick_weighted({30, 15, 18, 12, 25})) {
				case 0: emit_simple_function(); break;
				case 1: emit_recursive_function(); break;
				case 2: emit_ref_mutator(); break;
				case 3: emit_var_ref_mutator(); break;
				default: emit_coroutine(); break;
			}
		}

		std::vector<param_info> gen_params(int max_params, bool allow_ref) {
			std::vector<param_info> params;
			int n = static_cast<int>(rng_.range(0, max_params));
			for (int i = 0; i < n; ++i) {
				param_info p;
				p.name = fresh("p");
				switch (pick_weighted({40, 15, 15, 10, allow_ref ? 20u : 0u})) {
					case 0: p.type_text = "int"; p.type = vt::i; break;
					case 1: p.type_text = "auto"; p.type = vt::i; break;
					case 2: p.type_text = "var"; p.type = vt::any; break;
					case 3: p.type_text = "string"; p.type = vt::s; break;
					default: p.type_text = "int&"; p.type = vt::i; p.is_ref = true; break;
				}
				params.push_back(std::move(p));
			}
			return params;
		}

		void begin_function_scope(const std::vector<param_info>& params) {
			scopes_.push_back({});
			for (const auto& p : params) declare(p.name, p.type == vt::any ? vt::i : p.type);
			in_function_ = true;
		}
		void end_function_scope() {
			scopes_.pop_back();
			in_function_ = false;
		}

		std::string params_text(const std::vector<param_info>& params) {
			std::string out;
			for (size_t i = 0; i < params.size(); ++i) {
				if (i) out += ", ";
				out += params[i].type_text + " " + params[i].name;
			}
			return out;
		}

		void emit_simple_function() {
			func_info f;
			f.name = fresh("fn");
			f.params = gen_params(3, true);
			int rk = pick_weighted({50, 20, 20, 10});
			f.ret = rk == 0 ? vt::i : rk == 1 ? vt::s : rk == 2 ? vt::f : vt::b;
			const char* ret_text = rk == 0 ? "int" : rk == 1 ? "string" : rk == 2 ? "float" : "bool";
			lines_.push_back("function " + f.name + "(" + params_text(f.params) + ") -> " + ret_text + " {");
			begin_function_scope(f.params);
			int body = static_cast<int>(rng_.range(1, 3));
			vt saved_ret = current_ret_;
			current_ret_ = f.ret;
			for (int i = 0; i < body; ++i) emit_stmt(1);
			lines_.push_back("return " + expr(f.ret, 2) + ";");
			current_ret_ = saved_ret;
			end_function_scope();
			lines_.push_back("}");
			funcs_.push_back(std::move(f));
		}

		void emit_recursive_function() {
			func_info f;
			f.name = fresh("rec");
			f.ret = vt::i;
			param_info p; p.name = fresh("n"); p.type_text = "int"; p.type = vt::i;
			f.params.push_back(p);
			lines_.push_back("function " + f.name + "(int " + p.name + ") -> int {");
			lines_.push_back("if (" + p.name + " <= 0) {");
			lines_.push_back("return " + std::to_string(rng_.range(0, 3)) + ";");
			lines_.push_back("}");
			lines_.push_back("return " + p.name + " + " + f.name + "(" + p.name + " - 1);");
			lines_.push_back("}");
			funcs_.push_back(std::move(f));
		}

		void emit_ref_mutator() {
			func_info f;
			f.name = fresh("bump");
			f.ret = vt::any;
			param_info p; p.name = fresh("x"); p.type_text = "int&"; p.type = vt::i; p.is_ref = true;
			f.params.push_back(p);
			lines_.push_back("function " + f.name + "(int& " + p.name + ") {");
			if (rng_.chance(30))
				lines_.push_back(p.name + " += " + std::to_string(rng_.range(1, 9)) + ";");
			else
				lines_.push_back(p.name + " = " + p.name + " + " + std::to_string(rng_.range(1, 9)) + ";");
			lines_.push_back("}");
			funcs_.push_back(std::move(f));
		}

		void emit_var_ref_mutator() {
			func_info f;
			f.name = fresh("vset");
			f.ret = vt::any;
			param_info p; p.name = fresh("x"); p.type_text = "var&"; p.type = vt::any; p.is_ref = true;
			f.params.push_back(p);
			lines_.push_back("function " + f.name + "(var& " + p.name + ") {");
			lines_.push_back(p.name + " = " + expr(vt::any, 1) + ";");
			lines_.push_back("}");
			funcs_.push_back(std::move(f));
		}

		void emit_coroutine() {
			func_info f;
			f.name = fresh("co");
			f.ret = vt::i;
			f.is_coroutine = true;
			bool takes_arg = rng_.chance(60);
			std::string pname;
			if (takes_arg) {
				param_info p; p.name = fresh("a"); p.type_text = "int"; p.type = vt::i;
				pname = p.name;
				f.params.push_back(p);
			}
			lines_.push_back("coroutine int " + f.name + "(" + (takes_arg ? "int " + pname : std::string()) + ") {");
			std::string acc = takes_arg ? pname : int_literal();
			int yields = static_cast<int>(rng_.range(1, 3));
			for (int i = 0; i < yields; ++i) {
				lines_.push_back("yield " + acc + " + " + std::to_string(i) + ";");
			}
			if (rng_.chance(70)) {
				// typed final return, sometimes a float needing conversion (spicy)
				if (rng_.chance(25)) lines_.push_back("return " + float_literal() + ";");
				else lines_.push_back("return " + acc + " * 2;");
			}
			lines_.push_back("}");
			funcs_.push_back(std::move(f));
		}

		// ---------- statements ----------
		void emit_stmt(int nesting) {
			if (nesting > 3) { emit_leaf_stmt(); return; }
			switch (pick_weighted({26, 12, 8, 7, 7, 6, 6, 5, 5, 5, 4, 4, 5})) {
				case 0: emit_leaf_stmt(); break;
				case 1: emit_decl(); break;
				case 2: emit_if(nesting); break;
				case 3: emit_cfor(nesting); break;
				case 4: emit_range_for(nesting); break;
				case 5: emit_while(nesting); break;
				case 6: emit_try_catch(nesting); break;
				case 7: emit_switch(nesting); break;
				case 8: emit_lambda_stmt(); break;
				case 9: emit_coroutine_drive(); break;
				case 10: emit_container_mutation(); break;
				case 11: emit_ref_call(); break;
				default: emit_object_stmt(); break;
			}
		}

		void emit_leaf_stmt() {
			switch (pick_weighted({25, 22, 14, 12, 10, 9, 8})) {
				case 0: emit_decl(); break;
				case 1: { // assignment (occasionally deliberately ill-typed: matching errors = agreement)
					const var_info* v = nullptr;
					for (int tries = 0; tries < 3 && !v; ++tries) {
						auto t = random_value_type();
						if (t == vt::obj) t = vt::i;
						v = random_var(t);
					}
					if (!v) { emit_decl(); break; }
					vt rhs_t = v->type;
					if (v->is_var_decl && rng_.chance(35)) rhs_t = vt::any; // var may retype
					else if (rng_.chance(5)) rhs_t = (v->type == vt::s) ? vt::i : vt::s; // type-ladder violation
					lines_.push_back(v->name + " = " + expr(rhs_t, max_depth_ - 1) + ";");
					break;
				}
				case 2: { // compound assign
					if (const var_info* v = random_var(rng_.chance(70) ? vt::i : vt::s)) {
						if (v->type == vt::i) {
							static constexpr const char* const ops[] = {"+=", "-=", "*=", "/=", "%="};
							const char* op = ops[pick_weighted({40, 25, 20, 8, 7})];
							std::string rhs = int_expr(1);
							if (op[0] == '/' || op[0] == '%') rhs = "(" + rhs + " + 3)";
							lines_.push_back(v->name + " " + op + " " + rhs + ";");
						} else {
							lines_.push_back(v->name + " += " + string_expr(1) + ";");
						}
					} else {
						emit_decl();
					}
					break;
				}
				case 3: { // print
					int n = static_cast<int>(rng_.range(1, 3));
					std::string out = "print(";
					for (int i = 0; i < n; ++i) {
						if (i) out += ", ";
						out += expr(vt::any, 2);
					}
					lines_.push_back(out + ");");
					break;
				}
				case 4: { // ++/--
					std::string lv = int_lvalue();
					static constexpr const char* const forms[] = {"++%;", "--%;", "%++;", "%--;"};
					std::string f = forms[rng_.below(4)];
					size_t pos = f.find('%');
					lines_.push_back(f.substr(0, pos) + lv + f.substr(pos + 1));
					break;
				}
				case 5: { // call as statement
					if (!funcs_.empty()) {
						const func_info& f = funcs_[rng_.below(static_cast<uint32_t>(funcs_.size()))];
						if (!f.is_coroutine && !has_ref_param(f)) {
							lines_.push_back(call_text(f) + ";");
							break;
						}
					}
					emit_ref_call();
					break;
				}
				default: emit_object_stmt(); break;
			}
		}

		void emit_decl() {
			std::string name = fresh("v");
			switch (pick_weighted({22, 16, 16, 10, 10, 10, 8, 8})) {
				case 0:
					lines_.push_back("int " + name + " = " + int_expr(max_depth_ - 1) + ";");
					declare(name, vt::i);
					break;
				case 1: {
					vt t = random_value_type();
					if (t == vt::obj && classes_.empty()) t = vt::i;
					lines_.push_back("auto " + name + " = " + expr(t, max_depth_ - 1) + ";");
					int cidx = -1;
					if (t == vt::obj) cidx = last_obj_class_;
					declare(name, t, cidx);
					break;
				}
				case 2: {
					vt t = random_value_type();
					if (t == vt::obj && classes_.empty()) t = vt::i;
					lines_.push_back("var " + name + " = " + expr(t, max_depth_ - 1) + ";");
					declare(name, t, t == vt::obj ? last_obj_class_ : -1, true);
					break;
				}
				case 3:
					lines_.push_back("string " + name + " = " + string_expr(max_depth_ - 1) + ";");
					declare(name, vt::s);
					break;
				case 4:
					lines_.push_back("double " + name + " = " + float_expr(max_depth_ - 1) + ";");
					declare(name, vt::f);
					break;
				case 5:
					lines_.push_back("auto " + name + " = " + array_expr(2) + ";");
					declare(name, vt::arr);
					break;
				case 6:
					lines_.push_back("auto " + name + " = " + map_expr(1) + ";");
					declare(name, vt::map);
					break;
				default:
					if (!classes_.empty()) {
						int cidx = static_cast<int>(rng_.below(static_cast<uint32_t>(classes_.size())));
						const class_info& ci = classes_[cidx];
						int arity = ci.ctor_arities.empty() ? 0 : ci.ctor_arities[rng_.below(static_cast<uint32_t>(ci.ctor_arities.size()))];
						std::string args;
						for (int i = 0; i < arity; ++i) { if (i) args += ", "; args += int_expr(1); }
						lines_.push_back("auto " + name + " = " + ci.name + "(" + args + ");");
						declare(name, vt::obj, cidx);
						last_obj_class_ = cidx;
					} else {
						lines_.push_back("bool " + name + " = " + bool_expr(2) + ";");
						declare(name, vt::b);
					}
					break;
			}
		}
		int last_obj_class_ = 0;

		void emit_block(int nesting, int min_stmts = 1, int max_stmts = 3) {
			scopes_.push_back({});
			int n = static_cast<int>(rng_.range(min_stmts, max_stmts));
			for (int i = 0; i < n; ++i) emit_stmt(nesting + 1);
			scopes_.pop_back();
		}

		void emit_if(int nesting) {
			lines_.push_back("if (" + bool_expr(max_depth_ - 1) + ") {");
			emit_block(nesting);
			if (rng_.chance(45)) {
				lines_.push_back("} else {");
				emit_block(nesting);
			}
			lines_.push_back("}");
		}

		void emit_cfor(int nesting) {
			std::string iv = fresh("i");
			std::string bound = std::to_string(rng_.range(1, 6));
			std::string step = rng_.chance(70) ? iv + "++" : "++" + iv;
			lines_.push_back("for (int " + iv + " = 0; " + iv + " < " + bound + "; " + step + ") {");
			scopes_.push_back({});
			declare(iv, vt::i);
			++loop_depth_;
			int n = static_cast<int>(rng_.range(1, 3));
			for (int i = 0; i < n; ++i) emit_stmt(nesting + 1);
			// spice: redeclare an outer variable inside the loop body
			if (rng_.chance(18)) {
				if (const var_info* outer = random_var(vt::i))
					lines_.push_back("var " + outer->name + " = " + int_expr(1) + ";");
			}
			// spice: capture the loop var in a lambda, call it in-body
			if (rng_.chance(15)) {
				std::string lam = fresh("lam");
				lines_.push_back("auto " + lam + " = [=]() -> auto { return " + iv + " * 10; };");
				lines_.push_back("print(" + lam + "());");
			}
			if (loop_depth_ > 0 && rng_.chance(12)) lines_.push_back(rng_.chance(50) ? "break;" : "continue;");
			--loop_depth_;
			scopes_.pop_back();
			lines_.push_back("}");
		}

		void emit_range_for(int nesting) {
			const var_info* arr = random_var(vt::arr);
			std::string target;
			if (arr && rng_.chance(70)) target = arr->name;
			else target = array_expr(1);
			bool by_ref = rng_.chance(30) && !target.empty() && target[0] != '[';
			std::string ev = fresh("e");
			lines_.push_back("for (auto" + std::string(by_ref ? "&" : "") + " " + ev + " : " + target + ") {");
			scopes_.push_back({});
			declare(ev, vt::any);
			++loop_depth_;
			if (by_ref) lines_.push_back(ev + " = " + ev + " + " + std::to_string(rng_.range(1, 5)) + ";");
			else lines_.push_back("print(" + ev + ");");
			if (rng_.chance(35)) emit_stmt(nesting + 1);
			--loop_depth_;
			scopes_.pop_back();
			lines_.push_back("}");
			if (by_ref && arr) lines_.push_back("print(" + arr->name + "[0]);");
			// map iteration variant
			if (rng_.chance(25)) {
				if (const var_info* m = random_var(vt::map)) {
					std::string kv = fresh("kv");
					lines_.push_back("for (auto " + kv + " : " + m->name + ") {");
					lines_.push_back("print(" + kv + ".first, " + kv + ".second);");
					lines_.push_back("}");
				}
			}
		}

		void emit_while(int nesting) {
			std::string g = fresh("g");
			lines_.push_back("int " + g + " = 0;");
			declare(g, vt::i);
			if (rng_.chance(40)) {
				// ++ in the condition
				lines_.push_back("while (++" + g + " < " + std::to_string(rng_.range(2, 7)) + ") {");
				scopes_.push_back({});
				++loop_depth_;
				emit_stmt(nesting + 1);
				--loop_depth_;
				scopes_.pop_back();
				lines_.push_back("}");
			} else {
				lines_.push_back("while (" + bool_expr(2) + " && " + g + " < " + std::to_string(rng_.range(2, 7)) + ") {");
				scopes_.push_back({});
				++loop_depth_;
				lines_.push_back(g + " = " + g + " + 1;");
				emit_stmt(nesting + 1);
				--loop_depth_;
				scopes_.pop_back();
				lines_.push_back("}");
			}
		}

		void emit_try_catch(int nesting) {
			lines_.push_back("try {");
			scopes_.push_back({});
			switch (pick_weighted({30, 20, 15, 20, 15})) {
				case 0:
					emit_stmt(nesting + 1);
					lines_.push_back("throw " + (rng_.chance(60) ? string_literal() : int_literal()) + ";");
					break;
				case 1: // overflow edge inside try
					lines_.push_back("int " + fresh("ov") + " = 9223372036854775807 + " + int_expr(1) + ";");
					break;
				case 2: // INT64_MIN / -1
					lines_.push_back("int " + fresh("dv") + " = (-9223372036854775807 - 1) / (-1);");
					break;
				case 3: { // throwing operator / ctor
					bool emitted = false;
					for (size_t c = 0; c < classes_.size(); ++c) {
						if (classes_[c].has_op_plus) {
							std::string a = fresh("oa"), b2 = fresh("ob");
							int arity = classes_[c].ctor_arities.empty() ? 0 : classes_[c].ctor_arities[0];
							std::string args = arity == 1 ? std::to_string(rng_.range(0, 9)) : std::string();
							lines_.push_back("auto " + a + " = " + classes_[c].name + "(" + args + ");");
							lines_.push_back("auto " + b2 + " = " + classes_[c].name + "(" + args + ");");
							if (rng_.chance(25)) // obj + primitive (mismatched operand)
								lines_.push_back("print(" + a + " + 1);");
							else
								lines_.push_back("print(" + a + " + " + b2 + ");");
							emitted = true;
							break;
						}
					}
					if (!emitted) {
						emit_stmt(nesting + 1);
						lines_.push_back("throw \"plain-boom\";");
					}
					break;
				}
				default:
					emit_stmt(nesting + 1);
					emit_stmt(nesting + 1);
					break;
			}
			scopes_.pop_back();
			std::string ex = fresh("ex");
			lines_.push_back("} catch (" + ex + ") {");
			scopes_.push_back({});
			declare(ex, vt::any);
			lines_.push_back("print(\"caught:\", " + ex + ");");
			scopes_.pop_back();
			lines_.push_back("}");
			// rare top-level uncaught throw at the very end of a program is added
			// by the driver via final expression errors instead (keeps programs alive).
		}

		void emit_switch(int nesting) {
			(void)nesting;
			std::string subject = random_var(vt::i) ? random_var(vt::i)->name : int_expr(1);
			lines_.push_back("switch ((" + subject + ") % 4) {");
			int n_cases = static_cast<int>(rng_.range(2, 4));
			for (int c = 0; c < n_cases; ++c) {
				lines_.push_back("case " + std::to_string(c) + ":");
				lines_.push_back("print(\"case" + std::to_string(c) + "\");");
				switch (pick_weighted({40, 35, 25})) {
					case 0: lines_.push_back("break;"); break;
					case 1: lines_.push_back("fallthrough;"); break;
					default: break; // break-by-default semantics
				}
			}
			if (rng_.chance(70)) {
				lines_.push_back("default:");
				lines_.push_back("print(\"default\");");
			}
			lines_.push_back("}");
		}

		void emit_lambda_stmt() {
			std::string lam = fresh("lam");
			const var_info* cap = random_var(vt::i);
			if (cap && rng_.chance(45)) {
				// [&] mutating capture
				lines_.push_back("auto " + lam + " = [&]() -> auto { " + cap->name + " = " + cap->name + " + 1; return " + cap->name + "; };");
				lines_.push_back("print(" + lam + "());");
				if (rng_.chance(50)) lines_.push_back("print(" + cap->name + ");");
			} else if (cap) {
				const char* capture = rng_.chance(50) ? "[=]" : "";
				std::string p = fresh("q");
				lines_.push_back("auto " + lam + " = " + capture + "(auto " + p + ") -> auto { return " + p + " + " + cap->name + "; };");
				lines_.push_back("print(" + lam + "(" + int_literal() + "));");
			} else {
				lines_.push_back("auto " + lam + " = [=](auto q) -> auto { return q * 2; };");
				lines_.push_back("print(" + lam + "(" + int_literal() + "));");
			}
		}

		void emit_coroutine_drive() {
			const func_info* co = nullptr;
			for (const auto& f : funcs_) if (f.is_coroutine) co = &f;
			if (!co) { emit_leaf_stmt(); return; }
			std::string args = co->params.empty() ? "" : int_expr(1);
			if (rng_.chance(45)) {
				// range-for drive
				std::string yv = fresh("y");
				lines_.push_back("for (auto " + yv + " : " + co->name + "(" + args + ")) {");
				lines_.push_back("print(" + yv + ");");
				lines_.push_back("}");
			} else {
				std::string h = fresh("h");
				lines_.push_back("auto " + h + " = " + co->name + "(" + args + ");");
				int resumes = static_cast<int>(rng_.range(1, 4));
				for (int i = 0; i < resumes; ++i) {
					std::string r = fresh("r");
					lines_.push_back("auto " + r + " = " + h + ".resume();");
					lines_.push_back("print(" + r + ");");
				}
				declare(h, vt::any);
			}
		}

		void emit_container_mutation() {
			if (const var_info* a = random_var(vt::arr)) {
				switch (rng_.below(5)) {
					case 0: lines_.push_back(a->name + ".push(" + int_expr(1) + ");"); break;
					case 1: lines_.push_back(a->name + ".pop();"); break;
					case 2: lines_.push_back(a->name + "[" + std::to_string(rng_.range(0, 2)) + "] = " + int_expr(1) + ";"); break;
					case 3: lines_.push_back(a->name + ".sort();"); break;
					default: lines_.push_back("print(" + a->name + ".join(\",\"));"); break;
				}
				return;
			}
			if (const var_info* m = random_var(vt::map)) {
				switch (rng_.below(3)) {
					case 0: lines_.push_back(m->name + "[" + string_literal() + "] = " + expr(vt::any, 1) + ";"); break;
					case 1: lines_.push_back(m->name + ".erase(" + string_literal() + ");"); break;
					default: lines_.push_back("print(" + m->name + ".size());"); break;
				}
				return;
			}
			emit_decl();
		}

		void emit_ref_call() {
			const func_info* rf = nullptr;
			for (const auto& f : funcs_) if (has_ref_param(f)) rf = &f;
			if (!rf) { emit_decl(); return; }
			std::string args;
			for (size_t i = 0; i < rf->params.size(); ++i) {
				if (i) args += ", ";
				if (rf->params[i].is_ref) args += int_lvalue(); // plain var / obj.field / arr[i]
				else args += expr(rf->params[i].type, 1);
			}
			lines_.push_back(rf->name + "(" + args + ");");
			if (rng_.chance(50)) lines_.push_back("print(\"after-ref-call\");");
		}

		void emit_object_stmt() {
			const var_info* o = random_var(vt::obj);
			if (!o) { emit_decl(); return; }
			const class_info& ci = classes_[o->class_idx];
			switch (rng_.below(6)) {
				case 4: { // coroutine method drive (handle pins the instance)
					if (ci.coroutine_method.empty()) { break; }
					if (rng_.chance(40)) {
						std::string yv = fresh("y");
						lines_.push_back("for (auto " + yv + " : " + o->name + "." + ci.coroutine_method + "()) {");
						lines_.push_back("print(" + yv + ");");
						lines_.push_back("}");
					} else {
						std::string h = fresh("h");
						lines_.push_back("auto " + h + " = " + o->name + "." + ci.coroutine_method + "();");
						int resumes = static_cast<int>(rng_.range(1, 3));
						for (int i = 0; i < resumes; ++i) {
							std::string r = fresh("r");
							lines_.push_back("auto " + r + " = " + h + ".resume();");
							lines_.push_back("print(" + r + ");");
						}
						lines_.push_back("print(" + h + ".done());");
						declare(h, vt::any);
					}
					return;
				}
				case 5: { // typed-field converting writes (int field: float assigns/compounds truncate)
					if (ci.typed_int_field.empty()) { break; }
					switch (rng_.below(3)) {
						case 0: lines_.push_back(o->name + "." + ci.typed_int_field + " = " + float_literal() + ";"); break;
						case 1: lines_.push_back(o->name + "." + ci.typed_int_field + " += " + float_literal() + ";"); break;
						default: lines_.push_back(o->name + "." + ci.typed_int_field + " %= " + std::to_string(rng_.range(2, 7)) + ";"); break;
					}
					lines_.push_back("print(" + o->name + "." + ci.typed_int_field + ");");
					return;
				}
				default: break;
			}
			switch (rng_.below(4)) {
				case 0: { // method call
					for (const auto& m : ci.methods) {
						if (m.arity == 1) { lines_.push_back(o->name + "." + m.name + "(" + int_expr(1) + ");"); return; }
					}
					if (!ci.methods.empty())
						lines_.push_back("print(" + o->name + "." + ci.methods[0].name + "());");
					return;
				}
				case 1: { // field assign
					for (const auto& f : ci.fields) {
						if (f.second == vt::i) { lines_.push_back(o->name + "." + f.first + " = " + int_expr(2) + ";"); return; }
					}
					return;
				}
				case 2: { // static assign / read
					if (!ci.statics.empty()) {
						lines_.push_back(ci.name + "::" + ci.statics[0] + " = " + int_expr(1) + ";");
						lines_.push_back("print(" + ci.name + "::" + ci.statics[0] + ");");
						return;
					}
					if (!ci.fields.empty())
						lines_.push_back("print(" + o->name + "." + ci.fields[0].first + ");");
					return;
				}
				default: { // print a field
					if (!ci.fields.empty())
						lines_.push_back("print(" + o->name + "." + ci.fields[rng_.below(static_cast<uint32_t>(ci.fields.size()))].first + ");");
					return;
				}
			}
		}
	};

	inline program generate_program(uint64_t seed, int max_expr_depth = 4) {
		return generator(seed, max_expr_depth).generate();
	}

} // namespace jai::fuzz
