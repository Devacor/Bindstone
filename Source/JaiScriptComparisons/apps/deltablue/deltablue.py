# DeltaBlue: the incremental dataflow constraint solver (Maloney, Freeman-Benson), after the Octane (V8) deltablue.js.
import sys
import time

ITERATIONS = 200
CHAIN_SIZE = 100
PROJECTION_SIZE = 100


def fail(message):
	print(message)
	sys.exit(1)


class Strength:
	def __init__(self, strength_value, name):
		self.strength_value = strength_value
		self.name = name

	def next_weaker(self):
		# Octane's table, kept as the benchmark defines it (REQUIRED steps straight to WEAKEST)
		return NEXT_WEAKER[self.strength_value]


def stronger(s1, s2):
	return s1.strength_value < s2.strength_value


def weaker(s1, s2):
	return s1.strength_value > s2.strength_value


def weakest_of(s1, s2):
	return s1 if weaker(s1, s2) else s2


REQUIRED = Strength(0, "required")
STRONG_PREFERRED = Strength(1, "strongPreferred")
PREFERRED = Strength(2, "preferred")
STRONG_DEFAULT = Strength(3, "strongDefault")
NORMAL = Strength(4, "normal")
WEAK_DEFAULT = Strength(5, "weakDefault")
WEAKEST = Strength(6, "weakest")
NEXT_WEAKER = [WEAKEST, WEAK_DEFAULT, NORMAL, STRONG_DEFAULT, PREFERRED, REQUIRED]

NONE = 0
FORWARD = 1
BACKWARD = -1

planner = None


def remove_all(collection, element):
	index = 0
	skipped = 0
	for value in collection:
		if value is not element:
			collection[index] = value
			index += 1
		else:
			skipped += 1
	for _ in range(skipped):
		collection.pop()


class Constraint:
	def __init__(self, strength):
		self.strength = strength

	def add_constraint(self):
		self.add_to_graph()
		planner.incremental_add(self)

	def satisfy(self, mark):
		self.choose_method(mark)
		if not self.is_satisfied():
			if self.strength is REQUIRED:
				fail("Could not satisfy a required constraint!")
			return None
		self.mark_inputs(mark)
		out = self.output()
		overridden = out.determined_by
		if overridden is not None:
			overridden.mark_unsatisfied()
		out.determined_by = self
		if not planner.add_propagate(self, mark):
			fail("Cycle encountered")
		out.mark = mark
		return overridden

	def destroy_constraint(self):
		if self.is_satisfied():
			planner.incremental_remove(self)
		else:
			self.remove_from_graph()

	def is_input(self):
		return False


class UnaryConstraint(Constraint):
	def __init__(self, v, strength):
		super().__init__(strength)
		self.my_output = v
		self.satisfied = False
		self.add_constraint()

	def add_to_graph(self):
		self.my_output.add_constraint(self)
		self.satisfied = False

	def choose_method(self, mark):
		self.satisfied = self.my_output.mark != mark and stronger(self.strength, self.my_output.walk_strength)

	def is_satisfied(self):
		return self.satisfied

	def mark_inputs(self, mark):
		pass

	def output(self):
		return self.my_output

	def recalculate(self):
		self.my_output.walk_strength = self.strength
		self.my_output.stay = not self.is_input()
		if self.my_output.stay:
			self.execute()

	def mark_unsatisfied(self):
		self.satisfied = False

	def inputs_known(self, mark):
		return True

	def remove_from_graph(self):
		if self.my_output is not None:
			self.my_output.remove_constraint(self)
		self.satisfied = False


class StayConstraint(UnaryConstraint):
	def execute(self):
		pass


class EditConstraint(UnaryConstraint):
	def is_input(self):
		return True

	def execute(self):
		pass


