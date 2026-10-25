# Assembles the bytecode VM's fixed program and prints it as one comma-separated line, the CODE literal every port embeds.
import sys

OPS = ["HALT", "PUSH", "POP", "DUP", "LOAD", "STORE", "ADD", "SUB", "MUL", "MOD", "LT", "EQ", "JMP", "JZ", "CALL",
	"RET", "ENTER", "NEWARR", "GETARR", "SETARR"]

SOURCE = """
	PUSH 25
	CALL fib 1
	PUSH 300000
	CALL loop_sum 1
	PUSH 250000
	CALL sieve 1
	HALT

fib:
	LOAD 0
	PUSH 2
	LT
	JZ fib_recurse
	LOAD 0
	RET
fib_recurse:
	LOAD 0
	PUSH 1
	SUB
	CALL fib 1
	LOAD 0
	PUSH 2
	SUB
	CALL fib 1
	ADD
	RET

loop_sum:
	ENTER 2
loop_test:
	LOAD 1
	LOAD 0
	LT
	JZ loop_end
	LOAD 2
	LOAD 1
	DUP
	MUL
	ADD
	PUSH 1000003
	MOD
	STORE 2
	LOAD 1
	PUSH 1
	ADD
	STORE 1
	JMP loop_test
loop_end:
	LOAD 2
	RET

sieve:
	ENTER 4
	LOAD 0
	NEWARR
	STORE 1
	PUSH 2
	STORE 2
sieve_outer:
	LOAD 2
	LOAD 0
	LT
	JZ sieve_done
	LOAD 1
	LOAD 2
	GETARR
	JZ sieve_prime
	JMP sieve_next
sieve_prime:
	LOAD 4
	PUSH 1
	ADD
	STORE 4
	LOAD 2
	DUP
	MUL
	STORE 3
sieve_inner:
	LOAD 3
	LOAD 0
	LT
	JZ sieve_next
	LOAD 1
	LOAD 3
	PUSH 1
	SETARR
	LOAD 3
	LOAD 2
	ADD
	STORE 3
	JMP sieve_inner
sieve_next:
	LOAD 2
	PUSH 1
	ADD
	STORE 2
	JMP sieve_outer
sieve_done:
	LOAD 4
	RET
"""


def assemble(source):
	lines = [line.split() for line in source.splitlines() if line.strip()]
	labels = {}
	address = 0
	for parts in lines:
		if parts[0].endswith(":"):
			labels[parts[0][:-1]] = address
		else:
			address += len(parts)
	code = []
	for parts in lines:
		if parts[0].endswith(":"):
			continue
		code.append(OPS.index(parts[0]))
		for operand in parts[1:]:
			code.append(labels[operand] if operand in labels else int(operand))
	return code


if __name__ == "__main__":
	sys.stdout.write(", ".join(str(word) for word in assemble(SOURCE)) + "\n")
