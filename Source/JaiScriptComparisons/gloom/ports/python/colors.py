"""math::color for the showcase look: four binary32 components, emulated.

A color is an (r, g, b) tuple of Python floats that each hold a binary32
value (alpha never reaches a palette index, so it is not carried). Every
operation computes in binary64 and rounds each component to binary32 once,
through a precompiled struct.Struct("3f") pack/unpack, which is exact for
+, - and * of binary32 operands and is what JaiScript's color ops store;
a scalar operand rounds to binary32 first. mix_rgb mixes in binary64 and
rounds once. Color math only runs while tables, LUTs and panel gradients
are built, never per pixel.
"""

import struct

from util import rgb_idx

_C3 = struct.Struct("3f")
_PACK3 = _C3.pack
_UNPACK3 = _C3.unpack
_F1 = struct.Struct("f")
_PACK1 = _F1.pack
_UNPACK1 = _F1.unpack


def f32(x):
	return _UNPACK1(_PACK1(x))[0]


def color(r, g, b):
	"""math::color(r, g, b): each component rounded to binary32"""
	return _UNPACK3(_PACK3(r, g, b))


def rgb(r, g, b):
	"""0..255 ints -> math::color(r / 255.0, ...)"""
	return _UNPACK3(_PACK3(r / 255.0, g / 255.0, b / 255.0))


def cscale(c, s):
	"""color * scalar"""
	s = _UNPACK1(_PACK1(s))[0]
	return _UNPACK3(_PACK3(c[0] * s, c[1] * s, c[2] * s))


def cmul(a, b):
	return _UNPACK3(_PACK3(a[0] * b[0], a[1] * b[1], a[2] * b[2]))


def cadd(a, b):
	return _UNPACK3(_PACK3(a[0] + b[0], a[1] + b[1], a[2] + b[2]))


def mix_rgb(a, b, t):
	a0 = a[0]
	a1 = a[1]
	a2 = a[2]
	return _UNPACK3(_PACK3(a0 + (b[0] - a0) * t, a1 + (b[1] - a1) * t, a2 + (b[2] - a2) * t))


def cclamp(c, lo, hi):
	"""math::clamp(color, lo, hi) per component (lo and hi are binary32-exact here)"""
	r, g, b = c
	return (lo if r < lo else (hi if r > hi else r),
		lo if g < lo else (hi if g > hi else g),
		lo if b < lo else (hi if b > hi else b))


def color_idx(c):
	return rgb_idx(int(c[0] * 255.0 + 0.5), int(c[1] * 255.0 + 0.5), int(c[2] * 255.0 + 0.5))
