# Char semantics: the integral-promotion contract

The contract has two parts: (1) arithmetic/bitwise promotion, (2) comparison promotion +
byte minting.

## The model in one paragraph

`char` stays a distinct immediate type — no allocation, no strong_ptr traffic, `type_of`
says `"char"`, it hashes and keys maps. A char
operand enters **every binary numeric context** — arithmetic, bitwise, AND comparisons —
under C++-style **integral promotion**: it participates as an `int64` in **0..255**
(unsigned by spec; signed char is a binary-data footgun this language opts out of). One
rule, everywhere. Storage width and arithmetic width are different concerns, exactly like
typed arrays (packed storage, int64 math on top).

## What promotes

`char op int`, `char op char`, `char op float` under:

| operators | result |
|---|---|
| `+  -  *  /  %` | `int` (or `float` if the other operand is float) |
| `&  \|  ^  <<  >>` | `int` |
| `==  !=  <  <=  >  >=` | `bool` — `'a' == 97` is true; `s[i] == 0x1A` is a byte test |
| `<=>` | `int` (-1/0/1) |
| compound assigns `+= -= *= /= %=` | target's stored result via the same promotion |

```cpp
'z' - 'a'            // 25
s[i] - '0'           // digit value — the parser shape
s[i] == 0x1A         // byte test
s[i] > 127           // high-bit test, unsigned: byte 0xC3 is 195, never -61
c & 0x7F             // masking
var sum = 0; sum += s[i];   // accumulates byte values
'a' * 1.5            // 145.5 — promotes through to float like any int would
```

Everything is unsigned end to end — arithmetic, comparisons, and map-key ordering
(`script_value::operator<=>`) agree.

## Minting bytes

Both directions of the char↔int door are open:

- **`\xNN` escapes** in `"..."` strings and `'...'` char literals — **exactly two hex
  digits** (`"\x415"` is `'A'` then `"5"`; C++'s consume-all-digits rule is a known wart
  we skip). `"\x00"` embeds a real NUL. Malformed (`\xZ`, `\x4`) is a lex error.
- **`to_char(n)`** (stdlib) — explicit int → char, range-checked 0..255, throws outside.
  `out += to_char(b)` appends the byte to a string: the binary-writer shape.
- `string += char` appends as text (chars are text units). `string += int` raises
  by the Strong Types design — stringify explicitly if you mean it. The promotion makes
  `to_char(a) + to_char(b)` an int, so `out += to_char(a) + to_char(b)` and
  `parts.push(to_char(a) + to_char(b))` into an `array<string>` are that same mistake; the
  static checker sees the promoted type and reports both at load (`"" + to_char(a) + to_char(b)`
  builds the text).

There is **no implicit int→char**: `type_of('a' + 1) == "int"`.

## Casts

`cast<T>(e)` (grammar.md, "cast expressions") is the explicit spelling of the char↔int door
and the only conversion path: the assignment table has no char row, so `char c = 65` and
`int x = 'c'` stay type mismatches (strict static checking rejects the literal forms before
anything runs). The cast kernel carries four char rules, byte-identical on both backends:

| cast | result |
|---|---|
| `cast<int>(c)` | the promoted code point, `0..255` — `cast<int>('a') == 97`, `cast<int>('\xC3') == 195` |
| `cast<float>(c)` | the same code point widened — `cast<float>('a') == 97.0` |
| `cast<string>(c)` | the one-character string — `cast<string>('a') == "a"` |
| `cast<char>(e)` | an int in `0..255` (`cast<char>(97) == 'a'`) or a string of exactly one character (`cast<char>("a") == 'a'`) |

