// Richards: Martin Richards' OS scheduler simulation, after the Octane (V8) richards.js.
const int ITERATIONS = 200;
const int COUNT = 1000;
const int EXPECTED_QUEUE_COUNT = 2322;
const int EXPECTED_HOLD_COUNT = 928;

const int ID_IDLE = 0;
const int ID_WORKER = 1;
const int ID_HANDLER_A = 2;
const int ID_HANDLER_B = 3;
const int ID_DEVICE_A = 4;
const int ID_DEVICE_B = 5;
const int NUMBER_OF_IDS = 6;

const int KIND_DEVICE = 0;
const int KIND_WORK = 1;

const int STATE_RUNNING = 0;
const int STATE_RUNNABLE = 1;
const int STATE_SUSPENDED = 2;
const int STATE_HELD = 4;
const int STATE_SUSPENDED_RUNNABLE = STATE_SUSPENDED | STATE_RUNNABLE;
const int STATE_NOT_HELD = ~STATE_HELD;

const int DATA_SIZE = 4;

class Packet {
	shared_ptr<Packet> link = null;
	int ident = 0;
	int kind = 0;
	int a1 = 0;
	array<int> a2 = [0, 0, 0, 0];

	Packet(shared_ptr<Packet> a_link, int a_ident, int a_kind) {
		link = a_link;
		ident = a_ident;
		kind = a_kind;
	}

	shared_ptr<Packet> add_to(shared_ptr<Packet> queue) {
		link = null;
		if (queue == null) {
			return shared_from_this();
		}
		shared_ptr<Packet> next_packet = queue;
		shared_ptr<Packet> peek = next_packet.link;
		while (peek != null) {
			next_packet = peek;
			peek = next_packet.link;
		}
		next_packet.link = shared_from_this();
		return queue;
	}
}

class Task {
	shared_ptr<Scheduler> scheduler = null;

	Task(shared_ptr<Scheduler> a_scheduler) {
		scheduler = a_scheduler;
	}

	shared_ptr<TaskControlBlock> run(shared_ptr<Packet> packet) {
		return null;
	}
}

class TaskControlBlock {
	shared_ptr<TaskControlBlock> link = null;
	int ident = 0;
	int priority = 0;
	shared_ptr<Packet> queue = null;
	shared_ptr<Task> task = null;
	int state = 0;

	TaskControlBlock(shared_ptr<TaskControlBlock> a_link, int a_ident, int a_priority, shared_ptr<Packet> a_queue, shared_ptr<Task> a_task) {
		link = a_link;
		ident = a_ident;
		priority = a_priority;
		queue = a_queue;
		task = a_task;
		if (a_queue == null) {
			state = STATE_SUSPENDED;
		} else {
			state = STATE_SUSPENDED_RUNNABLE;
		}
	}

	void set_running() {
		state = STATE_RUNNING;
	}

	void mark_as_not_held() {
		state = state & STATE_NOT_HELD;
	}

	void mark_as_held() {
		state = state | STATE_HELD;
	}

	bool is_held_or_suspended() {
		return (state & STATE_HELD) != 0 || state == STATE_SUSPENDED;
	}

	void mark_as_suspended() {
		state = state | STATE_SUSPENDED;
	}

	void mark_as_runnable() {
		state = state | STATE_RUNNABLE;
	}

	shared_ptr<TaskControlBlock> run() {
		shared_ptr<Packet> packet = null;
		if (state == STATE_SUSPENDED_RUNNABLE) {
			packet = queue;
			queue = packet.link;
			if (queue == null) {
				state = STATE_RUNNING;
			} else {
				state = STATE_RUNNABLE;
			}
		}
		return task.run(packet);
	}

	shared_ptr<TaskControlBlock> check_priority_add(shared_ptr<TaskControlBlock> other, shared_ptr<Packet> packet) {
		if (queue == null) {
			queue = packet;
			mark_as_runnable();
			if (priority > other.priority) {
				return shared_from_this();
			}
		} else {
			queue = packet.add_to(queue);
		}
		return other;
	}
}

class Scheduler {
	int queue_count = 0;
	int hold_count = 0;
	array<shared_ptr<TaskControlBlock>> blocks = [null, null, null, null, null, null];
	shared_ptr<TaskControlBlock> list = null;
	shared_ptr<TaskControlBlock> current_tcb = null;
	int current_id = 0;

	void add_idle_task(int ident, int priority, shared_ptr<Packet> queue, int count) {
		add_running_task(ident, priority, queue, new IdleTask(shared_from_this(), 1, count));
	}

	void add_worker_task(int ident, int priority, shared_ptr<Packet> queue) {
		add_task(ident, priority, queue, new WorkerTask(shared_from_this(), ID_HANDLER_A, 0));
	}

	void add_handler_task(int ident, int priority, shared_ptr<Packet> queue) {
		add_task(ident, priority, queue, new HandlerTask(shared_from_this()));
	}

	void add_device_task(int ident, int priority, shared_ptr<Packet> queue) {
		add_task(ident, priority, queue, new DeviceTask(shared_from_this()));
	}

	void add_running_task(int ident, int priority, shared_ptr<Packet> queue, shared_ptr<Task> task) {
		add_task(ident, priority, queue, task);
		current_tcb.set_running();
	}

