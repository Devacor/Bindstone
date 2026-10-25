# A small stack-based bytecode VM running a fixed program (recursive fib, an arithmetic loop, an array sieve).
# CODE is assemble.py's output; the opcodes are numbered in its OPS order.
import time

ITERATIONS = 1
STACK_SIZE = 4096
CALL_DEPTH = 1024

HALT = 0
PUSH = 1
POP = 2
DUP = 3
LOAD = 4
STORE = 5
ADD = 6
SUB = 7
MUL = 8
MOD = 9
LT = 10
EQ = 11
JMP = 12
JZ = 13
CALL = 14
RET = 15
ENTER = 16
NEWARR = 17
GETARR = 18
SETARR = 19

CODE = [1, 25, 14, 16, 1, 1, 300000, 14, 44, 1, 1, 250000, 14, 77, 1, 0, 4, 0, 1, 2, 10, 13, 26, 4, 0, 15, 4, 0, 1, 1, 7, 14, 16, 1, 4, 0, 1, 2, 7, 14, 16, 1, 6, 15, 16, 2, 4, 1, 4, 0, 10, 13, 74, 4, 2, 4, 1, 3, 8, 6, 1, 1000003, 9, 5, 2, 4, 1, 1, 1, 6, 5, 1, 12, 46, 4, 2, 15, 16, 4, 4, 0, 17, 5, 1, 1, 2, 5, 2, 4, 2, 4, 0, 10, 13, 149, 4, 1, 4, 2, 18, 13, 104, 12, 140, 4, 4, 1, 1, 6, 5, 4, 4, 2, 3, 8, 5, 3, 4, 3, 4, 0, 10, 13, 140, 4, 1, 4, 3, 1, 1, 19, 4, 3, 4, 2, 6, 5, 3, 12, 117, 4, 2, 1, 1, 6, 5, 2, 12, 88, 4, 4, 15]


def run(code):
	stack = [0] * STACK_SIZE
	return_pcs = [0] * CALL_DEPTH
	frame_pointers = [0] * CALL_DEPTH
	heap = []
	pc = 0
	sp = 0
	fp = 0
	csp = 0
	while True:
		op = code[pc]
		if op == HALT:
			break
		elif op == PUSH:
			stack[sp] = code[pc + 1]
			sp += 1
			pc += 2
		elif op == POP:
			sp -= 1
			pc += 1
		elif op == DUP:
			stack[sp] = stack[sp - 1]
			sp += 1
			pc += 1
		elif op == LOAD:
			stack[sp] = stack[fp + code[pc + 1]]
			sp += 1
			pc += 2
		elif op == STORE:
			sp -= 1
			stack[fp + code[pc + 1]] = stack[sp]
			pc += 2
		elif op == ADD:
			sp -= 1
			stack[sp - 1] = stack[sp - 1] + stack[sp]
			pc += 1
		elif op == SUB:
			sp -= 1
			stack[sp - 1] = stack[sp - 1] - stack[sp]
			pc += 1
		elif op == MUL:
			sp -= 1
			stack[sp - 1] = stack[sp - 1] * stack[sp]
			pc += 1
		elif op == MOD:
			sp -= 1
			stack[sp - 1] = stack[sp - 1] % stack[sp]
			pc += 1
		elif op == LT:
			sp -= 1
			stack[sp - 1] = 1 if stack[sp - 1] < stack[sp] else 0
			pc += 1
		elif op == EQ:
			sp -= 1
			stack[sp - 1] = 1 if stack[sp - 1] == stack[sp] else 0
			pc += 1
		elif op == JMP:
			pc = code[pc + 1]
		elif op == JZ:
			sp -= 1
			if stack[sp] == 0:
				pc = code[pc + 1]
			else:
				pc += 2
		elif op == CALL:
			return_pcs[csp] = pc + 3
			frame_pointers[csp] = fp
			csp += 1
			fp = sp - code[pc + 2]
			pc = code[pc + 1]
		elif op == RET:
			result = stack[sp - 1]
			sp = fp
			csp -= 1
			pc = return_pcs[csp]
			fp = frame_pointers[csp]
			stack[sp] = result
			sp += 1
		elif op == ENTER:
			for _ in range(code[pc + 1]):
				stack[sp] = 0
				sp += 1
			pc += 2
		elif op == NEWARR:
			heap.append([0] * stack[sp - 1])
			stack[sp - 1] = len(heap) - 1
			pc += 1
		elif op == GETARR:
			sp -= 1
			stack[sp - 1] = heap[stack[sp - 1]][stack[sp]]
			pc += 1
		elif op == SETARR:
			sp -= 3
			heap[stack[sp]][stack[sp + 1]] = stack[sp + 2]
			pc += 1
		else:
			raise RuntimeError(f"bad opcode {op} at {pc}")
	result = 0
	for i in range(sp):
		result = (result * 31 + stack[i]) & 0xFFFFFFFF
	return result


def main():
	start = time.perf_counter()
	result = 0
	for _ in range(ITERATIONS):
		result = (result * 31 + run(CODE)) & 0xFFFFFFFF
	elapsed = (time.perf_counter() - start) * 1000.0
	print(f"result {result}")
	print(f"time_ms {elapsed:.3f}")


main()
