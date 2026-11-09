// The wasm gate's check that the library catches the exceptions it throws, and that the browser's entropy seeds a generator
string caught = "uncaught";
try {
    string missing = read_file("no such file");
} catch (e) {
    caught = "caught";
}
random dice;
int roll = dice.next_int(1, 6);
print("x".to_int(-1), " ", "42".to_int(), " ", caught, " ", roll >= 1 && roll <= 6);
