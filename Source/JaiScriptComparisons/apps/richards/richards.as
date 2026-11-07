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

const int DATA_SIZE = 4;

class Packet {
	Packet@ link;
	int ident;
	int kind;
	int a1 = 0;
	array<int> a2(DATA_SIZE);

	Packet(Packet@ a_link, int a_ident, int a_kind) {
		@link = a_link;
		ident = a_ident;
		kind = a_kind;
	}

	Packet@ add_to(Packet@ queue) {
		@link = null;
		if (queue is null) {
			return this;
		}
		Packet@ next_packet = queue;
		Packet@ peek = next_packet.link;
		while (peek !is null) {
			@next_packet = peek;
			@peek = next_packet.link;
		}
		@next_packet.link = this;
		return queue;
	}
}

class TaskControlBlock {
	TaskControlBlock@ link;
	int ident;
	int priority;
	Packet@ queue;
	Task@ task;
	int state;

	TaskControlBlock(TaskControlBlock@ a_link, int a_ident, int a_priority, Packet@ a_queue, Task@ a_task) {
		@link = a_link;
		ident = a_ident;
		priority = a_priority;
		@queue = a_queue;
		@task = a_task;
		if (a_queue is null) {
			state = STATE_SUSPENDED;
		} else {
			state = STATE_SUSPENDED_RUNNABLE;
		}
	}

	void set_running() {
		state = STATE_RUNNING;
	}

	void mark_as_not_held() {
		state = state & ~STATE_HELD;
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

	TaskControlBlock@ run() {
		Packet@ packet;
		if (state == STATE_SUSPENDED_RUNNABLE) {
			@packet = queue;
			@queue = packet.link;
			if (queue is null) {
				state = STATE_RUNNING;
			} else {
				state = STATE_RUNNABLE;
			}
		}
		return task.run(packet);
	}

	TaskControlBlock@ check_priority_add(TaskControlBlock@ other, Packet@ packet) {
		if (queue is null) {
			@queue = packet;
			mark_as_runnable();
			if (priority > other.priority) {
				return this;
			}
		} else {
			@queue = packet.add_to(queue);
		}
		return other;
	}
}

abstract class Task {
	Scheduler@ scheduler;

	Task(Scheduler@ a_scheduler) {
		@scheduler = a_scheduler;
	}

	TaskControlBlock@ run(Packet@ packet) {
		return null;
	}
}

class IdleTask : Task {
	int v1;
	int count;

	IdleTask(Scheduler@ a_scheduler, int a_v1, int a_count) {
		super(a_scheduler);
		v1 = a_v1;
		count = a_count;
	}

	TaskControlBlock@ run(Packet@ packet) override {
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
	Packet@ v1;

	DeviceTask(Scheduler@ a_scheduler) {
		super(a_scheduler);
	}

	TaskControlBlock@ run(Packet@ packet) override {
		if (packet is null) {
			if (v1 is null) {
				return scheduler.suspend_current();
			}
			Packet@ v = v1;
			@v1 = null;
			return scheduler.queue(v);
		}
		@v1 = packet;
		return scheduler.hold_current();
	}
}

class WorkerTask : Task {
	int v1;
	int v2;

	WorkerTask(Scheduler@ a_scheduler, int a_v1, int a_v2) {
		super(a_scheduler);
		v1 = a_v1;
		v2 = a_v2;
	}

