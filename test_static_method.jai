class Counter {
    static int count = 0;

    function test() {
        print("In test method, count = " + to_string(count));
    }

    Counter() {
        print("In constructor, attempting to access count...");
        print("count = " + to_string(count));
    }
}

print("Before creating instance:");
print("Counter::count = " + to_string(Counter::count));

auto c = Counter();
