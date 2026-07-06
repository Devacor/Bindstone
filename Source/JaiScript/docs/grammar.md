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
include int map namespace null nullptr override private public return
shared_ptr static string super switch this throw true try var void
weak_ptr while yield
```

Notes:
- `nullptr` is an alias for `null` (both lex to the null literal).
- There is **no `new` keyword**. Objects are constructed with `Type(args)` or `Type{args}`.
- `const` is accepted only where documented below (range-`for` bindings). It does not apply to
  parameters or general variable declarations.
- A leading `#` before `include`/`import` is skipped by the lexer, so `#include "x"` and
  `include "x"` are identical.

### Operators

```
// Arithmetic
+ - * / %

// Assignment (complete set — there are NO bitwise compound assigns)
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
also work. Nested braces inside `${...}` are balanced (e.g. `${ {"k": 1}["k"] }`).

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

// Parsed and serialized, but enforced NOWHERE — see design note 12
accessLabel = ("public" | "private") ":"

constructorDeclaration = IDENTIFIER "(" parameterList? ")" (":" ctorInitializer ("," ctorInitializer)*)? blockStatement

// Initializer lists accept ONLY super(...) / this(...) delegation.
// Field initializers like `: name(n)` do NOT parse — initialize fields in the body.
ctorInitializer = "super" "(" argumentList? ")"
                | "this" "(" argumentList? ")"

destructorDeclaration = "~" IDENTIFIER "(" ")" blockStatement

methodDeclaration = "coroutine"? "static"? "override"? type IDENTIFIER "(" parameterList? ")" ("->" type)? "override"? blockStatement
                  | "coroutine"? "function" ("static" | "override")* (IDENTIFIER | operatorName) "(" parameterList? ")" ("->" type)? blockStatement

operatorName = "operator" ("=" | "+" | "-" | "*" | "/" | "[]" | "<" | ">" | "<=" | ">=" | "==" | "!=")

fieldDeclaration = "static"? type IDENTIFIER ("=" expression)? ";"

// Function declarations
functionDeclaration = "coroutine"? ( "function" IDENTIFIER "(" parameterList? ")" ("->" type)? blockStatement
                                   | type "&"? IDENTIFIER "(" parameterList? ")" blockStatement
                                   | "auto" IDENTIFIER "(" parameterList? ")" "->" type blockStatement )

parameterList = parameter ("," parameter)*

// Four accepted parameter shapes; a default value may follow any of them.
// Once a parameter has a default, all later parameters must too.
parameter = type "&"? IDENTIFIER ("=" expression)?     // classic:  int x, Creature& c
          | type ":" IDENTIFIER ("=" expression)?      // shorthand: int: x
          | ":" IDENTIFIER ("=" expression)?           // auto shorthand: :x
          | IDENTIFIER ("=" expression)?               // untyped -> auto: foo(x)

// Variable declarations (reference declarations alias the initializer's storage)
variableDeclaration = type "&"? IDENTIFIER ("=" expression)? ";"
                    | type IDENTIFIER "{" argumentList? "}" ";"      // brace construction

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
                  | "(" expression ")"
                  | lambdaExpression
                  | arrayLiteral
                  | mapLiteral

literal = INTEGER_LITERAL
        | FLOAT_LITERAL
        | STRING_LITERAL            // includes desugared template strings
        | CHAR_LITERAL
        | BOOLEAN_LITERAL
        | "null" | "nullptr"

lambdaExpression = "[" captureList? "]" "(" parameterList? ")" ("->" type?)? blockStatement

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

Limitation (pending): an operator method cannot declare a **named class type** as its return
type (`Vec operator+(...)` is a parse error) — use the `function operator+(...)` form, which
infers the return type.

### Functions, Defaults, References
```cpp
function greet(name, string greeting = "Hello") {   // untyped param -> auto; defaults allowed
    print("{} {}", greeting, name);
}

int square(int: n) { return n * n; }                // type: name shorthand
auto id = [](:x) -> { return x; };                  // :name shorthand (auto), auto return

void heal(Creature& c, int amount) { c.health += amount; }
heal(party[0], 25);            // lvalue arguments bind by reference: arr[i], obj.field, locals

int& pick(array<int>& a, int i) { return a[i]; }    // reference returns
auto& slot = pick(nums, 2);                          // reference locals alias storage
slot = 99;                                           // writes through to nums[2]
```

### Coroutines
```cpp
coroutine function counter(int limit) {
    for (int i = 0; i < limit; i++) {
        yield i;                       // yield is an expression; bare `yield` also legal
    }
}

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

// Replacing an existing namespace function REQUIRES override:
namespace game::combat {
    override int base_damage() { return 12; }
}

enum Color { red, green, blue }
auto c = Color::green;

include "setup.jai";     // PHP-template style: ALWAYS runs the file's code in place,
                          // and may produce a value (the file's last/returned value)
import "creature.jai";   // module style: cached per path — declarations (classes, functions,
                          // namespaces) load ONCE; never produces a value
import(pathExpression);   // computed paths work for both; so does `#include <lib.jai>`
```

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
   `&` parameters.
4. **RAII**: Constructors and destructors called automatically.
5. **Generic Types**: Limited to built-in containers plus C++-bound template types; not full
   script-side templates.
6. **Lambda Syntax**: C++11-style with explicit captures, `[=]`/`[&]` defaults, and `this`.
7. **Ternary Operator**: Right-associative conditional expression.
8. **Virtual by Default**: All methods are virtual; `override` on class methods is optional
   documentation. Replacing a same-name/same-arity **namespace function**, however, REQUIRES
   `override` (silent replacement is an error without it).
9. **No `new`**: construction is `Type(args)` or `Type{args}`; script objects have shared
   reference semantics.
10. **Super Access**: `super::` to call parent class methods; `: super(...)`/`: this(...)` for
    constructor delegation (the ONLY initializer-list forms).
11. **Switch Semantics**: Break-by-default. Use `fallthrough;` for explicit fall-through.
12. **Visibility**: `public:`/`private:` labels parse and serialize but are **not enforced** —
    all members are effectively public at runtime.
13. **Collection Literals**: arrays always use `[...]`; maps use `{...}` with either
    `{key, value}` entries or JSON-style `key: value` pairs.
14. **Subscript Operations**: Full support for array/map subscripting with both read and write
    operations; `operator[]` is overloadable on script classes.
