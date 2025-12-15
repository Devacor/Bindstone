class Calculator {
    auto result = 0.0;
    auto memory = 0.0;

    void add(x) {
        result = result + x;
        print("After add, result = " + to_string(result));
    }

    void multiply(x) {
        result = result * x;
        print("After multiply, result = " + to_string(result));
    }

    void store() {
        print("Before store: result = " + to_string(result) + ", memory = " + to_string(memory));
        memory = result;
        print("After store: memory = " + to_string(memory));
    }
}

auto calc = Calculator();
calc.add(5.0);
calc.multiply(3.0);
calc.store();

print("Final: result = " + to_string(calc.result) + ", memory = " + to_string(calc.memory));

calc.result == 15.0 && calc.memory == 15.0
