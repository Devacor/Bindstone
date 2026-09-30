# Program-scope declarations across executions

A JaiScript engine runs many sources over its life: files, `import`s, host strings, a hook line every
frame. Each is a unit, and a variable declared at program scope by one unit is that unit's variable.
When a later unit declares the same name, the new declaration shadows the old one the way a nested
block shadows in C++: code that belongs to the earlier unit keeps its binding, code that belongs to
the later unit sees the new one. Nothing is rewritten under running code.

## Units

A unit is one source the engine executes as one thing:

- a file, identified by its path (a reload of the same path is the same unit),
- a literal string, identified by its content (the same hook string executed every frame is one unit,
  two different strings are two),
- a `jaibite` object, one unit per object.

Units are numbered in the order of their first execution, and every run of a unit is numbered too; a
declaration remembers the run that first made it. The host itself is unit 0.

## Declarations

A variable declaration at program scope (`var`, a typed declaration, `auto`, a reference
declaration) binds the name for the declaring unit:

- A name no unit has declared gets a cell.
- The same unit declaring the name again writes the existing cell in place: a file reloaded at its
  path, a module an import re-runs, a hook string executed every frame. A declaration that assigns
  (`int kills = 0;`) runs its initializer again; one written without an initializer (`int kills;`,
  `array<Item> pack;`, `var loose;`) keeps the value the variable holds, unless its declared type
  changed, which resets it to the new type's default (the rule a class field follows on hot reload).
  The first run of a declaration without an initializer takes its type's default.
- A different unit declaring the name gets a new cell. The earlier cell stays the binding of its
  own unit's code and of code compiled before the new declaration.

