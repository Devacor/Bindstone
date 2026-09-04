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
assign_member; `interpreter.cpp` static access.

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

## Untouched families (one-line recommendation each)

| family | representative texts | recommendation |
|---|---|---|
| undefined names | `Undefined variable 'x'`, `Undefined variable 'x' (no 'this' in scope)`, `Undefined variable 'x' (not a field of 'this')`, `Undefined identifier 'x' in class 'C'`, `Class 'C' not found`, `Base class 'B' not found` | keep; consistent and named |
| references | `Cannot take reference of undefined variable`, `Cannot take reference of non-lvalue expression (...)`, `Cannot pass non-lvalue to reference parameter`, `Reference target is null`, `Invalid reference in assignment`, `Reference variable must be initialized`, `Cannot return reference to 'int' from a function returning 'string'`, `Function with reference return type must return an lvalue reference`, `Cannot iterate a string by reference: its chars are not lvalues (use for (auto c : s))` (`for (auto& c : s)`, raised before the first element, `detail/range_for_diagnostic.hpp`) | name the variable / parameter where the site has it (`Cannot take reference of undefined variable 'x'`) |
| non-object stores | `Cannot assign to member of non-object type`, `Cannot assign property to non-object value` (two texts for one fact, split by backend path) | fold into one `Cannot assign 'x' on int: not an object` shape naming the member and type |
| element / key boundary | `Cannot assign 'string' to element of type 'int'`, `Cannot assign 'S' to field of type 'S'`, `Cannot push 'string' to array<int>`, `Cannot use 'int' as a key of type 'string'` | unquote the types to match family D (`Cannot push string to array<int>`) |
| handles | `Cannot assign shared_ptr<A> to shared_ptr<B>`, `Cannot assign weak_ptr to shared_ptr - use weak.lock() instead`, `Cannot initialize shared_ptr with this type`, `Cannot initialize shared_ptr<T> with X: type must match or be a subclass`, `Cannot infer shared_ptr<auto> pointee ...`, `Cannot create weak_ptr from ...`, `weak_ptr type mismatch: ...` (handle VARIABLES at declaration / `=` are family D's handle-variable row) | route the rest through family D's slot shapes (`Cannot assign shared_ptr<A> to 'p' (declared shared_ptr<B>)`) |
| `auto` container inference | `Array 'auto' requires homogeneous elements - type mismatch at index 3`, `Array 'auto' requires homogeneous class types - mismatch at index 3`, `Map 'auto' requires homogeneous values - type mismatch at index 3`, `Map 'auto' requires homogeneous class types - mismatch at index 3` (the index rides the pre-interned count table; past 63 it is interned on the main thread, and a worker gets the index-free lead, `... - type mismatch`; texts in `detail/arity_diagnostic.hpp`), `Map 'auto' requires homogeneous keys`, `Map 'auto' requires homogeneous values` | keep; name the two types seen (`int at 0, string at 3`) |
| iteration | `Cannot iterate int: not an array, map, string or coroutine` (range-`for` over anything else; the type spelled like family D's source - `Foo`, `null`, `float`; `detail/range_for_diagnostic.hpp`), `'pair' type not registered - make sure stdlib is loaded` | keep |
| arithmetic | `Division by zero`, `Division by zero in integer operation`, `Modulo by zero in float operation`, `Integer overflow in '/'`, `Invalid operands for + operator`, `Left operand must be numeric`, `Bitwise & requires integer operands`, `Shift amount must be between 0 and 63`, `Unary minus requires numeric operand` | `Invalid operands for + operator` should name the operand types (`Cannot apply + to string and map`); the rest keep |
| indexing | `Array index 5 out of bounds for array of size 3`, `Array index must be an integer`, `String index must be an integer`, `Strings are read-only through subscript: ...`, `Subscript can only be used on arrays, maps, or types with [] operator`, `at() index out of bounds`, `Cannot pop from empty array`, `Cannot get front of empty array` | keep (named and specific) |
| builtin argument checks | `find() first argument must be a string`, `sort() comparator must be a function`, `pad_left() padding character cannot be empty`, `to_int() base must be 0 or between 2 and 36` | keep; consistent `name() ... must be ...` shape |
| method / class plumbing | `Method called without 'this' object`, `Property getter requires 'this' object`, `'this' can only be used inside methods`, `'super' can only be used inside methods`, `Cannot call super() - class has no base class`, `No matching parent constructor found for super()`, `Constructor found for 'C' but no class definition available`, `Class definition no longer exists` | `No matching parent constructor found for super()` belongs to family C's constructor shape once its site carries the argument list |
| captures | `Cannot capture undefined variable 'x'`, `Cannot capture variable 'x' by reference` | keep |
| limits | `Maximum recursion depth (10000) exceeded`, `Script execution budget exceeded - raise engine::execution_budget or break up the work`, memory cap | keep (terminal errors, deliberately verbose) |
| parallel walls | `method 'm' was not admitted in this parallel body (...)`, `only direct field stores on class instances are allowed in a parallel body (...)`, `custom operator dispatch on class instances is not admitted in a parallel body (...)` | keep (they carry the remedy) |
| script layers | `Script layer 'C' was detached (script_layer_detach, or a move to another engine): this handle no longer reaches its object`, `Cannot copy an instance of nocopy class 'C'`, `No copy constructor available for class C` | keep |
| static stores | `Static member assignment requires a class name`, `'C' is not a class`, `'C' is not a valid class`, `Cannot assign to static member: field 's' not found` | `Cannot assign to static member: field 's' not found` -> family B's `class C has no static member 's'` (same fact, second spelling) |
| casts | `Cannot cast 'X' to 'T'`, char range texts (`detail/cast_kernel.hpp`) | keep; the quoted-type spelling is this family's own contract (documented in CLAUDE.md) |
| internal | `Unknown expression type in dispatch_expr`, `Unknown callable kind`, `Complex assignment targets not yet implemented`, `this_id_ mismatch with symbolizer`, `operator= failed` | internal invariants; keep |
