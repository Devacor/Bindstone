# Char semantics: the integral-promotion contract

Rulings 2026-07-12 (Dev, out of the DOOM-in-JaiScript WAD-parser feedback), landed same
day in two increments: (1) arithmetic/bitwise promotion, (2) comparison promotion +
byte minting. Suites green ×2 backends, differential fuzz 3000 seeds / 0 new divergences.

## The model in one paragraph

`char` stays a distinct immediate type — no allocation, no strong_ptr traffic, `type_of`
says `"char"`, it hashes and keys maps. What changed: it is no longer a dead end. A char
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
| `==  !=  <  <=  >  >=` | `bool` — `'a' == 97` is true; `s[i] == 0x1A` finally works |
| `<=>` | `int` (-1/0/1) |
| compound assigns `+= -= *= /= %=` | target's stored result via the same promotion |

```cpp
'z' - 'a'            // 25
s[i] - '0'           // digit value — the parser shape
s[i] == 0x1A         // byte test (was SILENTLY FALSE before the comparison ruling)
s[i] > 127           // high-bit test, unsigned: byte 0xC3 is 195, never -61
c & 0x7F             // masking
var sum = 0; sum += s[i];   // accumulates byte values
'a' * 1.5            // 145.5 — promotes through to float like any int would
```

Everything is unsigned end to end now — arithmetic, comparisons, and map-key ordering
(`script_value::operator<=>`) agree; the old platform-signed comparison rows are gone.

## Minting bytes

Both directions of the char↔int door are open:

- **`\xNN` escapes** in `"..."` strings and `'...'` char literals — **exactly two hex
  digits** (`"\x415"` is `'A'` then `"5"`; C++'s consume-all-digits rule is a known wart
  we skip). `"\x00"` embeds a real NUL. Malformed (`\xZ`, `\x4`) is a lex error.
- **`to_char(n)`** (stdlib) — explicit int → char, range-checked 0..255, throws outside.
  `out += to_char(b)` appends the byte to a string: the binary-writer shape.
- `string += char` appends as text (chars are text units). `string += int` still raises
  by pinned Strong Types design — stringify explicitly if you mean it.

There is still **no implicit int→char**: `type_of('a' + 1) == "int"`.

## What deliberately does NOT change (additive contract)

- **`char + string` / `string + char` concatenate as text.** Promotion only fires when
  *both* operands are numeric-or-char.
- **`'a' == "a"` is false** — promotion never crosses into the string domain.
- **Object custom operators receive the raw char** — promotion never rewrites an operand
  that flows to a user-defined operator.
- Serialization, `print`, JSON rendering of chars: unchanged. Map keys keep type-rank
  ordering (char keys never interleave with int keys).

## Recipes for binary-format work

```cpp
var b = s[i] + 0;                            // byte value of a char, 0..255
var d = s[i] - '0';                          // ASCII digit
var word = (s[i] + 0) | ((s[i+1] + 0) << 8); // little-endian u16
if (s[i] == 0x1A) { ... }                    // byte sentinel test — just works now
out += to_char(len & 0xFF);                  // write a computed byte
var magic = "\x50\x4B\x03\x04";              // literal byte sequences in source
```

Still deferred: `char(n)` cast *syntax* (needs new parser surface; `to_char` covers the
semantics), `s.byte(i)` garnish (`s[i] + 0` covers it), a stdlib `utf8(cp)`, unary
`-c`/`~c` (binary `0 - c` / `c ^ 0xFF` work), and the packed byte buffer
(`bytes`/`array<byte>` storage lane) — the real answer for big formats, parked with the
lanes work it belongs to.

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
  jaibite format version bumped v2→v3 per the bump-on-any-parse-change contract.
- stdlib: `to_char` registered beside `type_of` (stdlib/io.hpp).
- static checker: `binary_result` types char as numeric under arithmetic/bitwise/spaceship.
- tests: `"Char Promotion"` suite (19 tests) in
  `source/tests/language/review_regression_tests.cpp`; the reversed pin is documented in
  `gloom_feedback_tests.cpp::char_ordering_comparisons`; the Strong Types `string += int`
  raise stays pinned in `strong_types_tests.cpp`.

Known gap: the differential fuzz grammar (`source/tests/fuzz/fuzz_generator.hpp`) does not
yet generate char literals — extend it when the fuzz-gate seeds aren't load-bearing for
in-flight work.
