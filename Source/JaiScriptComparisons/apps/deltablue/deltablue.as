// DeltaBlue: the incremental dataflow constraint solver (Maloney, Freeman-Benson), after the Octane (V8) deltablue.js.
const int ITERATIONS = 200;
const int CHAIN_SIZE = 100;
const int PROJECTION_SIZE = 100;

const int NONE = 0;
const int FORWARD = 1;
const int BACKWARD = -1;

class Strength {
	int strength_value;
	string name;

	Strength(int a_strength_value, const string &in a_name) {
		strength_value = a_strength_value;
		name = a_name;
	}

	Strength@ next_weaker() {
		// Octane's table, kept as the benchmark defines it (REQUIRED steps straight to WEAKEST)
		return NEXT_WEAKER[strength_value];
	}
}

bool stronger(Strength@ s1, Strength@ s2) {
	return s1.strength_value < s2.strength_value;
}

bool weaker(Strength@ s1, Strength@ s2) {
	return s1.strength_value > s2.strength_value;
}

Strength@ weakest_of(Strength@ s1, Strength@ s2) {
	return weaker(s1, s2) ? s1 : s2;
}

Strength@ REQUIRED = Strength(0, "required");
Strength@ STRONG_PREFERRED = Strength(1, "strongPreferred");
Strength@ PREFERRED = Strength(2, "preferred");
Strength@ STRONG_DEFAULT = Strength(3, "strongDefault");
Strength@ NORMAL = Strength(4, "normal");
Strength@ WEAK_DEFAULT = Strength(5, "weakDefault");
Strength@ WEAKEST = Strength(6, "weakest");
array<Strength@> NEXT_WEAKER = {WEAKEST, WEAK_DEFAULT, NORMAL, STRONG_DEFAULT, PREFERRED, REQUIRED};

Planner@ planner;

// AngelScript has no abstract methods: the base carries empty bodies every constraint overrides
abstract class Constraint {
	Strength@ strength;

	Constraint(Strength@ a_strength) {
		@strength = a_strength;
	}

	void add_constraint() {
		add_to_graph();
		planner.incremental_add(this);
	}

	Constraint@ satisfy(int mark) {
		choose_method(mark);
		if (!is_satisfied()) {
			if (strength is REQUIRED) {
				throw("Could not satisfy a required constraint!");
			}
			return null;
		}
		mark_inputs(mark);
		Variable@ out_var = output();
		Constraint@ overridden = out_var.determined_by;
		if (overridden !is null) {
			overridden.mark_unsatisfied();
		}
		@out_var.determined_by = this;
		if (!planner.add_propagate(this, mark)) {
			throw("Cycle encountered");
		}
		out_var.mark = mark;
		return overridden;
	}

	void destroy_constraint() {
		if (is_satisfied()) {
			planner.incremental_remove(this);
		} else {
			remove_from_graph();
		}
	}

	bool is_input() {
		return false;
	}

	void add_to_graph() {}
	void choose_method(int mark) {}
	bool is_satisfied() { return false; }
	void mark_inputs(int mark) {}
	Variable@ output() { return null; }
	void recalculate() {}
	void mark_unsatisfied() {}
	bool inputs_known(int mark) { return false; }
	void remove_from_graph() {}
	void execute() {}
}

abstract class UnaryConstraint : Constraint {
	Variable@ my_output;
	bool satisfied = false;

	UnaryConstraint(Variable@ v, Strength@ a_strength) {
		super(a_strength);
		@my_output = v;
		add_constraint();
	}

	void add_to_graph() override {
		my_output.add_constraint(this);
		satisfied = false;
	}

	void choose_method(int mark) override {
		satisfied = my_output.mark != mark && stronger(strength, my_output.walk_strength);
	}

	bool is_satisfied() override {
		return satisfied;
	}

	void mark_inputs(int mark) override {}

	Variable@ output() override {
		return my_output;
	}

	void recalculate() override {
		@my_output.walk_strength = strength;
		my_output.stay = !is_input();
		if (my_output.stay) {
			execute();
		}
	}

	void mark_unsatisfied() override {
		satisfied = false;
	}

	bool inputs_known(int mark) override {
		return true;
	}

	void remove_from_graph() override {
		if (my_output !is null) {
			my_output.remove_constraint(this);
		}
		satisfied = false;
	}
}

class StayConstraint : UnaryConstraint {
	StayConstraint(Variable@ v, Strength@ a_strength) {
		super(v, a_strength);
	}

	void execute() override {}
}

class EditConstraint : UnaryConstraint {
	EditConstraint(Variable@ v, Strength@ a_strength) {
		super(v, a_strength);
	}

	bool is_input() override {
		return true;
	}

	void execute() override {}
}

abstract class BinaryConstraint : Constraint {
	Variable@ v1;
	Variable@ v2;
	int direction = NONE;

	BinaryConstraint(Variable@ var1, Variable@ var2, Strength@ a_strength) {
		super(a_strength);
		@v1 = var1;
		@v2 = var2;
		add_constraint();
	}

