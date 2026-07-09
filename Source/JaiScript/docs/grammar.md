# JaiScript Grammar Specification

## Overview

JaiScript uses a C++-like syntax with RAII semantics and built-in support for hot-reloading and
serialization. This reference is regenerated from the shipped lexer/parser (`detail/lexer.hpp`,
`lexer.cpp` keyword table, `parser.cpp`); when in doubt, those are the ground truth.

## Lexical Elements

### Keywords

Exactly the lexer's keyword table (`lexer::keywords_` in `lexer.cpp`):

```
array auto bool break case catch char class const continue coroutine
default else enum fallthrough false float for function if import
include int map namespace new null nullptr override private public
return shared_ptr static string super switch this throw true try var
void weak_ptr while yield
```

Notes:
- `nullptr` is an alias for `null` (both lex to the null literal).
- `new T(args)` is **pure sugar for `shared_ptr<T>(args)` construction** — the
  reference-semantics opt-in (see "new expressions" below). Plain value-semantics
  construction stays `Type(args)` / `Type{args}`.
- `const` is accepted only where documented below (range-`for` bindings). It does not apply to
  parameters or general variable declarations.
- A leading `#` before `include`/`import` is skipped by the lexer, so `#include "x"` and
  `include "x"` are identical.

### Operators

```
// Arithmetic
+ - * / %

// Assignment (complete set — bitwise compound assignments (|= &= ^= <<= >>=) are
// deliberately omitted; spell it out: x = x | y)
= += -= *= /= %=

// Comparison
== != < > <= >= <=>

// Logical
&& || !

// Bitwise
& | ^ ~ << >>

// Increment/Decrement (prefix and postfix)
++ --

// Member access
. ?. ->

// Scope / static access
::

// Reference (parameters, captures, declarations, range-for)
&

// Ternary
? :
```

### Literals
- **Integer**: `42`, `-17`, `0xFF`, `0b1010`, `0755` (leading-zero octal; digits 8/9 are an error)
- **Float**: `3.14`, `-0.5`, `1e10`, `2.5e-3`
- **String**: `"hello"`, `"escaped \"quote\""`, `"newline\n"`
- **Template string**: `` `hp: ${creature.health}` `` — backtick string with `${expression}`
  interpolation (see below)
- **Character**: `'a'`, `'\n'`, `'\\'`
- **Boolean**: `true`, `false`
- **Null**: `null` (or `nullptr`)

Escape sequences in `"..."` strings and `'...'` chars are exactly `\n \r \t \\ \" \'`.
Anything else (including `\0`) is an "Invalid escape sequence" error.

### Template Strings

Backtick strings interpolate arbitrary expressions with `${...}`. The lexer desugars the literal
into string concatenation (`("" + "part" + (expr) + "part")`), so non-string values convert
automatically and the result is an ordinary string expression usable anywhere:

```cpp
auto name = "Dragon";
print(`The ${name} has ${hp * 2} hp`);   // The Dragon has 84 hp
```

Inside a template string, `\`` escapes a backtick and `\$` escapes a dollar sign; `\n \r \t \\`
also work. Splices take arbitrary expressions, including indexing (e.g. with `var m = {"k": 1};`
declared, `` `${m["k"]}` `` yields `"1"`), and whitespace inside the splice is fine (`${ x }`).

A backtick string may span multiple source lines: a literal newline in the text becomes a `\n`
in the value. Line endings in such multi-line string literals are normalized to `\n` (a CRLF or a
lone CR both fold to a single `\n`), so the same script yields byte-identical strings regardless
of the file's line-ending style. (An explicit `\r` escape is preserved as-is.) Plain `"..."`
strings do **not** span lines — a literal newline in one is an "Unterminated string literal"
error, so they have no line-ending sensitivity.

#### Format specs: `${expr:spec}`

A splice may end in a format spec — a deliberate subset of C++ `std::format`:

```ebnf
format_spec = [[fill] align] [width] ["." precision] [type]
align       = "<" | ">" | "^"                  // left / right / center
fill        = any char except "{" and "}"      // default " "; only valid before an align
width       = [1-9][0-9]*                      // max 1024; no "0" zero-pad flag (use "0>N")
precision   = "." [0-9]+                       // numeric values only; max 1024
type        = "f" | "x" | "X" | "b"            // fixed float; lower/upper hex int; binary int
```

```cpp
`${hp:.1f}`      // 73.5      fixed, 1 decimal (bare ".N" also means fixed)
`${gold:6}`      // "  1234"  numbers right-align by default
`${name:<12}`    // "Grubwell    "  strings (and char/bool/etc.) left-align by default
`${gold:*>8}`    // "****1234" fill char + align
`${n:x}` `${n:X}` `${n:b}`   // hex / HEX / binary (ints only; negatives keep the "-")
`${hp:>6.1f}`    // "  73.5"  combos: fill+align+width+precision+type
```

