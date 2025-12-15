class Test {
    int value = 42;

    function getValue() -> int {
        return value;  // implicit instance field access
    }

    function setValue(int v) {
        value = v;  // implicit assignment
    }
}

auto t = Test();
print("Initial value: " + to_string(t.getValue()));
t.setValue(100);
print("After setValue: " + to_string(t.getValue()));
t.getValue()
