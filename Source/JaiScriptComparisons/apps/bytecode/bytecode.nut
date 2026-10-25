// A small stack-based bytecode VM running a fixed program (recursive fib, an arithmetic loop, an array sieve).
// CODE is assemble.py's output; the opcodes are numbered in its OPS order.
const ITERATIONS = 1;
const STACK_SIZE = 4096;
const CALL_DEPTH = 1024;

const HALT = 0;
const PUSH = 1;
const POP = 2;
const DUP = 3;
const LOAD = 4;
const STORE = 5;
const ADD = 6;
const SUB = 7;
const MUL = 8;
const MOD = 9;
const LT = 10;
const EQ = 11;
const JMP = 12;
const JZ = 13;
const CALL = 14;
const RET = 15;
const ENTER = 16;
const NEWARR = 17;
const GETARR = 18;
const SETARR = 19;

CODE <- [1, 25, 14, 16, 1, 1, 300000, 14, 44, 1, 1, 250000, 14, 77, 1, 0, 4, 0, 1, 2, 10, 13, 26, 4, 0, 15, 4, 0, 1, 1, 7, 14, 16, 1, 4, 0, 1, 2, 7, 14, 16, 1, 6, 15, 16, 2, 4, 1, 4, 0, 10, 13, 74, 4, 2, 4, 1, 3, 8, 6, 1, 1000003, 9, 5, 2, 4, 1, 1, 1, 6, 5, 1, 12, 46, 4, 2, 15, 16, 4, 4, 0, 17, 5, 1, 1, 2, 5, 2, 4, 2, 4, 0, 10, 13, 149, 4, 1, 4, 2, 18, 13, 104, 12, 140, 4, 4, 1, 1, 6, 5, 4, 4, 2, 3, 8, 5, 3, 4, 3, 4, 0, 10, 13, 140, 4, 1, 4, 3, 1, 1, 19, 4, 3, 4, 2, 6, 5, 3, 12, 117, 4, 2, 1, 1, 6, 5, 2, 12, 88, 4, 4, 15];

function run(code) {
	local stack = array(STACK_SIZE, 0);
	local return_pcs = array(CALL_DEPTH, 0);
	local frame_pointers = array(CALL_DEPTH, 0);
	local heap = [];
	local pc = 0;
	local sp = 0;
	local fp = 0;
	local csp = 0;
	local running = true;
	while (running) {
		local op = code[pc];
		switch (op) {
			case HALT:
				running = false;
				break;
			case PUSH:
				stack[sp] = code[pc + 1];
				sp++;
				pc += 2;
				break;
			case POP:
				sp--;
				pc++;
				break;
			case DUP:
				stack[sp] = stack[sp - 1];
				sp++;
				pc++;
				break;
			case LOAD:
				stack[sp] = stack[fp + code[pc + 1]];
				sp++;
				pc += 2;
				break;
			case STORE:
				sp--;
				stack[fp + code[pc + 1]] = stack[sp];
				pc += 2;
				break;
			case ADD:
				sp--;
				stack[sp - 1] = stack[sp - 1] + stack[sp];
				pc++;
				break;
			case SUB:
				sp--;
				stack[sp - 1] = stack[sp - 1] - stack[sp];
				pc++;
				break;
			case MUL:
				sp--;
				stack[sp - 1] = stack[sp - 1] * stack[sp];
				pc++;
				break;
			case MOD:
				sp--;
				stack[sp - 1] = stack[sp - 1] % stack[sp];
				pc++;
				break;
			case LT:
				sp--;
				stack[sp - 1] = stack[sp - 1] < stack[sp] ? 1 : 0;
				pc++;
				break;
			case EQ:
				sp--;
				stack[sp - 1] = stack[sp - 1] == stack[sp] ? 1 : 0;
				pc++;
				break;
			case JMP:
				pc = code[pc + 1];
				break;
			case JZ:
				sp--;
				if (stack[sp] == 0) {
					pc = code[pc + 1];
				} else {
					pc += 2;
				}
				break;
			case CALL:
				return_pcs[csp] = pc + 3;
				frame_pointers[csp] = fp;
				csp++;
				fp = sp - code[pc + 2];
				pc = code[pc + 1];
				break;
			case RET: {
				local result = stack[sp - 1];
				sp = fp;
				csp--;
				pc = return_pcs[csp];
				fp = frame_pointers[csp];
				stack[sp] = result;
				sp++;
				break;
			}
			case ENTER: {
				local count = code[pc + 1];
				for (local i = 0; i < count; i++) {
					stack[sp] = 0;
					sp++;
				}
				pc += 2;
				break;
			}
			case NEWARR:
				heap.append(array(stack[sp - 1], 0));
				stack[sp - 1] = heap.len() - 1;
				pc++;
				break;
			case GETARR:
				sp--;
				stack[sp - 1] = heap[stack[sp - 1]][stack[sp]];
				pc++;
				break;
			case SETARR:
				sp -= 3;
				heap[stack[sp]][stack[sp + 1]] = stack[sp + 2];
				pc++;
				break;
			default:
				throw format("bad opcode %d at %d", op, pc);
		}
	}
	local result = 0;
	for (local i = 0; i < sp; i++) {
		result = (result * 31 + stack[i]) & 0xFFFFFFFF;
	}
	return result;
}

local start = clock();
local result = 0;
for (local i = 0; i < ITERATIONS; i++) {
	result = (result * 31 + run(CODE)) & 0xFFFFFFFF;
}
local elapsed = (clock() - start) * 1000.0;
print(format("result %d\n", result));
print(format("time_ms %.3f\n", elapsed));
