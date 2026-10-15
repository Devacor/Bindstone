# Temporaries die at the end of their full-expression

Not built: the table's last column is what both backends do.

C++ ([class.temporary]/4): a temporary is destroyed as the last step of evaluating the full-expression that created it,
in reverse order of construction. JaiScript destroys a temporary when its last reference drops, which for a temporary
read only for a member is right after the read:

| program | C++ | JaiScript (both backends) |
|---|---|---|
| `string s = Token("t").name + mark();` | `mt` | `tm` |
| `use(Token("a").name);` | `ua` (after the call) | `au` |
| `if (Token("c").name == "c") { body }` | `c` then the body | the same |

A prvalue argument is already the parameter itself (C++17 elision, `detail::uniquely_held`), so call arguments create
no temporaries; what remains are temporaries a full-expression consumes partway: a member or element read, a method
call's receiver, an operand an operator copies out of.

## Design

A deferred-destruction queue on the engine, used only while `engine::destructors_armed()`:

- An armed instance's deleter (`class_definition::destroy_chain`) checks the engine's deferral depth. Inside a
  full-expression it parks the dying instance (its raw pointer and its class) on the queue instead of running its
  destructor; outside one it runs as today.
- Each full-expression end flushes the queue: the parked instances run their destructor chains and are freed, the last
  parked first (reverse construction order). A destructor that parks more temporaries flushes them in the same pass.
- The queue holds the instances alive, so nothing parked can be observed half destroyed; a flush is the only place a
  parked destructor runs.

### Flush points (each is a full-expression end)

Both backends flush at exactly these, so the logs agree:

1. The end of an expression statement (`expression_stmt`, `expression_decl`), after its value is discarded.
2. The end of a variable declaration's initializer, after the value is bound (a temporary bound to the declared local
   is no temporary: it is the local).
3. A condition: after an `if`, `while`, `do`-`while` or C-style `for` condition is converted to its truth value, before
   the branch or the body runs.
4. A C-style `for` update, after its value is discarded.
5. A range-for container expression, once the iteration has taken the container (the container itself lives for the
   loop, as C++'s `__range` reference extends it).
6. A `switch` condition, after the case value is chosen; each `case` value expression.
7. A `return` value: after the result is constructed (moved or copied into the return slot) and before the function's
   locals die.
8. A `throw` operand: after the thrown value is captured, before unwinding destroys any scope.
9. A `yield` operand: after the yielded value is captured, before the coroutine suspends (a suspended coroutine parks
   nothing).
10. A default argument expression and a field initializer: each is its own full-expression.
11. A host entry (`execute`, a direct call of a script function, a resume) returning to the host: a final flush, so
    nothing stays parked across host code.
12. Unwinding: a throw leaving a full-expression flushes what that full-expression parked before the enclosing scopes
    die (C++ destroys a full-expression's temporaries during unwinding too).

### Cost

- The interpreter flushes at points it already visits (statement boundaries, condition ends, return), behind one test
  of the queue being non-empty.
- The vm emits `op_flush_temporaries` only after a full-expression that can create a destructor-owning temporary,
  decided when the body compiles: a call whose declared result is a class, container or handle type that may own a
  destructor (`slot_may_own_destructor`), a constructor call of such a class, or a callee of unknown result type while
  any script class declares a destructor. A program without destructor classes compiles no flush and parks nothing.
- The deleter's check is one load of the deferral depth, reached only by armed instances.

### Open points

- Assignment (Dev ruling 2026-10-03): `x = Token("b")` copy-assigns into `x`'s object and the temporary dies at the
  statement's end, a flush point in this design too. A `var` slot keeps replace: the old value dies when the store drops
  it and a temporary moves in. Operator arguments still copy (`x += Token("b")` through `operator+(Token o)` binds `o`
  from a copy, so the temporary and `o` both die); the queue's flush would order them, elision would remove the copy.
- A cell block region never parks (class temporaries do not enter cells); a flush inside a closed loop is not needed.