- `:f` with no precision means 6 decimals (like `std::format`); `.N` without a type is also
  fixed. Precision on an int with `f` works (`${gold:.1f}` → `1234.0`).
- `^` centering puts the odd fill char on the right (like `std::format`).
- The desugar produces a call to the engine-core builtin `format_value(value, "spec")`
  (registered on every engine, no stdlib needed; callable directly). Don't shadow the name.
- **Errors**: a spec outside this subset (`${x:q}`, `${x:06}`, `${x:}`, …) is rejected **at lex
  time** — `Unsupported format spec ':q' in template string` with the source position. A spec
  that doesn't fit the value's type (`${str:.2f}`, `${flt:x}`) is a catchable **runtime** error:
  `format spec ':.2f' does not apply to string value`. Both backends produce identical text.

**Ambiguity rule (as implemented):** while lexing a splice, a `:` at top level (outside any
nested `()`/`[]`/`{}`) that does **not** close a pending top-level `?` starts the spec, which
runs as raw text to the closing `}` (so whitespace inside a spec is significant, and the fill
char cannot be `:`). Ternaries keep working unchanged: `` `${crit ? "CRIT" : "hit"}` `` is all
expression; to put a spec after a ternary, parenthesize it: `` `${(crit ? 150 : 100):>5}` ``.
Map literals (`${ {"k": 1}["k"]:4 }`) and subscripts (`${m["k"]:6}`) are fine — their colons
are nested. A bare top-level `:` is never a valid expression, so nothing legal is claimed.

The same spec mini-language works in the stdlib `format()`/`print()` placeholders: `{:spec}`
(sequential) and `{n:spec}` (positional), e.g. `format("{:.2f}", pi)` → `"3.14"`. There an
invalid spec simply isn't a placeholder (stays literal), matching those functions' lenient
style; type mismatches raise the same runtime error as above.

### Comments

`// line` and `/* block */`.

## Grammar Rules (EBNF-style)

