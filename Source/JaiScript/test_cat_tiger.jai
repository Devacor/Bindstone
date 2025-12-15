class Cat {
    int a = 0;
    Cat(int val) { a = val; }
}

class Tiger : Cat {
    int b = 5;
    Tiger() : super(5) {}

    int getTotal() {
        return a + b;
    }
}

auto tiger = Tiger();
tiger.getTotal();
