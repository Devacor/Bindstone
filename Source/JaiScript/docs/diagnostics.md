# Runtime diagnostics

Every runtime error text is byte-identical on both backends (the interpreter and the vm raise
through the same kernels or through byte-parallel twins). This catalog lists the texts a script
can observe (`catch (e)` sees the text alone; an uncaught error's `what()` appends the error
code's category message, `<text>: <category>`), grouped by family. Four families were redesigned
in one pass (null receiver, "no member", arity/overloads, typed boundary); the rest are listed
with a one-line recommendation each and are otherwise untouched.

Conventions the redesigned families follow (the static checker's voice, with the runtime's
capitalized first word):

- names are quoted (`'x'`, `'fib'`, `'Vec2.dot'`), types are not (`int`, `string`, `Vec2`,
  `array<int>`);
- the text leads with the action that failed (`Cannot call`, `Cannot pass`, `Cannot assign`);
- the slot is named whenever the site knows it; a site that cannot name it uses the slot-less
  shape and never invents a name;
- numbers render as numbers (arity counts ride the pre-interned count table);
- error paths intern nothing new while the symbolizer is frozen (a parallel region): a composed
  description (`parameter 'n' of 'fib' (declared int)`, `(int, string)`) is interned only on
  the main thread; a worker gets the slot-less / list-free shape of the same text.

Symbol arguments: `{0}`/`{1}` are the two `checked_result` symbol slots. Base-type spellings
are interned once at backend init (`base_type_symbols`), arity counts once at init
(`arity_count_symbols`), names come from parse-time symbols, class names from their definitions.

## Engine privileges

The engine owns the grants; script-visible flags and `reflect::is_enabled` do not
authorize an operation. Protected operations share `engine::require_privilege`.

| Text | Meaning |
| --- | --- |
| `Insufficient privilege: filesystem.` | A filesystem operation or location query was refused (`insufficient_privilege`). Catchable; an unhandled error reaches the host. |
| `Insufficient privilege: {name}.` | A binding requires a disabled custom privilege (`insufficient_privilege`). The name uses its engine-interned symbol; no interning or allocation in the checked error path. |
| `reflect::is_enabled expects at least 1 argument` | The query needs one or more flag or name arguments. |
| `reflect::is_enabled expects valid privilege flags` | An argument is neither a name string nor an integer flag set, or contains unimplemented bits. |
| `Invalid privilege flags` | A host grant/requirement request contains unimplemented bits. |
| `Unknown privilege name` | A host tries to grant, revoke, require, or attach an unregistered name. Checked require calls return an error; boolean queries return false. Neither operation interns or registers an unknown name. |
| `Privilege name must not be empty` | Host registration needs a nonempty name. |
| `Cannot change privileges during a parallel region` | A host attempted to register a privilege or modify grants before the region's workers joined. |

Grant changes apply to future operations, not work already started. Configure on
the engine thread (including a reentrant C++ callback) or while idle, never
concurrently from another thread. Grants use plain engine-owned bits, not atomic
or globally shared state. Automatic cache access and checker dependency discovery
are skipped when filesystem is disabled; explicitly requested file/path operations
raise the refusal above. Host-supplied source and bytecode bytes remain usable.

## Family A - null receiver (`member_access_on_null`, kernel `detail/null_member_diagnostic.hpp`)

| receiver shape | before | after (access) | after (call) |
|---|---|---|---|
| named (`v8.push`) | `Member 'push' accessed on null: 'v8' is null` | `Cannot access 'push' on null ('v8' is null)` | `Cannot call 'push' on null ('v8' is null)` |
| call result (`f().x`) | `Member 'x' accessed on null: 'f(...)' returned null` | `Cannot access 'x' on null ('f(...)' returned null)` | `Cannot call 'x' on null ('f(...)' returned null)` |
| unnamed (`a[i].x`, `(c ? a : b).x`) | `Member 'x' accessed on null value` | `Cannot access 'x' on null` | `Cannot call 'x' on null` |

Call vs access: the interpreter's generic call path marks the callee lookup
(`member_callee_lookup_`, consumed at `visit_member_expr` entry); the vm's `op_probe_member`
and `exec_call_method` pass `as_callee` to `member_access_value`. Uncaught: the category suffix
stays (`...: Member access on null value`); the message no longer restates "member".

Sites: `interpreter.cpp` visit_member_expr; `vm_backend.cpp` exec_probe_member,
member_access_value.

## Family B - "no member" (`member_not_found` / string exceptions)

| receiver | before | after |
|---|---|---|
| int/float/bool/char/function | `int has no member 'nosuch'` | unchanged |
| string | `String has no method 'foo'` | `string has no member 'foo'` |
| array | `Array has no method 'foo'` | `array has no member 'foo'` |
| map | `Map has no method or key 'foo'` | `map has no member or key 'foo'` |
| weak_ptr | `weak_ptr has no method 'x'` | `weak_ptr has no member 'x'` |
| coroutine_handle | `coroutine_handle has no member 'x'` | unchanged |
| object of class Vec2 | `Object has no member 'x'` | `Vec2 has no member 'x'` (`object has no member 'x'` when no class is known) |
| `super.x` | `Parent class has no method 'x'` | `parent class Base has no member 'x'` |
| `C::s` | `Class 'C' has no static member 's'` | `class C has no static member 's'` |
| store to a missing member | `Cannot assign to non-existent member 'x'` | `Cannot assign to 'x': Vec2 has no such member` |
| store to a read-only C++ property (binder `.property()` with a getter only / a `nullptr` setter / a member pointer whose type is not copy-assignable, e.g. a signal), or to a host-registered namespace entry (`string::to_lower = v`; `docs/namespaces.md`) | (silent no-op; `Class 'string' not found` for the namespace entry) | `Cannot assign to 'value': it is read-only` - every spelling: `obj.value = v`, `this.value = v`, a bare `value = v` in a hybrid method, compound and `++`/`--` (the getter and the arithmetic run first), stores through a PROPERTY reference (`auto& r = value; r = v`, `f(value)` into a `T&` parameter), `reflect::set`; a script `_set_value` override or a same-named script field in the chain takes the store instead; serialization loads (`from_json`, the binary archive) keep discarding the setter's result and skip silently. Uncaught, `what()` appends `: Invalid assignment target` except on the reference-store path, which throws the text alone like its family's `Reference to a removed field`. The static checker reports the same mistake at load on a closed class (`cannot assign to read-only property 'value'`, `docs/static_checking.md` Members) |
| mutating or aliasing a map KEY through a range-for pair (`for (auto kv : m)` / `for (auto& kv : m)` over a statically known map, C++ `pair<const K, V>`: a string mutator on `kv.first` - `kv.first.to_upper()`, `insert`, `erase`, ... (`k_string_mutator_method_names`) - a store `kv.first = v`, compound `kv.first += "hey"`, `++`/`--`; or a mutable alias: `var& cheeky = kv.first` / `auto&` / `T&`, `shared_ptr<var> cheeky = kv.first` / `shared_ptr<auto>` / a store into a shared_ptr-declared variable, `cast<T&>` / `cast<shared_ptr<T>>` of the key, `kv.first` passed where every viable same-script overload takes `T&` or `shared_ptr<T>`, `return kv.first` from a `-> T&` / `-> shared_ptr<T>` function; the marker follows the pair through `var p = kv` / `auto& p = kv` and a ternary over keys) | (the key node was rewritten in place: `{"A":1}`, lookups and the ordering invariant broken) | STATIC ONLY: the checker rejects the load under strict (warn mode prints it) with `cannot mutate 'kv.first': map keys are read-only` for the mutation spellings and `cannot alias 'kv.first': map keys are read-only (copy it instead: var k = kv.first)` for the alias spellings (`a map key` replaces the spelling when the operand is a ternary; `docs/static_checking.md`). The runtime raises nothing: strings are copy-on-write at the in-place write gate (`docs/invariants.md` 11), so the copy (`var okay = kv.first`) and the paths the checker cannot see (`var m`, `m.keys()[0].to_upper()`) mutate the copy alone and the map stays intact - `first` is a shared handle in both bindings, no per-iteration copy |
| calling a plain value (`obj.x(...)`, `f(...)` with `f = 5`) | caught: `` (empty); uncaught: `Cannot call non-function value` | `Cannot call 'x': it is an int, not a function` (member or identifier callee); `Cannot call a value of type int: it is not a function` (computed callee: `fs[0](x)`, `pick()(x)`) |

The callee's type spells the class name for class instances (`it is a Vec2`), the base type
otherwise; the article follows the spelling (`an int`, `a string`); a null callee reads
`Cannot call 'f': it is null, not a function` / `Cannot call null: it is not a function`.
Uncaught, the not-a-function code keeps its category suffix (`...: Cannot call non-function
value`). `reflect::` keeps its own host-API texts (`Class 'C' has no method 'm'` in
`stdlib/reflection.hpp`), outside this pass.

Sites: `interpreter.cpp` visit_call_expr (method fast path + generic callable check),
visit_member_expr, assign_member_value; `vm_backend.cpp` exec_probe_callee, exec_probe_member,
exec_call_from_scratch, exec_call, exec_call_method, member_access_value, exec_get_static,
assign_member; `interpreter.cpp` static access. The read-only text lives ONCE: the binder
registers a rejecting `_set_<name>` for a getter-only property (`dynamic_binder.hpp`
`register_read_only_setter`; the field id is interned at registration, nothing on the error
path), and every store landing on both backends already funnels to that setter
(`member_scope.hpp` write_cpp_property, assign_member_value / assign_member's user_accessor
call, `value.cpp` store_property for PROPERTY references, `reflection.hpp` reflect::set), so
no backend site names the text. `has_property_setter()` / `get_property_setter_id()` answer
non-zero for a read-only property (the rejection IS the setter); `is_read_only_property()`
is the writability question.

## Family C - arity and overloads (`argument_count_mismatch`, `no_constructor_found`)

| case | before | after |
|---|---|---|
| script function / method / coroutine | `Function expected 1 arguments but got 2` | `'fib' expects 1 argument, got 2` (methods by name: `'dot' expects 1 argument, got 0`) |
| lambda | `Function expected 1 arguments but got 0` | `lambda expects 1 argument, got 0` |
| same, on a parallel worker | (same) | `'fib' expects 1 argument` / `lambda expects 2 arguments` (no composed detail) |
| script method overloads, none viable | `No matching overload found` | `No overload of 'dot' takes (int, string)` (worker: `No overload of 'dot' matches this call`) |
| method whose overload list vanished (reload) | `No method overloads found` | `No overload of 'dot' exists` |
| host function overloads (`engine.cpp` dispatchers) | `No matching overload found for 'f' with <symbol#2> arguments` (the count was passed as a symbol id) | `No overload of 'f' takes (int, string)` |
| host `[]` overloads | `No matching overload found for '[]' with <symbol> arguments` | `No overload of '[]' takes (string)` |
| namespace function overloads | `No matching overload in namespace 'ns' for <symbol> arguments` | `No overload of 'f' takes (int)` |
| script constructor overloads | `No constructor found with matching arguments` | `No constructor of 'Vec2' takes (string)` |
| `: Base(...)` / ancestor initializer | `No matching constructor for ancestor class` | `No constructor of 'Base' takes (string)` |
| `: this(...)` delegation | `No matching constructor found for this() delegation` | `No constructor of 'Vec2' takes (string)` |
| C++ method overloads by count | `Method argument count mismatch: no overload accepts this number of arguments` | `No overload of 'dot' takes (int, string)` |
| C++ method overloads by type | `No method overload matches the argument types` | `No overload of 'dot' takes (int, string)` |

The argument list spells the payload's type per argument (class names for objects, `null`,
`array`, `map`, the primitives), at most 6 listed then `...`. Class qualification of method
names (`'Vec2.dot'`) was NOT adopted: the vm's arity site (`call_script_function`) has no class
context, so both backends name the method alone (byte parity).