```ebnf
// Program structure
program = declaration*

declaration = classDeclaration
            | namespaceDeclaration
            | enumDeclaration
            | includeDirective
            | importDirective
            | functionDeclaration
            | variableDeclaration
            | destructuringDeclaration
            | statement

// Class declarations
classDeclaration = "class" IDENTIFIER (":" classBaseList)? "{" classMember* "}" ";"?

classBaseList = IDENTIFIER ("," IDENTIFIER)*   // Multiple inheritance

classMember = accessLabel
            | constructorDeclaration
            | destructorDeclaration
            | methodDeclaration
            | fieldDeclaration

// ENFORCED at member access — see design note 12
accessLabel = ("public" | "private" | "protected") ":"

constructorDeclaration = IDENTIFIER "(" parameterList? ")" (":" ctorInitializer ("," ctorInitializer)*)? blockStatement

// Initializer lists accept ONLY super(...) / this(...) delegation, and ONLY on
// constructors — on free functions, methods, or destructors they are a parse error.
// Field initializers like `: name(n)` do NOT parse — initialize fields in the body.
ctorInitializer = "super" "(" argumentList? ")"
                | "this" "(" argumentList? ")"

destructorDeclaration = "~" IDENTIFIER "(" ")" blockStatement

methodDeclaration = "coroutine"? "static"? "override"? type (IDENTIFIER | operatorName) "(" parameterList? ")" ("->" type)? "override"? blockStatement
                  | "coroutine"? "function" ("static" | "override")* (IDENTIFIER | operatorName) "(" parameterList? ")" ("->" type)? blockStatement

operatorName = "operator" ("=" | "+" | "-" | "*" | "/" | "[]" | "<" | ">" | "<=" | ">=" | "==" | "!=")

fieldDeclaration = "static"? type IDENTIFIER ("=" expression)? ";"

// Function declarations. `type` is ANY type — builtin, container, or a user class
// name (`Point mk() {...}` works at top level, in namespaces, and in classes).
// A leading return type AND a trailing `->` type may both be given only if they
// MATCH (redundant-legal); a contradictory pair is a parse error. `-> {` (arrow
// straight into the body) means auto return — or keeps the leading type if one
// was given.
functionDeclaration = "coroutine"? ( "function" IDENTIFIER "(" parameterList? ")" ("->" type)? blockStatement
                                   | type "&"? IDENTIFIER "(" parameterList? ")" ("->" type)? blockStatement )

parameterList = parameter ("," parameter)*

// Four accepted parameter shapes; a default value may follow any of them.
// Once a parameter has a default, all later parameters must too.
// NOTE: defaults are honored by EVERY callable kind - free functions, lambdas,
// static and instance methods, constructors (incl. this()/super() delegation),
// and namespace overloads (a callable with N params, K defaulted, accepts N-K..N
// arguments; within a resolution tier the candidate using fewest defaults wins).
parameter = type "&"? IDENTIFIER ("=" expression)?     // classic:  int x, Creature& c
          | IDENTIFIER ("=" expression)?               // untyped -> auto: foo(x), foo(x = 3)
          | type ":" IDENTIFIER ("=" expression)?      // typed shorthand: int: n (no '&' form)
          | ":" IDENTIFIER ("=" expression)?           // auto shorthand: :x (like `auto x`)

// Variable declarations (reference declarations alias the initializer's storage).
// `function name = expr;` / `function name;` declares a function-typed variable
// (auto semantics); also legal as a class field and in namespaces.
variableDeclaration = type "&"? IDENTIFIER ("=" expression)? ";"
                    | type IDENTIFIER "{" argumentList? "}" ";"      // brace construction
                    | "function" IDENTIFIER ("=" expression)? ";"    // function-typed variable

destructuringDeclaration = ("auto" | "var") "[" IDENTIFIER ("," IDENTIFIER)* "]" "=" expression ";"

// Namespaces (nested C++17 style supported)
namespaceDeclaration = "namespace" NAME ("::" NAME)* "{" declaration* "}"

// Enums (plain value lists; values are namespaced: Color::red)
enumDeclaration = "enum" IDENTIFIER "{" IDENTIFIER ("," IDENTIFIER)* "}"

// Include / import ("#" prefix optional; trailing ";" optional)
includeDirective = "#"? "include" (STRING_LITERAL | "<" path ">" | "(" expression ")") ";"?
importDirective  = "#"? "import"  (STRING_LITERAL | "<" path ">" | "(" expression ")") ";"?

type = primitiveType
     | IDENTIFIER ("<" type ("," type)* ">")?   // User-defined (optionally templated) type
     | "array" "<" type ">"
     | "map" "<" type "," type ">"
     | "weak_ptr" "<" type ">"
     | "shared_ptr" "<" type ">"                // Explicit reference semantics (no clone on assign)
     | "function"                               // Callable
     | "auto"                                   // Type inference with locking
     | "var"                                    // Dynamic typing (any type allowed)

primitiveType = "int" | "float" | "string" | "char" | "bool" | "void"

// Statements
statement = expressionStatement
          | blockStatement
          | ifStatement
          | whileStatement
          | forStatement
          | forRangeStatement
          | switchStatement
          | returnStatement
          | breakStatement
          | continueStatement
          | tryStatement
          | variableDeclaration

expressionStatement = expression ";"     // includes throw/yield — both are expressions

blockStatement = "{" declaration* "}"

ifStatement = "if" "(" expression ")" statement ("else" statement)?

whileStatement = "while" "(" expression ")" statement

forStatement = "for" "(" (variableDeclaration | expression)? ";" expression? ";" expression? ")" statement

forRangeStatement = "for" "(" "const"? type "&"? IDENTIFIER ":" expression ")" statement

switchStatement = "switch" "(" expression ")" "{" caseClause* "}"

caseClause = ("case" expression | "default") ":" statement* fallthroughStatement?

fallthroughStatement = "fallthrough" ";"

returnStatement = "return" expression? ";"

breakStatement = "break" ";"

continueStatement = "continue" ";"

tryStatement = "try" blockStatement catchClause

catchClause = "catch" ("(" IDENTIFIER ")")? blockStatement

// Expressions (precedence from lowest to highest)
expression = assignmentExpression

assignmentExpression = ternaryExpression (assignmentOperator assignmentExpression)?

assignmentOperator = "=" | "+=" | "-=" | "*=" | "/=" | "%="
                   // bitwise compound forms (|= &= ^= <<= >>=) deliberately omitted

ternaryExpression = logicalOrExpression ("?" expression ":" ternaryExpression)?

logicalOrExpression = logicalAndExpression ("||" logicalAndExpression)*

logicalAndExpression = bitwiseOrExpression ("&&" bitwiseOrExpression)*

bitwiseOrExpression = bitwiseXorExpression ("|" bitwiseXorExpression)*

bitwiseXorExpression = bitwiseAndExpression ("^" bitwiseAndExpression)*

bitwiseAndExpression = equalityExpression ("&" equalityExpression)*

equalityExpression = relationalExpression (("==" | "!=") relationalExpression)*

relationalExpression = shiftExpression (("<" | ">" | "<=" | ">=" | "<=>") shiftExpression)*

shiftExpression = additiveExpression (("<<" | ">>") additiveExpression)*

additiveExpression = multiplicativeExpression (("+" | "-") multiplicativeExpression)*

multiplicativeExpression = unaryExpression (("*" | "/" | "%") unaryExpression)*

unaryExpression = ("!" | "-" | "++" | "--" | "&" | "~") unaryExpression
                | throwExpression
                | yieldExpression
                | postfixExpression

throwExpression = "throw" expression?          // bare `throw` re-throws inside catch

yieldExpression = "yield" expression?          // only inside a coroutine (parse error otherwise)

postfixExpression = primaryExpression (postfixOperator)*

postfixOperator = "++"
                | "--"
                | "." IDENTIFIER
                | "?." IDENTIFIER              // null-safe member access
                | "->" IDENTIFIER
                | "::" IDENTIFIER              // static / namespace / enum member access
                | "[" expression "]"
                | "(" argumentList? ")"
                | "{" argumentList? "}"        // brace construction (type names only)

primaryExpression = literal
                  | IDENTIFIER
                  | "this"
                  | "super"
                  | newExpression
                  | includeExpression
                  | "(" expression ")"
                  | lambdaExpression
                  | anonymousFunctionExpression
                  | arrayLiteral
                  | mapLiteral

// Sugar: `new T(args)` ≡ `shared_ptr<T>(args)` (idempotent when T is already shared_ptr<...>)
newExpression = "new" type ( "(" argumentList? ")" | "{" argumentList? "}" )

// include in expression position evaluates to the included file's result value
// (its last top-level expression — exactly what engine::execute returns for it)
includeExpression = "include" (STRING_LITERAL | "<" path ">" | "(" expression ")")

literal = INTEGER_LITERAL
        | FLOAT_LITERAL
        | STRING_LITERAL            // includes desugared template strings
        | CHAR_LITERAL
        | BOOLEAN_LITERAL
        | "null" | "nullptr"

lambdaExpression = "[" captureList? "]" "(" parameterList? ")" ("->" type?)? blockStatement

// Anonymous function expression — desugars to a NO-CAPTURE lambda ([]-equivalent
// auto-capture: enclosing-function locals snapshot by value at creation, globals
// resolve live). There is no coroutine form (coroutine lambdas do not exist), and
// no named form (`function g(x) {...}` in expression position stays an error).
anonymousFunctionExpression = "function" "(" parameterList? ")" ("->" type?)? blockStatement

captureList = captureDefault ("," capture)*    // [=] / [&] with optional explicit exceptions
            | capture ("," capture)*           // explicit captures: [x, &y, this]

captureDefault = "=" | "&"

capture = "&"? (IDENTIFIER | "this")

arrayLiteral = "[" (expression ("," expression)*)? "]"

// BOTH map-literal styles parse:
mapLiteral = "{" (cppMapEntry ("," cppMapEntry)*)? "}"             // C++ style
           | "{" (jsonMapEntry ("," jsonMapEntry)*)? "}"           // JSON style

cppMapEntry = "{" expression "," expression "}"

jsonMapEntry = (IDENTIFIER | STRING_LITERAL) ":" expression        // bare key means "key"

argumentList = expression ("," expression)*

// Lexical tokens
IDENTIFIER = [a-zA-Z_][a-zA-Z0-9_]*

INTEGER_LITERAL = [0-9]+
                | "0" [0-7]+                   // octal
                | "0x" [0-9a-fA-F]+
                | "0b" [01]+

FLOAT_LITERAL = [0-9]+ ("." [0-9]+)? ([eE] [+-]? [0-9]+)?   // at least one of . / exponent

STRING_LITERAL = "\"" (escape_sequence | [^"\\\n])* "\""

CHAR_LITERAL = "'" (escape_sequence | [^'\\]) "'"

BOOLEAN_LITERAL = "true" | "false"

escape_sequence = "\\" [nrt"'\\]               // template strings also allow \` and \$
```

## Example Programs

### Basic Class
```cpp
class Creature {
public:
    string name;          // typed fields are enforced like typed locals
    int health = 100;

    // Initializer lists are super(...)/this(...) only; init fields in the body.
    // Parameters cannot be const.
    Creature(string n) { name = n; }

    void takeDamage(int amount) {
        health -= amount;
        if (health <= 0) {
            onDeath();
        }
    }

    // All methods are virtual by default
    void onDeath() {
        print("Creature {} has died", name);
    }
}

// Inheritance — same-name methods override automatically.
// `override` is OPTIONAL documentation here (parsed, not required).
class Dragon : Creature {
public:
    Dragon() : super("Dragon") {}     // delegate to the base constructor

    void onDeath() override {
        print("The mighty dragon falls!");
        super::onDeath();  // Call parent implementation
    }
}
```

### Constructor Delegation, Static Members, Operators
```cpp
class Vec {
    float x = 0.0;
    float y = 0.0;
    static int liveCount = 0;

    Vec(float px, float py) { x = px; y = py; liveCount++; }
    Vec() : this(0.0, 0.0) {}                  // delegate to another ctor

    static function count() { return liveCount; }

    function operator+(other) {                // overloadable: = + - * / [] < > <= >= == !=
        return Vec(x + other.x, y + other.y);
    }
    function operator==(other) { return x == other.x && y == other.y; }
}

auto v = Vec(1.0, 2.0) + Vec{3.0, 4.0};        // Type(args) and Type{args} both construct
print(Vec::count());
```

Typed operator methods accept named-class, container, and shared_ptr return types
(`Vec operator+(Vec other) { ... }` parses and chains), in both the typed-first and the
`function operator+(...)` spellings.

### Functions — the definitive form catalog

Free functions (top level and namespaces; class methods accept the same spellings):

```cpp
function f(a, b) { return a + b; }          // function keyword, inferred return
function f(a) -> int { return a; }          // function keyword, trailing return
function f(a) -> { return a; }              // -> { means auto return
int f(int a) { return a + 1; }              // C++ return-type-first (any builtin type)
void f() {}                                 //   incl. void,
array<int> f() { return [1]; }              //   containers,
shared_ptr<P> f() { return new P(); }       //   shared_ptr,
Point mk() { return Point(); }              //   and user class types
auto f(a) { return a; }                     // auto: inferred return
auto f(a) -> int { return a; }              // auto + trailing return
var f(a) { return a; }                      // var: dynamic (any-typed) return
int f() -> int { return 4; }                // leading + matching trailing: redundant, legal
                                            // (a CONTRADICTORY pair is a parse error:
                                            //  int f() -> string  ==> "Conflicting return types")
int f() -> { return 4; }                    // leading + bare arrow: keeps the leading type
coroutine function f() { yield 1; }         // coroutine variants of every form above
coroutine int f() { yield 1; }              //   (top level and class methods; NOT
                                            //    yet inside namespaces)
```

The same catalog applies verbatim INSIDE CLASSES — including `var`/`auto` returns.
(GLOOM's feel notes claimed `var name() { ... }` methods silently vanished; the full
form family is verified and pinned in gloom_feedback_tests.cpp var_auto_method_forms —
if a method ever "disappears," suspect a recoverable parse error in the BODY, not the
`var` spelling):

```cpp
class G {
    var gather() { return {"fwd": true}; }     // var-returning method
    auto pick() { return 3; }                  // auto-returning method
    static var mk() { return 42; }             // static + var
    coroutine var brain() { yield 1; }         // coroutine + var
    var take(var kinds) { return kinds[0]; }   // var params too
}
class H : G {
    override var gather() { return {}; }       // override + var (before the type)
}
```

Function-typed variables and fields hold any callable (auto semantics):

```cpp
function f = [](x) { return x * 2; };       // top level, function bodies, namespaces
function g;                                 // starts null; assign later: g = f;
class K { function cb = [](){ return 1; }; }   // fields too

var d = function(x) { return x * 2; };      // anonymous function EXPRESSION - a
apply(function(v) { return v + 1; }, 41);   // no-capture lambda; legal anywhere an
function(a, b) { return a * b; }(6, 7);     // expression is, incl. args and IIFE
```

Parameters and defaults:

```cpp
function greet(name, string greeting = "Hello") {   // untyped param -> auto
    print("{} {}", greeting, name);
}
function f(x = 3) { return x; }             // untyped params take defaults too
// Defaults are honored by EVERY callable kind: free functions, lambdas, static and
// instance methods, constructors (incl. this()/super() delegation), namespace
// overloads. NOTE: a defaulted-parameter constructor is NOT a converting constructor
// (deliberate C++ divergence - conversion requires a true one-parameter constructor;
// write a delegating overload to opt in).

int square(int: n) { return n * n; }        // type: name — alternative typed spelling
auto id = [](:x) -> { return x; };          // :x — auto-like parameter (same as `auto x`)

void heal(Creature& c, int amount) { c.health += amount; }
heal(party[0], 25);            // lvalue arguments bind by reference: arr[i], obj.field, locals
```

Reference returns (shipped with the reference-cells rework):

```cpp
int& pick(array<int>& a, int i) { return a[i]; }   // leading form: free functions
Box& get() { return b; }                            // class-typed leading form works too
class Orc {
    int hp = 10;
    function heal() -> int& { return this.hp; }     // methods/lambdas: trailing -> T& form
}

auto& slot = pick(nums, 2);    // reference locals alias the returned reference
slot = 99;                     // writes through to nums[2]
int& leak() { int v = 7; return v; }   // returning a LOCAL is legal: the local boxes
auto& r = leak(); r = r + 1;           // into a cell the returned handle keeps alive
```

Rules: the returned operand must be an lvalue (a variable, `arr[i]`, `obj.field`, a
chain, or a call that itself returns a reference) — returning a temporary raises
"Function with reference return type must return an lvalue reference". The referent's
type must match the declared referent exactly (no int/float conversion applies through
a reference). Coroutines cannot return references (parse error). At top level
`ClassName& name(` reads as a function declaration, like `ClassName name(` — spell a
bitwise-and of a class value as `(ClassName) & name()`.

Redefinition: at top level a same-name function definition replaces the previous one
(last wins); with static checking enabled (`warn`/`strict`) a duplicate definition inside
one textual parse unit reports a checker WARNING (never an error — strict mode still
executes). Composition through `include`/`import` stays silent: included files parse and
check as their own units. In namespaces, same name + same arity requires `override`
(placed AFTER the parameter list: `int f() override { ... }`); different arities coexist
as overloads. Class methods and constructors overload by arity (trailing defaults
widen each overload's accepted range; fewest defaults used wins ties). A
defaulted-parameter constructor is NOT a converting constructor (deliberate C++
divergence): conversion is opted into with a one-parameter overload
(`Money(int a) : this(a, 0) {}`) and opted out of with a defaulted sentinel
parameter (`Money(float a, int _ignored = 0)`) - the explicit keyword is unnecessary.

### Coroutines
```cpp
coroutine function counter(int limit) {      // typed-first spellings work too:
    for (int i = 0; i < limit; i++) {         //   coroutine int counter(...) { ... }
        yield i;                       // yield is an expression; bare `yield` also legal
    }
}
// Coroutines are NOT yet supported inside namespace bodies (parse error).

for (auto n : counter(3)) { print(n); }   // range-for drives the coroutine

class Spawner {
    coroutine function waves(int count) {     // coroutine METHODS work too
        for (int i = 0; i < count; i++) { yield spawnWave(i); }
    }
}
```

### Namespaces, Enums, Include/Import
```cpp
namespace game::combat {                  // nested a::b::c namespaces
    int base_damage() { return 10; }
}
print(game::combat::base_damage());

// Replacing an existing namespace function REQUIRES override (after the params):
namespace game::combat {
    int base_damage() override { return 12; }
}

enum Color { red, green, blue }
auto c = Color::green;

include "setup.jai";     // PHP-template style: ALWAYS runs the file's code in place;
                          // statement position discards the file's value
var cfg = include "config.jai";   // expression position: evaluates to the file's result
                                   // (its last top-level expression) — the data-file idiom.
                                   // include never caches, so the value is always fresh.
import "creature.jai";   // module style: cached per path — declarations (classes, functions,
                          // namespaces) load ONCE; never produces a value
import(pathExpression);   // computed paths work for both; so does `#include <lib.jai>`
```

Every file-based load (include in all forms, import, `engine::execute_file`) transparently
maintains a sibling parse cache: `foo.jai` keeps a `foo.jaibite` (its pre-parsed AST) next to
it. A sibling that is strictly newer by mtime with a matching registration fingerprint loads
instead of parsing; anything else (edited source, equal mtimes, corrupt data, different host
registrations) reparses and rewrites it — so an edited `.jai` always wins and hot reload is
unaffected. Cache writes are silent best-effort (read-only script dirs are fine, the write is
just skipped); disable per engine with `engine->jaibite_cache(false)`.

### Destructuring
```cpp
auto [x, y] = [10, 20];       // also: var [a, b, c] = someArray;
```

### Lambda and Function Variables
```cpp
auto add = [](int a, int b) -> int { return a + b; };
auto result = add(5, 3);

// Capture by value / by reference
int multiplier = 10;
auto scale = [multiplier](int x) -> int { return x * multiplier; };

int counter = 0;
auto increment = [&counter]() { counter++; };

// Capture defaults, with optional exceptions and `this`
auto f = [=]() { return counter + multiplier; };       // everything by value
auto g = [&]() { counter += multiplier; };             // everything by reference
auto h = [&, multiplier]() { counter += multiplier; }; // by-ref default, one by-value exception
auto m = [this]() { return health; };                  // inside a method

// AUTO-CAPTURE: an empty [] still sees outer locals it references — they are
// snapshot BY VALUE at creation time (use [&] / [&name] for write-through).
int base = 10;
auto n = []() { return base + 1; };                    // 11
```

### Variable Types: auto vs var
```cpp
// auto - type inference with locking (homogeneous containers)
auto x = 5;              // Locked to int
x = 10;                  // OK
x = 3.9;                 // OK: truncates to 3 (C++-style conversion, NOT an error)
x = "hello";             // ERROR: type mismatch (no int conversion)

auto nums = [1, 2, 3];   // All elements must be same type
auto mixed = [1, "x"];   // ERROR: heterogeneous not allowed

// var - dynamic typing (heterogeneous containers allowed)
var y = 5;               // Any type
y = "hello";             // OK: var allows any type

var mixed = [1, "two", 3.14];  // OK: var allows mixed types
```
See `STRONG_TYPES.md` for the full runtime type ladder and `static_checking.md` for
compile-time enforcement (off/warn/strict).

### Generic Containers
```cpp
// Arrays use square brackets []
array<int> numbers = [1, 2, 3, 4, 5];
auto matrix = [[1, 2], [3, 4], [5, 6]];

// Maps: BOTH literal styles parse
map<string, int> scores = {
    {"Alice", 100},           // C++ style: {key, value} entries
    {"Bob", 85}
};
auto point = {x: 10, y: 20};             // JSON style: bare keys become strings
auto config = {"width": 800, "height": 600};

// Complex nested structures
array<map<string, int>> locations = [
    {{"x", 10}, {"y", 20}},
    {{"x", 30}, {"y", 40}}
];

weak_ptr<Creature> target;
shared_ptr<Creature> leader = boss;      // explicit reference semantics (no clone on assign)
shared_ptr<auto> chief = boss;           // pointee INFERRED from the initializer's class,
                                         // then enforced like the explicit spelling (note 16)

var boss2 = new Creature("Boss");        // `new` = shared_ptr sugar: boss2 is a
auto ally = boss2;                        // shared_ptr<Creature>; copies alias, so
ally.health = 50;                         // boss2.health is 50 too
```

### Control Flow
```cpp
// Switch with break-by-default (explicit fallthrough required)
switch (weapon_type) {
    case "sword":
        damage = 10;
    case "bow":
        damage = 8;
    case "magic":
        damage = 15;
        fallthrough;  // Explicit fallthrough
    case "enchanted":
        damage += 5;
    default:
        damage = 5;
}

// Range-based for — optional const and & bindings
for (auto item : items) { process(item); }
for (auto& x : nums) { x *= 2; }             // mutate in place
for (const auto& row : grid) { render(row); }

// Traditional for
for (int i = 0; i < 10; i++) {
    sum += i;
}
```

### Exception Handling
```cpp
try {
    throw "Something went wrong!";
} catch (e) {
    print("Caught: {}", e);       // e is the THROWN VALUE with its type preserved
}

try { throw 42; } catch (e) { /* e is the int 42, not a string */ }

// Catch without variable
try {
    risky_operation();
} catch {
    print("An error occurred");
}

// Re-throw (throw is an expression, so it also composes: x > 0 ? x : throw "neg")
try {
    throw 42;
} catch (e) {
    throw;  // Re-throw same exception
}
```
Terminal errors (execution-budget exhaustion, latched memory-cap) skip every `catch` and
surface at the host boundary — see `EXCEPTION_DESIGN.md`.

### Ternary Operator
```cpp
int health = 100;
string status = health > 0 ? "alive" : "dead";

// Nested ternary
string healthLevel = health > 75 ? "healthy" :
                     health > 25 ? "wounded" : "critical";
```

## Design Notes

1. **Type Inference**: `auto` infers type from initializer and locks it; conversions then follow
   C++ rules (float→locked-int truncates). `var` allows any type dynamically.
2. **Container Homogeneity**: `auto` containers require homogeneous elements. `var` allows heterogeneous.
3. **Reference Semantics**: `&` for reference parameters, captures, declarations, returns, and
   range-for bindings. Lvalue arguments (`f(obj.field)`, `f(arr[i])`) bind by reference to
   `&` parameters. References into **typed containers carry the element type**: storing a
   mismatched type through `auto& x = intArr[i]`, `for (auto& x : intArr)`, or a ref
   parameter bound to an element errors exactly like direct element assignment. `var`
   (any-tagged) containers stay unconstrained.
4. **RAII**: Constructors and destructors called automatically.
5. **Generic Types**: Limited to built-in containers plus C++-bound template types; not full
   script-side templates.
6. **Lambda Syntax**: C++11-style with explicit captures, `[=]`/`[&]` defaults, and `this`.
7. **Ternary Operator**: Right-associative conditional expression.
8. **Virtual by Default**: All methods are virtual; `override` on class methods is optional
   documentation. Replacing a same-name/same-arity **namespace function**, however, REQUIRES
   `override` placed after the parameter list (`int f() override {}`); silent replacement is
   an error without it. Top-level free functions are NOT override-gated: a same-name
   definition silently replaces the previous one.
9. **`new` is shared_ptr sugar**: `new T(args)` (or `new T{args}`) is exactly
   `shared_ptr<T>(args)` — it constructs a `T` and tags the result with reference
   semantics, so copies share instead of clone. A `var`/`auto` declaration keeps the
   marker: `var p = new P();` IS `shared_ptr<P> p = P();`. Plain construction stays
   `Type(args)` / `Type{args}` (value semantics).
10. **Super Access**: `super::` to call parent class methods; `: super(...)`/`: this(...)` for
    constructor delegation (the ONLY initializer-list forms).
11. **Switch Semantics**: Break-by-default. Use `fallthrough;` for explicit fall-through.
12. **Visibility**: `public:`/`private:`/`protected:` labels are **enforced at member
    access** on both backends: private = the declaring class's methods only; protected =
    declaring class + subclasses; default is public. Lambdas defined inside a method
    inherit that class's access; free functions and top-level code see public members
    only. Violations raise a catchable error ("Cannot access private member 'x' of
    class 'C'"). Scope notes: constructors are exempt (labels govern member access, not
    construction); operator methods dispatch through operators regardless of labels;
    unqualified self-field reads inside methods resolve through the instance scope and
    are not re-checked (explicit `this.x` is); the host C++ API (`get_field`/`set_field`,
    serialization, reflection) is deliberately unrestricted. Hot reload is permissive:
    enforcement consults the class's CURRENT definition at access time. Enforcement
    costs one flag test on classes with no nonpublic members (measured: within
    benchmark noise), so there is no engine toggle.
13. **Collection Literals**: arrays always use `[...]`; maps use `{...}` with either
    `{key, value}` entries or JSON-style `key: value` pairs.
14. **Subscript Operations**: Full support for array/map subscripting with both read and write
    operations; `operator[]` is overloadable on script classes. Strings support READ-ONLY
    char subscript: `s[i]` yields a `char` (bounds-checked like arrays: "String index {i}
    out of bounds for string of size {n}"; negative indices are out of bounds — `at(i)`
    keeps its negative-index normalization and returns a 1-char string). `s[i] = c` is a
    clear error ("Strings are read-only through subscript") — strings share storage under
    copy, so subscript writes would need copy-on-write; possible future work. `char`
    supports the full comparison set including ordering (`c >= '1' && c <= '9'`);
    char-vs-int ordering is deliberately an error (no silent promotion).
15. **Parallel Builtins (v0.5, no new syntax)**: `parallel_transform(arr, fn)` and
    `parallel_transform(arr, fn, weight_fn)` are ordinary builtin CALLS — the optional
    weight hint is simply the third argument (the `parallel_for` keyword and its hint
    syntax are future work; see docs/parallel_design.md). `fn` must be a script-defined
    function of one value parameter whose body passes the parallel admission walk
    (no unwhitelisted host calls, no lambdas/classes/yield); elements and results must
    be value-semantic (null/int/float/string/char/bool + arrays/maps of the same).
    **Captured reads (v0.5)**: the body may READ enclosing globals — scalars, strings,
    and value-semantic containers — seeing barrier-time content, identical at every
    worker count (an all-primitive container touched only by subscript reads is a
    zero-copy "borrow"; everything else is copied per worker at the barrier and
    charged to memory_cap). WRITES to enclosing state stay errors in every shape:
    direct assigns, subscript/compound/incdec stores, mutating (or unknown) methods
    on captured receivers, `var&` aliases, by-reference arguments, and by-reference
    iteration over captured containers. Violations raise
    "parallel_transform: <reason> at line:col" — never silently serial.
    `thread_count()` reports the worker count a region will use.
16. **Typed shared_ptr declarations enforce their pointee** (Dev ruling 2026-07):
    `shared_ptr<Bar> wrong = Foo();` and wrong-class sp-aliasing error ("type must match
    or be a subclass"); derived→base upcasts stay legal, null initialization is fine, and
    null stays assignable after establishment. `shared_ptr<auto>` INFERS the pointee from
    the initializer's exact class, then enforces it exactly like the explicit spelling
    (construct-and-share `shared_ptr<auto> v = T();` included) — a missing, null, or
    non-class initializer has nothing to infer and errors (use `var`, or an explicit
    `shared_ptr<T>`). Inference is a DECLARATION feature: `shared_ptr<auto>` fields infer
    from their default (field writes keep the pre-existing lenient field-store semantics,
    same as explicit `shared_ptr<T>` fields); `shared_ptr<auto>` parameters are not
    supported (they error at call time — a plain `auto`/untyped parameter already binds
    anything, or use an explicit `shared_ptr<T>`). The ladder has exactly three handle
    spellings: `shared_ptr<T>` (enforced pointee), `shared_ptr<auto>` (infer then
    enforce), and plain `var` (FULLY DYNAMIC — reassigns to anything: `=` with a handle
    OR a value OR a primitive rhs always REBINDS the variable to a copy of the rhs value,
    never reaching through a held handle and never refusing an assignment. `var p = new
    Foo(); p = new Bar();` rebinds the handle; `p = someFooValue;` rebinds to an
    independent copy of that value, leaving aliases of the old object untouched; `p = 5;`
    rebinds to an int. The typed/auto/`shared_ptr<T>`/`shared_ptr<auto>` tiers enforce and
    a value rhs to those still copy-assigns INTO the underlying object with its checks —
    `var` does neither; `auto` copies of a var-held handle re-lock to the plain spelling).
    `shared_ptr<var>`
    is DELIBERATELY a parse error ("shared_ptr<var> is not supported: use var (holds and
    rebinds any shared_ptr) or shared_ptr<auto> (infers then enforces the pointee)") —
    the constrained-but-dynamic middle ground has no use case, and `var` meaning
    something different inside angle brackets would muddy the ladder (Dev ruling 2026-07).