	void add_task(int ident, int priority, shared_ptr<Packet> queue, shared_ptr<Task> task) {
		current_tcb = new TaskControlBlock(list, ident, priority, queue, task);
		list = current_tcb;
		blocks[ident] = current_tcb;
	}

	void schedule() {
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

	shared_ptr<TaskControlBlock> release(int ident) {
		shared_ptr<TaskControlBlock> tcb = blocks[ident];
		if (tcb == null) {
			return tcb;
		}
		tcb.mark_as_not_held();
		if (tcb.priority > current_tcb.priority) {
			return tcb;
		}
		return current_tcb;
	}

	shared_ptr<TaskControlBlock> hold_current() {
		++hold_count;
		current_tcb.mark_as_held();
		return current_tcb.link;
	}

	shared_ptr<TaskControlBlock> suspend_current() {
		current_tcb.mark_as_suspended();
		return current_tcb;
	}

	shared_ptr<TaskControlBlock> queue(shared_ptr<Packet> packet) {
		shared_ptr<TaskControlBlock> t = blocks[packet.ident];
		if (t == null) {
			return t;
		}
		++queue_count;
		packet.link = null;
		packet.ident = current_id;
		return t.check_priority_add(current_tcb, packet);
	}
}

class IdleTask : Task {
	int v1 = 0;
	int count = 0;

	IdleTask(shared_ptr<Scheduler> a_scheduler, int a_v1, int a_count) : super(a_scheduler) {
		v1 = a_v1;
		count = a_count;
	}

	shared_ptr<TaskControlBlock> run(shared_ptr<Packet> packet) override {
		--count;
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

class DeviceTask : Task {
	shared_ptr<Packet> v1 = null;

	DeviceTask(shared_ptr<Scheduler> a_scheduler) : super(a_scheduler) {}

	shared_ptr<TaskControlBlock> run(shared_ptr<Packet> packet) override {
		if (packet == null) {
			if (v1 == null) {
				return scheduler.suspend_current();
			}
			shared_ptr<Packet> v = v1;
			v1 = null;
			return scheduler.queue(v);
		}
		v1 = packet;
		return scheduler.hold_current();
	}
}

class WorkerTask : Task {
	int v1 = 0;
	int v2 = 0;

	WorkerTask(shared_ptr<Scheduler> a_scheduler, int a_v1, int a_v2) : super(a_scheduler) {
		v1 = a_v1;
		v2 = a_v2;
	}

	shared_ptr<TaskControlBlock> run(shared_ptr<Packet> packet) override {
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
		for (int i = 0; i < DATA_SIZE; ++i) {
			++v2;
			if (v2 > 26) {
				v2 = 1;
			}
			packet.a2[i] = v2;
		}
		return scheduler.queue(packet);
	}
}

class HandlerTask : Task {
	shared_ptr<Packet> v1 = null;
	shared_ptr<Packet> v2 = null;

	HandlerTask(shared_ptr<Scheduler> a_scheduler) : super(a_scheduler) {}

	shared_ptr<TaskControlBlock> run(shared_ptr<Packet> packet) override {
		if (packet != null) {
			if (packet.kind == KIND_WORK) {
				v1 = packet.add_to(v1);
			} else {
				v2 = packet.add_to(v2);
			}
		}
		if (v1 != null) {
			int count = v1.a1;
			if (count < DATA_SIZE) {
				if (v2 != null) {
					shared_ptr<Packet> v = v2;
					v2 = v2.link;
					v.a1 = v1.a2[count];
					v1.a1 = count + 1;
					return scheduler.queue(v);
				}
			} else {
				shared_ptr<Packet> v = v1;
				v1 = v1.link;
				return scheduler.queue(v);
			}
		}
		return scheduler.suspend_current();
	}
}

int run_richards() {
	shared_ptr<Scheduler> scheduler = new Scheduler();
	scheduler.add_idle_task(ID_IDLE, 0, null, COUNT);
	shared_ptr<Packet> queue = new Packet(null, ID_WORKER, KIND_WORK);
	queue = new Packet(queue, ID_WORKER, KIND_WORK);
	scheduler.add_worker_task(ID_WORKER, 1000, queue);
	queue = new Packet(null, ID_DEVICE_A, KIND_DEVICE);
	queue = new Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	queue = new Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_A, 2000, queue);
	queue = new Packet(null, ID_DEVICE_B, KIND_DEVICE);
	queue = new Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	queue = new Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_B, 3000, queue);
	scheduler.add_device_task(ID_DEVICE_A, 4000, null);
	scheduler.add_device_task(ID_DEVICE_B, 5000, null);
	scheduler.schedule();
	if (scheduler.queue_count != EXPECTED_QUEUE_COUNT || scheduler.hold_count != EXPECTED_HOLD_COUNT) {
		throw "richards: bad counts " + to_string(scheduler.queue_count) + " " + to_string(scheduler.hold_count);
	}
	int result = scheduler.queue_count * 31 + scheduler.hold_count;
	// tasks hold the scheduler and the scheduler holds the tasks: break the cycle so reference counting frees them
	scheduler.blocks = [];
	scheduler.list = null;
	scheduler.current_tcb = null;
	return result;
}

time::stopwatch clock;
clock.start();
int result = 0;
for (int i = 0; i < ITERATIONS; ++i) {
	result = (result * 31 + run_richards()) & 0xFFFFFFFF;
}
float elapsed = clock.elapsed() * 1000.0;
print("result {}", result);
print("time_ms {:.3f}", elapsed);