class BinaryConstraint(Constraint):
	def __init__(self, var1, var2, strength):
		super().__init__(strength)
		self.v1 = var1
		self.v2 = var2
		self.direction = NONE
		self.add_constraint()

	def choose_method(self, mark):
		if self.v1.mark == mark:
			self.direction = FORWARD if self.v2.mark != mark and stronger(self.strength, self.v2.walk_strength) else NONE
		if self.v2.mark == mark:
			self.direction = BACKWARD if self.v1.mark != mark and stronger(self.strength, self.v1.walk_strength) else NONE
		if weaker(self.v1.walk_strength, self.v2.walk_strength):
			self.direction = BACKWARD if stronger(self.strength, self.v1.walk_strength) else NONE
		else:
			self.direction = FORWARD if stronger(self.strength, self.v2.walk_strength) else BACKWARD

	def add_to_graph(self):
		self.v1.add_constraint(self)
		self.v2.add_constraint(self)
		self.direction = NONE

	def is_satisfied(self):
		return self.direction != NONE

	def mark_inputs(self, mark):
		self.input().mark = mark

	def input(self):
		return self.v1 if self.direction == FORWARD else self.v2

	def output(self):
		return self.v2 if self.direction == FORWARD else self.v1

	def recalculate(self):
		ihn = self.input()
		out = self.output()
		out.walk_strength = weakest_of(self.strength, ihn.walk_strength)
		out.stay = ihn.stay
		if out.stay:
			self.execute()

	def mark_unsatisfied(self):
		self.direction = NONE

	def inputs_known(self, mark):
		i = self.input()
		return i.mark == mark or i.stay or i.determined_by is None

	def remove_from_graph(self):
		if self.v1 is not None:
			self.v1.remove_constraint(self)
		if self.v2 is not None:
			self.v2.remove_constraint(self)
		self.direction = NONE


class ScaleConstraint(BinaryConstraint):
	def __init__(self, src, scale, offset, dest, strength):
		self.direction = NONE
		self.scale = scale
		self.offset = offset
		super().__init__(src, dest, strength)

	def add_to_graph(self):
		super().add_to_graph()
		self.scale.add_constraint(self)
		self.offset.add_constraint(self)

	def remove_from_graph(self):
		super().remove_from_graph()
		if self.scale is not None:
			self.scale.remove_constraint(self)
		if self.offset is not None:
			self.offset.remove_constraint(self)

	def mark_inputs(self, mark):
		super().mark_inputs(mark)
		self.scale.mark = mark
		self.offset.mark = mark

	def execute(self):
		if self.direction == FORWARD:
			self.v2.value = self.v1.value * self.scale.value + self.offset.value
		else:
			self.v1.value = (self.v2.value - self.offset.value) // self.scale.value

	def recalculate(self):
		ihn = self.input()
		out = self.output()
		out.walk_strength = weakest_of(self.strength, ihn.walk_strength)
		out.stay = ihn.stay and self.scale.stay and self.offset.stay
		if out.stay:
			self.execute()


class EqualityConstraint(BinaryConstraint):
	def execute(self):
		self.output().value = self.input().value


class Variable:
	def __init__(self, name, initial_value):
		self.value = initial_value
		self.constraints = []
		self.determined_by = None
		self.mark = 0
		self.walk_strength = WEAKEST
		self.stay = True
		self.name = name

	def add_constraint(self, c):
		self.constraints.append(c)

	def remove_constraint(self, c):
		remove_all(self.constraints, c)
		if self.determined_by is c:
			self.determined_by = None


