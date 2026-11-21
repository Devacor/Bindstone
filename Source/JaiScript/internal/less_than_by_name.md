# `<` after a name: read by what the name names

`name < ... > (` is a comparison chain or a call with type arguments, and the two differ in shape: `f(a < b, c > (d))`
is two arguments or one. The reading is settled before the tokens after `<` are parsed, by what the name names where it
stands. This note is the rule, the mechanism and what each part of the load does with it.

## 1. The rule

A **site** is a `<` that follows a head (a name `x`, `::x`, a qualified name `A::B::x`, or a member `e.m`, `e->m`, `e?.m`)
and starts a type-shaped run that closes on a `>` which `(`, `{` or `::` follows. Every other `<` compares, as before, and
is no site. Three steps settle a site, in order:

1. **Shape no comparison spells.** The run holds a type's keyword (`l<int>(2)`), or the `>` is followed by `()` that no
   `=>` follows: type arguments, whatever the head (the checker refuses a callee that takes none).
   A `( ... )` after the `>` that `=>` follows is a lambda, which no call's argument list is: the site compares.
2. **A variable the parse holds.** A local, a parameter, a capture, a variable of an open program-scope block: compare.
   These are exact while parsing (a local declared below the site is not in scope at it) and are never asked again.
3. **Scoped lookup of the head**, nearest scope first; the first scope that declares the name decides:
   - *variable* (a field, own or inherited; a static field; a property a host class binds; an enum value; a namespace's
     variable; a program-scope variable of this unit, above or below, or of another unit of the program; a host variable;
     a variable an earlier unit left in the engine): **compare**.
   - *anything else declared* (a generic function, class or method; a plain function, method or class; an enum; a
     namespace; a host function, class or template; a builtin type's name): **type arguments**. A callee that takes none
     gets the checker's error (`'f' takes no type arguments ...`, `'Hero' takes no type arguments ...`).
   - *nothing declares it*: **compare** once the unit is settled (section 4); the checker reports the undeclared name.
     Names that arrive after the parse (a computed-path include or import, `[[warn_mode]]` code that runs after a later
     unit, an execute's instance variables, a debugger watch's frame locals) are variables to a program that compares
     them, and nothing can call a generic the load never saw.

Lookup order for a bare head `x` at a site in class `C` (when in one) inside namespace path `N`:

1. `C`'s members, then each base's, depth first in base order (the checker's `lookup_member` order). A class is found in
   the unit, then the pooled program, then the engine (a script class that ran, a host class). A base spelled
   `Base<Args>` is read through its generic's member table (section 5).
2. `N`'s members, then each enclosing namespace's, innermost first: unit, pooled program, engine (`script_namespaces_`,
   host entries).
3. Program scope: the unit's top-level declarations (anywhere in its text), the pooled program, then the engine (the
   global environment: a function value is a function, any other value a variable; host functions, classes, templates,
   enums).

Inside a template body's barrier the lookup starts at step 3 (`parser::template_hides`). `::x` is step 3 alone.
A qualified head `A::B::x` resolves `A::B` as a namespace (relative to `N` innermost first, then absolute), else a class,
else an enum, and reads `x` there: a namespace's variable, a static field or an enum value compares.
Inside a generic's text a type parameter is a type, so a head that is one, or begins with one (`T::x`), reads as type
arguments.

A **member head** has no scope to look in: the receiver's class is a checker fact. `e.m <` opens type arguments only when
some class of the program declares a generic method named `m` (`engine::names_generic_method`, the unit's own) or the
host binds a method named `m` that takes a `jai::script_type`; otherwise it compares. `p.x < w, h > (p.y)` therefore
compares, and `db.query<Entry>(sql)` is a call.

`{` and `::` after the `>` follow the same three steps: `a < b > ::k::lo` and `a < b > {}` on fields compare, and
`Box<Item>{}` and `Shelf<Item>::make()` name instances. A `>` that `;`, `,`, `)`, `]` or the end follows is no site: no
comparison ends there, so it stays the instance-as-a-value error. A statement that starts `Name<Args> x` stays a
declaration (`parser::declaration_rest`).

## 2. Why one ordinary parse per reading

Slots, capture plans, chained outer slots, node and token numbers are assigned while parsing
(`k_jaibite_version`'s comment, `ast_serializer.hpp`). A name read as a type argument has no slot and joins no capture
plan, so no pass may turn one reading's tree into the other's, and the two readings fork at the enclosing list, not at a
node. A site's reading is therefore fixed before its tokens are consumed, and a unit whose reading was wrong is parsed
again from its tokens. Nothing of a discarded parse survives: its tree and its generic definitions die with it, and what
it left in the engine is interned text and type records (`symbolizer_->intern`, `engine_access::get_type_info*`), the
load's file cache (`load_source`), and the idempotent `note_execute_elapsed_use`. The pools take a unit's declarations
only from the parse an entry holds (`pool_unit`), and `register_generics` replaces a definition by its newer one.

## 3. Mechanism

**Sites.** `parser::postfix_operator_ahead` is the one place an expression's `<` is judged
(`type_arguments_ahead`). At a site that steps 1 and 2 leave open it records a `less_site`: the `<` token's place (file,
line, column, byte offset), the head's form and spelling, the enclosing class's qualified name, the namespace path, the
template barrier, and the reading it took. A site reached twice (lookahead, a rewind) is one site.

**Lookup kernel.** `detail::less_than_lookup(engine, unit_declarations, site) -> variable | declared | nothing`
(`detail/less_than.hpp`, `implementation/less_than.cpp`) is the only implementation of section 1's step 3. It reads the
unit's tree, the engine's pools (`program_classes_`, `program_globals_`, `program_functions_`, `program_enums_`,
`generics_`, `program_names_`) and the engine's own tables, through `detail::engine_access`. The parser, the engine's
freshness check and the language server all call it.

**The parse loop.** `parser::parse()` runs a pass, then asks the kernel about every recorded site with the pass's full
tree (so a field or a variable declared below its use is found), and compares the answers with the readings taken. When
all agree the pass stands. Otherwise it parses the same tokens again with the readings fixed by site, and repeats until
they agree (two passes when the first parsed; a third only when the first failed and its recovered declarations missed
one). A pass that fails with sites still open is retried on what its recovered declarations answer; when no reading
changes, its errors are the parse's.
What a unit declares does not depend on a reading (a site lies inside one expression), so the answers of one pass hold for
the next.

**Provisional and settled.** A unit that names other files by literal `import`/`include` paths is first parsed before
they are pooled. In that parse a head nothing declares reads as type arguments (the reading that parses for every
generic use; `make<Pet>(x)` from an import is unknown there), and the entry is provisional. Once the program is pooled
the entry settles: the kernel answers again, and the unit parses again if any reading moved, with "nothing declares it"
now a comparison. A unit with no literal path settles in its first parse.

**Freshness.** `jaibite_context` (the parse's context an entry and a bite carry) holds each site's query and answer and
whether it is settled, beside the namespace queries. `implementation::parse_moved` recomputes the answers of an entry
that has sites and reports a move, so every place that already re-parses on a moved namespace answer does the same here:
`find_cached_script` (each cache hit), `reparse_if_moved` after the closure is pooled (`checked_report`, the reload
group), the file lane. `checked_report` settles a root with no imports too. An entry with no site holds two empty
vectors and is never asked. `unit_key` folds the answers in.
A reading is thus a function of the unit's text and of what the engine holds when the unit loads, never of cache
history.

## 4. What each hazard meets

- **Baked parse facts / shape.** Section 2: readings are inputs to a parse, never edits to a tree.
- **Program-wide knowledge.** Generics declared below, in another file or in an import cycle are in `generics_` once
  `pool_unit` ran over the closure (`register_generics`), before `reparse_if_moved`; host names are registered before any
  parse; an inherited field of another file's class is in `program_classes_`. The provisional parse reads every
  not-yet-known head as type arguments, so a generic call always parses, and the settled parse corrects the variables.
- **Generic bodies** (section 5).
- **jaibite** (section 6), **hot reload** (section 7), **language server** (section 8).
- **Cost.** A unit with no site records nothing, runs one pass, and is never asked on a cache hit; its bite's context
  gains one zero count. A unit with sites pays the kernel per site per load, and one more pass only when a reading was
  wrong.

## 5. Generic bodies

A generic's text is parsed once where it is declared (`parser::generic_declaration`, `parse_generic_text`) and once per
instance (`implementation::instantiate_generic`, a parser over `generic_instance_tokens`). Its sites are settled where it
is declared, in the unit's own loop, with the generic's class as the enclosing class and its type parameters as types.
Each settled `<` of the definition's tokens carries its reading (`token::less_reading`), `generic_instance_tokens` copies
tokens whole, and an instance's parse obeys a carried reading and records no site: every instance reads a site as its
generic does, whatever its arguments. `generic_stamp` covers the carried readings, so a reading that moves makes the
instances again, as an edit to the text does.
A generic class's definition keeps the names of its member variables, its other members and its bases
(`generic_definition`), filled from the declaration's parse: the kernel reads a base `Base<Args>` and a qualified
`Box<int>::made` through them.
Limit: a head that begins with a type parameter (`T::lo < hi, hi > (lo)`) reads as type arguments in every instance.

## 6. jaibite

A bite's context carries its sites (query, answer, settled). A cache file loads only where the kernel answers each of
them now as recorded (`try_load_jaibite_entry`, beside the namespace answers); otherwise the source is parsed and the
file rewritten. A unit whose sites rest on another file's names therefore re-parses on a cold load, exactly as one whose
namespace answers rest on another file does; one that instantiates another file's generic keeps no cache already.
A bite run without its source (`engine::run`) whose answers differ is refused with the namespace refusal's shape:
`the bite was parsed where 'x' named a variable; here it names a generic: save it again from source`.
A definition's tokens are written with their carried readings and its member names. `k_jaibite_version` moves.

## 7. Hot reload and later executes

A unit's readings are decided when it loads. A cached parse is reused only while every site answers as it did, so the
next load of the same text after a name changed kind (a host registration, a unit that ran and declared it, an edited
import) parses again; code already compiled keeps the reading it was compiled with until its unit loads again, like
every other name it resolved. A reload's group is pooled and then settled (`reparse_if_moved`), so a dependent of an
edited file follows a name that changed kind there. Scoped lookup keeps a nearer declaration in charge: a generic `id<T>`
declared anywhere later leaves `id < n, m > (k)` a comparison inside a class with a field `id`. A definition whose carried
readings moved has a new stamp, and its instances are made again.

## 8. Language server

`workspace::parse` (tools/lsp/analysis.cpp) keeps each unit's sites and answers beside its namespace answers and parses
the unit again where they moved, at the point it already does for namespace answers. The kernel reads the engine's
pools, so the server pools its program's variables and functions by kind (`pool_program_classes` carries classes, enums
and bare names today).

## 9. Other parses

The debugger's watch text, `engine::jaibite`, `engine::check` and a template's body go through `parser::parse` or the
expression entry and get the loop and the kernel; none is provisional. The parallel chunk skeletons have no site.

## 10. Tests

`source/tests/language/less_than_tests.cpp`, both backends. Each row is the form in each context; expected output is the
comparison's value unless the row says a call.

Contexts (C): call arguments `both(a < b, b > (a))`; array literal `[a < b, b > (a)]`; braced init `Pair{a < b, b > (a)}`;
single chain `a < b > (c)`; condition `if (...)`; `return`; lambda body (`[&]`, `[this]`, `[=]`); coroutine body; generic
class body and generic function body; namespace function; `{` and `::` followers.

| Head | Declared | Contexts | Same-named generic elsewhere |
|---|---|---|---|
| local, parameter, capture, loop/catch/condition variable | above | all C | with and without |
| field | above, below the method | all C | with and without |
| inherited field | base above, below, in an import, in an import cycle, host class | call args, chain, lambda | with and without |
| static field, bare and `Cfg::min` | above, below, import | call args, chain | with and without |
| enum value `Lvl::low`, namespace `ns::E::low` | above, below, import | call args, chain | - |
| namespace variable, bare inside and `ns::v` outside | above, below, another block of the namespace, import | call args, chain, lambda | with and without |
| program-scope variable | above, below the function, import, import cycle, earlier execute, host variable | all C | with and without |
| member head `p.x`, `this.x`, `p->x` | - | call args, chain | generic method `x` in another class (reads as a call: the documented limit) |
| nothing declares it | computed-path include, `[[warn_mode]]` + later execute, instance variables, debugger watch | call args, chain | - |
| lambda right operand `c > (x) => x`, empty `()` | - | call args | - |

Positive rows (type arguments must hold): generic function, class and method called with class-type arguments, declared
above, below the call, in an import, in an import cycle, in an earlier execute; `Name<Args>{...}`, `Name<Args>::member`,
`ns::Name<Args>(...)`; a generic method on a typed receiver and through `this`; host `from_json<Pet>(text)`, a bound
method `db.query<User>(sql)`, a host template `Point<Item>{}`; the same inside a generic body with `T` and with a class
argument; a plain function or class named with type arguments keeps its error text; `l<int>(2)` on a variable keeps its.
Freshness rows: a cached unit re-parses when a head changes kind (variable to generic and back) through a later execute,
a host registration, an edited import; a bite misses, and a bite without source is refused, on a moved answer; a moved
reading inside a generic body makes its instances again. Language-server rows: no diagnostic on each comparison row
across two files.
Outside the suite: every generic-free probe, example and site sample run on the 0.5.4 runner and on this one, outputs
compared; the fuzzer's grammar gains the site shapes.