Untouched in this family (builtin arity texts are their own consistent shape):
`push() takes exactly one argument`, `size() takes no arguments`, ... (`{name}() takes ...`,
~90 texts in `interpreter.cpp`'s builtin registry, shared by both backends),
`math::pow expects 2 arguments` (`math_intrinsics.hpp`), `weak_from_this() takes no arguments`,
`Constructor parameter count mismatch`, `Default constructor takes no arguments`,
`No constructor found for class 'C'` (the class has no constructor at all - a different fact),
`Coroutine has no function to run`.

## Family D - typed boundary (`type_mismatch`, `array_element_type_mismatch`)

One conversion table (`detail/typed_slot.hpp`), one text kernel
(`class_definition.hpp` `detail::typed_slot_rejection`), the site names the slot.

| site | before | after | slot-less shape (worker / unnamed site) |
|---|---|---|---|
| by-value parameter | `Type mismatch: expected int but got string` | `Cannot pass string to parameter 'n' of 'fib' (declared int)` (`of lambda` for lambdas) | `Cannot pass string to an int parameter` |
| typed return | `Type mismatch: expected int but got string` | `Cannot return string from 'f' (declared -> int)` | `Cannot return string from a function declared -> int` |
| `=` / compound | `Type mismatch in assignment` | `Cannot assign string to 'x' (declared int)` | `Cannot assign string to a variable declared int` |
| declaration | `Type mismatch in assignment` | `Cannot initialize 'x' (declared int) with string` | `Cannot initialize a variable declared int with string` |
| instance field store (script, `set_field` host store) | `Type mismatch in assignment` | `Cannot assign string to field 's' of Vec2 (declared int)` | `Cannot assign string to a field declared int` |
| static field store | `Type mismatch in assignment` | `Cannot assign string to static 's' of C (declared int)` | `Cannot assign string to a static declared int` |
| class into class at `=` / field | `Incompatible class types` | the named shape (`Cannot assign B to 'x' (declared A)`) | as above |
| handle variable (`weak_ptr<T> w`, `shared_ptr<T> p`) at declaration / `=` | `Cannot initialize weak_ptr<{}> from value-semantic object: use shared_ptr<T>`, `Cannot initialize weak_ptr<{}> with {}: use shared_ptr<T>`, `Cannot initialize weak_ptr<{}> from shared_ptr<{}>: type must match or be a subclass`, `Cannot assign value-semantic object to weak_ptr<{}>: use shared_ptr<T>`, `Cannot assign {} to weak_ptr<{}>: use shared_ptr<T>`, `Cannot assign shared_ptr<{}> to weak_ptr<{}>: type must match or be a subclass` (the `{}` placeholders never rendered), `Cannot assign any to shared_ptr<Foo>: no operator=({0}) defined` (a var-held payload spelled `any`, the second `{0}` never rendered) | the declaration / `=` shapes with the payload's type: `Cannot initialize 'w' (declared weak_ptr<Foo>) with Foo` / `with int` / `with Bar` (a `shared_ptr<Bar>` payload spells its class, like every handle source), `Cannot assign Foo to 'w' (declared weak_ptr<Foo>)`, `Cannot assign string to 'p' (declared shared_ptr<Foo>)` (no `operator=(string)` on the pointee); the field and parameter rows already read this way | `Cannot initialize a variable declared weak_ptr<Foo> with Foo`, `Cannot assign string to a variable declared Foo` (the kernel's slot-less shape spells a shared_ptr target by its pointee) |
| class conversion at parameter / return | `Cannot convert B to A (no suitable single-argument constructor)` | unchanged (checked once: it names both classes and the missing conversion) | - |
| container element boundary (every site) | `Cannot store 'string' in 'array<int>'` | `Cannot store string in array<int>` (types unquoted, like the rest of the family) | - |

