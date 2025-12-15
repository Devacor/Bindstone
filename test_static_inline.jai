class Counter {
    static int count = 0;

    Counter() {
        print("Before: count = " + to_string(count));
        count = count + 1;
        print("After: count = " + to_string(count));
    }
}

auto c1 = Counter();
auto c2 = Counter();
Counter::count