#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <memory>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test class to track object lifetime
class LifetimeTracker {
public:
    static int alive_count;
    static int total_created;
    static int copy_count;

    int id;
    int value;

    LifetimeTracker(int val = 0) : id(++total_created), value(val) {
        alive_count++;
        std::cout << "LifetimeTracker(" << value << ") created, id=" << id << ", alive=" << alive_count << std::endl;
    }

    // Copy constructor - track when copies are made
    LifetimeTracker(const LifetimeTracker& other) : id(other.id), value(other.value) {
        copy_count++;
        std::cout << "LifetimeTracker COPY from id=" << other.id << " (copy #" << copy_count << "), alive=" << alive_count << std::endl;
        // Note: We don't increment alive_count for copies to track only "real" objects
    }

    // Copy assignment - track when assigned
    LifetimeTracker& operator=(const LifetimeTracker& other) {
        if (this != &other) {
            id = other.id;
            value = other.value;
            std::cout << "LifetimeTracker ASSIGN from id=" << other.id << ", alive=" << alive_count << std::endl;
        }
        return *this;
    }

    ~LifetimeTracker() {
        alive_count--;
        std::cout << "~LifetimeTracker() id=" << id << ", alive=" << alive_count << std::endl;
        std::cout.flush(); // Ensure output is flushed
    }

    int get_value() const { return value; }
    void set_value(int v) { value = v; }

    static void reset() {
        alive_count = 0;
        total_created = 0;
        copy_count = 0;
    }
};

int LifetimeTracker::alive_count = 0;
int LifetimeTracker::total_created = 0;
int LifetimeTracker::copy_count = 0;

// Tree node for testing parent-child relationships
class TreeNode : public std::enable_shared_from_this<TreeNode> {
public:
    std::string name;
    std::weak_ptr<TreeNode> parent;  // This will be exposed to script as weak_ptr<TreeNode>
    std::vector<std::shared_ptr<TreeNode>> children;
    
    TreeNode(const std::string& n) : name(n) {}
    
    std::string get_name() const { return name; }
    
    void add_child(std::shared_ptr<TreeNode> child) {
        children.push_back(child);
        child->parent = weak_from_this();
    }
    
    std::string get_parent_name() const {
        if (auto p = parent.lock()) {
            return p->name;
        }
        return "null";
    }
    
    int child_count() const { return static_cast<int>(children.size()); }
};

class weak_ptr_tests : public suite {
public:
    weak_ptr_tests() : suite("weak_ptr Tests") {}
    
    void pre_test() override {
        // Reset static state before each test
        LifetimeTracker::reset();
    }
    
