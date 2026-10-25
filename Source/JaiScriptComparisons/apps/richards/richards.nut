// Richards: Martin Richards' OS scheduler simulation, after the Octane (V8) richards.js.
const ITERATIONS = 200;
const COUNT = 1000;
const EXPECTED_QUEUE_COUNT = 2322;
const EXPECTED_HOLD_COUNT = 928;

const ID_IDLE = 0;
const ID_WORKER = 1;
const ID_HANDLER_A = 2;
const ID_HANDLER_B = 3;
const ID_DEVICE_A = 4;
const ID_DEVICE_B = 5;
const NUMBER_OF_IDS = 6;

const KIND_DEVICE = 0;
const KIND_WORK = 1;

const STATE_RUNNING = 0;
const STATE_RUNNABLE = 1;
const STATE_SUSPENDED = 2;
const STATE_HELD = 4;
const STATE_SUSPENDED_RUNNABLE = 3;

const DATA_SIZE = 4;

class Packet {
	link = null;
	ident = 0;
	kind = 0;
	a1 = 0;
	a2 = null;

	constructor(l, i, k) {
		link = l;
		ident = i;
		kind = k;
		a1 = 0;
		a2 = array(DATA_SIZE, 0);
	}

	function add_to(queue) {
		link = null;
		if (queue == null) {
			return this;
		}
		local next_packet = queue;
		local peek = next_packet.link;
		while (peek != null) {
			next_packet = peek;
			peek = next_packet.link;
		}
		next_packet.link = this;
		return queue;
	}
}

class TaskControlBlock {
	link = null;
	ident = 0;
	priority = 0;
	queue = null;
	task = null;
	state = 0;

	constructor(l, i, p, q, t) {
		link = l;
		ident = i;
		priority = p;
		queue = q;
		task = t;
		if (q == null) {
			state = STATE_SUSPENDED;
		} else {
			state = STATE_SUSPENDED_RUNNABLE;
		}
	}

	function set_running() {
		state = STATE_RUNNING;
	}

	function mark_as_not_held() {
		state = state & ~STATE_HELD;
	}

	function mark_as_held() {
		state = state | STATE_HELD;
	}

	function is_held_or_suspended() {
		return (state & STATE_HELD) != 0 || state == STATE_SUSPENDED;
	}

	function mark_as_suspended() {
		state = state | STATE_SUSPENDED;
	}

	function mark_as_runnable() {
		state = state | STATE_RUNNABLE;
	}

	function run() {
		local packet;
		if (state == STATE_SUSPENDED_RUNNABLE) {
			packet = queue;
			queue = packet.link;
			if (queue == null) {
				state = STATE_RUNNING;
			} else {
				state = STATE_RUNNABLE;
			}
		} else {
			packet = null;
		}
		return task.run(packet);
	}

	function check_priority_add(t, packet) {
		if (queue == null) {
			queue = packet;
			mark_as_runnable();
			if (priority > t.priority) {
				return this;
			}
		} else {
			queue = packet.add_to(queue);
		}
		return t;
	}
}

class Task {
	scheduler = null;

	constructor(s) {
		scheduler = s;
	}

	function run(packet) {
		return null;
	}
}

class IdleTask extends Task {
	v1 = 0;
	count = 0;

	constructor(s, a_v1, a_count) {
		base.constructor(s);
		v1 = a_v1;
		count = a_count;
	}

	function run(packet) {
		count--;
		if (count == 0) {
			return scheduler.hold_current();
		}
		if ((v1 & 1) == 0) {
			v1 = v1 >> 1;
			return scheduler.release(ID_DEVICE_A);
		}
		v1 = (v1 >> 1) ^ 0xD008;
		return scheduler.release(ID_DEVICE_B);
	}
}

class DeviceTask extends Task {
	v1 = null;

	constructor(s) {
		base.constructor(s);
	}

	function run(packet) {
		if (packet == null) {
			if (v1 == null) {
				return scheduler.suspend_current();
			}
			local v = v1;
			v1 = null;
			return scheduler.queue(v);
		}
		v1 = packet;
		return scheduler.hold_current();
	}
}

class WorkerTask extends Task {
	v1 = 0;
	v2 = 0;

	constructor(s, a_v1, a_v2) {
		base.constructor(s);
		v1 = a_v1;
		v2 = a_v2;
	}