Inside a unit, a `{ }` block is a scope of its own, as in C++: a declaration inside a block (a bare
block, an `if`, a loop body, a `try`) shadows an outer declaration of the name for the block's extent
and vanishes at its end; a function called from inside the block still sees the binding of the scope it was
declared in (a function declared inside the block, an included one among them, sees the block's names); and a
block's declaration is not the unit's declaration of the name (it neither versions a global nor
writes one in place). Only a declaration at the unit's own top level binds the name for the unit.

An `include` is textual: the included file runs at the include site, in the current scope and under
the including unit, so its declarations are the includer's own. A later declaration of the same name
in the includer writes the included global in place, the included functions see that write, and an
include inside a block or a function body scopes with it (its names vanish with the block). Inside a
function body a literal include (`include "file"` or `include("file")`) is spliced at parse time, as
C++'s is: the included text is parsed into the function, so it reads and writes the function's
parameters and locals, the locals and functions it declares are the function's own, and the checker
and both backends see one function; a change to the included file re-parses the includer on every cache
tier (the parse cache, the file cache's fast path and the literal cache's fast path all revalidate the spliced
files' stamps before a hit runs), and such a unit keeps no jaibite sibling. A computed path (`include(p)`) stays a runtime include: it runs in the
function's environment and does not see the frame (its parameters and slot locals), and the checker
names them undeclared. A function without an include pays nothing for this. An included file the VM
compiles at run time belongs to the including unit:
two units including the same file each get their own compiled program, and a function it defines is
compiled once per unit, so no compiled code ever runs under two units.

An `import` is a unit of its own, keyed by the module's canonical path, and it takes no scope: it is a
statement allowed wherever a statement is (a function or lambda body, a block, a loop, a conditional,
a coroutine), and wherever it stands the module's declarations are program-scope declarations of the
module's unit. Directly in a class or a namespace body it is a parse error (`import is a statement, not a
member: it is not allowed directly in a class or a namespace body`), since there it would read as scoped.
An import runs its module when first reached; reached again, it re-runs the module only when the file's
(mtime, size) stamp moved, and an unchanged module costs the stamp check. A re-run is the same unit
declaring again (above): functions and classes replace in place and live instances migrate. The module
and the modules it imports are parsed and checked before any of it runs, so under strict a parse or
check failure is a catchable error naming the file and line, and the program stays as it was; under the
default `file_timestamp` behavior a failed re-run is attempted again once the file changes again. A run-time error during the run is catchable at the
import. Code compiled against the old functions sees the replacements (the vm's inlined calls guard the
callee's body at each call).

An `enum` follows the same scoping: declared inside a function or a block (written there, or brought
there by an include) it is local to that scope, so an enum of the same name at program scope keeps its
values; the checker warns when such a declaration names a program-scope enum (`docs/static_checking.md`).

A by-reference argument is the caller's code: it binds the cell the caller's unit resolves, and the
callee's unit governs only the callee's own code (its default arguments and its body).

Function and class definitions replace in place whatever the unit: a reload redefines them for every
caller and every instance, which is the hot-reload contract. A top-level variable a reloaded file
declares again is the same unit's declaration, so it also writes in place.

## Resolution

Code resolves a program-scope name to its own unit's cell when that unit declared the name.
Otherwise it resolves to the cell declared most recently among those declared no later than the
code's own compile time: for top-level code, the start of its current run (a reader string executed
again reads as of now); for a function, the run that declared it. A name declared only later (a
forward reference) resolves to the newest cell, so a function that reads a global a later file
declares keeps working.

Code belongs to the unit that executed its declaration:

- a named function or a lambda: the unit whose code declared it,
- a method, a constructor, a field initializer: the unit of the class declaration,
- a coroutine: the unit of its function,
- a namespace function: the unit of its namespace declaration.

A file that two units include declares its functions, lambdas and coroutines once per includer: the
callable value each includer took from it belongs to that includer's unit, so a factory file included
under two units hands each one a function that reads that unit's globals.

`::name` skips locals and members and then resolves the same way. Reads, stores, compound stores,
`++`/`--` and reference declarations all go through the same rule.

The host reads and assigns the newest cell: `add_global` after a redeclaration writes the newest
binding, and a host read outside any execution returns it. A host callback running inside a script
resolves as the running code does. A parallel region's workers resolve as the code that launched the
region does.

A reference bound to a variable before a redeclaration stays on its cell: `var& r = G;` in one unit
keeps aliasing that unit's `G` after another unit declares its own.

## Examples

```jaiscript
// first execute (unit 1)
var G = 1;
function f() { return G; }
function set(v) { G = v; }

// second execute (unit 2)
int G = 5;
function g() { return G; }

// third execute (unit 3)
f();       // 1: f belongs to unit 1
g();       // 5: g belongs to unit 2
G;         // 5: this text runs now, and unit 2's declaration is the newest one before this run
set(7);    // writes unit 1's cell
f();       // 7
G;         // 5
```

`extern var G;` is how a unit uses a global instead of owning one: the unit resolves the name to
the visible global (a declaration would shadow it) and declares nothing, so a use while no unit and
no host defines the name raises `'G' is declared extern but never defined (an 'inline' declaration
creates it)`. `inline int Score;` is the C++17 inline variable: one variable shared by every unit
that declares it. The first to run creates it (its initializer, else the type's default, owned by no
unit); every later inline declaration adopts it without running its initializer, as it adopts a host
global, so the inline units and the host keep one cell. Inline and plain declarations of one name
never mix across units, in either order: a plain declaration of an inline-created name raises
`'Score' is already defined inline; declare it 'inline int Score;' to share it` instead of shadowing
it, and an inline declaration of a name another unit declared plainly raises `'Score' is already
defined (not inline) in <file>; declare every definition 'inline' to share it`; two plain
declarations still shadow by unit. A unit whose re-run switches its own declaration between the two
converts the cell in place (to inline it keeps the value; to plain it follows the same-unit re-run rule: an
initializer re-runs, without one the value stays unless the declared type changed). A strict-checked unit reads a name only a later unit declares the
same way (`docs/static_checking.md`, escape hatches).

## Cost

A program in which no name is ever declared by two units pays nothing on the VM's armed lookup paths
and one compare per environment lookup elsewhere. A shadowed name keeps a list of its earlier cells
with their units; a lookup of such a name walks that list.

The unit of running code costs nothing until a program shadows. The engine derives it from the executing
frame (a function's own stamp; the run's at top level; a class's field initializers and a coroutine's
replay carry theirs explicitly) whenever a definition needs it, and writes the ambient stamp that lookups
consult only while some name has versions: a program that never redeclares a global across units pays no
store at any call or return, and the first shadow turns the writes on for every frame from then on.
