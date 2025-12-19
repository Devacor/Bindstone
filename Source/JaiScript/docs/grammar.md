# JaiScript Grammar Specification

## Overview

JaiScript uses a C++-like syntax with RAII semantics and built-in support for hot-reloading and serialization.

## Lexical Elements

### Keywords
```
auto array bool break case catch char class const continue
default else fallthrough false float for if int map new null
private public return string switch this throw true try var
void while super weak_ptr
```

### Operators
```
// Arithmetic
+ - * / %

// Assignment
= += -= *= /= %= &= |= ^= <<= >>=

// Comparison
== != < > <= >= <=>

// Logical
&& || !

// Bitwise
& | ^ ~ << >>

// Increment/Decrement
++ --

// Member access
. ->

// Reference
&

// Scope resolution
::

// Super class access
super::

// Ternary
? :
```

### Literals
- **Integer**: `42`, `-17`, `0xFF`, `0b1010`
- **Float**: `3.14`, `-0.5`, `1e10`, `2.5e-3`
- **String**: `"hello"`, `"escaped \"quote\""`, `"newline\n"`
- **Character**: `'a'`, `'\n'`, `'\\'`
- **Boolean**: `true`, `false`
- **Null**: `null`

## Grammar Rules (EBNF-style)

```ebnf
// Program structure
program = declaration*

declaration = classDeclaration
            | functionDeclaration
            | variableDeclaration
            | statement

// Class declarations
classDeclaration = "class" IDENTIFIER (":" classBaseList)? "{" classMember* "}"

classBaseList = IDENTIFIER ("," IDENTIFIER)*   // Multiple inheritance

classMember = visibility? (constructorDeclaration | destructorDeclaration | methodDeclaration | fieldDeclaration)

visibility = "public" | "private"

constructorDeclaration = IDENTIFIER "(" parameterList? ")" (":" initializerList)? blockStatement

destructorDeclaration = "~" IDENTIFIER "(" ")" blockStatement

methodDeclaration = type IDENTIFIER "(" parameterList? ")" blockStatement
                  | "auto" IDENTIFIER "(" parameterList? ")" "->" type blockStatement

fieldDeclaration = type IDENTIFIER ("=" expression)? ";"

// Function declarations
functionDeclaration = "auto" IDENTIFIER "(" parameterList? ")" "->" type blockStatement
                    | type IDENTIFIER "(" parameterList? ")" blockStatement
                    | "function" IDENTIFIER "(" parameterList? ")" blockStatement

parameterList = parameter ("," parameter)*

parameter = type "&"? IDENTIFIER

// Variable declarations
variableDeclaration = type IDENTIFIER ("=" expression)? ";"

type = primitiveType
     | IDENTIFIER                           // User-defined type
     | "array" "<" type ">"                // array<T>
     | "map" "<" type "," type ">"         // map<K,V>
     | "weak_ptr" "<" type ">"             // weak_ptr<T>
     | "auto"                              // Type inference with locking
     | "var"                               // Dynamic typing (any type allowed)

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
          | throwStatement
          | tryStatement
          | variableDeclaration

expressionStatement = expression ";"

blockStatement = "{" declaration* "}"

ifStatement = "if" "(" expression ")" statement ("else" statement)?

whileStatement = "while" "(" expression ")" statement

forStatement = "for" "(" variableDeclaration? ";" expression? ";" expression? ")" statement

forRangeStatement = "for" "(" variableDeclaration ":" expression ")" statement

switchStatement = "switch" "(" expression ")" "{" caseClause* "}"

caseClause = ("case" expression | "default") ":" statement* fallthroughStatement?

fallthroughStatement = "fallthrough" ";"

returnStatement = "return" expression? ";"

breakStatement = "break" ";"

continueStatement = "continue" ";"

throwStatement = "throw" expression? ";"

tryStatement = "try" blockStatement catchClause

catchClause = "catch" ("(" IDENTIFIER ")")? blockStatement

// Expressions (precedence from lowest to highest)
expression = assignmentExpression

assignmentExpression = ternaryExpression (assignmentOperator assignmentExpression)?

assignmentOperator = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>="

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
                | postfixExpression

postfixExpression = primaryExpression (postfixOperator)*

postfixOperator = "++"
                | "--"
                | "." IDENTIFIER
                | "->" IDENTIFIER
                | "[" expression "]"
                | "(" argumentList? ")"

primaryExpression = literal
                  | IDENTIFIER
                  | "this"
                  | "super"
                  | "(" expression ")"
                  | lambdaExpression
                  | newExpression
                  | arrayLiteral
                  | mapLiteral

literal = INTEGER_LITERAL
        | FLOAT_LITERAL
        | STRING_LITERAL
        | CHAR_LITERAL
        | BOOLEAN_LITERAL
        | "null"

lambdaExpression = "[" captureList? "]" "(" parameterList? ")" ("->" type)? blockStatement

captureList = capture ("," capture)*

capture = "&"? IDENTIFIER    // & for by-reference, default is by-value

newExpression = "new" type "(" argumentList? ")"

arrayLiteral = "[" (expression ("," expression)*)? "]"

mapLiteral = "{" (mapEntry ("," mapEntry)*)? "}"

mapEntry = "{" expression "," expression "}"

argumentList = expression ("," expression)*

// Lexical tokens
IDENTIFIER = [a-zA-Z_][a-zA-Z0-9_]*

INTEGER_LITERAL = [0-9]+
                | "0x" [0-9a-fA-F]+
                | "0b" [01]+

FLOAT_LITERAL = [0-9]+ "." [0-9]+ ([eE] [+-]? [0-9]+)?

STRING_LITERAL = "\"" (escape_sequence | [^"\\])* "\""

CHAR_LITERAL = "'" (escape_sequence | [^'\\]) "'"

BOOLEAN_LITERAL = "true" | "false"

escape_sequence = "\\" [nrt"'\\0]
```

