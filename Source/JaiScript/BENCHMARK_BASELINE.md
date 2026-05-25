# JaiScript Benchmark Baseline (2026-05-24, pre-optimization)

Build: x64-Release BENCHMARKS, MSVC 17.0, Windows 11

## Core Benchmarks (uS/iteration, 1000 iterations)

| Benchmark                        | JaiScript |
|----------------------------------|-----------|
| Integer Addition                 | 1         |
| Float Multiplication             | 1         |
| Variable Operations              | 5         |
| Function Calls                   | 6         |
| Array Push/Pop                   | 9         |
| Map Insert/Lookup                | 6         |
| Class Creation                   | 13        |
| Method Invocation                | 11        |
| For Loop (100 iterations)        | 9         |
| String Concatenation             | 4         |
| Engine Creation                  | 64        |
| Stdlib Registration              | 216       |
| Complex Expression               | 2         |
| Class Inheritance                | 128       |
| Hot Loop (1000 iterations)       | 61        |
| Simple Compound Assignment (x100)| 20        |
| Variable Lookup Heavy            | 7         |

## Loop Benchmarks

| Benchmark                                    | JaiScript |
|-----------------------------------------------|-----------|
| Hot Loop (10x100 nested)                      | 65        |
| Hot Loop (auto, 1000 iter)                    | 54        |
| Hot Loop (int, 1000 iter)                     | 53        |
| Hot Loop (var, 1000 iter)                     | 58        |
| Range-For (copy, 100 elements)                | 25        |
| Range-For (reference, 100 elements)           | 26        |
| Range-For (copy, 10 elements)                 | 4         |
| Range-For (reference, 10 elements)            | 5         |

## vs ChaiScript (uS/iteration)

| Benchmark                        | JaiScript | ChaiScript | Speedup |
|----------------------------------|-----------|------------|---------|
| Integer Addition                 | 1         | 2          | 2.0x    |
| Float Multiplication             | 1         | 2          | 2.0x    |
| Variable Operations              | 3         | 6          | 2.0x    |
| Function Calls                   | 2         | 7          | 3.5x    |
| Array Push/Pop                   | 12        | 23         | 1.9x    |
| Map Insert/Lookup                | 7         | 14         | 2.0x    |
| Class Creation                   | 4         | 21         | 5.3x    |
| Method Invocation                | 5         | 18         | 3.6x    |
| For Loop                         | 10        | 15         | 1.5x    |
| Variable Lookup Heavy            | 9         | 13         | 1.4x    |
| Complex Expression               | 2         | 6          | 3.0x    |
| Factorial(10)                    | 7         | 44         | 6.3x    |
| Fibonacci(6)                     | 17        | 99         | 5.8x    |
| Recurse with Locals (depth=10)   | 31        | 99         | 3.2x    |
| Binary Search                    | 11        | 30         | 2.7x    |
| Bubble Sort (10 elements)        | 94        | 107        | 1.1x    |
| BST (15 nodes)                   | 618       | skipped    | -       |
| String Copy (5 copies)           | 8         | 20         | 2.5x    |
| String Concat (20 iterations)    | 12        | 29         | 2.4x    |
| String find/substr/size          | 8         | 20         | 2.5x    |
| C++ BST (15 nodes)               | 68        | 104        | 1.5x    |

## vs Squirrel (uS/iteration)

| Benchmark                        | JaiScript | Squirrel   | Ratio   |
|----------------------------------|-----------|------------|---------|
| Integer Addition                 | 1         | 2          | 2.0x    |
| Float Multiplication             | 1         | 2          | 2.0x    |
| Variable Operations              | 2         | 2          | 1.0x    |
| Function Calls                   | 4         | 4          | 1.0x    |
| Array Push/Pop                   | 11        | 4          | 0.4x    |
| Map Insert/Lookup                | 6         | 3          | 0.5x    |
| Class Creation                   | 6         | 3          | 0.5x    |
| Method Invocation                | 8         | 6          | 0.8x    |
| For Loop                         | 13        | 4          | 0.3x    |
| Factorial(10)                    | 8         | 3          | 0.4x    |
| Fibonacci(15)                    | 1272      | 78         | 0.06x   |
| Recurse with Locals (depth=15)   | 51        | 3          | 0.06x   |
| Range-For (10 elements)          | 8         | 4          | 0.5x    |
| String Concat                    | 13        | 6          | 0.5x    |
| Null Check                       | 5         | 3          | 0.6x    |
| Hot Loop (1000 iter)             | 56        | 15         | 0.3x    |
| BST (15 nodes)                   | 593       | 19         | 0.03x   |
| C++ BST (15 nodes)               | 60        | 14         | 0.2x    |

## Key Observations

- JaiScript beats ChaiScript on every benchmark (1.1x-6.3x faster)
- Squirrel (bytecode VM) significantly outperforms JaiScript interpreter on:
  - Recursion: Fibonacci(15) 16x slower, BST 31x slower
  - Loops: For loop 3.3x slower, Hot loop 3.7x slower
  - Containers: Array push/pop 2.8x slower, Map 2x slower
- JaiScript wins on simple arithmetic (2x faster than both)
- The recursion and loop gaps vs Squirrel are the primary optimization targets
