#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>
#include <memory>

using namespace jai;
using namespace jai::foundry;

namespace jai::foundry::tests {

// Test class to track object lifetime
class LifetimeTracker {
public:
    static int alive_count;
    static int total_created;
    
    int id;
    int value;
    
    LifetimeTracker(int val = 0) : id(++total_created), value(val) {
        alive_count++;
        std::cout << "LifetimeTracker(" << value << ") created, id=" << id << ", alive=" << alive_count << std::endl;
    }
    
    ~LifetimeTracker() {
        alive_count--;
        std::cout << "~LifetimeTracker() id=" << id << ", alive=" << alive_count << std::endl;
    }
    
    int get_value() const { return value; }
    void set_value(int v) { value = v; }
    
    static void reset() {
        alive_count = 0;
        total_created = 0;
    }
};

int LifetimeTracker::alive_count = 0;
int LifetimeTracker::total_created = 0;

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
    
    int child_count() const { return children.size(); }
};

class weak_ptr_tests : public suite {
public:
    weak_ptr_tests() : suite("weak_ptr Tests") {}
    
    void forge_tests() override {
        test("weak_ptr_basic_syntax", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);
            
            // Test basic weak_ptr declaration and assignment
            auto result = eng->execute(R"(
                auto obj = LifetimeTracker(42);
                weak_ptr<LifetimeTracker> weak = obj;
                weak != null
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_null_checking", [this]() {
            auto eng = engine::make();
            register_lifetime_tracker(*eng);
            
            // Test null weak_ptr
            auto result = eng->execute(R"(
                weak_ptr<LifetimeTracker> weak;
                weak == null
            )");
            
            check_eq(result.as<bool>(), true);
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
            
            LifetimeTracker::reset();
            
            // Create weak_ptr
            eng->execute(R"(
                auto obj = LifetimeTracker(42);
                global_weak = weak_ptr<LifetimeTracker>(obj);
            )");
            
            // Object should be alive
            check_eq(LifetimeTracker::alive_count, 1);
            
            // Clear the strong reference
            eng->execute("obj = null;");
            
            // Force garbage collection if needed
            eng->execute("null;"); // Some activity to potentially trigger cleanup
            
            // Object should be dead
            check_eq(LifetimeTracker::alive_count, 0);
            
            // weak_ptr should now be invalid
            auto result = eng->execute("global_weak.lock() == null");
            check_eq(result.as<bool>(), true);
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
                auto val1 = weak.lock().get_value();
                
                weak = obj2;  // Reassign
                auto val2 = weak.lock().get_value();
                
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
            
            // Define a script class
            eng->execute(R"(
                class ScriptNode {
                    name = "";
                    value = 0;
                    parent = weak_ptr<ScriptNode>();
                    children = [];
                    
                    ScriptNode(string n, int v) {
                        name = n;
                        value = v;
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
            
            // Define a script class with static counter
            eng->execute(R"(
                class LifetimeTest {
                    static alive_count = 0;
                    name = "";
                    
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
            
            // Add a print function for debugging
            eng->add_variadic_function("print", [](const std::vector<script_value>& args) -> script_value {
                for (const auto& arg : args) {
                    std::cout << arg.to_string();
                }
                std::cout << std::endl;
                return script_value(script_value::serialization_tag{}, std::monostate{});
            });
            
            // Test weak_ptr doesn't keep object alive
            auto result = eng->execute(R"(
                // Create object and weak reference
                auto obj = LifetimeTest("test1");
                global_weak = weak_ptr<LifetimeTest>(obj);
                
                // Object should be alive
                auto count1 = LifetimeTest.alive_count;
                
                // Clear strong reference
                obj = null;
                
                // Object should be destroyed
                auto count2 = LifetimeTest.alive_count;
                
                // Weak ptr should be invalid
                auto is_invalid = global_weak.lock() == null;
                
                count1 == 1 && count2 == 0 && is_invalid
            )");
            
            check_eq(result.as<bool>(), true);
        });
        
        test("weak_ptr_circular_reference_script", [this]() {
            auto eng = engine::make();
            
            // Define a doubly-linked node class
            eng->execute(R"(
                class DNode {
                    value = 0;
                    next = null;
                    prev = weak_ptr<DNode>();  // Weak to avoid cycle
                    
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