# Call-site callee identity (VM)

How the VM resolves a bare-identifier callee (`fib(n - 2)`), what guards a cached
resolution, and the condition under which the probe instruction can leave a site.
The interpreter is unaffected: it resolves the callee at the same observation point
and stays the oracle.

## The probe pair today

An identifier callee compiles to `PROBE_CALLEE site, argc`, the argument code, then
`CALL_FROM_SCRATCH argc, site`. The probe runs at the C++17 observation point (callee
before arguments, `internal/invariants.md` 12): it resolves the name and parks the result in
the pending-callee register. The call pops the register and enters the callee. An
argument that rebinds the name cannot change which function the call enters, and a
callee that does not resolve raises before any argument runs.

The probe's fast lane is one guard over three facts:

| fact | check | what it pins |
|---|---|---|
| lookup context | `env_lookup_cache[ip*3].fast_env == environment_` | the scope chain the frame resolves through |
| shape | `fast_serial == env_epoch()` | no define, environment reset or reparent since the entry was filled |
| binding | `fast_ptr` holds a function whose identity is `site.ic_identity` | the cell's current value is the armed function |

The fast lane also requires a frame with no active member scope (a method's fields and
methods shadow globals), and no slot-resolved callee.

## Rebinding routes and what catches them

| route | effect | caught by |
|---|---|---|
| `fib = other` (any store into the global cell) | value replaced in place | binding identity |
| hot reload or a re-run of the defining unit | function replaced in place | binding identity |
| import re-run of a module defining the name | the module's top level runs again, replacing in place | binding identity |
| another unit declares the name (unit shadow) | a new cell is defined | shape epoch (define) |
| block-scope or `inline`/`extern` definition | define in a scope environment | shape epoch, lookup context |
| host `add_global` / engine global set | define or value replace | shape epoch or binding identity |
| engine reset, a new program | environments cleared | shape epoch |
| method frame whose class declares the name | member scope active | member-scope gate (general probe) |
| a local, parameter or capture of the name | slot-resolved callee | never takes the fast lane |
| parallel worker | `cached_global_env_` is null on workers | general probe |

Every rebinding route already invalidates the probe's fast lane through these three
checks. No new epoch is needed to guard a call-time resolution.

## Eliding the probe

Without the probe, the site resolves the callee at the call instruction, after the
arguments. When the guard holds at the call, no rebinding happened since the cache was
filled, so the virtual probe point would have seen the same function, and the direct entry
is exact. When the guard fails, the call resolves the name at that point and takes the
general path. That matches the probe only when no argument can

1. run user code: a getter, an operator method, a call, a destructor, an assignment that
   rebinds the name. If one could, the call would see the rebinding while the probe would
   not; or
2. raise. If the name is rebound to a non-callable, the probe raises "not callable" before
   any argument runs. An argument that raises first, such as an integer overflow, a type
   mismatch on a `var` operand, a null receiver or a removed-field reference, would report
   its own error instead.

The arguments that satisfy both are literals, constants and reads of by-value frame
slots, which hold values or plain cells and never a field, element or property reference.
Arithmetic fails condition 2: checked integer overflow raises, and a `var` operand can raise
a type error. So `fib(n - 2)` keeps its probe under exact semantics. Calls whose arguments
are plain locals (`merge(lower, equal, tmp)`) qualify.

A site that elides its probe must also stay translatable by the cell-block translation,
the inliner and the coverage census, which recognize the probe pair today.