	void choose_method(int mark) override {
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

	void add_to_graph() override {
		v1.add_constraint(this);
		v2.add_constraint(this);
		direction = NONE;
	}

	bool is_satisfied() override {
		return direction != NONE;
	}

	void mark_inputs(int mark) override {
		input().mark = mark;
	}

	Variable@ input() {
		return direction == FORWARD ? v1 : v2;
	}

	Variable@ output() override {
		return direction == FORWARD ? v2 : v1;
	}

	void recalculate() override {
		Variable@ ihn = input();
		Variable@ out_var = output();
		@out_var.walk_strength = weakest_of(strength, ihn.walk_strength);
		out_var.stay = ihn.stay;
		if (out_var.stay) {
			execute();
		}
	}

	void mark_unsatisfied() override {
		direction = NONE;
	}

	bool inputs_known(int mark) override {
		Variable@ i = input();
		return i.mark == mark || i.stay || i.determined_by is null;
	}

	void remove_from_graph() override {
		if (v1 !is null) {
			v1.remove_constraint(this);
		}
		if (v2 !is null) {
			v2.remove_constraint(this);
		}
		direction = NONE;
	}
}

class ScaleConstraint : BinaryConstraint {
	// no initializers: they would run after super() and wipe what the base constructor's add_constraint() relies on
	Variable@ scale;
	Variable@ offset;

	ScaleConstraint(Variable@ src, Variable@ a_scale, Variable@ a_offset, Variable@ dest, Strength@ a_strength) {
		@scale = a_scale;
		@offset = a_offset;
		super(src, dest, a_strength);
	}

	void add_to_graph() override {
		BinaryConstraint::add_to_graph();
		scale.add_constraint(this);
		offset.add_constraint(this);
	}

	void remove_from_graph() override {
		BinaryConstraint::remove_from_graph();
		if (scale !is null) {
			scale.remove_constraint(this);
		}
		if (offset !is null) {
			offset.remove_constraint(this);
		}
	}

	void mark_inputs(int mark) override {
		BinaryConstraint::mark_inputs(mark);
		scale.mark = mark;
		offset.mark = mark;
	}

	void execute() override {
		if (direction == FORWARD) {
			v2.value = v1.value * scale.value + offset.value;
		} else {
			v1.value = (v2.value - offset.value) / scale.value;
		}
	}

	void recalculate() override {
		Variable@ ihn = input();
		Variable@ out_var = output();
		@out_var.walk_strength = weakest_of(strength, ihn.walk_strength);
		out_var.stay = ihn.stay && scale.stay && offset.stay;
		if (out_var.stay) {
			execute();
		}
	}
}

class EqualityConstraint : BinaryConstraint {
	EqualityConstraint(Variable@ var1, Variable@ var2, Strength@ a_strength) {
		super(var1, var2, a_strength);
	}

	void execute() override {
		output().value = input().value;
	}
}

class Variable {
	int value;
	array<Constraint@> constraints;
	Constraint@ determined_by;
	int mark = 0;
	Strength@ walk_strength = WEAKEST;
	bool stay = true;
	string name;

	Variable(const string &in a_name, int initial_value) {
		name = a_name;
		value = initial_value;
	}

	void add_constraint(Constraint@ c) {
		constraints.insertLast(c);
	}

	void remove_constraint(Constraint@ c) {
		int index = 0;
		int skipped = 0;
		int n = constraints.length();
		for (int i = 0; i < n; ++i) {
			Constraint@ value = constraints[i];
			if (value !is c) {
				@constraints[index] = value;
				++index;
			} else {
				++skipped;
			}
		}
		for (int i = 0; i < skipped; ++i) {
			constraints.removeLast();
		}
		if (determined_by is c) {
			@determined_by = null;
		}
	}
}

class Plan {
	array<Constraint@> v;

	void add_constraint(Constraint@ c) {
		v.insertLast(c);
	}

	void execute() {
		foreach (Constraint@ c : v) {
			c.execute();
		}
	}
}

class Planner {
	int current_mark = 0;

	void incremental_add(Constraint@ c) {
		int mark = new_mark();
		Constraint@ overridden = c.satisfy(mark);
		while (overridden !is null) {
			@overridden = overridden.satisfy(mark);
		}
	}

	void incremental_remove(Constraint@ c) {
		Variable@ out_var = c.output();
		c.mark_unsatisfied();
		c.remove_from_graph();
		array<Constraint@>@ unsatisfied = remove_propagate_from(out_var);
		Strength@ strength = REQUIRED;
		do {
			foreach (Constraint@ u : unsatisfied) {
				if (u.strength is strength) {
					incremental_add(u);
				}
			}
			@strength = strength.next_weaker();
		} while (strength !is WEAKEST);
	}

	int new_mark() {
		return ++current_mark;
	}

	Plan@ make_plan(array<Constraint@>@ sources) {
		int mark = new_mark();
		Plan plan;
		array<Constraint@>@ todo = sources;
		while (todo.length() > 0) {
			Constraint@ c = todo[0];
			todo.removeAt(0);
			if (c.output().mark != mark && c.inputs_known(mark)) {
				plan.add_constraint(c);
				c.output().mark = mark;
				add_constraints_consuming_to(c.output(), todo);
			}
		}
		return plan;
	}

