// DeltaBlue: the incremental dataflow constraint solver (Maloney, Freeman-Benson), after the Octane (V8) deltablue.js.
const int ITERATIONS = 200;
const int CHAIN_SIZE = 100;
const int PROJECTION_SIZE = 100;

const int NONE = 0;
const int FORWARD = 1;
const int BACKWARD = -1;

void fail(string message) {
	throw message;
}

class Strength {
	int strength_value = 0;
	string name = "";

	Strength(int a_strength_value, string a_name) {
		strength_value = a_strength_value;
		name = a_name;
	}

	shared_ptr<Strength> next_weaker() {
		// Octane's table, kept as the benchmark defines it (REQUIRED steps straight to WEAKEST)
		return NEXT_WEAKER[strength_value];
	}
}

bool stronger(shared_ptr<Strength> s1, shared_ptr<Strength> s2) {
	return s1.strength_value < s2.strength_value;
}

bool weaker(shared_ptr<Strength> s1, shared_ptr<Strength> s2) {
	return s1.strength_value > s2.strength_value;
}

shared_ptr<Strength> weakest_of(shared_ptr<Strength> s1, shared_ptr<Strength> s2) {
	return weaker(s1, s2) ? s1 : s2;
}

shared_ptr<Strength> REQUIRED = new Strength(0, "required");
shared_ptr<Strength> STRONG_PREFERRED = new Strength(1, "strongPreferred");
shared_ptr<Strength> PREFERRED = new Strength(2, "preferred");
shared_ptr<Strength> STRONG_DEFAULT = new Strength(3, "strongDefault");
shared_ptr<Strength> NORMAL = new Strength(4, "normal");
shared_ptr<Strength> WEAK_DEFAULT = new Strength(5, "weakDefault");
shared_ptr<Strength> WEAKEST = new Strength(6, "weakest");
array<shared_ptr<Strength>> NEXT_WEAKER = [WEAKEST, WEAK_DEFAULT, NORMAL, STRONG_DEFAULT, PREFERRED, REQUIRED];

class Variable {
	int value = 0;
	array<shared_ptr<Constraint>> constraints = [];
	shared_ptr<Constraint> determined_by = null;
	int mark = 0;
	shared_ptr<Strength> walk_strength = WEAKEST;
	bool stay = true;
	string name = "";

	Variable(string a_name, int initial_value) {
		name = a_name;
		value = initial_value;
	}

	void add_constraint(shared_ptr<Constraint> c) {
		constraints.push(c);
	}

	void remove_constraint(shared_ptr<Constraint> c) {
		int index = 0;
		int skipped = 0;
		int n = constraints.size();
		for (int i = 0; i < n; ++i) {
			shared_ptr<Constraint> value = constraints[i];
			if (value != c) {
				constraints[index] = value;
				++index;
			} else {
				++skipped;
			}
		}
		for (int i = 0; i < skipped; ++i) {
			constraints.pop();
		}
		if (determined_by == c) {
			determined_by = null;
		}
	}
}

shared_ptr<Planner> planner = null;

class Constraint {
	shared_ptr<Strength> strength = null;

	Constraint(shared_ptr<Strength> a_strength) {
		strength = a_strength;
	}

	// the constructors' addConstraint() runs at the creation site: a C++-style constructor dispatches to its own class
	void add_constraint() {
		add_to_graph();
		planner.incremental_add(shared_from_this());
	}

	shared_ptr<Constraint> satisfy(int mark) {
		choose_method(mark);
		if (!is_satisfied()) {
			if (strength == REQUIRED) {
				fail("Could not satisfy a required constraint!");
			}
			return null;
		}
		mark_inputs(mark);
		shared_ptr<Variable> out = output();
		shared_ptr<Constraint> overridden = out.determined_by;
		if (overridden != null) {
			overridden.mark_unsatisfied();
		}
		out.determined_by = shared_from_this();
		if (!planner.add_propagate(shared_from_this(), mark)) {
			fail("Cycle encountered");
		}
		out.mark = mark;
		return overridden;
	}

