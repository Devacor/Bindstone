# JaiScript Grammar Specification

## Overview

JaiScript uses a C++-like syntax with RAII semantics and built-in support for hot-reloading and serialization.

## Lexical Elements

### Keywords
```
bool break char class const continue else false float for
if int map array new null private public return string 
this true void while auto var super
```

### Operators
```
// Arithmetic
+ - * / %

// Assignment
= += -= *= /= %=

// Comparison
== != < > <= >=

// Logical
&& || !

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

parameterList = parameter ("," parameter)*

parameter = type "&"? IDENTIFIER

// Variable declarations
variableDeclaration = type IDENTIFIER ("=" expression)? ";"

type = primitiveType
     | IDENTIFIER                           // User-defined type
     | "array" "<" type ">"                // Array<T>
     | "map" "<" type "," type ">"         // Map<K,V>
     | "SharedPtr" "<" type ">"            // SharedPtr<T>
     | "WeakPtr" "<" type ">"              // WeakPtr<T>
     | "auto"                              // Type inference
     | "var"                               // Type inference (same as auto)

primitiveType = "int" | "float" | "string" | "char" | "bool" | "void"

// Statements
statement = expressionStatement
          | blockStatement
          | ifStatement
          | whileStatement
          | forStatement
          | returnStatement
          | breakStatement
          | continueStatement
          | variableDeclaration

expressionStatement = expression ";"

blockStatement = "{" declaration* "}"

ifStatement = "if" "(" expression ")" statement ("else" statement)?

whileStatement = "while" "(" expression ")" statement

forStatement = "for" "(" variableDeclaration? ";" expression? ";" expression? ")" statement

returnStatement = "return" expression? ";"

breakStatement = "break" ";"

continueStatement = "continue" ";"

// Expressions (precedence from lowest to highest)
expression = assignmentExpression

assignmentExpression = ternaryExpression (assignmentOperator assignmentExpression)?

assignmentOperator = "=" | "+=" | "-=" | "*=" | "/=" | "%="

ternaryExpression = logicalOrExpression ("?" expression ":" ternaryExpression)?

logicalOrExpression = logicalAndExpression ("||" logicalAndExpression)*

logicalAndExpression = equalityExpression ("&&" equalityExpression)*

equalityExpression = relationalExpression (("==" | "!=") relationalExpression)*

relationalExpression = additiveExpression (("<" | ">" | "<=" | ">=") additiveExpression)*

additiveExpression = multiplicativeExpression (("+" | "-") multiplicativeExpression)*

multiplicativeExpression = unaryExpression (("*" | "/" | "%") unaryExpression)*

unaryExpression = ("!" | "-" | "++" | "--" | "&") unaryExpression
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

escape_sequence = "\\" [nrt"'\\]
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
        print("Creature {} has died\n", name);
    }
};

// Inheritance - methods automatically override
class Dragon : Creature {
public:
    Dragon() : Creature("Dragon") {}
    
    // Automatically overrides parent's onDeath
    void onDeath() {
        print("The mighty dragon falls!\n");
        super::onDeath();  // Call parent implementation
    }
};

// Multiple inheritance (no diamond problem allowed)
class Swimmer {
public:
    void swim() { print("Swimming\n"); }
};

class Flyer {
public:
    void fly() { print("Flying\n"); }
};

class Duck : Swimmer, Flyer {
public:
    void move() {
        swim();  // From Swimmer
        fly();   // From Flyer
    }
};

// This would be an error - methods collide:
// class BadExample : ClassWithFoo, AnotherClassWithFoo { };
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

### Generic Containers
```cpp
// Arrays use square brackets []
array<int> numbers = [1, 2, 3, 4, 5];
var matrix = [[1, 2], [3, 4], [5, 6]];

// Maps use curly braces {} with nested entries
map<string, int> scores = {
    {"Alice", 100},
    {"Bob", 85}
};

// Type construction uses Type{args}
Vec2 position = Vec2{10.5, 20.3};
var points = [Vec2{0, 0}, Vec2{1, 1}, Vec2{2, 2}];

// Complex nested structures
array<map<string, Vec2>> locations = [
    {{"player", Vec2{1, 1}}, {"enemy", Vec2{5, 3}}},
    {{"boss", Vec2{10, 10}}, {"treasure", Vec2{2, 8}}}
];

SharedPtr<Creature> boss = new Creature("Dragon");
```

### Ternary Operator
```cpp
int health = 100;
string status = health > 0 ? "alive" : "dead";

// Nested ternary
string healthLevel = health > 75 ? "healthy" : 
                     health > 25 ? "wounded" : "critical";

// In function calls
print("Player is {}\n", health > 0 ? "alive" : "dead");
```

## Design Notes

1. **Type Inference**: `auto` and `var` keywords for type inference (equivalent)
2. **Reference Semantics**: `&` for reference types in parameters and captures
3. **RAII**: Constructors and destructors called automatically
4. **Generic Types**: Limited to built-in containers, not full templates
5. **Lambda Syntax**: C++11-style with explicit captures
6. **Ternary Operator**: Right-associative conditional expression
7. **Virtual by Default**: All methods are virtual, no need for `virtual` keyword
8. **Smart Pointers**: `new` returns `SharedPtr<T>` automatically
9. **Super Access**: `super::` to call parent class methods
10. **Multiple Inheritance**: Allowed but no diamond pattern - methods must not collide
11. **Collection Literals**: 
    - **Arrays** always use `[...]` syntax
    - **Maps** always use `{...}` syntax with entries as `{key, value}`
    - **Type construction** uses `Type{args}` for C++ compatibility
    - This eliminates ambiguity and provides clear, consistent syntax
12. **Subscript Operations**: Full support for array/map subscripting with both read and write operations
    - **Array subscript**: `arr[index]` for reading, `arr[index] = value` for writing
    - **Map subscript**: `map[key]` for reading, `map[key] = value` for writing
    - **Nested subscripts**: `matrix[i][j]` works as expected