## Example Programs

### Basic Class
```cpp
class Creature {
public:
    string name;
    int health = 100;

    Creature(const string& n) : name(n) {}

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

// Inheritance - methods automatically override
class Dragon : Creature {
public:
    Dragon() : Creature("Dragon") {}

    void onDeath() {
        print("The mighty dragon falls!");
        super::onDeath();  // Call parent implementation
    }
}
```

### Lambda and Function Variables
```cpp
auto add = [](int a, int b) -> int { return a + b; };
auto result = add(5, 3);

// Capture by value
int multiplier = 10;
auto scale = [multiplier](int x) -> int { return x * multiplier; };

// Capture by reference
int counter = 0;
auto increment = [&counter]() { counter++; };
```

### Variable Types: auto vs var
```cpp
// auto - type inference with locking (homogeneous containers)
auto x = 5;              // Locked to int
x = 10;                  // OK
x = "hello";             // ERROR: type mismatch

auto nums = [1, 2, 3];   // All elements must be same type
auto mixed = [1, "x"];   // ERROR: heterogeneous not allowed

// var - dynamic typing (heterogeneous containers allowed)
var y = 5;               // Any type
y = "hello";             // OK: var allows any type

var mixed = [1, "two", 3.14];  // OK: var allows mixed types
```

### Generic Containers
```cpp
// Arrays use square brackets []
array<int> numbers = [1, 2, 3, 4, 5];
auto matrix = [[1, 2], [3, 4], [5, 6]];

// Maps use curly braces {} with nested entries
map<string, int> scores = {
    {"Alice", 100},
    {"Bob", 85}
};

// Complex nested structures
array<map<string, int>> locations = [
    {{"x", 10}, {"y", 20}},
    {{"x", 30}, {"y", 40}}
];

weak_ptr<Creature> target;
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

// Range-based for
for (auto item : items) {
    process(item);
}

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
    print("Caught: {}", e);
}

// Catch without variable
try {
    risky_operation();
} catch {
    print("An error occurred");
}

// Re-throw
try {
    throw 42;
} catch (e) {
    throw;  // Re-throw same exception
}
```

### Ternary Operator
```cpp
int health = 100;
string status = health > 0 ? "alive" : "dead";

// Nested ternary
string healthLevel = health > 75 ? "healthy" :
                     health > 25 ? "wounded" : "critical";
```

## Design Notes

1. **Type Inference**: `auto` infers type from initializer and locks it. `var` allows any type dynamically.
2. **Container Homogeneity**: `auto` containers require homogeneous elements. `var` allows heterogeneous.
3. **Reference Semantics**: `&` for reference types in parameters and captures
4. **RAII**: Constructors and destructors called automatically
5. **Generic Types**: Limited to built-in containers, not full templates
6. **Lambda Syntax**: C++11-style with explicit captures
7. **Ternary Operator**: Right-associative conditional expression
8. **Virtual by Default**: All methods are virtual, no need for `virtual` keyword
9. **Smart Pointers**: `new` returns `shared_ptr<T>` automatically
10. **Super Access**: `super::` to call parent class methods
11. **Switch Semantics**: Break-by-default. Use `fallthrough;` for explicit fall-through.
12. **Collection Literals**:
    - **Arrays** always use `[...]` syntax
    - **Maps** always use `{...}` syntax with entries as `{key, value}`
13. **Subscript Operations**: Full support for array/map subscripting with both read and write operations