    void forge_tests() override {
        /* Temporarily disabled - crashes in full suite
        test("weak_ptr_basic_syntax", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);
            
            // Test basic weak_ptr declaration and assignment
            try {
                auto result = eng->execute(R"(
                    auto obj = LifetimeTracker(42);
                    weak_ptr<LifetimeTracker> weak = obj;
                    !weak.expired()
                )");
                
                check_eq(result.as<bool>(), true);
            } catch (const std::exception& e) {
                std::cerr << "weak_ptr_basic_syntax error: " << e.what() << std::endl;
                throw;
            }
        });
        */
        
        test("weak_ptr_null_checking", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);
            
            // Test null weak_ptr using expired()
            auto result = eng->execute(R"(
                weak_ptr<LifetimeTracker> weak;
                weak.expired()
            )");
            
            check_eq(result.as<bool>(), true);
            
            // Test that lock() returns null
            auto lock_result = eng->execute(R"(
                weak_ptr<LifetimeTracker> weak;
                weak.lock() == null
            )");
            
            check_eq(lock_result.as<bool>(), true);
        });
        
        test("weak_ptr_lock_valid", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test locking a valid weak_ptr (must use shared_ptr for reference semantics)
            auto result = eng->execute(R"(
                auto obj = shared_ptr<LifetimeTracker>(42);
                weak_ptr<LifetimeTracker> weak = obj;
                auto locked = weak.lock();
                locked != null && locked.get_value() == 42
            )");

            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_becomes_invalid", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test weak_ptr becoming invalid when object is destroyed
            auto result = eng->execute(R"(
                // Check initial count
                auto initial_count = LifetimeTracker::alive_count;

                // Create shared_ptr object and weak_ptr in local scope
                weak_ptr<LifetimeTracker> weak;
                {
                    auto obj = shared_ptr<LifetimeTracker>(42);
                    weak = weak_ptr<LifetimeTracker>(obj);
                    // Verify object is alive
                    auto during_scope = LifetimeTracker::alive_count == initial_count + 1;
                    // obj goes out of scope here
                }
                // Check if weak_ptr is now invalid
                auto after_scope = LifetimeTracker::alive_count == initial_count;
                weak.expired() && after_scope
            )");

            std::cout << "Initial LifetimeTracker::alive_count: 0" << std::endl;
            std::cout << "After scope, LifetimeTracker::alive_count: " << LifetimeTracker::alive_count << std::endl;
            std::cout << "Test result: " << result.to_string() << " (type: " << (result.is_bool() ? "bool" : "other") << ")" << std::endl;

            check_eq(result.as<bool>(), true);
            // The key test is that weak.expired() returns true AND alive_count decreased
        });
        
        test("weak_ptr_tree_structure", [this]() {
            auto eng = engine::make();
            register_tree_node(*eng);
            
            auto result = eng->execute(R"(
                auto root = TreeNode("root");
                auto child1 = TreeNode("child1");
                auto child2 = TreeNode("child2");
                
                root.add_child(child1);
                root.add_child(child2);
                
                // Check parent relationships
                child1.get_parent_name() == "root" && 
                child2.get_parent_name() == "root" &&
                root.get_parent_name() == "null"
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_reassignment", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            auto result = eng->execute(R"(
                auto obj1 = shared_ptr<LifetimeTracker>(10);
                auto obj2 = shared_ptr<LifetimeTracker>(20);

                weak_ptr<LifetimeTracker> weak = obj1;
                auto locked1 = weak.lock();
                auto val1 = locked1.get_value();

                weak = obj2;  // Reassign
                auto locked2 = weak.lock();
                auto val2 = locked2.get_value();

                val1 == 10 && val2 == 20
            )");

            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_in_containers", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            auto result = eng->execute(R"(
                auto obj1 = shared_ptr<LifetimeTracker>(1);
                auto obj2 = shared_ptr<LifetimeTracker>(2);
                auto obj3 = shared_ptr<LifetimeTracker>(3);

                // Array of weak_ptrs
                auto weak_refs = [
                    weak_ptr<LifetimeTracker>(obj1),
                    weak_ptr<LifetimeTracker>(obj2),
                    weak_ptr<LifetimeTracker>(obj3)
                ];

                // Sum all values through weak_ptrs
                auto sum = 0;
                for (auto i = 0; i < weak_refs.size(); i = i + 1) {
                    auto locked = weak_refs[i].lock();
                    if (locked != null) {
                        sum = sum + locked.get_value();
                    }
                }

                sum == 6
            )");

            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_expired_method", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            auto result = eng->execute(R"(
                weak_ptr<LifetimeTracker> weak;

                // Test with valid object (must use shared_ptr)
                auto obj = shared_ptr<LifetimeTracker>(42);
                weak = obj;
                auto not_expired = !weak.expired();

                // Clear object
                obj = null;

                // Should now be expired
                auto is_expired = weak.expired();

                not_expired && is_expired
            )");

            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_script_class_simplified", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);
            
            // Define a script class - objects are internally shared_ptr in JaiScript
            eng->execute(R"(
                class ScriptNode {
                    string name = "";
                    int value = 0;
                    weak_ptr<ScriptNode> parent = weak_ptr<ScriptNode>();
                    array<ScriptNode> children = [];

                    ScriptNode(string n, int val) {
                        name = n;
                        value = val;
                    }

                    auto add_child(ScriptNode child) {
                        // Set parent on the child - when child is shared_ptr, this modifies the actual object
                        child.parent = weak_from_this();
                        // Push shared_ptr - won't be cloned because it has jai_shared_ptr_type
                        children.push(child);
                        print("[add_child] Added child '" + child.name + "' to '" + name + "'. Children count now: " + to_string(children.size()));
                    }

                    auto get_parent_name() {
                        auto p = parent.lock();
                        if (p != null) {
                            return p.name;
                        }
                        return "no parent";
                    }

                    auto sum_tree() {
                        auto sum = value;
                        print("[sum_tree] Node '" + name + "' has " + to_string(children.size()) + " children");
                        for (auto i = 0; i < children.size(); i = i + 1) {
                            print("[sum_tree] Processing child " + to_string(i) + ": " + children[i].name);
                            sum = sum + children[i].sum_tree();
                        }
                        print("[sum_tree] Node '" + name + "' subtree sum = " + to_string(sum));
                        return sum;
                    }
                }
            )");
            
            auto result = eng->execute(R"(
                // Wrap objects in shared_ptr to get reference semantics
                auto root = shared_ptr<ScriptNode>("root", 1);
                auto child1 = shared_ptr<ScriptNode>("child1", 2);
                auto child2 = shared_ptr<ScriptNode>("child2", 3);
                auto grandchild = shared_ptr<ScriptNode>("grandchild", 4);

                // Set up tree structure - shared_ptr provides reference semantics
                root.add_child(child1);
                root.add_child(child2);
                child1.add_child(grandchild);

                // Test parent relationships with debug output
                auto parent1 = child1.get_parent_name();
                print("[DEBUG] child1.get_parent_name() = " + parent1);
                auto test1 = parent1 == "root";
                print("[DEBUG] test1 (child1 parent) = " + to_string(test1));

                auto parent2 = child2.get_parent_name();
                print("[DEBUG] child2.get_parent_name() = " + parent2);
                auto test2 = parent2 == "root";
                print("[DEBUG] test2 (child2 parent) = " + to_string(test2));

                auto parent3 = grandchild.get_parent_name();
                print("[DEBUG] grandchild.get_parent_name() = " + parent3);
                auto test3 = parent3 == "child1";
                print("[DEBUG] test3 (grandchild parent) = " + to_string(test3));

                auto parent4 = root.get_parent_name();
                print("[DEBUG] root.get_parent_name() = " + parent4);
                auto test4 = parent4 == "no parent";
                print("[DEBUG] test4 (root has no parent) = " + to_string(test4));

                // Test tree sum
                auto sum = root.sum_tree();
                print("[DEBUG] root.sum_tree() = " + to_string(sum));
                auto test5 = sum == 10;  // 1+2+3+4
                print("[DEBUG] test5 (sum == 10) = " + to_string(test5));

                test1 && test2 && test3 && test4 && test5
            )");
            
            check_eq(result.as<bool>(), true);
        });

        test("weak_ptr_assignment_debug", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Use C++ bound class instead of script class to isolate the issue
            auto result = eng->execute(R"(
                auto obj = shared_ptr<LifetimeTracker>(42);
                auto weak = weak_ptr<LifetimeTracker>(obj);

                auto before = !weak.expired();
                obj = null;
                auto after = weak.expired();

                before && after
            )");

            check_eq(result.as<bool>(), true, "weak_ptr should expire after obj = null");
        });

        test("weak_ptr_script_class_lifetime", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Register the C++ static counter as a global
            eng->add_global_ref("alive_count", LifetimeTracker::alive_count);

            // Define a script class that uses the global counter
            eng->execute(R"(
                class LifetimeTest {
                    string name = "";

                    LifetimeTest(string n) {
                        name = n;
                        alive_count = alive_count + 1;
                        print("Created " + name + ", alive: " + to_string(alive_count));
                    }

                    ~LifetimeTest() {
                        alive_count = alive_count - 1;
                        print("Destroyed " + name + ", alive: " + to_string(alive_count));
                    }
                }
            )");

            // Test weak_ptr behavior with destructors (must use shared_ptr for script classes too)
            auto result = eng->execute(R"(
                // Reset counter for this test
                alive_count = 0;

                // Test 1: Basic weak_ptr functionality (use shared_ptr for reference semantics)
                // Use block scope to ensure cleanup happens when expected
                auto test1 = false;
                auto test2a = false;
                auto test2b = false;
                auto test2c = false;
                {
                    auto obj = shared_ptr<LifetimeTest>("test1");
                    auto count_after_create = alive_count;  // Should be 1

                    auto weak = weak_ptr<LifetimeTest>(obj);
                    auto locked = weak.lock();

                    // Simplified test
                    test1 = locked != null;

                    // Test 2: Weak ptr becomes invalid when object is cleared
                    obj = null;
                    locked = null;

                    // Now destructor should have been called
                    auto count_after_destroy = alive_count;  // Should be 0
                    auto locked2 = weak.lock();

                    // Check each condition separately
                    test2a = locked2 == null;
                    test2b = weak.expired();
                    test2c = count_after_destroy == 0;
                }

                // Test 3: Multiple weak references with proper cleanup
                auto test3 = false;
                auto test4a = false;
                auto test4b = false;
                auto test4c = false;
                {
                    auto obj2 = shared_ptr<LifetimeTest>("test2");
                    auto count_with_obj2 = alive_count;  // Should be 1

                    auto weak1 = weak_ptr<LifetimeTest>(obj2);
                    auto weak2 = weak_ptr<LifetimeTest>(obj2);

                    test3 = !weak1.expired() && !weak2.expired() && count_with_obj2 == 1;

                    // Clear obj2 and verify destructor
                    obj2 = null;
                    auto final_count = alive_count;  // Should be 0

                    // Check each condition separately
                    test4a = weak1.expired();
                    test4b = weak2.expired();
                    test4c = final_count == 0;
                }

                [test1, test2a, test2b, test2c, test3, test4a, test4b, test4c]
            )");

            check(result.is_array(), "Result should be an array");
            auto arr = result.as_array();
            check_eq(arr.size(), 8);

            // Debug output
            std::cout << "  Test results: test1=" << arr[0].as_bool()
                      << ", test2a(locked==null)=" << arr[1].as_bool()
                      << ", test2b(expired)=" << arr[2].as_bool()
                      << ", test2c(count==0)=" << arr[3].as_bool()
                      << ", test3=" << arr[4].as_bool()
                      << ", test4a(weak1.expired)=" << arr[5].as_bool()
                      << ", test4b(weak2.expired)=" << arr[6].as_bool()
                      << ", test4c(count==0)=" << arr[7].as_bool() << std::endl;

            check_eq(arr[0].as_bool(), true, "test1 failed: Basic weak_ptr lock should work");
            check_eq(arr[1].as_bool(), true, "test2a failed: locked2 should be null after obj destroyed");
            check_eq(arr[2].as_bool(), true, "test2b failed: weak.expired() should return true after obj destroyed");
            check_eq(arr[3].as_bool(), true, "test2c failed: alive count should be 0 after destruction");
            check_eq(arr[4].as_bool(), true, "test3 failed: Multiple weak_ptrs should both be valid");
            check_eq(arr[5].as_bool(), true, "test4a failed: weak1.expired() should return true");
            check_eq(arr[6].as_bool(), true, "test4b failed: weak2.expired() should return true");
            check_eq(arr[7].as_bool(), true, "test4c failed: alive count should be 0 after destruction");
        });
        
        test("weak_ptr_circular_reference_script", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Define a doubly-linked node class
            eng->execute(R"(
                class DNode {
                    int value = 0;
                    shared_ptr<DNode> next = null;  // Use shared_ptr for next to maintain reference
                    weak_ptr<DNode> prev = weak_ptr<DNode>();  // Weak to avoid cycle

                    DNode(int v) {
                        value = v;
                    }

                    auto link_next(shared_ptr<DNode> node) {
                        // Set next and prev to create bidirectional link
                        node.prev = weak_from_this();
                        next = node;
                    }

                    auto count_forward() {
                        auto count = 1;
                        if (next != null) {
                            count = count + next.count_forward();
                        }
                        return count;
                    }

                    auto count_backward() {
                        auto count = 1;
                        auto p = prev.lock();
                        if (p != null) {
                            count = count + p.count_backward();
                        }
                        return count;
                    }
                }
            )");

            auto result = eng->execute(R"(
                // Wrap nodes in shared_ptr for reference semantics
                auto node1 = shared_ptr<DNode>(1);
                auto node2 = shared_ptr<DNode>(2);
                auto node3 = shared_ptr<DNode>(3);

                // Link nodes - no reassignment needed with shared_ptr
                node1.link_next(node2);
                node2.link_next(node3);

                // Test forward counting
                auto forward = node1.count_forward() == 3;

                // Test backward counting
                auto backward = node3.count_backward() == 3;

                // Test middle node can go both ways
                auto mid_forward = node2.count_forward() == 2;
                auto mid_backward = node2.count_backward() == 2;

                forward && backward && mid_forward && mid_backward
            )");

            check_eq(result.as<bool>(), true);
        });

        // Error handling tests - verify helpful error messages
        test("weak_ptr_rejects_value_constructor", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test that weak_ptr constructor rejects value-semantic objects with helpful error
            try {
                eng->execute(R"(
                    auto obj = LifetimeTracker(42);
                    auto weak = weak_ptr<LifetimeTracker>(obj);
                )");

                // Should not reach here
                throw test_failure("Expected error when constructing weak_ptr from value-semantic object");
            } catch (const test_failure&) {
                throw;  // Re-throw test failures
            } catch (const std::exception& e) {
                std::string error_msg = e.what();

                // Verify error message is helpful and mentions shared_ptr
                check(error_msg.find("weak_ptr") != std::string::npos,
                      "Error message should mention 'weak_ptr'");
                check(error_msg.find("value-semantic") != std::string::npos ||
                      error_msg.find("shared_ptr") != std::string::npos,
                      "Error message should mention 'value-semantic' or 'shared_ptr'");
                check(error_msg.find("shared_ptr<") != std::string::npos,
                      "Error message should show shared_ptr<T> syntax");

                std::cout << "  Error message: " << error_msg << std::endl;
            }
        });

        test("weak_ptr_rejects_value_initialization", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test that weak_ptr variable initialization rejects value-semantic objects
            try {
                eng->execute(R"(
                    auto obj = LifetimeTracker(42);
                    weak_ptr<LifetimeTracker> weak = obj;
                )");

                throw test_failure("Expected error when initializing weak_ptr with value-semantic object");
            } catch (const test_failure&) {
                throw;  // Re-throw test failures
            } catch (const std::exception& e) {
                std::string error_msg = e.what();

                // Verify error message is helpful
                check(error_msg.find("weak_ptr") != std::string::npos,
                      "Error message should mention 'weak_ptr'");
                check(error_msg.find("value-semantic") != std::string::npos ||
                      error_msg.find("shared_ptr") != std::string::npos,
                      "Error message should guide user to use shared_ptr");

                std::cout << "  Error message: " << error_msg << std::endl;
            }
        });

        test("weak_ptr_rejects_value_assignment", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test that weak_ptr assignment rejects value-semantic objects
            try {
                eng->execute(R"(
                    weak_ptr<LifetimeTracker> weak;
                    auto obj = LifetimeTracker(42);
                    weak = obj;
                )");

                throw test_failure("Expected error when assigning value-semantic object to weak_ptr");
            } catch (const test_failure&) {
                throw;  // Re-throw test failures
            } catch (const std::exception& e) {
                std::string error_msg = e.what();

                // Verify error message is helpful
                check(error_msg.find("weak_ptr") != std::string::npos,
                      "Error message should mention 'weak_ptr'");
                check(error_msg.find("value-semantic") != std::string::npos ||
                      error_msg.find("shared_ptr") != std::string::npos,
                      "Error message should guide user to use shared_ptr");

                std::cout << "  Error message: " << error_msg << std::endl;
            }
        });

        test("weak_ptr_accepts_shared_ptr", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test that weak_ptr DOES accept shared_ptr<T> (positive case)
            // Note: shared_ptr<T>(args...) forwards args to T's constructor
            auto result = eng->execute(R"(
                auto obj = shared_ptr<LifetimeTracker>(42);
                weak_ptr<LifetimeTracker> weak = obj;
                auto locked = weak.lock();
                locked != null && locked.get_value() == 42
            )");

            check_eq(result.as<bool>(), true);
        });

        // =============== SHARED_PTR CREATION TESTS ===============
        // Test valid and invalid ways to create shared_ptr

        test("shared_ptr_from_constructor_call", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // shared_ptr<T>(args...) - args forwarded to T's constructor (like make_shared)
            auto result = eng->execute(R"(
                auto obj = shared_ptr<LifetimeTracker>(42);
                auto copy = obj;  // Should share (reference semantics)
                copy.set_value(100);  // Modify through copy
                obj.get_value()  // Should see the change
            )");

            check_eq(result.as<script_int>(), 100);
        });

        test("value_semantics_default", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Direct construction has value semantics (deep copy on assignment)
            auto result = eng->execute(R"(
                auto obj = LifetimeTracker(42);
                auto copy = obj;  // Should deep copy (value semantics)
                copy.set_value(100);  // Modify through copy
                obj.get_value()  // Should NOT see the change (independent copy)
            )");

            check_eq(result.as<script_int>(), 42);
        });

        test("shared_ptr_vs_value_semantics", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Compare: direct construction (value) vs shared_ptr (reference)
            auto result = eng->execute(R"(
                // Value semantics - independent copies
                auto val1 = LifetimeTracker(10);
                auto val2 = val1;
                val2.set_value(99);
                auto val1_unchanged = val1.get_value() == 10;

                // Reference semantics with shared_ptr - args forwarded to constructor
                auto ref1 = shared_ptr<LifetimeTracker>(20);
                auto ref2 = ref1;
                ref2.set_value(88);
                auto ref1_changed = ref1.get_value() == 88;

                val1_unchanged && ref1_changed
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_wrapping_variable_should_fail", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // shared_ptr<T>(args...) forwards args to T's constructor
            // So shared_ptr<LifetimeTracker>(b) tries to call LifetimeTracker(LifetimeTracker_object)
            // This fails because the constructor expects int, not a LifetimeTracker
            // This naturally prevents the confusing "wrap a variable" pattern
            try {
                eng->execute(R"(
                    auto b = LifetimeTracker(42);
                    auto a = shared_ptr<LifetimeTracker>(b);
                )");

                throw test_failure("Expected error when passing wrong type to constructor");
            } catch (const test_failure&) {
                throw;  // Re-throw test failures
            } catch (const std::exception& e) {
                std::string error_msg = e.what();
                std::cout << "  Error (expected): " << error_msg << std::endl;
                // Error is expected - can't pass LifetimeTracker to LifetimeTracker(int) constructor
                check(true);
            }
        });

        test("shared_ptr_default_constructor", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // shared_ptr<T>() with no args calls T's default constructor
            auto result = eng->execute(R"(
                auto obj = shared_ptr<LifetimeTracker>();
                obj.get_value()  // Default constructor sets value to 0
            )");

            check_eq(result.as<script_int>(), 0);
        });

        // =============== SHARED_PTR AUTO-UNWRAP ASSIGNMENT TESTS ===============
        // Tests for the auto-unwrap semantics where shared_ptr<T> delegates
        // assignment to the underlying object's operator= when RHS is value-like

        test("shared_ptr_operator_equals_auto_unwrap", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Define a class with operator=
            eng->execute(R"(
                class Counter {
                    int value = 0;
                    Counter(int v) { value = v; }

                    auto operator=(int v) {
                        value = v;
                        return this;
                    }
                }
            )");

            // Test: assigning int to shared_ptr<Counter> should call operator=(int)
            auto result = eng->execute(R"(
                auto a = shared_ptr<Counter>(5);
                auto b = a;  // b shares with a

                a = 10;  // Auto-unwrap: calls Counter::operator=(int)

                // Both should see the change (shared mutation)
                a.value == 10 && b.value == 10
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_same_type_copies_contents", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Test: assigning same type to shared_ptr should copy contents, not reassign pointer
            eng->execute(R"(
                class Point {
                    int x = 0;
                    int y = 0;
                    Point(int px, int py) { x = px; y = py; }
                }
            )");

            auto result = eng->execute(R"(
                auto a = shared_ptr<Point>(1, 2);
                auto b = a;  // b shares with a

                a = Point(10, 20);  // Copy contents into shared object

                // Both should see the change (contents copied, still sharing)
                a.x == 10 && a.y == 20 && b.x == 10 && b.y == 20
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_null_reassigns_pointer", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test: assigning null to shared_ptr should nullify it
            auto result = eng->execute(R"(
                auto a = shared_ptr<LifetimeTracker>(42);
                auto b = a;  // b shares with a

                a = null;  // Pointer op: nullify a

                // a is null, b still has the object
                a == null && b.get_value() == 42
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_polymorphic_assignment", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Test: shared_ptr<Derived> can be assigned to shared_ptr<Base>
            eng->execute(R"(
                class Animal {
                    string name = "";
                    Animal(string n) { name = n; }
                }

                class Dog : Animal {
                    Dog(string n) : super(n) {}
                }
            )");

            auto result = eng->execute(R"(
                auto dog = shared_ptr<Dog>("Rex");
                shared_ptr<Animal> animal = dog;  // Polymorphic assignment

                animal.name == "Rex"
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_reassign_same_shared_ptr_type", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // Test: assigning shared_ptr<T> to shared_ptr<T> should reassign pointer
            auto result = eng->execute(R"(
                auto a = shared_ptr<LifetimeTracker>(10);
                auto b = shared_ptr<LifetimeTracker>(20);
                auto c = a;  // c shares with a

                a = b;  // Pointer op: a now shares with b

                // a and b share (20), c still has original (10)
                a.get_value() == 20 && b.get_value() == 20 && c.get_value() == 10
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_no_operator_equals_error", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Test: assigning incompatible type without operator= should error
            eng->execute(R"(
                class Simple {
                    int value = 0;
                    Simple(int v) { value = v; }
                    // No operator=(string) defined
                }
            )");

            try {
                eng->execute(R"(
                    auto a = shared_ptr<Simple>(5);
                    a = "hello";  // Should error: no operator=(string)
                )");

                throw test_failure("Expected error when assigning incompatible type without operator=");
            } catch (const test_failure&) {
                throw;  // Re-throw test failures
            } catch (const std::exception& e) {
                std::string error_msg = e.what();
                // Verify error mentions operator= or type mismatch
                check(error_msg.find("operator=") != std::string::npos ||
                      error_msg.find("Cannot assign") != std::string::npos,
                      "Error message should mention operator= or assignment issue");
                std::cout << "  Error (expected): " << error_msg << std::endl;
            }
        });

        test("shared_ptr_box_pattern", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Test: Box<T> pattern for shared primitives
            eng->execute(R"(
                class BoxInt {
                    int value = 0;
                    BoxInt(int v) { value = v; }

                    auto operator=(int v) {
                        value = v;
                        return this;
                    }
                }
            )");

            auto result = eng->execute(R"(
                auto x = shared_ptr<BoxInt>(5);
                auto y = x;  // y shares with x

                y = 10;  // Auto-unwrap: BoxInt::operator=(int)

                // Shared mutation works
                x.value == 10 && y.value == 10
            )");

            check_eq(result.as<bool>(), true);
        });

        test("shared_ptr_chained_mutations", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);

            // Test: multiple mutations through shared references
            eng->execute(R"(
                class Accumulator {
                    int total = 0;
                    Accumulator(int t) { total = t; }

                    auto operator=(int v) {
                        total = total + v;  // Add instead of replace
                        return this;
                    }
                }
            )");

            auto result = eng->execute(R"(
                auto a = shared_ptr<Accumulator>(0);
                auto b = a;
                auto c = a;

                a = 10;  // total = 0 + 10 = 10
                b = 20;  // total = 10 + 20 = 30
                c = 5;   // total = 30 + 5 = 35

                a.total == 35 && b.total == 35 && c.total == 35
            )");

            check_eq(result.as<bool>(), true);
        });

        // Test C++ bound types with auto-unwrap operator=
        test("shared_ptr_cpp_bound_type_auto_unwrap", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng, true);  // Register with assignment_from<int>

            // Create shared_ptr and verify auto-unwrap assignment works
            eng->execute(R"(
                auto tracker = shared_ptr<LifetimeTracker>(42);
                auto ref = tracker;

                // Auto-unwrap assignment: tracker = 100 calls operator=(100)
                tracker = 100;
            )");

            // Verify both references see the change (shared mutation via operator=)
            auto tracker_value = eng->execute("tracker.get_value()");
            check_eq(tracker_value.as<int>(), 100);

            auto ref_value = eng->execute("ref.get_value()");
            check_eq(ref_value.as<int>(), 100);

            // Assign through the other reference
            eng->execute("ref = 200;");

            // Both should see the new value
            tracker_value = eng->execute("tracker.get_value()");
            check_eq(tracker_value.as<int>(), 200);
        });

        // Test that C++ bound types WITHOUT operator= give proper error
        test("shared_ptr_cpp_bound_type_no_operator_error", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng, false);  // No assignment_from registered

            eng->execute(R"(
                auto tracker = shared_ptr<LifetimeTracker>(42);
            )");

            // This should fail with "no operator= defined" error
            bool threw = false;
            std::string error_msg;
            try {
                eng->execute("tracker = 100;");
            } catch (const std::exception& e) {
                threw = true;
                error_msg = e.what();
                std::cout << "  Error (expected): " << error_msg << std::endl;
            }
            check_eq(threw, true);
            check_true(error_msg.find("operator=") != std::string::npos ||
                      error_msg.find("no operator") != std::string::npos);
        });

        // Test same_as() for shared_ptr - pointer identity comparison
        test("shared_ptr_same_as", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            // same_as returns true for same underlying object
            auto result = eng->execute(R"(
                auto a = shared_ptr<LifetimeTracker>(42);
                auto b = a;   // b shares with a
                auto c = shared_ptr<LifetimeTracker>(42);  // Different object, same value

                auto same_ab = a.same_as(b);   // Should be true - same object
                auto same_ac = a.same_as(c);   // Should be false - different objects

                same_ab && !same_ac
            )");
            check_eq(result.as<bool>(), true);
        });

        // Test same_as() for weak_ptr
        test("weak_ptr_same_as", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            auto result = eng->execute(R"(
                auto a = shared_ptr<LifetimeTracker>(10);
                auto b = a;
                weak_ptr<LifetimeTracker> weak_a = a;
                weak_ptr<LifetimeTracker> weak_b = b;

                auto c = shared_ptr<LifetimeTracker>(10);
                weak_ptr<LifetimeTracker> weak_c = c;

                // weak_a and weak_b reference the same object
                auto same_ab = weak_a.same_as(weak_b);
                // weak_a and weak_c reference different objects
                auto same_ac = weak_a.same_as(weak_c);

                same_ab && !same_ac
            )");
            check_eq(result.as<bool>(), true);
        });

        // Test same_as() for regular objects (value semantics)
        test("object_same_as", [this]() {
            auto eng = engine::make();

            auto result = eng->execute(R"(
                class Obj {
                    int x = 0;
                }

                auto a = Obj();
                auto b = a;   // b is a COPY of a (value semantics)
                auto c = a;   // c is another COPY

                // Each is a different object due to value semantics
                auto same_ab = a.same_as(b);   // Should be false - copies are different
                auto same_aa = a.same_as(a);   // Should be true - same object

                !same_ab && same_aa
            )");
            check_eq(result.as<bool>(), true);
        });

        // Test same_as() comparing shared_ptr with weak_ptr
        test("shared_weak_same_as", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);

            auto result = eng->execute(R"(
                auto shared = shared_ptr<LifetimeTracker>(5);
                weak_ptr<LifetimeTracker> weak = shared;

                // Cross-type same_as
                shared.same_as(weak.lock())
            )");
            check_eq(result.as<bool>(), true);
        });
    }

