-- Richards: Martin Richards' OS scheduler simulation, after the Octane (V8) richards.js.
local ITERATIONS = 200
local COUNT = 1000
local EXPECTED_QUEUE_COUNT = 2322
local EXPECTED_HOLD_COUNT = 928

local ID_IDLE = 0
local ID_WORKER = 1
local ID_HANDLER_A = 2
local ID_HANDLER_B = 3
local ID_DEVICE_A = 4
local ID_DEVICE_B = 5

local KIND_DEVICE = 0
local KIND_WORK = 1

local STATE_RUNNING = 0
local STATE_RUNNABLE = 1
local STATE_SUSPENDED = 2
local STATE_HELD = 4
local STATE_SUSPENDED_RUNNABLE = STATE_SUSPENDED | STATE_RUNNABLE
local STATE_NOT_HELD = ~STATE_HELD

local DATA_SIZE = 4

local Packet = {}
Packet.__index = Packet

function Packet.new(link, ident, kind)
	local a2 = {}
	for i = 1, DATA_SIZE do a2[i] = 0 end
	return setmetatable({link = link, ident = ident, kind = kind, a1 = 0, a2 = a2}, Packet)
end

function Packet:add_to(queue)
	self.link = nil
	if queue == nil then
		return self
	end
	local next_packet = queue
	local peek = next_packet.link
	while peek ~= nil do
		next_packet = peek
		peek = next_packet.link
	end
	next_packet.link = self
	return queue
end

local TaskControlBlock = {}
TaskControlBlock.__index = TaskControlBlock

function TaskControlBlock.new(link, ident, priority, queue, task)
	local state = STATE_SUSPENDED_RUNNABLE
	if queue == nil then
		state = STATE_SUSPENDED
	end
	return setmetatable({link = link, ident = ident, priority = priority, queue = queue, task = task, state = state}, TaskControlBlock)
end

function TaskControlBlock:set_running()
	self.state = STATE_RUNNING
end

function TaskControlBlock:mark_as_not_held()
	self.state = self.state & STATE_NOT_HELD
end

function TaskControlBlock:mark_as_held()
	self.state = self.state | STATE_HELD
end

function TaskControlBlock:is_held_or_suspended()
	return (self.state & STATE_HELD) ~= 0 or self.state == STATE_SUSPENDED
end

function TaskControlBlock:mark_as_suspended()
	self.state = self.state | STATE_SUSPENDED
end

function TaskControlBlock:mark_as_runnable()
	self.state = self.state | STATE_RUNNABLE
end

function TaskControlBlock:run()
	local packet
	if self.state == STATE_SUSPENDED_RUNNABLE then
		packet = self.queue
		self.queue = packet.link
		if self.queue == nil then
			self.state = STATE_RUNNING
		else
			self.state = STATE_RUNNABLE
		end
	else
		packet = nil
	end
	return self.task:run(packet)
end

function TaskControlBlock:check_priority_add(task, packet)
	if self.queue == nil then
		self.queue = packet
		self:mark_as_runnable()
		if self.priority > task.priority then
			return self
		end
	else
		self.queue = packet:add_to(self.queue)
	end
	return task
end

local IdleTask = {}
IdleTask.__index = IdleTask
local DeviceTask = {}
DeviceTask.__index = DeviceTask
local WorkerTask = {}
WorkerTask.__index = WorkerTask
local HandlerTask = {}
HandlerTask.__index = HandlerTask

local Scheduler = {}
Scheduler.__index = Scheduler

function Scheduler.new()
	return setmetatable({queue_count = 0, hold_count = 0, blocks = {}, list = nil, current_tcb = nil, current_id = 0}, Scheduler)
end

function Scheduler:add_idle_task(ident, priority, queue, count)
	self:add_running_task(ident, priority, queue, IdleTask.new(self, 1, count))
end

function Scheduler:add_worker_task(ident, priority, queue)
	self:add_task(ident, priority, queue, WorkerTask.new(self, ID_HANDLER_A, 0))
end

function Scheduler:add_handler_task(ident, priority, queue)
	self:add_task(ident, priority, queue, HandlerTask.new(self))
end

function Scheduler:add_device_task(ident, priority, queue)
	self:add_task(ident, priority, queue, DeviceTask.new(self))
end

function Scheduler:add_running_task(ident, priority, queue, task)
	self:add_task(ident, priority, queue, task)
	self.current_tcb:set_running()
end

function Scheduler:add_task(ident, priority, queue, task)
	self.current_tcb = TaskControlBlock.new(self.list, ident, priority, queue, task)
	self.list = self.current_tcb
	self.blocks[ident] = self.current_tcb
end

function Scheduler:schedule()
	self.current_tcb = self.list
	while self.current_tcb ~= nil do
		if self.current_tcb:is_held_or_suspended() then
			self.current_tcb = self.current_tcb.link
		else
			self.current_id = self.current_tcb.ident
			self.current_tcb = self.current_tcb:run()
		end
	end
end

function Scheduler:release(ident)
	local tcb = self.blocks[ident]
	if tcb == nil then
		return tcb
	end
	tcb:mark_as_not_held()
	if tcb.priority > self.current_tcb.priority then
		return tcb
	end
	return self.current_tcb