	function run(packet) {
		if (packet == null) {
			return scheduler.suspend_current();
		}
		if (v1 == ID_HANDLER_A) {
			v1 = ID_HANDLER_B;
		} else {
			v1 = ID_HANDLER_A;
		}
		packet.ident = v1;
		packet.a1 = 0;
		for (local i = 0; i < DATA_SIZE; i++) {
			v2++;
			if (v2 > 26) {
				v2 = 1;
			}
			packet.a2[i] = v2;
		}
		return scheduler.queue(packet);
	}
}

class HandlerTask extends Task {
	v1 = null;
	v2 = null;

	constructor(s) {
		base.constructor(s);
	}

	function run(packet) {
		if (packet != null) {
			if (packet.kind == KIND_WORK) {
				v1 = packet.add_to(v1);
			} else {
				v2 = packet.add_to(v2);
			}
		}
		if (v1 != null) {
			local count = v1.a1;
			if (count < DATA_SIZE) {
				if (v2 != null) {
					local v = v2;
					v2 = v2.link;
					v.a1 = v1.a2[count];
					v1.a1 = count + 1;
					return scheduler.queue(v);
				}
			} else {
				local v = v1;
				v1 = v1.link;
				return scheduler.queue(v);
			}
		}
		return scheduler.suspend_current();
	}
}

class Scheduler {
	queue_count = 0;
	hold_count = 0;
	blocks = null;
	list = null;
	current_tcb = null;
	current_id = 0;

	constructor() {
		blocks = array(NUMBER_OF_IDS, null);
	}

	function add_idle_task(ident, priority, queue, count) {
		add_running_task(ident, priority, queue, IdleTask(this, 1, count));
	}

	function add_worker_task(ident, priority, queue) {
		add_task(ident, priority, queue, WorkerTask(this, ID_HANDLER_A, 0));
	}

	function add_handler_task(ident, priority, queue) {
		add_task(ident, priority, queue, HandlerTask(this));
	}

	function add_device_task(ident, priority, queue) {
		add_task(ident, priority, queue, DeviceTask(this));
	}

	function add_running_task(ident, priority, queue, task) {
		add_task(ident, priority, queue, task);
		current_tcb.set_running();
	}

	function add_task(ident, priority, queue, task) {
		current_tcb = TaskControlBlock(list, ident, priority, queue, task);
		list = current_tcb;
		blocks[ident] = current_tcb;
	}

	function schedule() {
		current_tcb = list;
		while (current_tcb != null) {
			if (current_tcb.is_held_or_suspended()) {
				current_tcb = current_tcb.link;
			} else {
				current_id = current_tcb.ident;
				current_tcb = current_tcb.run();
			}
		}
	}

	function release(ident) {
		local tcb = blocks[ident];
		if (tcb == null) {
			return tcb;
		}
		tcb.mark_as_not_held();
		if (tcb.priority > current_tcb.priority) {
			return tcb;
		}
		return current_tcb;
	}

	function hold_current() {
		hold_count++;
		current_tcb.mark_as_held();
		return current_tcb.link;
	}

	function suspend_current() {
		current_tcb.mark_as_suspended();
		return current_tcb;
	}

	function queue(packet) {
		local t = blocks[packet.ident];
		if (t == null) {
			return t;
		}
		queue_count++;
		packet.link = null;
		packet.ident = current_id;
		return t.check_priority_add(current_tcb, packet);
	}
}

function run_richards() {
	local scheduler = Scheduler();
	scheduler.add_idle_task(ID_IDLE, 0, null, COUNT);
	local queue = Packet(null, ID_WORKER, KIND_WORK);
	queue = Packet(queue, ID_WORKER, KIND_WORK);
	scheduler.add_worker_task(ID_WORKER, 1000, queue);
	queue = Packet(null, ID_DEVICE_A, KIND_DEVICE);
	queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_A, 2000, queue);
	queue = Packet(null, ID_DEVICE_B, KIND_DEVICE);
	queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_B, 3000, queue);
	scheduler.add_device_task(ID_DEVICE_A, 4000, null);
	scheduler.add_device_task(ID_DEVICE_B, 5000, null);
	scheduler.schedule();
	if (scheduler.queue_count != EXPECTED_QUEUE_COUNT || scheduler.hold_count != EXPECTED_HOLD_COUNT) {
		throw format("richards: bad counts %d %d", scheduler.queue_count, scheduler.hold_count);
	}
	return scheduler.queue_count * 31 + scheduler.hold_count;
}

local start = clock();
local result = 0;
for (local i = 0; i < ITERATIONS; i++) {
	result = (result * 31 + run_richards()) & 0xFFFFFFFF;
}
local elapsed = (clock() - start) * 1000.0;
print(format("result %d\n", result));
print(format("time_ms %.3f\n", elapsed));
