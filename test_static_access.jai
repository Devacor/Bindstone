class Counter {
    static int count = 0;
}

print("Testing static field access via class name:");
print("Counter::count = " + to_string(Counter::count));

Counter::count = 5;
print("After assignment: Counter::count = " + to_string(Counter::count));