Anything else raises the catchable cast text: `cast<char>(256)` / `cast<char>(-1)` →
`Cannot cast 'int' to 'char'`, `cast<char>("ab")` / `cast<char>("")` → `Cannot cast 'string'
to 'char'`, and bool/float never cast to char. The reference form `cast<char&>(i)` never
converts (a reference keeps its referent's type). Round trips are exact —
`cast<char>(cast<int>('z')) == 'z'` — and `cast<char>(n)` is a parse-proven typed store
(`char c = cast<char>(65);`). `to_char(n)` remains the stdlib function form of the same
range-checked int→char rule.

## What promotion leaves alone

- **`char + string` / `string + char` concatenate as text.** Promotion only fires when
  *both* operands are numeric-or-char.
- **`'a' == "a"` is false** — promotion never crosses into the string domain.
- **Object custom operators receive the raw char** — promotion never rewrites an operand
  that flows to a user-defined operator.
- Serialization, `print` and JSON render a char as a char. Map keys keep type-rank
  ordering (char keys never interleave with int keys).

## Iterating a string

Range-`for` over a string yields its chars in order, one per element, exactly the reads
`s[i]` gives (`type_of(c) == "char"`, promotion applies as above). The binding is by value
only: a char is a read, never a cell, so `for (auto& c : s)` raises `Cannot iterate a string
by reference: its chars are not lvalues (use for (auto c : s))` before the first element.
An empty string iterates zero times; `break`, `continue` and `yield` behave as in an array
loop; the string itself is never modified by the loop.

```cpp
int sum = 0;
for (auto c : s) { sum += c; }                // byte sum, chars promote to 0..255
for (const auto c : "\x50\x4B") { ... }       // literals and nested reads (m["k"]) iterate too
```

## Recipes for binary-format work

```cpp
var b = s[i] + 0;                            // byte value of a char, 0..255
var b2 = cast<int>(s[i]);                    // the same, spelled as a cast
var d = s[i] - '0';                          // ASCII digit
var word = (s[i] + 0) | ((s[i+1] + 0) << 8); // little-endian u16
if (s[i] == 0x1A) { ... }                    // byte sentinel test
out += to_char(len & 0xFF);                  // write a computed byte
var magic = "\x50\x4B\x03\x04";              // literal byte sequences in source
```

Not provided: `s.byte(i)` (`s[i] + 0` covers it), a stdlib `utf8(cp)`, unary `-c`/`~c`
(binary `0 - c` / `c ^ 0xFF` work), and a packed byte-buffer kind for chars.

## Implementation map (for language work, not consumers)

One shared kernel, `include/jaiscript/detail/char_promotion.hpp`
(`char_operands_promote` + `char_promoted`), consumed VERBATIM by both backends — the
ref_lvalue.hpp parity-by-construction pattern:

- interpreter: `interpreter_dispatch.cpp` handlers (arithmetic + bitwise + all six
  comparisons + spaceship), `interpreter::evaluate_arithmetic`, and the identifier
  compound in-place ladder in `interpreter.cpp` (incl. the string+=char row).
- vm: `vm_backend.cpp` twin handlers, the bitwise cases of `handle_binary_op`,
  `vm_backend::evaluate_arithmetic`, and `exec_compound_store`'s in-place ladder.
- value ordering: `script_value::operator<=>`'s char row compares via `unsigned char`
  (value.cpp) so map ordering agrees with script-level comparisons.
- lexer: `\xNN` in `scan_string`/`scan_char` (`hex_digit_value`, exactly two digits);
  a parse change bumps `k_jaibite_version` (`detail/ast_serializer.hpp`).
- stdlib: `to_char` registered beside `type_of` (stdlib/io.hpp).
- casts: `include/jaiscript/detail/cast_kernel.hpp` (`cast_char_rule_applies` +
  `cast_char_value`), consumed verbatim by `interpreter::visit_cast_expr` and
  `vm_backend::exec_cast` ahead of the assignment table; rows in the Strong Types cast
  tests (`cast_char_rules`, `strong_types_tests.cpp`).
- static checker: `binary_result` types char as numeric under arithmetic/bitwise/spaceship.
- tests: `"Char Promotion"` suite (19 tests) in
  `source/tests/language/review_regression_tests.cpp`; char ordering (char against char, and
  char against int by code point) is pinned in `gloom_feedback_tests.cpp::char_ordering_comparisons`;
  the Strong Types `string += int` raise is pinned in `strong_types_tests.cpp`.

The differential fuzz generator (`source/tests/fuzz/fuzz_generator.hpp`) does not generate
char literals.
