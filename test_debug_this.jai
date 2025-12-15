class Counter {
    static int count = 0;
    int value = 0;

    Counter() {
        print("In constructor");
        print("this type: " + type_of(this));

        // Try accessing instance field - should work
        value = 5;
        print("value = " + to_string(value));

        // Try accessing static field via class name - should work
        print("Counter::count = " + to_string(Counter::count));

        // Try accessing static field without prefix - this is what's failing
        print("count (implicit) = " + to_string(count));
    }
}

print("Creating Counter instance...");
auto c = Counter();
