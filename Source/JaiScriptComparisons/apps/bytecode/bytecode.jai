// A small stack-based bytecode VM running a fixed program (recursive fib, an arithmetic loop, an array sieve).
// CODE is assemble.py's output; the opcodes are numbered in its OPS order.
const int ITERATIONS = 1;
const int STACK_SIZE = 4096;
const int CALL_DEPTH = 1024;

const int HALT = 0;
const int PUSH = 1;
const int POP = 2;
const int DUP = 3;
const int LOAD = 4;
const int STORE = 5;
const int ADD = 6;
const int SUB = 7;
const int MUL = 8;
const int MOD = 9;
const int LT = 10;
const int EQ = 11;
const int JMP = 12;
const int JZ = 13;
const int CALL = 14;
const int RET = 15;
const int ENTER = 16;
const int NEWARR = 17;
const int GETARR = 18;
const int SETARR = 19;

array<int> CODE = [1, 25, 14, 16, 1, 1, 300000, 14, 44, 1, 1, 250000, 14, 77, 1, 0, 4, 0, 1, 2, 10, 13, 26, 4, 0, 15, 4, 0, 1, 1, 7, 14, 16, 1, 4, 0, 1, 2, 7, 14, 16, 1, 6, 15, 16, 2, 4, 1, 4, 0, 10, 13, 74, 4, 2, 4, 1, 3, 8, 6, 1, 1000003, 9, 5, 2, 4, 1, 1, 1, 6, 5, 1, 12, 46, 4, 2, 15, 16, 4, 4, 0, 17, 5, 1, 1, 2, 5, 2, 4, 2, 4, 0, 10, 13, 149, 4, 1, 4, 2, 18, 13, 104, 12, 140, 4, 4, 1, 1, 6, 5, 4, 4, 2, 3, 8, 5, 3, 4, 3, 4, 0, 10, 13, 140, 4, 1, 4, 3, 1, 1, 19, 4, 3, 4, 2, 6, 5, 3, 12, 117, 4, 2, 1, 1, 6, 5, 2, 12, 88, 4, 4, 15];

array<int> zeros(int n) {
	array<int> result = [];
	for (int i = 0; i < n; ++i) {
		result.push(0);
	}
	return result;
}

int run(array<int>& code) {
	array<int> stack = zeros(STACK_SIZE);
	array<int> return_pcs = zeros(CALL_DEPTH);
	array<int> frame_pointers = zeros(CALL_DEPTH);
	array<array<int>> heap = [];
	int pc = 0;
	int sp = 0;
	int fp = 0;
	int csp = 0;
	while (true) {
		int op = code[pc];
		if (op == HALT) {
			break;
		}
		switch (op) {
			case PUSH:
				stack[sp] = code[pc + 1];
				++sp;
				pc += 2;
			case POP:
				--sp;
				++pc;
			case DUP:
				stack[sp] = stack[sp - 1];
				++sp;
				++pc;
			case LOAD:
				stack[sp] = stack[fp + code[pc + 1]];
				++sp;
				pc += 2;
			case STORE:
				--sp;
				stack[fp + code[pc + 1]] = stack[sp];
				pc += 2;
			case ADD:
				--sp;
				stack[sp - 1] = stack[sp - 1] + stack[sp];
				++pc;
			case SUB:
				--sp;
				stack[sp - 1] = stack[sp - 1] - stack[sp];
				++pc;
			case MUL:
				--sp;
				stack[sp - 1] = stack[sp - 1] * stack[sp];
				++pc;
			case MOD:
				--sp;
				stack[sp - 1] = stack[sp - 1] % stack[sp];
				++pc;
			case LT:
				--sp;
				stack[sp - 1] = stack[sp - 1] < stack[sp] ? 1 : 0;
				++pc;
			case EQ:
				--sp;
				stack[sp - 1] = stack[sp - 1] == stack[sp] ? 1 : 0;
				++pc;
			case JMP:
				pc = code[pc + 1];
			case JZ:
				--sp;
				if (stack[sp] == 0) {
					pc = code[pc + 1];
				} else {
					pc += 2;
				}
			case CALL:
				return_pcs[csp] = pc + 3;
				frame_pointers[csp] = fp;
				++csp;
				fp = sp - code[pc + 2];
				pc = code[pc + 1];
			case RET: {
				int result = stack[sp - 1];
				sp = fp;
				--csp;
				pc = return_pcs[csp];
				fp = frame_pointers[csp];
				stack[sp] = result;
				++sp;
			}
			case ENTER: {
				int count = code[pc + 1];
				for (int i = 0; i < count; ++i) {
					stack[sp] = 0;
					++sp;
				}
				pc += 2;
			}
			case NEWARR:
				heap.push(zeros(stack[sp - 1]));
				stack[sp - 1] = heap.size() - 1;
				++pc;
			case GETARR:
				--sp;
				stack[sp - 1] = heap[stack[sp - 1]][stack[sp]];
				++pc;
			case SETARR:
				sp -= 3;
				heap[stack[sp]][stack[sp + 1]] = stack[sp + 2];
				++pc;
			default:
				throw "bad opcode " + to_string(op) + " at " + to_string(pc);
		}
	}
	int result = 0;
	for (int i = 0; i < sp; ++i) {
		result = (result * 31 + stack[i]) & 0xFFFFFFFF;
	}
	return result;
}

time::stopwatch clock;
clock.start();
int result = 0;
for (int i = 0; i < ITERATIONS; ++i) {
	result = (result * 31 + run(CODE)) & 0xFFFFFFFF;
}
float elapsed = clock.elapsed() * 1000.0;
print("result {}", result);
print("time_ms {:.3f}", elapsed);