	void destroy_constraint() {
		if (is_satisfied()) {
			planner.incremental_remove(shared_from_this());
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
	shared_ptr<Variable> output() { return null; }
	void recalculate() {}
	void mark_unsatisfied() {}
	bool inputs_known(int mark) { return false; }
	void remove_from_graph() {}
	void execute() {}
}

class UnaryConstraint : Constraint {
	shared_ptr<Variable> my_output = null;
	bool satisfied = false;

	UnaryConstraint(shared_ptr<Variable> v, shared_ptr<Strength> a_strength) : super(a_strength) {
		my_output = v;
	}

	void add_to_graph() override {
		my_output.add_constraint(shared_from_this());
		satisfied = false;
	}

	void choose_method(int mark) override {
		satisfied = my_output.mark != mark && stronger(strength, my_output.walk_strength);
	}

	bool is_satisfied() override {
		return satisfied;
	}

	void mark_inputs(int mark) override {}

	shared_ptr<Variable> output() override {
		return my_output;
	}

	void recalculate() override {
		my_output.walk_strength = strength;
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
		if (my_output != null) {
			my_output.remove_constraint(shared_from_this());
		}
		satisfied = false;
	}
}

class StayConstraint : UnaryConstraint {
	StayConstraint(shared_ptr<Variable> v, shared_ptr<Strength> a_strength) : super(v, a_strength) {}

	void execute() override {}
}

class EditConstraint : UnaryConstraint {
	EditConstraint(shared_ptr<Variable> v, shared_ptr<Strength> a_strength) : super(v, a_strength) {}

	bool is_input() override {
		return true;
	}

	void execute() override {}
}

class BinaryConstraint : Constraint {
	shared_ptr<Variable> v1 = null;
	shared_ptr<Variable> v2 = null;
	int direction = NONE;

	BinaryConstraint(shared_ptr<Variable> var1, shared_ptr<Variable> var2, shared_ptr<Strength> a_strength) : super(a_strength) {
		v1 = var1;
		v2 = var2;
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
		v1.add_constraint(shared_from_this());
		v2.add_constraint(shared_from_this());
		direction = NONE;
	}

	bool is_satisfied() override {
		return direction != NONE;
	}

	void mark_inputs(int mark) override {
		input().mark = mark;
	}

	shared_ptr<Variable> input() {
		return direction == FORWARD ? v1 : v2;
	}

	shared_ptr<Variable> output() override {
		return direction == FORWARD ? v2 : v1;
	}

	void recalculate() override {
		shared_ptr<Variable> ihn = input();
		shared_ptr<Variable> out = output();
		out.walk_strength = weakest_of(strength, ihn.walk_strength);
		out.stay = ihn.stay;
		if (out.stay) {
			execute();
		}
	}

	void mark_unsatisfied() override {
		direction = NONE;
	}

	bool inputs_known(int mark) override {
		shared_ptr<Variable> i = input();
		return i.mark == mark || i.stay || i.determined_by == null;
	}

	void remove_from_graph() override {
		if (v1 != null) {
			v1.remove_constraint(shared_from_this());
		}
		if (v2 != null) {
			v2.remove_constraint(shared_from_this());
		}
		direction = NONE;
	}
}

class ScaleConstraint : BinaryConstraint {
	shared_ptr<Variable> scale = null;
	shared_ptr<Variable> offset = null;

	ScaleConstraint(shared_ptr<Variable> src, shared_ptr<Variable> a_scale, shared_ptr<Variable> a_offset, shared_ptr<Variable> dest, shared_ptr<Strength> a_strength) : super(src, dest, a_strength) {
		scale = a_scale;
		offset = a_offset;
	}

	void add_to_graph() override {
		super::add_to_graph();
		scale.add_constraint(shared_from_this());
		offset.add_constraint(shared_from_this());
	}

	void remove_from_graph() override {
		super::remove_from_graph();
		if (scale != null) {
			scale.remove_constraint(shared_from_this());
		}
		if (offset != null) {
			offset.remove_constraint(shared_from_this());
		}
	}

	void mark_inputs(int mark) override {
		super::mark_inputs(mark);
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
		shared_ptr<Variable> ihn = input();
		shared_ptr<Variable> out = output();
		out.walk_strength = weakest_of(strength, ihn.walk_strength);
		out.stay = ihn.stay && scale.stay && offset.stay;
		if (out.stay) {
			execute();
		}
	}
}

class EqualityConstraint : BinaryConstraint {
	EqualityConstraint(shared_ptr<Variable> var1, shared_ptr<Variable> var2, shared_ptr<Strength> a_strength) : super(var1, var2, a_strength) {}

	void execute() override {
		output().value = input().value;
	}
}

class Plan {
	array<shared_ptr<Constraint>> v = [];

	void add_constraint(shared_ptr<Constraint> c) {
		v.push(c);
	}

	void execute() {
		for (auto& c : v) {
			c.execute();
		}
	}
}

class Planner {
	int current_mark = 0;

	void incremental_add(shared_ptr<Constraint> c) {
		int mark = new_mark();
		shared_ptr<Constraint> overridden = c.satisfy(mark);
		while (overridden != null) {
			overridden = overridden.satisfy(mark);
		}
	}

	void incremental_remove(shared_ptr<Constraint> c) {
		shared_ptr<Variable> out = c.output();
		c.mark_unsatisfied();
		c.remove_from_graph();
		array<shared_ptr<Constraint>> unsatisfied = remove_propagate_from(out);
		shared_ptr<Strength> strength = REQUIRED;
		do {
			for (auto& u : unsatisfied) {
				if (u.strength == strength) {
					incremental_add(u);
				}
			}
			strength = strength.next_weaker();
		} while (strength != WEAKEST);
	}

	int new_mark() {
		return ++current_mark;
	}

	Plan make_plan(array<shared_ptr<Constraint>> sources) {
		int mark = new_mark();
		Plan plan;
		array<shared_ptr<Constraint>>& todo = sources;
		while (todo.size() > 0) {
			shared_ptr<Constraint> c = todo[0];
			todo.remove(0);
			if (c.output().mark != mark && c.inputs_known(mark)) {
				plan.add_constraint(c);
				c.output().mark = mark;
				add_constraints_consuming_to(c.output(), todo);
			}
		}
		return plan;
	}

	Plan extract_plan_from_constraints(array<shared_ptr<Constraint>>& constraints) {
		array<shared_ptr<Constraint>> sources = [];
		for (auto& c : constraints) {
			if (c.is_input() && c.is_satisfied()) {
				sources.push(c);
			}
		}
		return make_plan(sources);
	}

	bool add_propagate(shared_ptr<Constraint> c, int mark) {
		array<shared_ptr<Constraint>> todo = [c];
		while (todo.size() > 0) {
			shared_ptr<Constraint> d = todo[0];
			todo.remove(0);
			if (d.output().mark == mark) {
				incremental_remove(c);
				return false;
			}
			d.recalculate();
			add_constraints_consuming_to(d.output(), todo);
		}
		return true;
	}

	array<shared_ptr<Constraint>> remove_propagate_from(shared_ptr<Variable> out) {
		out.determined_by = null;
		out.walk_strength = WEAKEST;
		out.stay = true;
		array<shared_ptr<Constraint>> unsatisfied = [];
		array<shared_ptr<Variable>> todo = [out];
		while (todo.size() > 0) {
			shared_ptr<Variable> v = todo[0];
			todo.remove(0);
			for (auto& c : v.constraints) {
				if (!c.is_satisfied()) {
					unsatisfied.push(c);
				}
			}
			shared_ptr<Constraint> determining = v.determined_by;
			for (auto& next_constraint : v.constraints) {
				if (next_constraint != determining && next_constraint.is_satisfied()) {
					next_constraint.recalculate();
					todo.push(next_constraint.output());
				}
			}
		}
		return unsatisfied;
	}

	void add_constraints_consuming_to(shared_ptr<Variable> v, array<shared_ptr<Constraint>>& coll) {
		shared_ptr<Constraint> determining = v.determined_by;
		for (auto& c : v.constraints) {
			if (c != determining && c.is_satisfied()) {
				coll.push(c);
			}
		}
	}
}

// variables and constraints hold each other: break the cycles so reference counting frees them
void release_graph(array<shared_ptr<Variable>>& variables) {
	for (auto& v : variables) {
		v.constraints = [];
		v.determined_by = null;
	}
}

int chain_test(int n) {
	planner = new Planner();
	array<shared_ptr<Variable>> variables = [];
	shared_ptr<Variable> prev = null;
	shared_ptr<Variable> first = null;
	shared_ptr<Variable> last = null;
	for (int i = 0; i <= n; ++i) {
		shared_ptr<Variable> v = new Variable("v" + to_string(i), 0);
		variables.push(v);
		if (prev != null) {
			(new EqualityConstraint(prev, v, REQUIRED)).add_constraint();
		}
		if (i == 0) {
			first = v;
		}
		if (i == n) {
			last = v;
		}
		prev = v;
	}
	(new StayConstraint(last, STRONG_DEFAULT)).add_constraint();
	shared_ptr<Constraint> edit = new EditConstraint(first, PREFERRED);
	edit.add_constraint();
	array<shared_ptr<Constraint>> edits = [edit];
	Plan plan = planner.extract_plan_from_constraints(edits);
	int result = 0;
	for (int i = 0; i < 100; ++i) {
		first.value = i;
		plan.execute();
		if (last.value != i) {
			fail("Chain test failed.");
		}
		result = (result * 31 + last.value) & 0xFFFFFFFF;
	}
	release_graph(variables);
	return result;
}

void change(shared_ptr<Variable> v, int new_value) {
	shared_ptr<Constraint> edit = new EditConstraint(v, PREFERRED);
	edit.add_constraint();
	array<shared_ptr<Constraint>> edits = [edit];
	Plan plan = planner.extract_plan_from_constraints(edits);
	for (int i = 0; i < 10; ++i) {
		v.value = new_value;
		plan.execute();
	}
	edit.destroy_constraint();
}

int projection_test(int n) {
	planner = new Planner();
	shared_ptr<Variable> scale = new Variable("scale", 10);
	shared_ptr<Variable> offset = new Variable("offset", 1000);
	array<shared_ptr<Variable>> variables = [scale, offset];
	shared_ptr<Variable> src = null;
	shared_ptr<Variable> dst = null;
	array<shared_ptr<Variable>> dests = [];
	for (int i = 0; i < n; ++i) {
		src = new Variable("src" + to_string(i), i);
		dst = new Variable("dst" + to_string(i), i);
		variables.push(src);
		variables.push(dst);
		dests.push(dst);
		(new StayConstraint(src, NORMAL)).add_constraint();
		(new ScaleConstraint(src, scale, offset, dst, REQUIRED)).add_constraint();
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
	for (int i = 0; i < n - 1; ++i) {
		if (dests[i].value != i * 5 + 1000) {
			fail("Projection 3 failed");
		}
	}
	change(offset, 2000);
	for (int i = 0; i < n - 1; ++i) {
		if (dests[i].value != i * 5 + 2000) {
			fail("Projection 4 failed");
		}
	}
	int result = 0;
	for (auto& d : dests) {
		result = (result * 31 + d.value) & 0xFFFFFFFF;
	}
	release_graph(variables);
	return result;
}

time::stopwatch clock;
clock.start();
int result = 0;
for (int i = 0; i < ITERATIONS; ++i) {
	result = (result * 31 + chain_test(CHAIN_SIZE)) & 0xFFFFFFFF;
	result = (result * 31 + projection_test(PROJECTION_SIZE)) & 0xFFFFFFFF;
}
float elapsed = clock.elapsed() * 1000.0;
print("result {}", result);
print("time_ms {:.3f}", elapsed);
