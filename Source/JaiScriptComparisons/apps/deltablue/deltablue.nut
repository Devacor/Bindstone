// DeltaBlue: the incremental dataflow constraint solver (Maloney, Freeman-Benson), after the Octane (V8) deltablue.js.
const ITERATIONS = 200;
const CHAIN_SIZE = 100;
const PROJECTION_SIZE = 100;

const NONE = 0;
const FORWARD = 1;
const BACKWARD = -1;

function fail(message) {
	throw message;
}

class Strength {
	strength_value = 0;
	name = "";

	constructor(a_strength_value, a_name) {
		strength_value = a_strength_value;
		name = a_name;
	}

	function next_weaker() {
		// Octane's table, kept as the benchmark defines it (REQUIRED steps straight to WEAKEST)
		return ::NEXT_WEAKER[strength_value];
	}
}

function stronger(s1, s2) {
	return s1.strength_value < s2.strength_value;
}

function weaker(s1, s2) {
	return s1.strength_value > s2.strength_value;
}

function weakest_of(s1, s2) {
	return weaker(s1, s2) ? s1 : s2;
}

REQUIRED <- Strength(0, "required");
STRONG_PREFERRED <- Strength(1, "strongPreferred");
PREFERRED <- Strength(2, "preferred");
STRONG_DEFAULT <- Strength(3, "strongDefault");
NORMAL <- Strength(4, "normal");
WEAK_DEFAULT <- Strength(5, "weakDefault");
WEAKEST <- Strength(6, "weakest");
NEXT_WEAKER <- [WEAKEST, WEAK_DEFAULT, NORMAL, STRONG_DEFAULT, PREFERRED, REQUIRED];

planner <- null;

function remove_all(collection, element) {
	local index = 0;
	local skipped = 0;
	local n = collection.len();
	for (local i = 0; i < n; i++) {
		local value = collection[i];
		if (value != element) {
			collection[index] = value;
			index++;
		} else {
			skipped++;
		}
	}
	for (local i = 0; i < skipped; i++) {
		collection.pop();
	}
}

class Constraint {
	strength = null;

	function add_constraint() {
		add_to_graph();
		::planner.incremental_add(this);
	}

	function satisfy(mark) {
		choose_method(mark);
		if (!is_satisfied()) {
			if (strength == ::REQUIRED) {
				fail("Could not satisfy a required constraint!");
			}
			return null;
		}
		mark_inputs(mark);
		local out = output();
		local overridden = out.determined_by;
		if (overridden != null) {
			overridden.mark_unsatisfied();
		}
		out.determined_by = this;
		if (!::planner.add_propagate(this, mark)) {
			fail("Cycle encountered");
		}
		out.mark = mark;
		return overridden;
	}

	function destroy_constraint() {
		if (is_satisfied()) {
			::planner.incremental_remove(this);
		} else {
			remove_from_graph();
		}
	}

	function is_input() {
		return false;
	}
}

class UnaryConstraint extends Constraint {
	my_output = null;
	satisfied = false;

	constructor(v, a_strength) {
		strength = a_strength;
		my_output = v;
		satisfied = false;
		add_constraint();
	}

	function add_to_graph() {
		my_output.add_constraint(this);
		satisfied = false;
	}

	function choose_method(mark) {
		satisfied = my_output.mark != mark && stronger(strength, my_output.walk_strength);
	}

	function is_satisfied() {
		return satisfied;
	}

	function mark_inputs(mark) {
	}

	function output() {
		return my_output;
	}

	function recalculate() {
		my_output.walk_strength = strength;
		my_output.stay = !is_input();
		if (my_output.stay) {
			execute();
		}
	}

	function mark_unsatisfied() {
		satisfied = false;
	}

	function inputs_known(mark) {
		return true;
	}

	function remove_from_graph() {
		if (my_output != null) {
			my_output.remove_constraint(this);
		}
		satisfied = false;
	}
}

class StayConstraint extends UnaryConstraint {
	function execute() {
	}
}

class EditConstraint extends UnaryConstraint {
	function is_input() {
		return true;
	}

	function execute() {
	}
}

class BinaryConstraint extends Constraint {
	v1 = null;
	v2 = null;
	direction = NONE;

	constructor(var1, var2, a_strength) {
		strength = a_strength;
		v1 = var1;
		v2 = var2;
		direction = NONE;
		add_constraint();
	}

