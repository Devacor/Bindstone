# Richards: Martin Richards' OS scheduler simulation, after the Octane (V8) richards.js.
import sys
import time

ITERATIONS = 200
COUNT = 1000
EXPECTED_QUEUE_COUNT = 2322
EXPECTED_HOLD_COUNT = 928

ID_IDLE = 0
ID_WORKER = 1
ID_HANDLER_A = 2
ID_HANDLER_B = 3
ID_DEVICE_A = 4
ID_DEVICE_B = 5
NUMBER_OF_IDS = 6

KIND_DEVICE = 0
KIND_WORK = 1

STATE_RUNNING = 0
STATE_RUNNABLE = 1
STATE_SUSPENDED = 2
STATE_HELD = 4
STATE_SUSPENDED_RUNNABLE = STATE_SUSPENDED | STATE_RUNNABLE
STATE_NOT_HELD = ~STATE_HELD

DATA_SIZE = 4


class Packet:
	def __init__(self, link, ident, kind):
		self.link = link
		self.ident = ident
		self.kind = kind
		self.a1 = 0
		self.a2 = [0] * DATA_SIZE

	def add_to(self, queue):
		self.link = None
		if queue is None:
			return self
		next_packet = queue
		peek = next_packet.link
		while peek is not None:
			next_packet = peek
			peek = next_packet.link
		next_packet.link = self
		return queue


class TaskControlBlock:
	def __init__(self, link, ident, priority, queue, task):
		self.link = link
		self.ident = ident
		self.priority = priority
		self.queue = queue
		self.task = task
		if queue is None:
			self.state = STATE_SUSPENDED
		else:
			self.state = STATE_SUSPENDED_RUNNABLE

	def set_running(self):
		self.state = STATE_RUNNING

	def mark_as_not_held(self):
		self.state = self.state & STATE_NOT_HELD

	def mark_as_held(self):
		self.state = self.state | STATE_HELD

	def is_held_or_suspended(self):
		return (self.state & STATE_HELD) != 0 or self.state == STATE_SUSPENDED

	def mark_as_suspended(self):
		self.state = self.state | STATE_SUSPENDED

	def mark_as_runnable(self):
		self.state = self.state | STATE_RUNNABLE

	def run(self):
		if self.state == STATE_SUSPENDED_RUNNABLE:
			packet = self.queue
			self.queue = packet.link
			if self.queue is None:
				self.state = STATE_RUNNING
			else:
				self.state = STATE_RUNNABLE
		else:
			packet = None
		return self.task.run(packet)

	def check_priority_add(self, task, packet):
		if self.queue is None:
			self.queue = packet
			self.mark_as_runnable()
			if self.priority > task.priority:
				return self
		else:
			self.queue = packet.add_to(self.queue)
		return task


class Scheduler:
	def __init__(self):
		self.queue_count = 0
		self.hold_count = 0
		self.blocks = [None] * NUMBER_OF_IDS
		self.list = None
		self.current_tcb = None
		self.current_id = 0

	def add_idle_task(self, ident, priority, queue, count):
		self.add_running_task(ident, priority, queue, IdleTask(self, 1, count))

	def add_worker_task(self, ident, priority, queue):
		self.add_task(ident, priority, queue, WorkerTask(self, ID_HANDLER_A, 0))

	def add_handler_task(self, ident, priority, queue):
		self.add_task(ident, priority, queue, HandlerTask(self))

	def add_device_task(self, ident, priority, queue):
		self.add_task(ident, priority, queue, DeviceTask(self))

	def add_running_task(self, ident, priority, queue, task):
		self.add_task(ident, priority, queue, task)
		self.current_tcb.set_running()

	def add_task(self, ident, priority, queue, task):
		self.current_tcb = TaskControlBlock(self.list, ident, priority, queue, task)
		self.list = self.current_tcb
		self.blocks[ident] = self.current_tcb

	def schedule(self):
		self.current_tcb = self.list
		while self.current_tcb is not None:
			if self.current_tcb.is_held_or_suspended():
				self.current_tcb = self.current_tcb.link
			else:
				self.current_id = self.current_tcb.ident
				self.current_tcb = self.current_tcb.run()

	def release(self, ident):
		tcb = self.blocks[ident]
		if tcb is None:
			return tcb
		tcb.mark_as_not_held()
		if tcb.priority > self.current_tcb.priority:
			return tcb
		return self.current_tcb

	def hold_current(self):
		self.hold_count += 1
		self.current_tcb.mark_as_held()
		return self.current_tcb.link

	def suspend_current(self):
		self.current_tcb.mark_as_suspended()
		return self.current_tcb

	def queue(self, packet):
		t = self.blocks[packet.ident]
		if t is None:
			return t
		self.queue_count += 1
		packet.link = None
		packet.ident = self.current_id
		return t.check_priority_add(self.current_tcb, packet)