Hot-reload migration never raises (an unconvertible value falls back to the new default), so
no text there. `value.cpp`'s two `Type mismatch in assignment` throws are unreachable defensive
walls behind typed-buffer references and stay.

## Compound assignment (`type_mismatch`, kernel `class_definition.hpp` `detail::compound_mismatch`)

`Cannot apply '+=' to {0} and {1}` (likewise `'-='`, `'*='`, `'/='`, `'%='`)

`{0}` is the target's type and `{1}` the operand's, spelled like family D's source (the
payload's own type: a class name for an object, `null`, `array`, `map`, the primitives). Every
compound site raises it when the operator does not take the operands: a local or a global, a
field through a receiver (`c.s += 5`) or bare in a method, a static (`C::i += "x"`), a bound C++
variable. `string s; s += 5` is `Cannot apply '+=' to string and int` (a string target appends
text only - a string, a char, a utf8_string; `string += int` raises by design, unlike binary
`+`), `float f; f += "x"` is `Cannot apply '+=' to float and string`. Catchable, identical on
both backends; uncaught, `what()` appends `: Type mismatch`. `++`/`--` on a target that is not a
number (any spelling: a variable, a member, an element, a bare field) is `Cannot apply '++' to
string` / `Cannot apply '--' to bool` (`invalid_numeric_operand`, `detail::incdec_mismatch`),
which a `catch` used to see as an empty string. An element or map entry target
(`names[i] += 5`) evaluates `names[i] + 5` and stores it, so a string element concatenates.
Before: the variable and static sites raised a bare `Type mismatch` (a `catch` saw an empty
string), the member sites `Invalid operands for +=`, and a float target with a non-numeric
operand escaped every `try` as a C++ exception (`script_value is not a float or integer`).

