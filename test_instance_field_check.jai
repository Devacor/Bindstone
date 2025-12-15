class TestClass {
    static int staticField = 42;
    int instanceField = 10;

    function checkFields() {
        print("Checking fields inside method:");

        // Instance field should work
        print("  instanceField = " + to_string(instanceField));

        // Static field via explicit should work
        print("  TestClass::staticField = " + to_string(TestClass::staticField));

        // Static field via implicit - this is what fails
        print("  staticField (implicit) = " + to_string(staticField));
    }
}

auto t = TestClass();
t.checkFields();
