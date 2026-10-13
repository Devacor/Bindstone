#!/usr/bin/env python3
"""Translate the Lua 5.4 port into Lua 5.1 / LuaJIT source: the same program text with
   a // b -> jfloor((a) / (b)),  a & b -> jband(a, b),  a | b -> jbor(a, b),  a ~ b -> jbxor(a, b),
   ~a -> jbnot(a),  a << b -> jlshift(a, b),  a >> b -> jrshift(a, b)
(a full Lua expression parser decides the operands), with the bit library bound to locals at the top
of each file. gq_util.lua is not translated: luajit/gq_util.lua is its hand-written LuaJIT twin
(32-bit hash, RNG and C-division helpers on doubles).

  python to_luajit.py [OUTDIR]       (default: ./luajit next to this script)
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))

TOKEN = re.compile(r'''
	(?P<ws>(?:\s+|--\[(?P<eq1>=*)\[.*?\](?P=eq1)\]|--[^\n]*)+)
	|(?P<long>\[(?P<eq2>=*)\[.*?\](?P=eq2)\])
	|(?P<str>"(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')
	|(?P<num>0[xX][0-9a-fA-F]+|(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)
	|(?P<name>[A-Za-z_][A-Za-z_0-9]*)
	|(?P<op>\.\.\.|\.\.|==|~=|<=|>=|<<|>>|//|::|[-+*/%^#&~|<>=(){}\[\];:,.])
''', re.S | re.X)

KEYWORDS = {'and', 'break', 'do', 'else', 'elseif', 'end', 'false', 'for', 'function', 'goto', 'if', 'in',
	'local', 'nil', 'not', 'or', 'repeat', 'return', 'then', 'true', 'until', 'while'}
BINARY = {'or': (1, 1), 'and': (2, 2), '<': (3, 3), '>': (3, 3), '<=': (3, 3), '>=': (3, 3), '~=': (3, 3), '==': (3, 3),
	'|': (4, 4), '~': (5, 5), '&': (6, 6), '<<': (7, 7), '>>': (7, 7), '..': (9, 8), '+': (10, 10), '-': (10, 10),
	'*': (11, 11), '/': (11, 11), '//': (11, 11), '%': (11, 11), '^': (14, 13)}
UNARY_PRIORITY = 12
REWRITE = {'//': 'jfloor', '&': 'jband', '|': 'jbor', '~': 'jbxor', '<<': 'jlshift', '>>': 'jrshift'}

def tokenize(src):
	toks, pre, pos = [], '', 0
	while pos < len(src):
		m = TOKEN.match(src, pos)
		if not m:
			raise SyntaxError('bad character at %d: %r' % (pos, src[pos:pos + 20]))
		pos = m.end()
		if m.group('ws') is not None:
			pre += m.group(0)
			continue
		kind = 'name' if m.group('name') else ('op' if m.group('op') else 'lit')
		text = m.group(0)
		if kind == 'name' and text in KEYWORDS:
			kind = 'kw'
		toks.append([kind, text, pre])
		pre = ''
	toks.append(['eof', '', pre])
	return toks

class Parser:
	def __init__(self, toks, ops=REWRITE):
		self.t, self.i, self.nodes, self.ops = toks, 0, [], ops

	def peek(self, k=0): return self.t[self.i + k]
	def is_(self, text): return self.t[self.i][1] == text and self.t[self.i][0] in ('op', 'kw')
	def take(self, text=None):
		tok = self.t[self.i]
		if text is not None and not (tok[1] == text and tok[0] in ('op', 'kw')):
			raise SyntaxError('expected %r, got %r near token %d' % (text, tok[1], self.i))
		self.i += 1
		return tok
	def name(self):
		tok = self.take()
		if tok[0] != 'name':
			raise SyntaxError('expected a name, got %r' % tok[1])

	def block(self):
		while True:
			k = self.peek()
			if k[0] == 'eof' or (k[0] == 'kw' and k[1] in ('end', 'else', 'elseif', 'until')):
				return
			if self.is_('return'):
				self.take()
				if not (self.peek()[0] == 'eof' or self.is_(';') or (self.peek()[0] == 'kw' and self.peek()[1] in ('end', 'else', 'elseif', 'until'))):
					self.explist()
				if self.is_(';'):
					self.take()
				continue
			self.statement()

	def statement(self):
		k = self.peek()
		w = k[1] if k[0] in ('kw', 'op') else None
		if w == ';': self.take()
		elif w == 'if':
			self.take(); self.expr(); self.take('then'); self.block()
			while self.is_('elseif'):
				self.take(); self.expr(); self.take('then'); self.block()
			if self.is_('else'):
				self.take(); self.block()
			self.take('end')
		elif w == 'while':
			self.take(); self.expr(); self.take('do'); self.block(); self.take('end')
		elif w == 'do':
			self.take(); self.block(); self.take('end')
		elif w == 'for':
			self.take(); self.name()
			if self.is_('='):
				self.take(); self.expr(); self.take(','); self.expr()
				if self.is_(','):
					self.take(); self.expr()
			else:
				while self.is_(','):
					self.take(); self.name()
				self.take('in'); self.explist()
			self.take('do'); self.block(); self.take('end')
		elif w == 'repeat':
			self.take(); self.block(); self.take('until'); self.expr()
		elif w == 'function':
			self.take(); self.name()
			while self.is_('.'):
				self.take(); self.name()
			if self.is_(':'):
				self.take(); self.name()
			self.body()
		elif w == 'local':
			self.take()
			if self.is_('function'):
				self.take(); self.name(); self.body()
			else:
				self.name()
				while self.is_(','):
					self.take(); self.name()
				if self.is_('='):
					self.take(); self.explist()
		elif w == 'break': self.take()
		elif w == 'goto': self.take(); self.name()
		elif w == '::': self.take(); self.name(); self.take('::')
		else:
			self.suffixedexp()
			if self.is_('=') or self.is_(','):
				while self.is_(','):
					self.take(); self.suffixedexp()
				self.take('='); self.explist()

	def body(self):
		self.take('(')
		while not self.is_(')'):
			if self.is_('...'): self.take()
			else: self.name()
			if self.is_(','): self.take()
		self.take(')'); self.block(); self.take('end')

	def explist(self):
		self.expr()
		while self.is_(','):
			self.take(); self.expr()

	def expr(self, limit=0):
		start = self.i
		tok = self.peek()
		if (tok[0] == 'op' and tok[1] in ('-', '#', '~')) or (tok[0] == 'kw' and tok[1] == 'not'):
			self.take()
			ostart = self.i
			self.expr(UNARY_PRIORITY)
			if tok[1] == '~':
				self.nodes.append(('bnot', start, self.i, (ostart, self.i), None))
		else:
			self.simpleexp()
		while True:
			tok = self.peek()
			op = tok[1] if tok[0] in ('op', 'kw') else None
			if op not in BINARY or BINARY[op][0] <= limit:
				return
			lend = self.i
			self.take()
			rstart = self.i
			self.expr(BINARY[op][1])
			if op in self.ops:
				self.nodes.append((op, start, self.i, (start, lend), (rstart, self.i)))

	def simpleexp(self):
		tok = self.peek()
		if tok[0] == 'lit' or (tok[0] == 'kw' and tok[1] in ('nil', 'true', 'false')) or self.is_('...'):
			self.take()
		elif self.is_('{'): self.constructor()
		elif self.is_('function'):
			self.take(); self.body()
		else:
			self.suffixedexp()

	def suffixedexp(self):
		if self.is_('('):
			self.take(); self.expr(); self.take(')')
		else:
			self.name()
		while True:
			if self.is_('.'):
				self.take(); self.name()
			elif self.is_('['):
				self.take(); self.expr(); self.take(']')
			elif self.is_(':'):
				self.take(); self.name(); self.args()
			elif self.is_('(') or self.is_('{') or self.peek()[0] == 'lit' and self.peek()[1][0] in '"\'[':
				self.args()
			else:
				return

	def args(self):
		if self.is_('('):
			self.take()
			if not self.is_(')'):
				self.explist()
			self.take(')')
		elif self.is_('{'):
			self.constructor()
		else:
			self.take()

	def constructor(self):
		self.take('{')
		while not self.is_('}'):
			if self.is_('['):
				self.take(); self.expr(); self.take(']'); self.take('='); self.expr()
			elif self.peek()[0] == 'name' and self.peek(1)[1] == '=' and self.peek(1)[0] == 'op':
				self.take(); self.take(); self.expr()
			else:
				self.expr()
			if self.is_(',') or self.is_(';'):
				self.take()
		self.take('}')

def translate(src, rewrite=REWRITE, bnot='jbnot'):
	"""rewrite maps a binary operator to the function that replaces it ('//' -> 'jfloor' is a division);
	an operator it leaves out stays as written"""
	toks = tokenize(src)
	p = Parser(toks, rewrite)
	p.block()
	if p.peek()[0] != 'eof':
		raise SyntaxError('trailing tokens at %d' % p.i)
	starts = {}
	for n in p.nodes:
		starts.setdefault(n[1], []).append(n)

	def emit(i, j, drop_lead=False):
		out, k = [], i
		while k < j:
			best = None
			for n in starts.get(k, ()):
				if n[2] <= j and (best is None or n[2] > best[2]):
					best = n
			if best is not None:
				lead = toks[k][2]
				if best[0] == 'bnot':
					inner = emit(best[3][0], best[3][1], True)
					text = '%s(%s)' % (bnot, inner)
				else:
					left = emit(best[3][0], best[3][1], True)
					right = emit(best[4][0], best[4][1], True)
					if rewrite[best[0]] == 'jfloor':
						text = 'jfloor((%s) / (%s))' % (left, right)
					else:
						text = '%s(%s, %s)' % (rewrite[best[0]], left, right)
				out.append(('' if (drop_lead and k == i) else lead) + text)
				k = best[2]
			else:
				out.append(('' if (drop_lead and k == i) else toks[k][2]) + toks[k][1])
				k += 1
		return ''.join(out)

	body = emit(0, len(toks) - 1) + toks[-1][2]
	return body

HEADER = ('local bit = require("bit")\n'
	'local jband, jbor, jbxor, jbnot, jlshift, jrshift, jfloor = bit.band, bit.bor, bit.bxor, bit.bnot, bit.lshift, bit.rshift, math.floor\n')

def main():
	out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'luajit')
	os.makedirs(out, exist_ok=True)
	for name in sorted(os.listdir(HERE)):
		if not name.endswith('.lua') or name == 'gq_util.lua':
			continue
		src = open(os.path.join(HERE, name), encoding='utf-8').read()
		text = translate(src)
		with open(os.path.join(out, name), 'w', encoding='utf-8', newline='\n') as f:
			f.write('-- GENERATED by to_luajit.py from ../' + name + ' (Lua 5.1 / LuaJIT operators); do not edit.\n')
			f.write(HEADER)
			f.write(text)
		print('wrote', os.path.join(out, name))

if __name__ == '__main__':
	main()
