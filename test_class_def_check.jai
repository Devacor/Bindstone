class Counter {
    static int count = 0;
    int value = 0;

    function getClassName() -> string {
        return "Counter";
    }

    function hasStaticCount() -> bool {
        // Try checking if we can access count via explicit path
        return Counter::count >= 0;  // Just to see if it exists
    }
}

auto c = Counter();
print("Class name: " + c.getClassName());
print("Has static count check: " + to_string(c.hasStaticCount()));

// Direct access should work
print("Direct Counter::count = " + to_string(Counter::count));
