# JaiScript String Library - Implementation Status

## Completed

### Member Methods (in `interpreter.cpp` string_methods_)
All methods mutate in place when called on variables and return `self` for chaining.

**Observer Methods (non-mutating):**
- `length()` / `size()` - returns string length
- `empty()` - returns true if string is empty
- `at(index)` - returns char at index (supports negative indices)
- `front()` / `back()` - returns first/last char
- `substr(start, [count])` - returns substring (supports negative start)
- `find(substr, [start])` - finds first occurrence, returns -1 if not found
- `rfind(substr, [start])` - finds last occurrence
- `find_first_of(chars, [start])` - finds first char in set
- `find_last_of(chars, [start])` - finds last char in set
- `find_first_not_of(chars, [start])` - finds first char not in set
- `find_last_not_of(chars, [start])` - finds last char not in set
- `contains(substr)` - returns true if contains substring
- `starts_with(prefix)` - returns true if starts with prefix
- `ends_with(suffix)` - returns true if ends with suffix
- `count(substr)` - counts non-overlapping occurrences

**Parsing Methods:**
- `to_int([default], [base])` - parses string to int, supports bases 2-36
- `to_float([default])` - parses string to float

**Mutating Methods (modify in place, return self):**
- `to_lower()` - converts to lowercase
- `to_upper()` - converts to uppercase
- `trim([chars])` - trims chars from both ends (default: whitespace)
- `trim_left([chars])` - trims from left
- `trim_right([chars])` - trims from right
- `pad_left(len, [char])` - pads on left to target length
- `pad_right(len, [char])` - pads on right to target length
- `pad_center(len, [char])` - pads both sides (favors right on odd)
- `replace_first(old, new)` - replaces first occurrence
- `replace_last(old, new)` - replaces last occurrence
- `replace_all(old, new)` - replaces all occurrences (non-overlapping)
- `insert(pos, text)` - inserts text at position
- `erase(pos, count)` - erases count chars at position
- `remove_prefix(n)` - removes first n chars
- `remove_suffix(n)` - removes last n chars
- `reverse()` - reverses string in place
- `repeat(count)` - repeats string count times
- `split([delim])` - splits into array (no delim = split into chars)

### Array Method
- `join([sep])` - joins array elements into string with separator

### In-Place Mutation Fix
Added special handling in `visit_call_expr` to detect `variable.method()` pattern for strings and get a mutable reference via `environment_->get_ref()` instead of copying. This allows `s.to_lower()` to actually mutate `s`.

### Tests
Created `source/tests/stdlib/string_tests.cpp` with 61 comprehensive tests covering all methods.

---

## Remaining / Future Work

### Static Namespace Functions (`string::func(s)`)
The plan was to add copy-returning functions like:
```
string::to_lower(s)    // returns lowercase copy, doesn't mutate s
string::trim(s)        // returns trimmed copy
string::slice(s, start, end)  // Python-style slicing
```

**Implementation approach:** These can be defined in JaiScript itself:
```jai
namespace string {
    string to_lower(string s) {
        return s.to_lower();  // s is a copy, so mutating it is safe
    }

    string trim(string s, string chars = " \t\r\n") {
        return s.trim(chars);
    }
}
```

### Case Conversion Variants
- `capitalize_first()` - capitalize first character
- `capitalize_words()` - capitalize first char of each word
- `capitalize_sentences()` - capitalize first char of each sentence
- `to_camel_case()` / `to_pascal_case()` / `to_snake_case()` / `to_screaming_snake_case()`

### Utility Functions
- `string::slice(s, start, end)` - Python-style slicing with end-exclusive semantics
- `string::to_base64(s)` / `string::from_base64(s)` - Base64 encoding

### Regex Support (separate namespace)
- `regex::match(pattern, s)`
- `regex::search(pattern, s)`
- `regex::replace(pattern, replacement, s)`
- `regex::split(pattern, s)`

### Unicode Support (separate namespace, future)
- `unicode::to_lower(s)` - proper Unicode case conversion
- `unicode::to_upper(s)`
- `unicode::normalize(s, form)`

---

## Design Notes

1. **Mutation semantics:** Member methods mutate in place and return `self` for chaining. This works for simple variable access (`s.to_lower()`). For expressions like `get_string().to_lower()`, the mutation happens on a temporary.

2. **Pad character:** Accepts both `char` and `string` (uses first char of string).

3. **Negative indices:** Supported in `at()`, `substr()` - normalized as `idx += length`.

4. **Search returns:** All find methods return `-1` when not found.

5. **Error handling:** Throws on out-of-bounds access, empty search strings for replace.