end

function Scheduler:hold_current()
	self.hold_count = self.hold_count + 1
	self.current_tcb:mark_as_held()
	return self.current_tcb.link
end

function Scheduler:suspend_current()
	self.current_tcb:mark_as_suspended()
	return self.current_tcb
end

function Scheduler:queue(packet)
	local t = self.blocks[packet.ident]
	if t == nil then
		return t
	end
	self.queue_count = self.queue_count + 1
	packet.link = nil
	packet.ident = self.current_id
	return t:check_priority_add(self.current_tcb, packet)
end

function IdleTask.new(scheduler, v1, count)
	return setmetatable({scheduler = scheduler, v1 = v1, count = count}, IdleTask)
end

function IdleTask:run(packet)
	self.count = self.count - 1
	if self.count == 0 then
		return self.scheduler:hold_current()
	end
	if (self.v1 & 1) == 0 then
		self.v1 = self.v1 >> 1
		return self.scheduler:release(ID_DEVICE_A)
	end
	self.v1 = (self.v1 >> 1) ~ 0xD008
	return self.scheduler:release(ID_DEVICE_B)
end

function DeviceTask.new(scheduler)
	return setmetatable({scheduler = scheduler, v1 = nil}, DeviceTask)
end

function DeviceTask:run(packet)
	if packet == nil then
		if self.v1 == nil then
			return self.scheduler:suspend_current()
		end
		local v = self.v1
		self.v1 = nil
		return self.scheduler:queue(v)
	end
	self.v1 = packet
	return self.scheduler:hold_current()
end

function WorkerTask.new(scheduler, v1, v2)
	return setmetatable({scheduler = scheduler, v1 = v1, v2 = v2}, WorkerTask)
end

function WorkerTask:run(packet)
	if packet == nil then
		return self.scheduler:suspend_current()
	end
	if self.v1 == ID_HANDLER_A then
		self.v1 = ID_HANDLER_B
	else
		self.v1 = ID_HANDLER_A
	end
	packet.ident = self.v1
	packet.a1 = 0
	for i = 1, DATA_SIZE do
		self.v2 = self.v2 + 1
		if self.v2 > 26 then
			self.v2 = 1
		end
		packet.a2[i] = self.v2
	end
	return self.scheduler:queue(packet)
end

function HandlerTask.new(scheduler)
	return setmetatable({scheduler = scheduler, v1 = nil, v2 = nil}, HandlerTask)
end

function HandlerTask:run(packet)
	if packet ~= nil then
		if packet.kind == KIND_WORK then
			self.v1 = packet:add_to(self.v1)
		else
			self.v2 = packet:add_to(self.v2)
		end
	end
	if self.v1 ~= nil then
		local count = self.v1.a1
		if count < DATA_SIZE then
			if self.v2 ~= nil then
				local v = self.v2
				self.v2 = self.v2.link
				v.a1 = self.v1.a2[count + 1]
				self.v1.a1 = count + 1
				return self.scheduler:queue(v)
			end
		else
			local v = self.v1
			self.v1 = self.v1.link
			return self.scheduler:queue(v)
		end
	end
	return self.scheduler:suspend_current()
end

local function run_richards()
	local scheduler = Scheduler.new()
	scheduler:add_idle_task(ID_IDLE, 0, nil, COUNT)
	local queue = Packet.new(nil, ID_WORKER, KIND_WORK)
	queue = Packet.new(queue, ID_WORKER, KIND_WORK)
	scheduler:add_worker_task(ID_WORKER, 1000, queue)
	queue = Packet.new(nil, ID_DEVICE_A, KIND_DEVICE)
	queue = Packet.new(queue, ID_DEVICE_A, KIND_DEVICE)
	queue = Packet.new(queue, ID_DEVICE_A, KIND_DEVICE)
	scheduler:add_handler_task(ID_HANDLER_A, 2000, queue)
	queue = Packet.new(nil, ID_DEVICE_B, KIND_DEVICE)
	queue = Packet.new(queue, ID_DEVICE_B, KIND_DEVICE)
	queue = Packet.new(queue, ID_DEVICE_B, KIND_DEVICE)
	scheduler:add_handler_task(ID_HANDLER_B, 3000, queue)
	scheduler:add_device_task(ID_DEVICE_A, 4000, nil)
	scheduler:add_device_task(ID_DEVICE_B, 5000, nil)
	scheduler:schedule()
	if scheduler.queue_count ~= EXPECTED_QUEUE_COUNT or scheduler.hold_count ~= EXPECTED_HOLD_COUNT then
		print("richards: bad counts", scheduler.queue_count, scheduler.hold_count)
		os.exit(1)
	end
	return scheduler.queue_count * 31 + scheduler.hold_count
end

local start = os.clock()
local result = 0
for _ = 1, ITERATIONS do
	result = (result * 31 + run_richards()) & 0xFFFFFFFF
end
local elapsed = (os.clock() - start) * 1000.0
print(string.format("result %d", result))
print(string.format("time_ms %.3f", elapsed))
