# Integer Semantics

JaiScript's `int` is a signed 64-bit integer. This document defines how arithmetic,
bitwise operations, and the overflow policy interact.

## Checked arithmetic

The **arithmetic** operators `+  -  *  /  unary -` (and their compound forms `+=`, `-=`,
`*=`, `++`, `--`) are governed by the compile-time overflow policy
(`detail/integer_ops.hpp`):

- **`JAISCRIPT_CHECKED_OVERFLOW`** (the default): an operation whose true mathematical
  result does not fit in int64 raises a catchable runtime error
  (`Integer overflow in '+'`, `'-'`, `'*'`, …). `INT64_MIN / -1` and `-INT64_MIN`
  are overflow. `a % -1` is `0` for every `a` (never a trap).
- **`JAISCRIPT_WRAP_ON_OVERFLOW`**: silent two's-complement wraparound.

`engine::throw_on_overflow()` reports which policy the build carries. Under either
policy no integer operation is ever undefined behavior.

## Bitwise operations are mod 2^64

The **bitwise** operators `&  |  ^  <<  >>` operate on the 64-bit two's-complement
pattern and are **not** subject to the overflow policy — in both builds:

- `<<` discards bits shifted past bit 63. `1 << 63` is `INT64_MIN`; shifting a value
  into the sign bit yields a negative number, never an error. This is the operation's
  definition (a bit move, like `& 0` is a bit clear), not an unchecked overflow — the
  same line checked-arithmetic languages draw (Rust, C#).
- `>>` is an arithmetic shift: it replicates the sign bit, so `-8 >> 1` is `-4`.
- The shift **count** must be in `0..63`; any other count raises a catchable runtime
  error (`Shift amount must be between 0 and 63`).

Idioms this contract exists for: xorshift RNG steps (`s ^ (s << 25)`), hash folds,
byte packing/unpacking (`(hi << 8) | lo`), and wide constants built in halves
(`(0x9E3779B9 << 32) | 0x7F4A7C15`).

## Explicit wraparound arithmetic: `math::wrap_*`

When an algorithm needs mod-2^64 **arithmetic** — hash multiplies, LCG/splitmix RNG
steps, checksum accumulation — use the wrap intrinsics instead of relying on the build
policy:

```jaiscript
math::wrap_add(a, b)   // (a + b) mod 2^64
math::wrap_sub(a, b)   // (a - b) mod 2^64
math::wrap_mul(a, b)   // (a * b) mod 2^64
```

They take exactly two `int` arguments (floats are rejected — silent truncation would
defeat explicit wrap math) and return the two's-complement wrapped result **in both
overflow builds**, so scripts using them are portable across policies. Like every
`math::` intrinsic they execute in-loop on both backends with no call machinery.

A full-width xorshift-multiply step, without limb decomposition:

```jaiscript
s = s ^ ((s >> 12) & 0xFFFFFFFFFFFFF);   // mask off sign-extension: logical shift
s = s ^ (s << 25);                        // << wraps by contract
s = s ^ ((s >> 27) & 0x1FFFFFFFFF);
result = math::wrap_mul(s, 0x2545F491 << 32 | 0x4F6CDD1D);
```

## Char operands

`char` promotes to int64 `0..255` (unsigned) under arithmetic, bitwise, and comparison
operators — see `char_semantics.md`. The `math::` intrinsics are numeric-only and do
not accept `char`.