private:
    void register_lifetime_tracker(engine& eng, bool with_assignment = false) {
        auto builder = class_builder<LifetimeTracker>(eng, "LifetimeTracker")
            .constructor<>()
            .constructor<int>()
            .method("get_value", &LifetimeTracker::get_value)
            .method("set_value", &LifetimeTracker::set_value)
            .property("value", &LifetimeTracker::value)
            .property("id", &LifetimeTracker::id)
            .static_property("alive_count", &LifetimeTracker::alive_count)
            .static_property("total_created", &LifetimeTracker::total_created);

        if (with_assignment) {
            // Register operator= to enable auto-unwrap for shared_ptr<LifetimeTracker>
            builder.method("=", [](LifetimeTracker& self, int v) {
                self.value = v;
            });
        }

        builder.build();
    }
    
    void register_tree_node(engine& eng) {
        class_builder<TreeNode>(eng, "TreeNode")
            .constructor<std::string>()
            .method("get_name", &TreeNode::get_name)
            .method("add_child", &TreeNode::add_child)
            .method("get_parent_name", &TreeNode::get_parent_name)
            .method("child_count", &TreeNode::child_count)
            .property("name", &TreeNode::name)
            .property("parent", &TreeNode::parent, jai::skip_type_check)  // Circular dependency: TreeNode references itself
            // Don't expose children vector directly - use methods instead
            .build();
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::weak_ptr_tests)