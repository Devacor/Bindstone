-- DeltaBlue: the incremental dataflow constraint solver (Maloney, Freeman-Benson), after the Octane (V8) deltablue.js.
local ITERATIONS = 200
local CHAIN_SIZE = 100
local PROJECTION_SIZE = 100

local function fail(message)
	print(message)
	os.exit(1)
end

local function class(base)
	local c = {}
	c.__index = c
	if base then
		setmetatable(c, {__index = base})
	end
	return c
end

local Strength = class()

function Strength.new(strength_value, name)
	return setmetatable({strength_value = strength_value, name = name}, Strength)
end

local NEXT_WEAKER

function Strength:next_weaker()
	-- Octane's table, kept as the benchmark defines it (REQUIRED steps straight to WEAKEST)
	return NEXT_WEAKER[self.strength_value + 1]
end

local function stronger(s1, s2)
	return s1.strength_value < s2.strength_value
end

local function weaker(s1, s2)
	return s1.strength_value > s2.strength_value
end

local function weakest_of(s1, s2)
	if weaker(s1, s2) then
		return s1
	end
	return s2
end

local REQUIRED = Strength.new(0, "required")
local STRONG_PREFERRED = Strength.new(1, "strongPreferred")
local PREFERRED = Strength.new(2, "preferred")
local STRONG_DEFAULT = Strength.new(3, "strongDefault")
local NORMAL = Strength.new(4, "normal")
local WEAK_DEFAULT = Strength.new(5, "weakDefault")
local WEAKEST = Strength.new(6, "weakest")
NEXT_WEAKER = {WEAKEST, WEAK_DEFAULT, NORMAL, STRONG_DEFAULT, PREFERRED, REQUIRED}

local NONE = 0
local FORWARD = 1
local BACKWARD = -1

local planner = nil