	TaskControlBlock@ run(Packet@ packet) override {
		if (packet is null) {
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
	Packet@ v1;
	Packet@ v2;

	HandlerTask(Scheduler@ a_scheduler) {
		super(a_scheduler);
	}

	TaskControlBlock@ run(Packet@ packet) override {
		if (packet !is null) {
			if (packet.kind == KIND_WORK) {
				@v1 = packet.add_to(v1);
			} else {
				@v2 = packet.add_to(v2);
			}
		}
		if (v1 !is null) {
			int count = v1.a1;
			if (count < DATA_SIZE) {
				if (v2 !is null) {
					Packet@ v = v2;
					@v2 = v2.link;
					v.a1 = v1.a2[count];
					v1.a1 = count + 1;
					return scheduler.queue(v);
				}
			} else {
				Packet@ v = v1;
				@v1 = v1.link;
				return scheduler.queue(v);
			}
		}
		return scheduler.suspend_current();
	}
}

class Scheduler {
	int queue_count = 0;
	int hold_count = 0;
	array<TaskControlBlock@> blocks(NUMBER_OF_IDS);
	TaskControlBlock@ list;
	TaskControlBlock@ current_tcb;
	int current_id = 0;

	void add_idle_task(int ident, int priority, Packet@ queue, int count) {
		add_running_task(ident, priority, queue, IdleTask(this, 1, count));
	}

	void add_worker_task(int ident, int priority, Packet@ queue) {
		add_task(ident, priority, queue, WorkerTask(this, ID_HANDLER_A, 0));
	}

	void add_handler_task(int ident, int priority, Packet@ queue) {
		add_task(ident, priority, queue, HandlerTask(this));
	}

	void add_device_task(int ident, int priority, Packet@ queue) {
		add_task(ident, priority, queue, DeviceTask(this));
	}

	void add_running_task(int ident, int priority, Packet@ queue, Task@ task) {
		add_task(ident, priority, queue, task);
		current_tcb.set_running();
	}

	void add_task(int ident, int priority, Packet@ queue, Task@ task) {
		@current_tcb = TaskControlBlock(list, ident, priority, queue, task);
		@list = current_tcb;
		@blocks[ident] = current_tcb;
	}

	void schedule() {
		@current_tcb = list;
		while (current_tcb !is null) {
			if (current_tcb.is_held_or_suspended()) {
				@current_tcb = current_tcb.link;
			} else {
				current_id = current_tcb.ident;
				@current_tcb = current_tcb.run();
			}
		}
	}

	TaskControlBlock@ release(int ident) {
		TaskControlBlock@ tcb = blocks[ident];
		if (tcb is null) {
			return tcb;
		}
		tcb.mark_as_not_held();
		if (tcb.priority > current_tcb.priority) {
			return tcb;
		}
		return current_tcb;
	}

	TaskControlBlock@ hold_current() {
		++hold_count;
		current_tcb.mark_as_held();
		return current_tcb.link;
	}

	TaskControlBlock@ suspend_current() {
		current_tcb.mark_as_suspended();
		return current_tcb;
	}

	TaskControlBlock@ queue(Packet@ packet) {
		TaskControlBlock@ t = blocks[packet.ident];
		if (t is null) {
			return t;
		}
		++queue_count;
		@packet.link = null;
		packet.ident = current_id;
		return t.check_priority_add(current_tcb, packet);
	}
}

int run_richards() {
	Scheduler@ scheduler = Scheduler();
	scheduler.add_idle_task(ID_IDLE, 0, null, COUNT);
	Packet@ queue = Packet(null, ID_WORKER, KIND_WORK);
	@queue = Packet(queue, ID_WORKER, KIND_WORK);
	scheduler.add_worker_task(ID_WORKER, 1000, queue);
	@queue = Packet(null, ID_DEVICE_A, KIND_DEVICE);
	@queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	@queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_A, 2000, queue);
	@queue = Packet(null, ID_DEVICE_B, KIND_DEVICE);
	@queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	@queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE);
	scheduler.add_handler_task(ID_HANDLER_B, 3000, queue);
	scheduler.add_device_task(ID_DEVICE_A, 4000, null);
	scheduler.add_device_task(ID_DEVICE_B, 5000, null);
	scheduler.schedule();
	if (scheduler.queue_count != EXPECTED_QUEUE_COUNT || scheduler.hold_count != EXPECTED_HOLD_COUNT) {
		throw("richards: bad counts " + scheduler.queue_count + " " + scheduler.hold_count);
	}
	return scheduler.queue_count * 31 + scheduler.hold_count;
}

void main() {
	double start = clock();
	int64 result = 0;
	for (int i = 0; i < ITERATIONS; ++i) {
		result = (result * 31 + run_richards()) & 0xFFFFFFFF;
	}
	double elapsed = (clock() - start) * 1000.0;
	print("result " + result);
	print("time_ms " + formatFloat(elapsed, "", 0, 3));
}