## Checked float-to-integer conversion (`invalid_numeric_operand`)

`Cannot convert float to integer: non-finite or out of range`

One checked kernel (`detail/numeric_conversion.hpp`) owns the range check and text
for scalar typed slots (declarations, assignments, fields, parameters, returns and
`to_int()` results), `cast<int>`, `math::itrunc`, `math::ifloor`, and floating-point
`math::random_seed` arguments. These errors are catchable and identical on both
backends; a failed conversion does not write the scalar destination or seed the RNG.

Signed conversion truncates toward zero and requires a finite result in
`[-2^63, 2^63)`. `ifloor` floors first. Integer inputs to the intrinsics remain
exact. Floating RNG seeds retain their unsigned range: their truncated value must
be in `[0, 2^64)`. Integer seeds retain their existing modulo-2^64 interpretation.
The upper bounds are exclusive because converting either maximum integer to a
double rounds up. These checks apply even in a wrapping integer-arithmetic build.

This is a scalar/script-boundary change, not a redefinition of every host or
container conversion. The public C++ `script_value::as_int()` accessor retains its
existing saturating policy (NaN to zero).

## Reserved engine namespaces (`type_mismatch`)

`Namespace '{0}' is reserved for the engine (C++-registered only)`

`{0}` names the rejected namespace. Script declarations cannot open `reflect`,
`jai`, or their root-qualified subnames (`reflect::sub`, `jai::sub::deep`). The
shared `core/script_namespace.hpp` predicate and checked error kernel are used by
both backends, including execution of saved programs and with static checking off.
Rejection happens before namespace members or their initializers run. Ordinary
uncaught `what()` appends `: Type mismatch`. This replaces the old parenthetical
`(reflect:: is C++-registered only)` with wording valid for both reserved roots.