	function choose_method(mark) {
		if (v1.mark == mark) {
			direction = (v2.mark != mark && stronger(strength, v2.walk_strength)) ? FORWARD : NONE;
		}
		if (v2.mark == mark) {
			direction = (v1.mark != mark && stronger(strength, v1.walk_strength)) ? BACKWARD : NONE;
		}
		if (weaker(v1.walk_strength, v2.walk_strength)) {
			direction = stronger(strength, v1.walk_strength) ? BACKWARD : NONE;
		} else {
			direction = stronger(strength, v2.walk_strength) ? FORWARD : BACKWARD;
		}
	}

	function add_to_graph() {
		v1.add_constraint(this);
		v2.add_constraint(this);
		direction = NONE;
	}

	function is_satisfied() {
		return direction != NONE;
	}

	function mark_inputs(mark) {
		input().mark = mark;
	}

	function input() {
		return direction == FORWARD ? v1 : v2;
	}

	function output() {
		return direction == FORWARD ? v2 : v1;
	}

	function recalculate() {
		local ihn = input();
		local out = output();
		out.walk_strength = weakest_of(strength, ihn.walk_strength);
		out.stay = ihn.stay;
		if (out.stay) {
			execute();
		}
	}

	function mark_unsatisfied() {
		direction = NONE;
	}

	function inputs_known(mark) {
		local i = input();
		return i.mark == mark || i.stay || i.determined_by == null;
	}

	function remove_from_graph() {
		if (v1 != null) {
			v1.remove_constraint(this);
		}
		if (v2 != null) {
			v2.remove_constraint(this);
		}
		direction = NONE;
	}
}

class ScaleConstraint extends BinaryConstraint {
	scale = null;
	offset = null;

	constructor(src, a_scale, a_offset, dest, a_strength) {
		direction = NONE;
		scale = a_scale;
		offset = a_offset;
		base.constructor(src, dest, a_strength);
	}

	function add_to_graph() {
		base.add_to_graph();
		scale.add_constraint(this);
		offset.add_constraint(this);
	}

	function remove_from_graph() {
		base.remove_from_graph();
		if (scale != null) {
			scale.remove_constraint(this);
		}
		if (offset != null) {
			offset.remove_constraint(this);
		}
	}

	function mark_inputs(mark) {
		base.mark_inputs(mark);
		scale.mark = mark;
		offset.mark = mark;
	}

	function execute() {
		if (direction == FORWARD) {
			v2.value = v1.value * scale.value + offset.value;
		} else {
			v1.value = (v2.value - offset.value) / scale.value;
		}
	}

	function recalculate() {
		local ihn = input();
		local out = output();
		out.walk_strength = weakest_of(strength, ihn.walk_strength);
		out.stay = ihn.stay && scale.stay && offset.stay;
		if (out.stay) {
			execute();
		}
	}
}

class EqualityConstraint extends BinaryConstraint {
	function execute() {
		output().value = input().value;
	}
}

class Variable {
	value = 0;
	constraints = null;
	determined_by = null;
	mark = 0;
	walk_strength = null;
	stay = true;
	name = "";

	constructor(a_name, initial_value) {
		value = initial_value;
		constraints = [];
		determined_by = null;
		mark = 0;
		walk_strength = ::WEAKEST;
		stay = true;
		name = a_name;
	}

	function add_constraint(c) {
		constraints.append(c);
	}

	function remove_constraint(c) {
		remove_all(constraints, c);
		if (determined_by == c) {
			determined_by = null;
		}
	}
}

class Plan {
	v = null;

	constructor() {
		v = [];
	}

	function add_constraint(c) {
		v.append(c);
	}

	function execute() {
		foreach (c in v) {
			c.execute();
		}
	}
}

class Planner {
	current_mark = 0;

	function incremental_add(c) {
		local mark = new_mark();
		local overridden = c.satisfy(mark);
		while (overridden != null) {
			overridden = overridden.satisfy(mark);
		}
	}

	function incremental_remove(c) {
		local out = c.output();
		c.mark_unsatisfied();
		c.remove_from_graph();
		local unsatisfied = remove_propagate_from(out);
		local strength = ::REQUIRED;
		do {
			foreach (u in unsatisfied) {
				if (u.strength == strength) {
					incremental_add(u);
				}
			}
			strength = strength.next_weaker();
		} while (strength != ::WEAKEST);
	}

	function new_mark() {
		return ++current_mark;
	}

