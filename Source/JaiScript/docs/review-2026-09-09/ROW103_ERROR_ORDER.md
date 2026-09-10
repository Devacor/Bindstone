# Rows 100 and 103: operand evaluation order

The first row 103 preview could evaluate an inner element read before resolving an undefined outer container. Fable confirmed the missing admission guard and added it to the preview before landing.

## Counterexample

```jai
[[warn_mode]]
array<int> Q = [1, 2];
function probe(int k, int i) { return missing_outer[k + Q[i % 16]]; }
try { print(probe(0, 9)); } catch (e) { print(e); }
```

The interpreter resolves `missing_outer` before evaluating `Q[9]`. The expected output is:

```text
Undefined variable 'missing_outer'
```

Astra ran the program with freshly built Release runners from `d6c9959b` and `4bd53021`, on the interpreter and VM. All four runs printed that diagnostic and exited zero because the script catches the error. `[[warn_mode]]` admits the deliberately undefined name for this runtime-order test.

## Review and correction

The new arm of `try_emit_fused_index` accepted an identifier container without checking that it was a frame-slot operand. It emitted the inner read into a temporary before constructing the outer fused read. The preview's explanation assumed a frame-slot container, so the stated precondition exceeded the code's admission rule.

The refreshed preview checks `quiet_partner(expr->left.get())` before emitting that arm. Its matching `fusable_index_expr` predicate checks the same precondition. The existing simple nested-index arm already had this check. Fable added the counterexample to the Cell Loops suite and reported interpreter/VM parity after the correction.

This finding was established by reading the original preview and running the expected behavior on the two references. Astra did not execute the faulty preview and does not claim an observed wrong diagnostic from that binary. The correction was reviewed in the exported patch; Fable owns the compiler change and its full gate chain.

A separate hosted getter/reference-mutation control returned `12`, with final referenced value `10`, on both backends with proofs enabled and disabled. It did not expose a divergence and is retained as a negative control.

## Deferred getter controls on landed row 102

A frame slot can contain a PROPERTY reference. Dereferencing it calls its getter, which can throw. Proving that an operand is a local slot is therefore insufficient to move it after a potentially failing element read.

Astra built a host against frozen `4bd53021` and ran five shapes on both backends, with proofs disabled and enabled: 20 runs. `Q` contains two elements, `i` is 9, and `k` is 0. The host arms the property getter after the reference is bound; touching the property then throws `OUTER_GETTER_FIRST`.

| Shape | Interpreter, proofs off/on | VM, proofs off | VM, proofs on |
|---|---|---|---|
| `p[k + Q[i % 16]]` | Getter first | Getter first | Getter first |
| `p[Q[i % 16]]` | Getter first | Getter first | **Inner bounds error** |
| `n + Q[i % 16]` | Getter first | Getter first | **Inner bounds error** |
| `p[k] = Q[i % 16]` | Inner bounds error | Inner bounds error | Inner bounds error |
| `p[n] = Q[i % 16]` | Inner bounds error | Inner bounds error | Inner bounds error |

Here `p` is an array property reference in the first, second and fourth rows; `n` is a scalar property reference in the third and fifth. The last row uses a plain array for `p`. The bounds diagnostic is exactly:

```text
Array index 9 out of bounds for array of size 2: Array index out of bounds
```

The two bold outcomes are confirmed divergences in the landed row 100 lowering. The first row establishes the oracle for the additional row 103 shape, which was not yet present in the frozen library. The assignment rows are negative controls: their right-hand side is evaluated before dereferencing the target. A fix must preserve that distinction instead of applying a blanket left-to-right rule to assignment.

Fable owns the correction. The proposed direction is to materialize earlier-evaluated reference operands once before hoisting a later read, retaining the result in a compiler temporary. These results specify which operands are actually earlier; they do not authorize evaluating an assignment target before its right-hand side. No syntax or conversion change is required by these counterexamples.

## Evidence

All paths are relative to `Source/JaiScript/`:

- `out/astra/row102-audit-20260909/review103/missing-outer-before-inner.jai`
- `out/astra/row102-audit-20260909/review103/oracle-results.json` and four runner logs
- `out/astra/row102-audit-20260909/review103/fixed-row103_preview.patch`, `fixed-NOTE.md`, `fixed-STAT.txt`
- `out/astra/row102-audit-20260909/review103/evidence-sha256.json`
- `out/astra/row102-audit-20260909/review103/reference-order-matrix.cpp`, `.log` and `.json`: the 20-run current-master matrix
- `out/astra/row102-audit-20260909/load-gauge/getter-order.cpp` and `review103/getter-order.log`

No timing result is inferred from these controls. No runtime-owned live file was edited by Astra.