local function remove_all(collection, element)
	local index = 1
	local skipped = 0
	local n = #collection
	for i = 1, n do
		local value = collection[i]
		if value ~= element then
			collection[index] = value
			index = index + 1
		else
			skipped = skipped + 1
		end
	end
	for _ = 1, skipped do
		collection[#collection] = nil
	end
end

local Constraint = class()

function Constraint:add_constraint()
	self:add_to_graph()
	planner:incremental_add(self)
end

function Constraint:satisfy(mark)
	self:choose_method(mark)
	if not self:is_satisfied() then
		if self.strength == REQUIRED then
			fail("Could not satisfy a required constraint!")
		end
		return nil
	end
	self:mark_inputs(mark)
	local out = self:output()
	local overridden = out.determined_by
	if overridden ~= nil then
		overridden:mark_unsatisfied()
	end
	out.determined_by = self
	if not planner:add_propagate(self, mark) then
		fail("Cycle encountered")
	end
	out.mark = mark
	return overridden
end

function Constraint:destroy_constraint()
	if self:is_satisfied() then
		planner:incremental_remove(self)
	else
		self:remove_from_graph()
	end
end

function Constraint:is_input()
	return false
end

local UnaryConstraint = class(Constraint)

function UnaryConstraint.init(self, v, strength)
	self.strength = strength
	self.my_output = v
	self.satisfied = false
	self:add_constraint()
	return self
end

function UnaryConstraint:add_to_graph()
	self.my_output:add_constraint(self)
	self.satisfied = false
end

function UnaryConstraint:choose_method(mark)
	self.satisfied = self.my_output.mark ~= mark and stronger(self.strength, self.my_output.walk_strength)
end

function UnaryConstraint:is_satisfied()
	return self.satisfied
end

function UnaryConstraint:mark_inputs(mark)
end

function UnaryConstraint:output()
	return self.my_output
end

function UnaryConstraint:recalculate()
	self.my_output.walk_strength = self.strength
	self.my_output.stay = not self:is_input()
	if self.my_output.stay then
		self:execute()
	end
end

function UnaryConstraint:mark_unsatisfied()
	self.satisfied = false
end

function UnaryConstraint:inputs_known(mark)
	return true
end

function UnaryConstraint:remove_from_graph()
	if self.my_output ~= nil then
		self.my_output:remove_constraint(self)
	end
	self.satisfied = false
end

local StayConstraint = class(UnaryConstraint)

function StayConstraint.new(v, strength)
	return UnaryConstraint.init(setmetatable({}, StayConstraint), v, strength)
end

function StayConstraint:execute()
end

local EditConstraint = class(UnaryConstraint)

function EditConstraint.new(v, strength)
	return UnaryConstraint.init(setmetatable({}, EditConstraint), v, strength)
end

function EditConstraint:is_input()
	return true
end

function EditConstraint:execute()
end

local BinaryConstraint = class(Constraint)

function BinaryConstraint.init(self, var1, var2, strength)
	self.strength = strength
	self.v1 = var1
	self.v2 = var2
	self.direction = NONE
	self:add_constraint()
	return self
end

function BinaryConstraint:choose_method(mark)
	if self.v1.mark == mark then
		if self.v2.mark ~= mark and stronger(self.strength, self.v2.walk_strength) then
			self.direction = FORWARD
		else
			self.direction = NONE
		end
	end
	if self.v2.mark == mark then
		if self.v1.mark ~= mark and stronger(self.strength, self.v1.walk_strength) then
			self.direction = BACKWARD
		else
			self.direction = NONE
		end
	end
	if weaker(self.v1.walk_strength, self.v2.walk_strength) then
		if stronger(self.strength, self.v1.walk_strength) then
			self.direction = BACKWARD
		else
			self.direction = NONE
		end
	else
		if stronger(self.strength, self.v2.walk_strength) then
			self.direction = FORWARD
		else
			self.direction = BACKWARD
		end
	end
end

function BinaryConstraint:add_to_graph()
	self.v1:add_constraint(self)
	self.v2:add_constraint(self)
	self.direction = NONE
end

function BinaryConstraint:is_satisfied()
	return self.direction ~= NONE
end

function BinaryConstraint:mark_inputs(mark)
	self:input().mark = mark
end

function BinaryConstraint:input()
	if self.direction == FORWARD then
		return self.v1
	end
	return self.v2
end

function BinaryConstraint:output()
	if self.direction == FORWARD then
		return self.v2
	end
	return self.v1
end

function BinaryConstraint:recalculate()
	local ihn = self:input()
	local out = self:output()
	out.walk_strength = weakest_of(self.strength, ihn.walk_strength)
	out.stay = ihn.stay
	if out.stay then
		self:execute()
	end
end

function BinaryConstraint:mark_unsatisfied()
	self.direction = NONE
end

function BinaryConstraint:inputs_known(mark)
	local i = self:input()
	return i.mark == mark or i.stay or i.determined_by == nil
end

function BinaryConstraint:remove_from_graph()
	if self.v1 ~= nil then
		self.v1:remove_constraint(self)
	end
	if self.v2 ~= nil then
		self.v2:remove_constraint(self)
	end
	self.direction = NONE
end

local ScaleConstraint = class(BinaryConstraint)

function ScaleConstraint.new(src, scale, offset, dest, strength)
	local self = setmetatable({}, ScaleConstraint)
	self.direction = NONE
	self.scale = scale
	self.offset = offset
	return BinaryConstraint.init(self, src, dest, strength)
end

function ScaleConstraint:add_to_graph()
	BinaryConstraint.add_to_graph(self)
	self.scale:add_constraint(self)
	self.offset:add_constraint(self)
end

function ScaleConstraint:remove_from_graph()
	BinaryConstraint.remove_from_graph(self)
	if self.scale ~= nil then
		self.scale:remove_constraint(self)
	end
	if self.offset ~= nil then
		self.offset:remove_constraint(self)
	end
end

function ScaleConstraint:mark_inputs(mark)
	BinaryConstraint.mark_inputs(self, mark)
	self.scale.mark = mark
	self.offset.mark = mark
end

function ScaleConstraint:execute()
	if self.direction == FORWARD then
		self.v2.value = self.v1.value * self.scale.value + self.offset.value
	else
		self.v1.value = (self.v2.value - self.offset.value) // self.scale.value
	end
end

function ScaleConstraint:recalculate()
	local ihn = self:input()
	local out = self:output()
	out.walk_strength = weakest_of(self.strength, ihn.walk_strength)
	out.stay = ihn.stay and self.scale.stay and self.offset.stay
	if out.stay then
		self:execute()
	end
end

local EqualityConstraint = class(BinaryConstraint)

function EqualityConstraint.new(var1, var2, strength)
	return BinaryConstraint.init(setmetatable({}, EqualityConstraint), var1, var2, strength)
end

function EqualityConstraint:execute()
	self:output().value = self:input().value
end

local Variable = class()

function Variable.new(name, initial_value)
	return setmetatable({value = initial_value, constraints = {}, determined_by = nil, mark = 0,
		walk_strength = WEAKEST, stay = true, name = name}, Variable)
end

function Variable:add_constraint(c)
	local constraints = self.constraints
	constraints[#constraints + 1] = c
end

function Variable:remove_constraint(c)
	remove_all(self.constraints, c)
	if self.determined_by == c then
		self.determined_by = nil
	end
end

local Plan = class()

function Plan.new()
	return setmetatable({v = {}}, Plan)
end

function Plan:add_constraint(c)
	self.v[#self.v + 1] = c
end

function Plan:execute()
	local v = self.v
	for i = 1, #v do
		v[i]:execute()
	end
end

local Planner = class()

function Planner.new()
	return setmetatable({current_mark = 0}, Planner)
end

function Planner:incremental_add(c)
	local mark = self:new_mark()
	local overridden = c:satisfy(mark)
	while overridden ~= nil do
		overridden = overridden:satisfy(mark)
	end
end

function Planner:incremental_remove(c)
	local out = c:output()
	c:mark_unsatisfied()
	c:remove_from_graph()
	local unsatisfied = self:remove_propagate_from(out)
	local strength = REQUIRED
	repeat
		for i = 1, #unsatisfied do
			local u = unsatisfied[i]
			if u.strength == strength then
				self:incremental_add(u)
			end
		end
		strength = strength:next_weaker()
	until strength == WEAKEST
end

function Planner:new_mark()
	self.current_mark = self.current_mark + 1
	return self.current_mark
end

function Planner:make_plan(sources)
	local mark = self:new_mark()
	local plan = Plan.new()
	local todo = sources
	while #todo > 0 do
		local c = table.remove(todo, 1)
		if c:output().mark ~= mark and c:inputs_known(mark) then
			plan:add_constraint(c)
			c:output().mark = mark
			self:add_constraints_consuming_to(c:output(), todo)
		end
	end
	return plan
end

function Planner:extract_plan_from_constraints(constraints)
	local sources = {}
	for i = 1, #constraints do
		local c = constraints[i]
		if c:is_input() and c:is_satisfied() then
			sources[#sources + 1] = c
		end
	end
	return self:make_plan(sources)
end

function Planner:add_propagate(c, mark)
	local todo = {c}
	while #todo > 0 do
		local d = table.remove(todo, 1)
		if d:output().mark == mark then
			self:incremental_remove(c)
			return false
		end
		d:recalculate()
		self:add_constraints_consuming_to(d:output(), todo)
	end
	return true
end

function Planner:remove_propagate_from(out)
	out.determined_by = nil
	out.walk_strength = WEAKEST
	out.stay = true
	local unsatisfied = {}
	local todo = {out}
	while #todo > 0 do
		local v = table.remove(todo, 1)
		local constraints = v.constraints
		for i = 1, #constraints do
			local c = constraints[i]
			if not c:is_satisfied() then
				unsatisfied[#unsatisfied + 1] = c
			end
		end
		local determining = v.determined_by
		for i = 1, #constraints do
			local next_constraint = constraints[i]
			if next_constraint ~= determining and next_constraint:is_satisfied() then
				next_constraint:recalculate()
				todo[#todo + 1] = next_constraint:output()
			end
		end
	end
	return unsatisfied
end

function Planner:add_constraints_consuming_to(v, coll)
	local determining = v.determined_by
	local constraints = v.constraints
	for i = 1, #constraints do
		local c = constraints[i]
		if c ~= determining and c:is_satisfied() then
			coll[#coll + 1] = c
		end
	end
end

local function chain_test(n)
	planner = Planner.new()
	local prev, first, last = nil, nil, nil
	for i = 0, n do
		local v = Variable.new("v" .. i, 0)
		if prev ~= nil then
			EqualityConstraint.new(prev, v, REQUIRED)
		end
		if i == 0 then
			first = v
		end
		if i == n then
			last = v
		end
		prev = v
	end
	StayConstraint.new(last, STRONG_DEFAULT)
	local edit = EditConstraint.new(first, PREFERRED)
	local plan = planner:extract_plan_from_constraints({edit})
	local result = 0
	for i = 0, 99 do
		first.value = i
		plan:execute()
		if last.value ~= i then
			fail("Chain test failed.")
		end
		result = (result * 31 + last.value) & 0xFFFFFFFF
	end
	return result
end

local function change(v, new_value)
	local edit = EditConstraint.new(v, PREFERRED)
	local plan = planner:extract_plan_from_constraints({edit})
	for _ = 1, 10 do
		v.value = new_value
		plan:execute()
	end
	edit:destroy_constraint()
end

local function projection_test(n)
	planner = Planner.new()
	local scale = Variable.new("scale", 10)
	local offset = Variable.new("offset", 1000)
	local src, dst = nil, nil
	local dests = {}
	for i = 0, n - 1 do
		src = Variable.new("src" .. i, i)
		dst = Variable.new("dst" .. i, i)
		dests[#dests + 1] = dst
		StayConstraint.new(src, NORMAL)
		ScaleConstraint.new(src, scale, offset, dst, REQUIRED)
	end
	change(src, 17)
	if dst.value ~= 1170 then
		fail("Projection 1 failed")
	end
	change(dst, 1050)
	if src.value ~= 5 then
		fail("Projection 2 failed")
	end
	change(scale, 5)
	for i = 0, n - 2 do
		if dests[i + 1].value ~= i * 5 + 1000 then
			fail("Projection 3 failed")
		end
	end
	change(offset, 2000)
	for i = 0, n - 2 do
		if dests[i + 1].value ~= i * 5 + 2000 then
			fail("Projection 4 failed")
		end
	end
	local result = 0
	for i = 1, #dests do
		result = (result * 31 + dests[i].value) & 0xFFFFFFFF
	end
	return result
end

local start = os.clock()
local result = 0
for _ = 1, ITERATIONS do
	result = (result * 31 + chain_test(CHAIN_SIZE)) & 0xFFFFFFFF
	result = (result * 31 + projection_test(PROJECTION_SIZE)) & 0xFFFFFFFF
end
local elapsed = (os.clock() - start) * 1000.0
print(string.format("result %d", result))
print(string.format("time_ms %.3f", elapsed))
