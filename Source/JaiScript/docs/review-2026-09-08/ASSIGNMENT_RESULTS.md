# Assignment expression results

A consumed assignment now returns the destination's stored value. Both backends
agree on all seven tested target forms. Two remaining edge cases are recorded
below for a language decision.

Each plain assignment stores 3.9 into an int slot initially holding 0. Each
compound assignment adds 2.9 to an int slot initially holding 1. The destination
holds int 3 in every case.

| Target | Consumed plain or compound result | Top-level implicit plain result | Top-level implicit compound result |
|---|---|---|---|
| Typed local | int 3 | int 3 | int 3 |
| Bare field inside a method | int 3 | not a top-level form | not a top-level form |
| Explicit instance field | int 3 | float 3.9 | float 3.9 |
| Static field through Class::field | int 3 | int 3 | float 3.9 |
| Accessor backed by an int field | int 3 | float 3.9 | float 3.9 |
| array<int> element | int 3 | float 3.9 | float 3.9 |
| map<string,int> value | int 3 | float 3.9 | float 3.9 |

Consumed means the result is used, as in:

```jai
class C { int n = 0; }
C c = C();
auto result = (c.n = 3.9);
// result is int 3; c.n is int 3.
```

The implicit result is the value returned to the host when the whole script
ends with the assignment statement. That path retains its previous behavior:
local stores and plain static stores return an int, while the other rows above
return the unconverted RHS or operation result.

A setter-only member has no readable destination. In the tested program,
`auto result = (w.x = 2.5)` returns float 2.5 while the int setter receives 2.
The checker follows that existing RHS-result behavior.

Questions for Dev and the runtime lane:

- Should the top-level implicit result also return the stored value uniformly?
- Should a setter-only member keep returning its RHS, or have another specified
  result when no getter or field can supply a stored value?

No runtime change is part of this evidence report. It records the landed
7475aeea assignment rule and its existing exceptions.

Verified by running 27 programs in check, strict and off modes on both backends:
162 processes, zero check errors, identical strict/off results. The snapshot is
dce797ff plus the reviewed rules 1-3 checker overlay.

Raw: out/astra/strict-20260908/assignment-results/results.json, identity.json,
build-identity.json, probe.cpp, after-row84.log and the .jai files.
The older result table and programs remain in evidence-before-row82/.
