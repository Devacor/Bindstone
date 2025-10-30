// Test file for new array and map methods

// Array methods tests
print("=== Array Methods Tests ===");

var arr = [1, 2, 3, 4, 5];
print("Original array: " + arr);

// Test index_of
print("index_of(3): " + arr.index_of(3));
print("index_of(10): " + arr.index_of(10));

// Test has/contains
print("has(3): " + arr.has(3));
print("has(10): " + arr.has(10));
print("contains(4): " + arr.contains(4));

// Test first/last/length
print("first(): " + arr.first());
print("last(): " + arr.last());
print("length(): " + arr.length());

// Test slice
var sliced = arr.slice(1, 3);
print("slice(1, 3): " + sliced);
var sliced_neg = arr.slice(-3, -1);
print("slice(-3, -1): " + sliced_neg);

// Test filter
var evens = arr.filter([](auto x) -> auto { return x % 2 == 0; });
print("filter(evens): " + evens);

// Test sort/reverse
var unsorted = [5, 2, 8, 1, 9];
print("Before sort: " + unsorted);
unsorted.sort();
print("After sort: " + unsorted);
unsorted.reverse();
print("After reverse: " + unsorted);

// Test remove
var to_remove = [1, 2, 3, 4, 5];
var removed = to_remove.remove(2);
print("After remove(2): " + to_remove + ", returned: " + removed);

// Test remove_if
var numbers = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
var count = numbers.remove_if([](auto x) -> auto { return x % 2 == 0; });
print("After remove_if(evens): " + numbers + ", removed: " + count);

print("");
print("=== Map Methods Tests ===");

var map = {"a": 1, "b": 2, "c": 3};
print("Original map: " + map);

// Test has/contains
print("has(\"a\"): " + map.has("a"));
print("has(\"z\"): " + map.has("z"));
print("contains(\"b\"): " + map.contains("b"));

// Test get
print("get(\"a\", 0): " + map.get("a", 0));
print("get(\"z\", 99): " + map.get("z", 99));

// Test length
print("length(): " + map.length());

// Test remove
var removed_key = map.remove("b");
print("After remove(\"b\"): " + map + ", returned: " + removed_key);

// Test filter
var map2 = {"a": 1, "b": 2, "c": 3, "d": 4};
var filtered = map2.filter([](auto k, auto v) -> auto { return v > 2; });
print("filter(value > 2): " + filtered);

// Test remove_if
var map3 = {"a": 1, "b": 2, "c": 3, "d": 4};
var removed_count = map3.remove_if([](auto k, auto v) -> auto { return v % 2 == 0; });
print("After remove_if(even values): " + map3 + ", removed: " + removed_count);

// Test to_array
var map4 = {"x": 10, "y": 20};
var pairs = map4.to_array();
print("to_array(): " + pairs);

print("");
print("=== All Tests Complete ===");