	function make_plan(sources) {
		local mark = new_mark();
		local plan = Plan();
		local todo = sources;
		while (todo.len() > 0) {
			local c = todo.remove(0);
			if (c.output().mark != mark && c.inputs_known(mark)) {
				plan.add_constraint(c);
				c.output().mark = mark;
				add_constraints_consuming_to(c.output(), todo);
			}
		}
		return plan;
	}

	function extract_plan_from_constraints(constraints) {
		local sources = [];
		foreach (c in constraints) {
			if (c.is_input() && c.is_satisfied()) {
				sources.append(c);
			}
		}
		return make_plan(sources);
	}

	function add_propagate(c, mark) {
		local todo = [c];
		while (todo.len() > 0) {
			local d = todo.remove(0);
			if (d.output().mark == mark) {
				incremental_remove(c);
				return false;
			}
			d.recalculate();
			add_constraints_consuming_to(d.output(), todo);
		}
		return true;
	}

	function remove_propagate_from(out) {
		out.determined_by = null;
		out.walk_strength = ::WEAKEST;
		out.stay = true;
		local unsatisfied = [];
		local todo = [out];
		while (todo.len() > 0) {
			local v = todo.remove(0);
			foreach (c in v.constraints) {
				if (!c.is_satisfied()) {
					unsatisfied.append(c);
				}
			}
			local determining = v.determined_by;
			foreach (next_constraint in v.constraints) {
				if (next_constraint != determining && next_constraint.is_satisfied()) {
					next_constraint.recalculate();
					todo.append(next_constraint.output());
				}
			}
		}
		return unsatisfied;
	}

	function add_constraints_consuming_to(v, coll) {
		local determining = v.determined_by;
		foreach (c in v.constraints) {
			if (c != determining && c.is_satisfied()) {
				coll.append(c);
			}
		}
	}
}

function chain_test(n) {
	::planner = Planner();
	local prev = null;
	local first = null;
	local last = null;
	for (local i = 0; i <= n; i++) {
		local v = Variable("v" + i, 0);
		if (prev != null) {
			EqualityConstraint(prev, v, ::REQUIRED);
		}
		if (i == 0) {
			first = v;
		}
		if (i == n) {
			last = v;
		}
		prev = v;
	}
	StayConstraint(last, ::STRONG_DEFAULT);
	local edit = EditConstraint(first, ::PREFERRED);
	local plan = ::planner.extract_plan_from_constraints([edit]);
	local result = 0;
	for (local i = 0; i < 100; i++) {
		first.value = i;
		plan.execute();
		if (last.value != i) {
			fail("Chain test failed.");
		}
		result = (result * 31 + last.value) & 0xFFFFFFFF;
	}
	return result;
}

function change(v, new_value) {
	local edit = EditConstraint(v, ::PREFERRED);
	local plan = ::planner.extract_plan_from_constraints([edit]);
	for (local i = 0; i < 10; i++) {
		v.value = new_value;
		plan.execute();
	}
	edit.destroy_constraint();
}

function projection_test(n) {
	::planner = Planner();
	local scale = Variable("scale", 10);
	local offset = Variable("offset", 1000);
	local src = null;
	local dst = null;
	local dests = [];
	for (local i = 0; i < n; i++) {
		src = Variable("src" + i, i);
		dst = Variable("dst" + i, i);
		dests.append(dst);
		StayConstraint(src, ::NORMAL);
		ScaleConstraint(src, scale, offset, dst, ::REQUIRED);
	}
	change(src, 17);
	if (dst.value != 1170) {
		fail("Projection 1 failed");
	}
	change(dst, 1050);
	if (src.value != 5) {
		fail("Projection 2 failed");
	}
	change(scale, 5);
	for (local i = 0; i < n - 1; i++) {
		if (dests[i].value != i * 5 + 1000) {
			fail("Projection 3 failed");
		}
	}
	change(offset, 2000);
	for (local i = 0; i < n - 1; i++) {
		if (dests[i].value != i * 5 + 2000) {
			fail("Projection 4 failed");
		}
	}
	local result = 0;
	foreach (d in dests) {
		result = (result * 31 + d.value) & 0xFFFFFFFF;
	}
	return result;
}

local start = clock();
local result = 0;
for (local i = 0; i < ITERATIONS; i++) {
	result = (result * 31 + chain_test(CHAIN_SIZE)) & 0xFFFFFFFF;
	result = (result * 31 + projection_test(PROJECTION_SIZE)) & 0xFFFFFFFF;
}
local elapsed = (clock() - start) * 1000.0;
print(format("result %d\n", result));
print(format("time_ms %.3f\n", elapsed));
