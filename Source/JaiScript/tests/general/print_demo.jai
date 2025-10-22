// Demonstration of enhanced print function in JaiScript

// Basic printing (unchanged)
print("Hello, World!");

// Format-style printing (unchanged)  
print("The answer is {}", 42);
print("{1} comes before {0}", "B", "A");

// NEW: Sequential printing when no {} in first argument
print("Hello", " ", "World", "!");  

// NEW: Control output behavior
print("This has no newline", skip_newline);
print(" <- See?");

// You can combine multiple values
auto name = "JaiScript";
auto version = 1.0;
print("Language: ", name, " Version: ", version);

// Format strings still work with control args
print("Progress: {}%", 75, skip_newline);
print(" [######### ]");

// skip_flush prevents both newline and flush
print("Debug output", skip_flush);

// Practical use case: progress indicator
for (auto i = 0; i <= 100; i += 20) {
    print("\rProgress: ", i, "%", skip_flush);
}