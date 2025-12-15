class Counter {
    static int count = 0;
    int value = 0;

    function showStaticViaExplicit() {
        // This uses explicit access - should work
        print("Explicit Counter::count = " + to_string(Counter::count));
    }

    function showStaticImplicit() {
        // This uses implicit access - may fail
        print("Implicit count = " + to_string(count));
    }

    function testMethod() {
        print("In testMethod:");
        showStaticViaExplicit();
        showStaticImplicit();
    }
}

print("Before creating any instance:");
print("Counter::count = " + to_string(Counter::count));

print("\nCreating instance...");
auto c = Counter();

print("\nCalling methods on instance:");
c.showStaticViaExplicit();
c.showStaticImplicit();
