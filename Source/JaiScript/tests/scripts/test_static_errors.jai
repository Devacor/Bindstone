class TestClass {
    static int static_field = 42;
    int instance_field = 10;
}

// This should throw - accessing non-existent static field
print("Test 1: Accessing non-existent static field");
try {
    print(TestClass::non_existent);
    print("ERROR: Should have thrown!");
} catch (e) {
    print("Good: Caught exception");
}

// This should throw - accessing instance field as static
print("Test 2: Accessing instance field as static");
try {
    print(TestClass::instance_field);
    print("ERROR: Should have thrown!");
} catch (e) {
    print("Good: Caught exception");
}

// This should throw - using :: on non-class
print("Test 3: Using :: on non-class");
try {
    auto x = 5;
    print(x::something);
    print("ERROR: Should have thrown!");
} catch (e) {
    print("Good: Caught exception");
}

print("All tests completed");
