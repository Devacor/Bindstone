// Minimal test to isolate weak_ptr issue
class ScriptNode {
    string name = "";
    int value = 0;
    weak_ptr<ScriptNode> parent = weak_ptr<ScriptNode>();
    array<ScriptNode> children = [];

    ScriptNode(string n, int val) {
        name = n;
        value = val;
    }
}

print("Creating root node...");
auto root = ScriptNode("root", 1);
print("Root created successfully");