	Plan@ extract_plan_from_constraints(array<Constraint@>@ constraints) {
		array<Constraint@> sources;
		foreach (Constraint@ c : constraints) {
			if (c.is_input() && c.is_satisfied()) {
				sources.insertLast(c);
			}
		}
		return make_plan(sources);
	}

	bool add_propagate(Constraint@ c, int mark) {
		array<Constraint@> todo = {c};
		while (todo.length() > 0) {
			Constraint@ d = todo[0];
			todo.removeAt(0);
			if (d.output().mark == mark) {
				incremental_remove(c);
				return false;
			}
			d.recalculate();
			add_constraints_consuming_to(d.output(), todo);
		}
		return true;
	}

	array<Constraint@>@ remove_propagate_from(Variable@ out_var) {
		@out_var.determined_by = null;
		@out_var.walk_strength = WEAKEST;
		out_var.stay = true;
		array<Constraint@> unsatisfied;
		array<Variable@> todo = {out_var};
		while (todo.length() > 0) {
			Variable@ v = todo[0];
			todo.removeAt(0);
			foreach (Constraint@ c : v.constraints) {
				if (!c.is_satisfied()) {
					unsatisfied.insertLast(c);
				}
			}
			Constraint@ determining = v.determined_by;
			foreach (Constraint@ next_constraint : v.constraints) {
				if (next_constraint !is determining && next_constraint.is_satisfied()) {
					next_constraint.recalculate();
					todo.insertLast(next_constraint.output());
				}
			}
		}
		return unsatisfied;
	}

	void add_constraints_consuming_to(Variable@ v, array<Constraint@>@ coll) {
		Constraint@ determining = v.determined_by;
		foreach (Constraint@ c : v.constraints) {
			if (c !is determining && c.is_satisfied()) {
				coll.insertLast(c);
			}
		}
	}
}

int64 chain_test(int n) {
	@planner = Planner();
	Variable@ prev;
	Variable@ first;
	Variable@ last;
	for (int i = 0; i <= n; ++i) {
		Variable@ v = Variable("v" + i, 0);
		if (prev !is null) {
			EqualityConstraint(prev, v, REQUIRED);
		}
		if (i == 0) {
			@first = v;
		}
		if (i == n) {
			@last = v;
		}
		@prev = v;
	}
	StayConstraint(last, STRONG_DEFAULT);
	EditConstraint edit(first, PREFERRED);
	Plan@ plan = planner.extract_plan_from_constraints({edit});
	int64 result = 0;
	for (int i = 0; i < 100; ++i) {
		first.value = i;
		plan.execute();
		if (last.value != i) {
			throw("Chain test failed.");
		}
		result = (result * 31 + last.value) & 0xFFFFFFFF;
	}
	return result;
}

void change(Variable@ v, int new_value) {
	EditConstraint edit(v, PREFERRED);
	Plan@ plan = planner.extract_plan_from_constraints({edit});
	for (int i = 0; i < 10; ++i) {
		v.value = new_value;
		plan.execute();
	}
	edit.destroy_constraint();
}

int64 projection_test(int n) {
	@planner = Planner();
	Variable@ scale = Variable("scale", 10);
	Variable@ offset = Variable("offset", 1000);
	Variable@ src;
	Variable@ dst;
	array<Variable@> dests;
	for (int i = 0; i < n; ++i) {
		@src = Variable("src" + i, i);
		@dst = Variable("dst" + i, i);
		dests.insertLast(dst);
		StayConstraint(src, NORMAL);
		ScaleConstraint(src, scale, offset, dst, REQUIRED);
	}
	change(src, 17);
	if (dst.value != 1170) {
		throw("Projection 1 failed");
	}
	change(dst, 1050);
	if (src.value != 5) {
		throw("Projection 2 failed");
	}
	change(scale, 5);
	for (int i = 0; i < n - 1; ++i) {
		if (dests[i].value != i * 5 + 1000) {
			throw("Projection 3 failed");
		}
	}
	change(offset, 2000);
	for (int i = 0; i < n - 1; ++i) {
		if (dests[i].value != i * 5 + 2000) {
			throw("Projection 4 failed");
		}
	}
	int64 result = 0;
	foreach (Variable@ d : dests) {
		result = (result * 31 + d.value) & 0xFFFFFFFF;
	}
	return result;
}

void main() {
	double start = clock();
	int64 result = 0;
	for (int i = 0; i < ITERATIONS; ++i) {
		result = (result * 31 + chain_test(CHAIN_SIZE)) & 0xFFFFFFFF;
		result = (result * 31 + projection_test(PROJECTION_SIZE)) & 0xFFFFFFFF;
	}
	double elapsed = (clock() - start) * 1000.0;
	print("result " + result);
	print("time_ms " + formatFloat(elapsed, "", 0, 3));
}