class Task:
	def __init__(self, scheduler):
		self.scheduler = scheduler

	def run(self, packet):
		return None


class IdleTask(Task):
	def __init__(self, scheduler, v1, count):
		super().__init__(scheduler)
		self.v1 = v1
		self.count = count

	def run(self, packet):
		self.count -= 1
		if self.count == 0:
			return self.scheduler.hold_current()
		if (self.v1 & 1) == 0:
			self.v1 = self.v1 >> 1
			return self.scheduler.release(ID_DEVICE_A)
		self.v1 = (self.v1 >> 1) ^ 0xD008
		return self.scheduler.release(ID_DEVICE_B)


class DeviceTask(Task):
	def __init__(self, scheduler):
		super().__init__(scheduler)
		self.v1 = None

	def run(self, packet):
		if packet is None:
			if self.v1 is None:
				return self.scheduler.suspend_current()
			v = self.v1
			self.v1 = None
			return self.scheduler.queue(v)
		self.v1 = packet
		return self.scheduler.hold_current()


class WorkerTask(Task):
	def __init__(self, scheduler, v1, v2):
		super().__init__(scheduler)
		self.v1 = v1
		self.v2 = v2

	def run(self, packet):
		if packet is None:
			return self.scheduler.suspend_current()
		if self.v1 == ID_HANDLER_A:
			self.v1 = ID_HANDLER_B
		else:
			self.v1 = ID_HANDLER_A
		packet.ident = self.v1
		packet.a1 = 0
		for i in range(DATA_SIZE):
			self.v2 += 1
			if self.v2 > 26:
				self.v2 = 1
			packet.a2[i] = self.v2
		return self.scheduler.queue(packet)


class HandlerTask(Task):
	def __init__(self, scheduler):
		super().__init__(scheduler)
		self.v1 = None
		self.v2 = None

	def run(self, packet):
		if packet is not None:
			if packet.kind == KIND_WORK:
				self.v1 = packet.add_to(self.v1)
			else:
				self.v2 = packet.add_to(self.v2)
		if self.v1 is not None:
			count = self.v1.a1
			if count < DATA_SIZE:
				if self.v2 is not None:
					v = self.v2
					self.v2 = self.v2.link
					v.a1 = self.v1.a2[count]
					self.v1.a1 = count + 1
					return self.scheduler.queue(v)
			else:
				v = self.v1
				self.v1 = self.v1.link
				return self.scheduler.queue(v)
		return self.scheduler.suspend_current()


def run_richards():
	scheduler = Scheduler()
	scheduler.add_idle_task(ID_IDLE, 0, None, COUNT)
	queue = Packet(None, ID_WORKER, KIND_WORK)
	queue = Packet(queue, ID_WORKER, KIND_WORK)
	scheduler.add_worker_task(ID_WORKER, 1000, queue)
	queue = Packet(None, ID_DEVICE_A, KIND_DEVICE)
	queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE)
	queue = Packet(queue, ID_DEVICE_A, KIND_DEVICE)
	scheduler.add_handler_task(ID_HANDLER_A, 2000, queue)
	queue = Packet(None, ID_DEVICE_B, KIND_DEVICE)
	queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE)
	queue = Packet(queue, ID_DEVICE_B, KIND_DEVICE)
	scheduler.add_handler_task(ID_HANDLER_B, 3000, queue)
	scheduler.add_device_task(ID_DEVICE_A, 4000, None)
	scheduler.add_device_task(ID_DEVICE_B, 5000, None)
	scheduler.schedule()
	if scheduler.queue_count != EXPECTED_QUEUE_COUNT or scheduler.hold_count != EXPECTED_HOLD_COUNT:
		print("richards: bad counts", scheduler.queue_count, scheduler.hold_count)
		sys.exit(1)
	return scheduler.queue_count * 31 + scheduler.hold_count


def main():
	start = time.perf_counter()
	result = 0
	for _ in range(ITERATIONS):
		result = (result * 31 + run_richards()) & 0xFFFFFFFF
	elapsed = (time.perf_counter() - start) * 1000.0
	print(f"result {result}")
	print(f"time_ms {elapsed:.3f}")


main()
