-- A small stack-based bytecode VM running a fixed program (recursive fib, an arithmetic loop, an array sieve).
-- CODE is assemble.py's output; the opcodes are numbered in its OPS order. Addresses and slots stay zero-based.
local ITERATIONS = 1
local STACK_SIZE = 4096
local CALL_DEPTH = 1024

local HALT = 0
local PUSH = 1
local POP = 2
local DUP = 3
local LOAD = 4
local STORE = 5
local ADD = 6
local SUB = 7
local MUL = 8
local MOD = 9
local LT = 10
local EQ = 11
local JMP = 12
local JZ = 13
local CALL = 14
local RET = 15
local ENTER = 16
local NEWARR = 17
local GETARR = 18
local SETARR = 19

local CODE = {[0] = 1, 25, 14, 16, 1, 1, 300000, 14, 44, 1, 1, 250000, 14, 77, 1, 0, 4, 0, 1, 2, 10, 13, 26, 4, 0, 15, 4, 0, 1, 1, 7, 14, 16, 1, 4, 0, 1, 2, 7, 14, 16, 1, 6, 15, 16, 2, 4, 1, 4, 0, 10, 13, 74, 4, 2, 4, 1, 3, 8, 6, 1, 1000003, 9, 5, 2, 4, 1, 1, 1, 6, 5, 1, 12, 46, 4, 2, 15, 16, 4, 4, 0, 17, 5, 1, 1, 2, 5, 2, 4, 2, 4, 0, 10, 13, 149, 4, 1, 4, 2, 18, 13, 104, 12, 140, 4, 4, 1, 1, 6, 5, 4, 4, 2, 3, 8, 5, 3, 4, 3, 4, 0, 10, 13, 140, 4, 1, 4, 3, 1, 1, 19, 4, 3, 4, 2, 6, 5, 3, 12, 117, 4, 2, 1, 1, 6, 5, 2, 12, 88, 4, 4, 15}

local function zeros(n)
	local result = {}
	for i = 0, n - 1 do
		result[i] = 0
	end
	return result
end

local function run(code)
	local stack = zeros(STACK_SIZE)
	local return_pcs = zeros(CALL_DEPTH)
	local frame_pointers = zeros(CALL_DEPTH)
	local heap = {}
	local heap_size = 0
	local pc = 0
	local sp = 0
	local fp = 0
	local csp = 0
	while true do
		local op = code[pc]
		if op == HALT then
			break
		elseif op == PUSH then
			stack[sp] = code[pc + 1]
			sp = sp + 1
			pc = pc + 2
		elseif op == POP then
			sp = sp - 1
			pc = pc + 1
		elseif op == DUP then
			stack[sp] = stack[sp - 1]
			sp = sp + 1
			pc = pc + 1
		elseif op == LOAD then
			stack[sp] = stack[fp + code[pc + 1]]
			sp = sp + 1
			pc = pc + 2
		elseif op == STORE then
			sp = sp - 1
			stack[fp + code[pc + 1]] = stack[sp]
			pc = pc + 2
		elseif op == ADD then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] + stack[sp]
			pc = pc + 1
		elseif op == SUB then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] - stack[sp]
			pc = pc + 1
		elseif op == MUL then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] * stack[sp]
			pc = pc + 1
		elseif op == MOD then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] % stack[sp]
			pc = pc + 1
		elseif op == LT then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] < stack[sp] and 1 or 0
			pc = pc + 1
		elseif op == EQ then
			sp = sp - 1
			stack[sp - 1] = stack[sp - 1] == stack[sp] and 1 or 0
			pc = pc + 1
		elseif op == JMP then
			pc = code[pc + 1]
		elseif op == JZ then
			sp = sp - 1
			if stack[sp] == 0 then
				pc = code[pc + 1]
			else
				pc = pc + 2
			end
		elseif op == CALL then
			return_pcs[csp] = pc + 3
			frame_pointers[csp] = fp
			csp = csp + 1
			fp = sp - code[pc + 2]
			pc = code[pc + 1]
		elseif op == RET then
			local result = stack[sp - 1]
			sp = fp
			csp = csp - 1
			pc = return_pcs[csp]
			fp = frame_pointers[csp]
			stack[sp] = result
			sp = sp + 1
		elseif op == ENTER then
			for _ = 1, code[pc + 1] do
				stack[sp] = 0
				sp = sp + 1
			end
			pc = pc + 2
		elseif op == NEWARR then
			heap[heap_size] = zeros(stack[sp - 1])
			stack[sp - 1] = heap_size
			heap_size = heap_size + 1
			pc = pc + 1
		elseif op == GETARR then
			sp = sp - 1
			stack[sp - 1] = heap[stack[sp - 1]][stack[sp]]
			pc = pc + 1
		elseif op == SETARR then
			sp = sp - 3
			heap[stack[sp]][stack[sp + 1]] = stack[sp + 2]
			pc = pc + 1
		else
			error(string.format("bad opcode %d at %d", op, pc))
		end
	end
	local result = 0
	for i = 0, sp - 1 do
		result = (result * 31 + stack[i]) & 0xFFFFFFFF
	end
	return result
end

local start = os.clock()
local result = 0
for _ = 1, ITERATIONS do
	result = (result * 31 + run(CODE)) & 0xFFFFFFFF
end
local elapsed = (os.clock() - start) * 1000.0
print(string.format("result %d", result))
print(string.format("time_ms %.3f", elapsed))
