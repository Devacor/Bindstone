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
  path, a hook string executed every frame.
- A different unit declaring the name gets a new cell. The earlier cell stays the binding of its
  own unit's code and of code compiled before the new declaration.

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

A strict-checked unit that reads a name only a later unit declares spells the forward reference with
`extern var G;` (`docs/static_checking.md`, escape hatches); the runtime resolves it late either way.

## Cost

A program in which no name is ever declared by two units pays nothing on the VM's armed lookup paths
and one compare per environment lookup elsewhere. A shadowed name keeps a list of its earlier cells
with their units; a lookup of such a name walks that list. A call pushes and pops the running unit
along with the frame it already pushes.