The host may register values/functions in these namespaces. Names that merely
start with the same letters (`jailed`, `reflector`) and unrelated qualified names
such as `outer::jai` are not reserved; `math` remains open to script extension.

## Errors in script the runtime calls

Script that a builtin, an operator or an archive runs on the program's behalf raises
its own errors: a `sort` comparator, a `filter`/`remove_if` predicate, a
`parallel_for` body, a user operator (`operator+`, `operator<`, `operator==`,
`operator=`, compound forms), `to_bool()` under a condition or a `bool` slot,
`to_string()` under string `+`, and a property accessor or `post_load` that the
script declares, run by `to_json`, `to_binary`, `from_json` or `from_binary`. The
error leaves the builtin or operator with its own text, catchable like any runtime
error, and an uncaught one ends the program with the callback's frames in the trace
(`at inner`, `at <lambda>`, `at <script>`). The builtin makes no further calls after
the first failure, and `sort` leaves the array as it was. Truthiness has no error
value, so a `to_bool()` error travels as the script exception its text would throw.
An archive tolerates only a store the member itself refuses: a read-only host
property, or a value the synthesized field store rejects, leaves the member at its
constructed value.

## Order of errors in an assignment

C++17 [expr.ass] (Dev ruling 2026-09-27): in `E1 = E2` and `E1 op= E2` every value computation and
side effect of `E2` comes before `E1`. For a member target (`obj.f`, `this.f`, `C::s`, `ns::x`) the
right side runs first, then the receiver, the member's read (a user getter included), the
arithmetic and the store. For a subscript target (`a[j] = e`, `a[j] op= e`) the value runs
first, then the container, the index and the element's read. An error in the right side is the
one raised, and the target is never evaluated (`nv.f += bad()` with a null `nv` and `a[5] += bad()`
past the end both raise `bad()`'s error, not the receiver's or the index's).

## Call-depth limits (`max_recursion_depth`)

`Maximum recursion depth exceeded - possible infinite recursion`

The shared `detail/execution_limits.hpp` kernel supplies this number-free text for
both backends, whether the engine-owned call-depth cap or a Windows native-stack
headroom check fires. Recursion errors are catchable. The ordinary uncaught
`std::system_error::what()` adds `: Maximum environment recursion depth exceeded`;
a native callback boundary that wraps a C++ exception may additionally append
`: C++ exception`. These boundary suffixes do not change the shared runtime text.

Default caps are 256 active calls for the interpreter and 10000 for the VM.
`engine::max_call_depth(backend_type, depth)` sets either cap between executions;
`engine::max_call_depth(backend_type) const` reads it. Values must be in
`[1, INT_MAX]`. Each engine owns its settings, and parallel workers snapshot them
at region entry. An explicitly supplied legacy `JAI_MAX_CALL_DEPTH` build macro
seeds both defaults; the host can still override either one. Raising a call-depth
cap does not disable the independent Windows native-stack guard. VM self tail
calls that reuse a frame consume no additional depth; the execution budget still
applies.

Host configuration errors from `engine.cpp` are `jai::runtime_error` exceptions:

| Text | Rejected configuration |
| --- | --- |
| `max_call_depth requires a built-in backend type` | A setter or getter backend other than interpreter or VM |
| `max_call_depth must be between 1 and INT_MAX` | A zero or oversized setter value |
| `Cannot change max_call_depth during execution` | A setter call while the engine is executing or a parallel region is active |

## Saved-program structural validation

The shared `.jaibite` reader rejects malformed structure before AST marking,
static checking or VM compilation. Public loader failures are catchable
`jai::runtime_error` exceptions; both backends use the same `jaibite: ` prefix
and text. The following checks do not change the encoded format:

| Text | Rejected structure |
| --- | --- |
| `jaibite: invalid program: local count exceeds encoded storage` | Impossible frame allocation claim or a count outside VM slot operand range |
| `jaibite: invalid program: local slot out of range` | A local/captured slot outside its recorded lexical frame |
| `jaibite: missing required block statement node` | Missing function, lambda, parallel, try or catch body |
| `jaibite: expected block statement node` | A non-block node where one of those bodies requires a block |
| `jaibite: invalid program: for initializer must be a variable or expression declaration` | Invalid traditional-for initializer kind |
| `jaibite: invalid program: class member must be a variable or function declaration` | Invalid class-member kind |
| `jaibite: invalid program: parallel parameter slot does not match its position` | Noncanonical/duplicate loop-parameter slots |
| `jaibite: invalid program: inconsistent case fallthrough` | Fallthrough marker and terminal statement disagree |
| `jaibite: invalid program: type parameter count` | Malformed built-in type arity, checked before interning can discard extra parameters |
| `jaibite: corrupt varint` | Overlong or overflowing encoded integer |
| `jaibite: AST nesting too deep` | AST traversal exceeds the existing 1000-level limit, or Windows native-stack headroom is low |
| `jaibite: type nesting too deep` | Type traversal exceeds the existing 1000-level limit, or Windows native-stack headroom is low |

Inferred/null type arguments remain permitted. Saved capture plans and the
`SIZE_MAX` environment-lookup sentinel are not rewritten by validation.
The nesting diagnostics also apply while saving. They are independent of the
parser's weighted recursion limit of 250, which is not an AST-depth limit or a
complete native-stack guarantee; recursive assignment and type parsing are not
covered by that parser counter.

## Binary data

One catalog for `to_bytes`, `string::from_bytes` and the `binary::` library (`detail/binary_data.hpp`), byte-identical
on both backends and catchable. A read or write that raises moves nothing: the reader's cursor stays, the writer's
bytes stay.

| site | texts |
|---|---|
| `s.to_bytes(offset, count)` | `to_bytes(4) out of bounds for string of size 3`, `to_bytes(1, 3) out of bounds for string of size 3` (a negative offset or count too; a parallel worker, whose symbolizer is frozen, gets `to_bytes() range out of bounds`), `to_bytes() arguments must be integers`, `to_bytes() takes 0 to 2 arguments ([offset], [count])` |
| `string::from_bytes(bytes)` | `string::from_bytes expects bytes (0 to 255): element 3 is 256` (the element's value, or its kind: `... element 1 is string`), `string::from_bytes expects an array of bytes, got int`, `string::from_bytes expects exactly 1 argument` |
| reader reads | `Cannot read int32 at position 10: 4 bytes wanted, 2 remain` (`1 remains` for one), `Cannot read text(5) at position 0: 5 bytes wanted, 3 remain`, `Cannot read text_null_terminated(8) at position 0: 8 bytes wanted, 3 remain`, `Cannot read bytes(-1): a count cannot be negative`; a layout field names itself: `Cannot read field 'pos' (int32) at position 4: 4 bytes wanted, 2 remain` |
| C-string scans (`text_null_terminated()`) | `Cannot read null-terminated text at position 1: no NUL terminator in the remaining 2 bytes` (the end of the data), `Cannot read null-terminated text at position 0: no NUL terminator within 1048576 bytes (the engine's binary scan limit; raise it from C++ with engine->binary_scan_limit)` |
| cursor | `Cannot seek to position 4: the data is 3 bytes`, `Cannot skip 2 bytes at position 2: the data is 3 bytes`; the writer: `Cannot seek to position 2: 1 byte written` |
| writer writes | `Cannot write 256 as uint8 (0 to 255)`, `Cannot write 1e+300 as float32 (-3.4028234663852886e+38 to 3.4028234663852886e+38)`, `Cannot write bytes: element 1 is 300, not 0 to 255`, `Cannot write text(s, -1): a count cannot be negative`, `Cannot write text_null_terminated: the text holds a NUL at byte 1`, `Cannot write text_null_terminated(s, 0): the field needs 1 byte for its NUL` |
| layouts | `binary::layout expects name, width pairs, got 3 arguments`, `binary::layout argument 1 must be a field name string, got int`, `binary::layout argument 2 must be a binary width (binary::int32, binary::text(8), ...), got int`, `binary::layout repeats the field name 'a'`, `binary::text expects a length of 0 or more, got -1`, `binary::text_null_terminated expects a length of 1 or more (room for the NUL), got 0`; the inline form spells `unpack` in place of `binary::layout` |
| `w.pack(layout, map)` | `Cannot pack field 'pos': the map has no 'pos'`, `Cannot pack field 'pos': cannot write 4294967296 as int32 (-2147483648 to 2147483647)`, `Cannot pack field 'name': int does not fit text(4)`, `Cannot pack field 'n': the text holds a NUL at byte 1` (a `text_null_terminated(n)` field), `pack expects a binary::layout, got int`, `pack expects a map of field values, got int` |
| `r.read(Class, layout)` | `Cannot read One from a layout of 2 fields: One has 1 field`, `Cannot read field 'name' (text(4)) into Two.name (declared int)`, `read expects a class, got int`, `read expects a script class, got binary::writer`, `read expects a binary::layout, got int` |
| `binary::reader(x)` | `binary::reader expects a string, got int` |

The strict checker reports the same facts at load where it can see them (an odd pairing, a non-width, a `read` whose
class and layout are both known, a strict signature's arity and argument kinds): `binary::layout argument 2 must be a
binary width ...`, `Cannot read layout field 2 into Bad.pos (declared float)`, `'binary::reader.int8' expects 0
argument(s), got 1`, `no overload of 'binary::reader.seek' accepts these argument types`.

## Host file reads

The runner's `read_file(path)` (and the crawler host's) raises one shape for a file it cannot hand back,
catchable on both backends: `Cannot read file 'saves/world.gqw': not found`, with `not a file` (a folder),
`permission denied`, `cannot be opened` or `read failed` in place of `not found`. An existing empty file
reads as `""`; absence is asked with `file_exists(path)` first.

## Untouched families (one-line recommendation each)

| family | representative texts | recommendation |
|---|---|---|
| undefined names | `Undefined variable 'x'`, `Undefined variable 'x' (no 'this' in scope)`, `Undefined variable 'x' (not a field of 'this')`, `Undefined identifier 'x' in class 'C'`, `Class 'C' not found`, `Base class 'B' not found` | keep; consistent and named |
| references | `Cannot take reference of undefined variable`, `Cannot take reference of non-lvalue expression (...)`, `Cannot pass non-lvalue to reference parameter`, `Reference target is null`, `Invalid reference in assignment`, `Reference variable must be initialized`, `Cannot return reference to 'int' from a function returning 'string'`, `Function with reference return type must return an lvalue reference`, `Cannot iterate a string by reference: its chars are not lvalues (use for (auto c : s))` (`for (auto& c : s)`, raised before the first element, `detail/range_for_diagnostic.hpp`) | name the variable / parameter where the site has it (`Cannot take reference of undefined variable 'x'`) |
| non-object stores | `Cannot assign to member of non-object type`, `Cannot assign property to non-object value` (two texts for one fact, split by backend path) | fold into one `Cannot assign 'x' on int: not an object` shape naming the member and type |
| element / key boundary | `Cannot assign 'string' to element of type 'int'`, `Cannot assign 'S' to field of type 'S'`, `Cannot push 'string' to array<int>`, `Cannot use 'int' as a key of type 'string'` (the source is the payload's type: a var-held `Box` is `Box`, never `any`; `detail::container_value_type_name`) | unquote the types to match family D (`Cannot push string to array<int>`) |
| handles | `Cannot assign shared_ptr<A> to shared_ptr<B>`, `Cannot assign weak_ptr to shared_ptr - use weak.lock() instead`, `Cannot initialize shared_ptr with this type`, `Cannot initialize shared_ptr<T> with X: type must match or be a subclass`, `Cannot infer shared_ptr<auto> pointee ...`, `Cannot create weak_ptr from ...`, `weak_ptr type mismatch: ...` (handle VARIABLES at declaration / `=` are family D's handle-variable row) | route the rest through family D's slot shapes (`Cannot assign shared_ptr<A> to 'p' (declared shared_ptr<B>)`) |
| `auto` container inference | `Array 'auto' requires homogeneous elements - type mismatch at index 3`, `Array 'auto' requires homogeneous class types - mismatch at index 3`, `Map 'auto' requires homogeneous values - type mismatch at index 3`, `Map 'auto' requires homogeneous class types - mismatch at index 3` (the index rides the pre-interned count table; past 63 it is interned on the main thread, and a worker gets the index-free lead, `... - type mismatch`; texts in `detail/arity_diagnostic.hpp`), `Map 'auto' requires homogeneous keys`, `Map 'auto' requires homogeneous values` | keep; name the two types seen (`int at 0, string at 3`) |
| iteration | `Cannot iterate int: not an array, map, string or coroutine` (range-`for` over anything else; the type spelled like family D's source - `Foo`, `null`, `float`; `detail/range_for_diagnostic.hpp`), `'pair' type not registered - make sure stdlib is loaded` | keep |
| arithmetic | `Division by zero`, `Division by zero in integer operation`, `Modulo by zero in float operation`, `Integer overflow in '/'`, `Invalid operands for + operator`, `Cannot compare: the values nest deeper than 200 levels` (memberwise `==` safety cap), `Left operand must be numeric`, `Bitwise & requires integer operands`, `Shift amount must be between 0 and 63`, `Unary minus requires numeric operand` | `Invalid operands for + operator` should name the operand types (`Cannot apply + to string and map`); the rest keep |
| indexing | `Array index 5 out of bounds for array of size 3`, `Array index must be an integer`, `String index must be an integer`, `Strings are read-only through subscript: ...`, `Subscript can only be used on arrays, maps, or types with [] operator`, `at() index out of bounds`, `Cannot pop from empty array`, `Cannot get front of empty array`; a compound / `++` / `--` whose subscript read yields no lvalue (an rvalue base `f()[i] += 1`, a string `s[i] += 1`, a missing map key `m[k] += "s"`) raises `Cannot assign to rvalue expression` after the arithmetic, both backends (the interpreter used to re-dispatch a synthetic `=` there: the read-only-string text, or a silent insert) | keep (named and specific) |
| builtin argument checks | `find() first argument must be a string`, `sort() comparator must be a function`, `pad_left() padding character cannot be empty`, `to_int() base must be 0 or between 2 and 36` | keep; consistent `name() ... must be ...` shape |
| strings and UTF-8 | `code() needs a string of exactly one UTF-8 character`, `partition() separator cannot be empty`, `split() second argument must be an integer`, `string::from_code expects a Unicode code point (0 to 0x10FFFF, no surrogate)`, `string::join expects an array of strings`, `string::repeat count cannot be negative`, `string::pad_left fill must be a char or a non-empty string`; an unknown namespace member names itself: `Undefined variable 'string::join'` (a namespace with no such entry and no class of the namespace's name); a utf8_string names its kind: `utf8_string has no member 'to_utf8'`, `Cannot iterate a utf8_string by reference: its characters are not lvalues (use for (auto c : s))`, and shares the string subscript texts (`String index 4 out of bounds for string of size 4` counts characters) | keep; the `string::` texts spell the qualified name |
| time and dates | `time::now expects no arguments`, `time::sleep expects a number of seconds`, `time::date_from_unix: the instant is outside the supported range (years 1 to 9999)`, `time::date: month must be 1 to 12, got 13`, `time::date: day 31 does not exist in 2024-04` (a refused field store leaves the date unchanged), `time::date: hour takes an int, got float`, `time::date::add_days expects a whole number`, `time::date::format: unknown placeholder '${bogus}'` (`time::date_now:` for the namespace call), `time::date::format: format spec ':05' does not apply to string value`; the parallel wall `time:: calls are not allowed in a parallel body (engine-owned clock)` | keep; every text spells the qualified name |
| method / class plumbing | `Method called without 'this' object`, `Property getter requires 'this' object`, `'this' can only be used inside methods`, `'super' can only be used inside methods`, `Cannot call super() - class has no base class`, `No matching parent constructor found for super()`, `Constructor found for 'C' but no class definition available`, `Class definition no longer exists` | `No matching parent constructor found for super()` belongs to family C's constructor shape once its site carries the argument list |
| captures | `Cannot capture undefined variable 'x'`, `Cannot capture variable 'x' by reference` | keep |
| limits | `Maximum recursion depth exceeded - possible infinite recursion`, `Script execution budget exceeded - raise engine::execution_budget or break up the work`, memory cap | keep; recursion is catchable (see above), execution budget exhaustion is terminal |
| parallel walls | `method 'm' was not admitted in this parallel body (...)`, `only direct field stores on class instances are allowed in a parallel body (...)`, `custom operator dispatch on class instances is not admitted in a parallel body (...)` | keep (they carry the remedy) |
| script layers | `Script layer 'C' was detached (script_layer_detach, or a move to another engine): this handle no longer reaches its object`, `Cannot copy an instance of nocopy class 'C'`, `No copy constructor available for class C` | keep |
| static stores | `Static member assignment requires a class name`, `'C' is not a class`, `'C' is not a valid class`, `Cannot assign to static member: field 's' not found` | `Cannot assign to static member: field 's' not found` -> family B's `class C has no static member 's'` (same fact, second spelling) |
| casts | `Cannot cast 'X' to 'T'`, char range texts (`detail/cast_kernel.hpp`) | keep; the quoted-type spelling is this family's own contract (documented in CLAUDE.md) |
| internal | `Unknown expression type in dispatch_expr`, `Unknown callable kind`, `Complex assignment targets not yet implemented`, `this_id_ mismatch with symbolizer` | internal invariants; keep |
