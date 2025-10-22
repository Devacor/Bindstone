#include <iostream>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>

int main() {
    using namespace jai;
    
    auto eng = engine::make();
    stdlib::register_all(*eng);
    
    std::cout << "=== JaiScript Class Features ===" << std::endl;
    
    // Example 1: Basic class with inheritance
    std::cout << "\n1. Class inheritance with super() calls:" << std::endl;
    eng->execute(R"JAI(
        class Cat {
            int age = 0;
            string name = "";
            
            Cat(string n, int a) {
                name = n;
                age = a;
                print("Cat constructor: " + name);
            }
            
            string speak() {
                return name + " meows";
            }
        }
        
        class Tiger : Cat {
            int stripes = 0;
            
            Tiger(string n, int a, int s) : super(n, a) {
                stripes = s;
                print("Tiger constructor: " + name + " with " + to_string(stripes) + " stripes");
            }
            
            string speak() {
                return super.speak() + " and ROARS!";
            }
            
            string hunt() {
                return name + " is hunting with " + to_string(stripes) + " stripes gleaming";
            }
        }
        
        auto tiger = Tiger("Shere Khan", 8, 42);
        print(tiger.speak());
        print(tiger.hunt());
    )JAI");
    
    // Example 2: Method chaining and implicit this
    std::cout << "\n2. Method chaining with implicit this:" << std::endl;
    eng->execute(R"JAI(
        class StringBuilder {
            string text = "";
            
            StringBuilder() {
                text = "";
            }
            
            StringBuilder& append(string s) {
                text = text + s;
                return this;
            }
            
            StringBuilder& appendLine(string s) {
                text = text + s + "\n";
                return this;
            }
            
            string toString() {
                return text;
            }
        }
        
        auto sb = StringBuilder();
        auto result = sb.append("Hello ")
                        .append("World")
                        .appendLine("!")
                        .append("How are you?")
                        .toString();
        print(result);
    )JAI");
    
    // Example 3: Constructor overloading
    std::cout << "\n3. Constructor overloading:" << std::endl;
    eng->execute(R"JAI(
        class Vector {
            float x = 0.0;
            float y = 0.0;
            float z = 0.0;
            
            Vector() {
                // Default constructor - all zeros
            }
            
            Vector(float val) {
                x = val;
                y = val;
                z = val;
            }
            
            Vector(float x_val, float y_val, float z_val) {
                x = x_val;
                y = y_val;
                z = z_val;
            }
            
            string toString() {
                return "(" + to_string(x) + ", " + to_string(y) + ", " + to_string(z) + ")";
            }
        }
        
        auto v1 = Vector();
        auto v2 = Vector(5.0);
        auto v3 = Vector(1.0, 2.0, 3.0);
        
        print("v1: " + v1.toString());
        print("v2: " + v2.toString());
        print("v3: " + v3.toString());
    )JAI");
    
    // Example 4: Arrays of objects
    std::cout << "\n4. Arrays of script objects:" << std::endl;
    eng->execute(R"JAI(
        class Student {
            string name = "";
            int grade = 0;
            
            Student(string n, int g) {
                name = n;
                grade = g;
            }
            
            string info() {
                return name + " (grade " + to_string(grade) + ")";
            }
        }
        
        auto students = [];
        students.push(Student("Alice", 95));
        students.push(Student("Bob", 87));
        students.push(Student("Charlie", 92));
        
        print("Students:");
        for (auto i = 0; i < students.size(); i = i + 1) {
            print("  " + students[i].info());
        }
        
        // Calculate average grade
        auto total = 0;
        for (auto i = 0; i < students.size(); i = i + 1) {
            total = total + students[i].grade;
        }
        auto average = total / students.size();
        print("Average grade: " + to_string(average));
    )JAI");
    
    // Example 5: Hot reload (field preservation)
    std::cout << "\n5. Hot reload with field preservation:" << std::endl;
    eng->execute(R"JAI(
        class Player {
            string name = "Unknown";
            int health = 100;
            int score = 0;
            
            Player(string n) {
                name = n;
            }
            
            void takeDamage(int dmg) {
                health = health - dmg;
                if (health < 0) health = 0;
            }
            
            string status() {
                return name + " - Health: " + to_string(health) + ", Score: " + to_string(score);
            }
        }
        
        global player1 = Player("Hero");
        player1.takeDamage(30);
        player1.score = 250;
        print("Before reload: " + player1.status());
    )JAI");
    
    // Redefine class (simulating hot reload)
    eng->execute(R"JAI(
        class Player {
            string name = "Unknown";
            int health = 100;
            int score = 0;
            int level = 1;  // NEW field
            
            Player(string n) {
                name = n;
                level = 1;
            }
            
            void takeDamage(int dmg) {
                health = health - dmg;
                if (health < 0) health = 0;
            }
            
            void levelUp() {  // NEW method
                level = level + 1;
                health = 100;  // Full heal on level up
            }
            
            string status() {
                return name + " - Level " + to_string(level) + 
                       ", Health: " + to_string(health) + ", Score: " + to_string(score);
            }
        }
        
        // Existing instance is migrated! Fields are preserved
        print("After reload: " + player1.status());
        player1.levelUp();
        print("After level up: " + player1.status());
    )JAI");
    
    std::cout << "\n=== Demo Complete ===" << std::endl;
    
    return 0;
}