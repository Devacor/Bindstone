#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    auto engine = jai::engine::make();
    jai::stdlib::register_all(*engine);

    try {
        engine->execute(R"(
            class Node {
                string name = "";
                array<Node> children = [];

                Node(string n) {
                    name = n;
                }

                auto add_child(Node child) {
                    print("In add_child, about to push");
                    children.push(child);
                    print("After push, children size = " + to_string(children.size()));
                }
            }

            auto root = shared_ptr<Node>(Node("root"));
            auto child1 = shared_ptr<Node>(Node("child1"));

            print("Before add_child");
            root.add_child(child1);
            print("After add_child");
            print("Root children size = " + to_string(root.children.size()));
        )");

        std::cout << "Test completed" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