class Planner:
	def __init__(self):
		self.current_mark = 0

	def incremental_add(self, c):
		mark = self.new_mark()
		overridden = c.satisfy(mark)
		while overridden is not None:
			overridden = overridden.satisfy(mark)

	def incremental_remove(self, c):
		out = c.output()
		c.mark_unsatisfied()
		c.remove_from_graph()
		unsatisfied = self.remove_propagate_from(out)
		strength = REQUIRED
		while True:
			for u in unsatisfied:
				if u.strength is strength:
					self.incremental_add(u)
			strength = strength.next_weaker()
			if strength is WEAKEST:
				break

	def new_mark(self):
		self.current_mark += 1
		return self.current_mark

	def make_plan(self, sources):
		mark = self.new_mark()
		plan = Plan()
		todo = sources
		while len(todo) > 0:
			c = todo.pop(0)
			if c.output().mark != mark and c.inputs_known(mark):
				plan.add_constraint(c)
				c.output().mark = mark
				self.add_constraints_consuming_to(c.output(), todo)
		return plan

	def extract_plan_from_constraints(self, constraints):
		sources = []
		for c in constraints:
			if c.is_input() and c.is_satisfied():
				sources.append(c)
		return self.make_plan(sources)

	def add_propagate(self, c, mark):
		todo = [c]
		while len(todo) > 0:
			d = todo.pop(0)
			if d.output().mark == mark:
				self.incremental_remove(c)
				return False
			d.recalculate()
			self.add_constraints_consuming_to(d.output(), todo)
		return True

	def remove_propagate_from(self, out):
		out.determined_by = None
		out.walk_strength = WEAKEST
		out.stay = True
		unsatisfied = []
		todo = [out]
		while len(todo) > 0:
			v = todo.pop(0)
			for c in v.constraints:
				if not c.is_satisfied():
					unsatisfied.append(c)
			determining = v.determined_by
			for next_constraint in v.constraints:
				if next_constraint is not determining and next_constraint.is_satisfied():
					next_constraint.recalculate()
					todo.append(next_constraint.output())
		return unsatisfied

	def add_constraints_consuming_to(self, v, coll):
		determining = v.determined_by
		for c in v.constraints:
			if c is not determining and c.is_satisfied():
				coll.append(c)


class Plan:
	def __init__(self):
		self.v = []

	def add_constraint(self, c):
		self.v.append(c)

	def execute(self):
		for c in self.v:
			c.execute()


def chain_test(n):
	global planner
	planner = Planner()
	prev = None
	first = None
	last = None
	for i in range(n + 1):
		v = Variable("v" + str(i), 0)
		if prev is not None:
			EqualityConstraint(prev, v, REQUIRED)
		if i == 0:
			first = v
		if i == n:
			last = v
		prev = v
	StayConstraint(last, STRONG_DEFAULT)
	edit = EditConstraint(first, PREFERRED)
	plan = planner.extract_plan_from_constraints([edit])
	result = 0
	for i in range(100):
		first.value = i
		plan.execute()
		if last.value != i:
			fail("Chain test failed.")
		result = (result * 31 + last.value) & 0xFFFFFFFF
	return result


def change(v, new_value):
	edit = EditConstraint(v, PREFERRED)
	plan = planner.extract_plan_from_constraints([edit])
	for _ in range(10):
		v.value = new_value
		plan.execute()
	edit.destroy_constraint()


def projection_test(n):
	global planner
	planner = Planner()
	scale = Variable("scale", 10)
	offset = Variable("offset", 1000)
	src = None
	dst = None
	dests = []
	for i in range(n):
		src = Variable("src" + str(i), i)
		dst = Variable("dst" + str(i), i)
		dests.append(dst)
		StayConstraint(src, NORMAL)
		ScaleConstraint(src, scale, offset, dst, REQUIRED)
	change(src, 17)
	if dst.value != 1170:
		fail("Projection 1 failed")
	change(dst, 1050)
	if src.value != 5:
		fail("Projection 2 failed")
	change(scale, 5)
	for i in range(n - 1):
		if dests[i].value != i * 5 + 1000:
			fail("Projection 3 failed")
	change(offset, 2000)
	for i in range(n - 1):
		if dests[i].value != i * 5 + 2000:
			fail("Projection 4 failed")
	result = 0
	for d in dests:
		result = (result * 31 + d.value) & 0xFFFFFFFF
	return result


def main():
	start = time.perf_counter()
	result = 0
	for _ in range(ITERATIONS):
		result = (result * 31 + chain_test(CHAIN_SIZE)) & 0xFFFFFFFF
		result = (result * 31 + projection_test(PROJECTION_SIZE)) & 0xFFFFFFFF
	elapsed = (time.perf_counter() - start) * 1000.0
	print(f"result {result}")
	print(f"time_ms {elapsed:.3f}")


main()
