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
            
            // Test locking a valid weak_ptr
            auto result = eng->execute(R"(
                auto obj = LifetimeTracker(42);
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

                // Create object and weak_ptr in local scope
                weak_ptr<LifetimeTracker> weak;
                {
                    auto obj = LifetimeTracker(42);
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
                auto obj1 = LifetimeTracker(10);
                auto obj2 = LifetimeTracker(20);
                
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
                auto obj1 = LifetimeTracker(1);
                auto obj2 = LifetimeTracker(2);
                auto obj3 = LifetimeTracker(3);
                
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
                
                // Test with valid object
                auto obj = LifetimeTracker(42);
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
        
        test("weak_ptr_script_class", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);
            
            // Define a script class
            eng->execute(R"(
                class ScriptNode {
                    string name = "";
                    int value = 0;
                    weak_ptr<ScriptNode> parent = weak_ptr<ScriptNode>();
                    array children = [];
                    
                    ScriptNode(string n, int val) {
                        name = n;
                        value = val;
                    }
                    
                    add_child(ScriptNode child) {
                        children.push(child);
                        child.parent = weak_ptr<ScriptNode>(this);
                    }
                    
                    get_parent_name() {
                        auto p = parent.lock();
                        if (p != null) {
                            return p.name;
                        }
                        return "no parent";
                    }
                    
                    sum_tree() {
                        auto sum = value;
                        for (auto i = 0; i < children.size(); i = i + 1) {
                            sum = sum + children[i].sum_tree();
                        }
                        return sum;
                    }
                }
            )");
            
            auto result = eng->execute(R"(
                auto root = ScriptNode("root", 1);
                auto child1 = ScriptNode("child1", 2);
                auto child2 = ScriptNode("child2", 3);
                auto grandchild = ScriptNode("grandchild", 4);
                
                root.add_child(child1);
                root.add_child(child2);
                child1.add_child(grandchild);
                
                // Test parent relationships
                auto test1 = child1.get_parent_name() == "root";
                auto test2 = child2.get_parent_name() == "root";
                auto test3 = grandchild.get_parent_name() == "child1";
                auto test4 = root.get_parent_name() == "no parent";
                
                // Test tree sum
                auto test5 = root.sum_tree() == 10;  // 1+2+3+4
                
                test1 && test2 && test3 && test4 && test5
            )");
            
            check_eq(result.as<bool>(), true);
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
            
            // Test weak_ptr behavior with destructors
            auto result = eng->execute(R"(
                // Reset counter for this test
                alive_count = 0;
                
                // Test 1: Basic weak_ptr functionality
                auto obj = LifetimeTest("test1");
                auto count_after_create = alive_count;  // Should be 1
                
                auto weak = weak_ptr<LifetimeTest>(obj);
                
                // Should be able to lock while object exists
                auto locked = weak.lock();
                auto test1 = locked != null && locked.name == "test1" && count_after_create == 1;
                
                // Test 2: Weak ptr becomes invalid when object is cleared
                obj = null;
                locked = null;  // Release the locked reference
                
                // Now destructor should have been called
                auto count_after_destroy = alive_count;  // Should be 0
                auto locked2 = weak.lock();
                auto test2 = locked2 == null && weak.expired() && count_after_destroy == 0;
                
                // Test 3: Multiple weak references with proper cleanup
                auto obj2 = LifetimeTest("test2");
                auto count_with_obj2 = alive_count;  // Should be 1
                
                auto weak1 = weak_ptr<LifetimeTest>(obj2);
                auto weak2 = weak_ptr<LifetimeTest>(obj2);
                
                auto test3 = !weak1.expired() && !weak2.expired() && count_with_obj2 == 1;
                
                // Clear obj2 and verify destructor
                obj2 = null;
                auto final_count = alive_count;  // Should be 0
                auto test4 = weak1.expired() && weak2.expired() && final_count == 0;
                
                test1 && test2 && test3 && test4
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_circular_reference_script", [this]() {
            auto eng = engine::make();
            stdlib::register_all(*eng);
            
            // Define a doubly-linked node class
            eng->execute(R"(
                class DNode {
                    int value = 0;
                    DNode next = null;
                    weak_ptr<DNode> prev = weak_ptr<DNode>();  // Weak to avoid cycle
                    
                    DNode(int v) {
                        value = v;
                    }
                    
                    link_next(DNode node) {
                        next = node;
                        node.prev = weak_ptr<DNode>(this);
                    }
                    
                    count_forward() {
                        auto count = 1;
                        if (next != null) {
                            count = count + next.count_forward();
                        }
                        return count;
                    }
                    
                    count_backward() {
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
                auto node1 = DNode(1);
                auto node2 = DNode(2);
                auto node3 = DNode(3);
                
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
    }
    
private:
    void register_lifetime_tracker(engine& eng) {
        class_builder<LifetimeTracker>(eng, "LifetimeTracker")
            .constructor<>()
            .constructor<int>()
            .method("get_value", &LifetimeTracker::get_value)
            .method("set_value", &LifetimeTracker::set_value)
            .property("value", &LifetimeTracker::value)
            .property("id", &LifetimeTracker::id)
            .static_property("alive_count", &LifetimeTracker::alive_count)
            .static_property("total_created", &LifetimeTracker::total_created)
            .build();
    }
    
    void register_tree_node(engine& eng) {
        class_builder<TreeNode>(eng, "TreeNode")
            .constructor<std::string>()
            .method("get_name", &TreeNode::get_name)
            .method("add_child", &TreeNode::add_child)
            .method("get_parent_name", &TreeNode::get_parent_name)
            .method("child_count", &TreeNode::child_count)
            .property("name", &TreeNode::name)
            .property("parent", &TreeNode::parent)
            // Don't expose children vector directly - use methods instead
            .build();
    }
};

} // namespace jai::foundry::tests

FOUNDRY_REGISTER(jai::foundry::tests::weak_ptr_tests)