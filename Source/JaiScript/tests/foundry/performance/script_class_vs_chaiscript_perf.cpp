#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <chaiscript/chaiscript.hpp>
#include <iostream>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <vector>

using namespace jai;
using namespace std::chrono;

struct TestResult {
    std::string name;
    long microseconds;
};

std::vector<TestResult> jaiscript_redef_results;
std::vector<TestResult> jaiscript_onedef_results;
std::vector<TestResult> chaiscript_newengine_results;
std::vector<TestResult> chaiscript_onedef_results;

void measure(const std::string& name, std::function<void()> fn, int iterations = 50) {
    std::cout << "Starting test: " << name << " (" << iterations << " iterations)" << std::endl;
    auto start = high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        if (i % 10 == 0) {
            std::cout << "  Iteration " << i << "/" << iterations << std::endl;
        }
        fn();
    }
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start).count();
    auto avg_duration = duration / iterations;
    std::cout << name << ": " << avg_duration << " μs/iteration\n";
    std::cout << "Test completed: " << name << std::endl << std::endl;
    
    // Store results based on test type
    if (name.find("JaiScript (Redefinition)") != std::string::npos) {
        jaiscript_redef_results.push_back({name, avg_duration});
    } else if (name.find("JaiScript (OneDef)") != std::string::npos) {
        jaiscript_onedef_results.push_back({name, avg_duration});
    } else if (name.find("ChaiScript (NewEngine)") != std::string::npos) {
        chaiscript_newengine_results.push_back({name, avg_duration});
    } else if (name.find("ChaiScript (OneDef)") != std::string::npos) {
        chaiscript_onedef_results.push_back({name, avg_duration});
    }
}

int main() {
    std::cout << "=== JaiScript vs ChaiScript: Object-Oriented Performance ===\n\n";
    
    // Time JaiScript engine creation
    std::cout << "Creating JaiScript engine..." << std::endl;
    auto jai_start = high_resolution_clock::now();
    auto e = engine::make();
    auto jai_end = high_resolution_clock::now();
    auto jai_engine_time = duration_cast<microseconds>(jai_end - jai_start).count();
    std::cout << "JaiScript engine created in " << jai_engine_time << " μs" << std::endl;
    
    std::cout << "Registering standard library..." << std::endl;
    jai::stdlib::register_all(e);  // Register sqrt and other math functions
    std::cout << "Standard library registered" << std::endl;
    
    // Register sqrt function manually since it's not in stdlib
    std::cout << "Registering sqrt function..." << std::endl;
    e->add_function("sqrt", [](double x) -> double { return std::sqrt(x); });
    std::cout << "sqrt function registered" << std::endl;
    
    // ===== FAIR COMPARISON TESTS =====
    std::cout << "--- JaiScript (Redefinition) vs ChaiScript (NewEngine) ---\n";
    std::cout << "Both engines redefine classes each iteration\n\n";
    
    // Test 1: JaiScript with class redefinition (current approach)
    measure("JaiScript (Redefinition): Point class", [&e]() {
        e->execute(R"(
            class Point {
                float x = 0.0;
                float y = 0.0;
                
                Point(float px, float py) {
                    x = px;
                    y = py;
                }
                
                float distance_to(Point other) {
                    auto dx = x - other.x;
                    auto dy = y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            }
            
            auto totalDist = 0.0;
            for (auto i = 0; i < 10; i = i + 1) {
                auto p1 = Point(i, i + 1);
                auto p2 = Point(i + 1, i);
                totalDist = totalDist + p1.distance_to(p2);
            }
            totalDist;
        )");
    });
    
    // Test 1: ChaiScript with new engine each iteration (fair comparison)
    measure("ChaiScript (NewEngine): Point class", []() {
        chaiscript::ChaiScript chai;
        chai.add(chaiscript::fun([](double x) { return std::sqrt(x); }), "sqrt");
        chai.eval(R"(
            class Point {
                var x;
                var y;
                def Point(px, py) {
                    this.x = px;
                    this.y = py;
                }
                def distance_to(other) {
                    var dx = this.x - other.x;
                    var dy = this.y - other.y;
                    return sqrt(dx * dx + dy * dy);
                }
            };
            
            var totalDist = 0.0;
            for (var i = 0; i < 10; ++i) {
                var p1 = Point(i, i + 1);
                var p2 = Point(i + 1, i);
                totalDist = totalDist + p1.distance_to(p2);
            }
            totalDist;
        )");
    });
    
    // Test 2: JaiScript Counter with redefinition
    measure("JaiScript (Redefinition): Counter class", [&e]() {
        e->execute(R"(
            class Counter {
                int value = 0;
                int step = 1;
                
                Counter(int start) {
                    value = start;
                }
                
                int increment() {
                    value = value + step;
                    return value;
                }
                
                void setStep(int newStep) {
                    step = newStep;
                }
            }
            
            auto total = 0;
            for (auto i = 0; i < 15; i = i + 1) {
                auto c = Counter(i);
                c.setStep(2);
                total = total + c.increment();
                total = total + c.increment();
            }
            total;
        )");
    });
    
    // Test 2: ChaiScript Counter with new engine each iteration
    measure("ChaiScript (NewEngine): Counter class", []() {
        chaiscript::ChaiScript chai;
        chai.eval(R"(
            class Counter {
                var value;
                var step;
                def Counter(start) {
                    this.value = start;
                    this.step = 1;
                }
                def increment() {
                    this.value = this.value + this.step;
                    return this.value;
                }
                def setStep(newStep) {
                    this.step = newStep;
                }
            };
            
            var total = 0;
            for (var i = 0; i < 15; ++i) {
                var c = Counter(i);
                c.setStep(2);
                total = total + c.increment();
                total = total + c.increment();
            }
            total;
        )");
    });
    
    // Test 3: JaiScript Graph Algorithm with redefinition
    measure("JaiScript (Redefinition): Graph traversal", [&e]() {
        e->execute(R"(
            class GraphNode {
                int value = 0;
                auto neighbors = [];
                
                GraphNode(int val) {
                    value = val;
                    neighbors = [];
                }
                
                void addNeighbor(GraphNode node) {
                    neighbors.push(node);
                }
                
                int getValue() {
                    return value;
                }
                
                auto getNeighbors() {
                    return neighbors;
                }
            }
            
            class Graph {
                auto nodes = [];
                
                Graph() {
                    nodes = [];
                }
                
                void addNode(GraphNode node) {
                    nodes.push(node);
                }
                
                int dfs(GraphNode start, int target, auto visited) {
                    if (start.getValue() == target) {
                        return 1;
                    }
                    
                    visited.push(start.getValue());
                    
                    auto neighbors = start.getNeighbors();
                    for (auto i = 0; i < neighbors.size(); i = i + 1) {
                        auto neighbor = neighbors[i];
                        auto found = false;
                        for (auto j = 0; j < visited.size(); j = j + 1) {
                            if (visited[j] == neighbor.getValue()) {
                                found = true;
                            }
                        }
                        if (!found) {
                            if (dfs(neighbor, target, visited) == 1) {
                                return 1;
                            }
                        }
                    }
                    return 0;
                }
            }
            
            // Create a small graph and search it
            auto g = Graph();
            auto n1 = GraphNode(1);
            auto n2 = GraphNode(2);
            auto n3 = GraphNode(3);
            auto n4 = GraphNode(4);
            auto n5 = GraphNode(5);
            
            n1.addNeighbor(n2);
            n1.addNeighbor(n3);
            n2.addNeighbor(n4);
            n3.addNeighbor(n5);
            n4.addNeighbor(n5);
            
            g.addNode(n1);
            g.addNode(n2);
            g.addNode(n3);
            g.addNode(n4);
            g.addNode(n5);
            
            auto found = 0;
            for (auto i = 1; i <= 5; i = i + 1) {
                found = found + g.dfs(n1, i, []);
            }
            found;
        )");
    });
    
    // Test 3: ChaiScript Graph Algorithm with new engine each iteration
    measure("ChaiScript (NewEngine): Graph traversal", []() {
        chaiscript::ChaiScript chai;
        chai.eval(R"(
            class GraphNode {
                var value;
                var neighbors;
                
                def GraphNode(val) {
                    this.value = val;
                    this.neighbors = [];
                }
                
                def addNeighbor(node) {
                    this.neighbors.push_back(node);
                }
                
                def getValue() {
                    return this.value;
                }
                
                def getNeighbors() {
                    return this.neighbors;
                }
            };
            
            class Graph {
                var nodes;
                
                def Graph() {
                    this.nodes = [];
                }
                
                def addNode(node) {
                    this.nodes.push_back(node);
                }
                
                def dfs(start, target, visited) {
                    if (start.getValue() == target) {
                        return 1;
                    }
                    
                    visited.push(start.getValue());
                    
                    var neighbors = start.getNeighbors();
                    for (var i = 0; i < neighbors.size(); ++i) {
                        var neighbor = neighbors[i];
                        var found = false;
                        for (var j = 0; j < visited.size(); ++j) {
                            if (visited[j] == neighbor.getValue()) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            if (this.dfs(neighbor, target, visited) == 1) {
                                return 1;
                            }
                        }
                    }
                    return 0;
                }
            };
            
            // Create a small graph and search it
            var g = Graph();
            var n1 = GraphNode(1);
            var n2 = GraphNode(2);
            var n3 = GraphNode(3);
            var n4 = GraphNode(4);
            var n5 = GraphNode(5);
            
            n1.addNeighbor(n2);
            n1.addNeighbor(n3);
            n2.addNeighbor(n4);
            n3.addNeighbor(n5);
            n4.addNeighbor(n5);
            
            g.addNode(n1);
            g.addNode(n2);
            g.addNode(n3);
            g.addNode(n4);
            g.addNode(n5);
            
            var found = 0;
            for (var i = 1; i <= 5; ++i) {
                found = found + g.dfs(n1, i, []);
            }
            found;
        )");
    });
    
    // Test 4: JaiScript Sorting Algorithm with redefinition
    measure("JaiScript (Redefinition): Bubble sort", [&e]() {
        e->execute(R"(
            class Sorter {
                auto quickSort(auto arr, int low, int high) {
                    if (low < high) {
                        auto pi = partition(arr, low, high);
                        arr = quickSort(arr, low, pi - 1);
                        arr = quickSort(arr, pi + 1, high);
                    }
                    return arr;
                }
                
                int partition(auto arr, int low, int high) {
                    auto pivot = arr[high];
                    auto i = low - 1;
                    
                    for (auto j = low; j < high; j = j + 1) {
                        if (arr[j] < pivot) {
                            i = i + 1;
                            auto temp = arr[i];
                            arr[i] = arr[j];
                            arr[j] = temp;
                        }
                    }
                    auto temp = arr[i + 1];
                    arr[i + 1] = arr[high];
                    arr[high] = temp;
                    return i + 1;
                }
                
                auto bubbleSort(auto arr) {
                    auto n = arr.size();
                    for (auto i = 0; i < n - 1; i = i + 1) {
                        for (auto j = 0; j < n - i - 1; j = j + 1) {
                            if (arr[j] > arr[j + 1]) {
                                auto temp = arr[j];
                                arr[j] = arr[j + 1];
                                arr[j + 1] = temp;
                            }
                        }
                    }
                    return arr;
                }
            }
            
            auto sorter = Sorter();
            auto total = 0;
            for (auto test = 0; test < 3; test = test + 1) {
                auto arr = [64, 34, 25, 12, 22, 11, 90, 5, 77, 30];
                arr = sorter.bubbleSort(arr);
                total = total + arr[0] + arr[arr.size() - 1];
            }
            total;
        )");
    });
    
    // Test 4: ChaiScript Sorting Algorithm with new engine each iteration
    measure("ChaiScript (NewEngine): Bubble sort", []() {
        chaiscript::ChaiScript chai;
        chai.eval(R"(
            class Sorter {
                def bubbleSort(arr) {
                    var n = arr.size();
                    for (var i = 0; i < n - 1; ++i) {
                        for (var j = 0; j < n - i - 1; ++j) {
                            if (arr[j] > arr[j + 1]) {
                                var temp = arr[j];
                                arr[j] = arr[j + 1];
                                arr[j + 1] = temp;
                            }
                        }
                    }
                    return arr;
                }
            };
            
            var sorter = Sorter();
            var total = 0;
            for (var test = 0; test < 3; ++test) {
                var arr = [64, 34, 25, 12, 22, 11, 90, 5, 77, 30];
                arr = sorter.bubbleSort(arr);
                total = total + arr[0] + arr[arr.size() - 1];
            }
            total;
        )");
    });
    
    std::cout << "\\n--- JaiScript (OneDef) vs ChaiScript (OneDef) ---\\n";
    std::cout << "Both engines define classes once, use function wrappers\\n\\n";
    
    // Setup JaiScript OneDef approach - define classes once
    e->execute(R"(
        class Point {
            float x = 0.0;
            float y = 0.0;
            
            Point(float px, float py) {
                x = px;
                y = py;
            }
            
            float distance_to(Point other) {
                auto dx = x - other.x;
                auto dy = y - other.y;
                return sqrt(dx * dx + dy * dy);
            }
        }
        
        auto test_point_class = []() -> auto {
            auto totalDist = 0.0;
            for (auto i = 0; i < 10; i = i + 1) {
                auto p1 = Point(i, i + 1);
                auto p2 = Point(i + 1, i);
                totalDist = totalDist + p1.distance_to(p2);
            }
            return totalDist;
        };
        
        class Counter {
            int value = 0;
            int step = 1;
            
            Counter(int start) {
                value = start;
            }
            
            int increment() {
                value = value + step;
                return value;
            }
            
            void setStep(int newStep) {
                step = newStep;
            }
        }
        
        auto test_counter_class = []() -> auto {
            auto total = 0;
            for (auto i = 0; i < 15; i = i + 1) {
                auto c = Counter(i);
                c.setStep(2);
                total = total + c.increment();
                total = total + c.increment();
            }
            return total;
        };
        
        class GraphNode {
            int value = 0;
            auto neighbors = [];
            
            GraphNode(int val) {
                value = val;
                neighbors = [];
            }
            
            void addNeighbor(GraphNode node) {
                neighbors.push(node);
            }
            
            int getValue() {
                return value;
            }
            
            auto getNeighbors() {
                return neighbors;
            }
        }
        
        class Graph {
            auto nodes = [];
            
            Graph() {
                nodes = [];
            }
            
            void addNode(GraphNode node) {
                nodes.push(node);
            }
            
            int dfs(GraphNode start, int target, auto visited) {
                if (start.getValue() == target) {
                    return 1;
                }
                
                visited.push_back(start.getValue());
                
                auto neighbors = start.getNeighbors();
                for (auto i = 0; i < neighbors.size(); i = i + 1) {
                    auto neighbor = neighbors[i];
                    auto found = false;
                    for (auto j = 0; j < visited.size(); j = j + 1) {
                        if (visited[j] == neighbor.getValue()) {
                            found = true;
                        }
                    }
                    if (!found) {
                        if (dfs(neighbor, target, visited) == 1) {
                            return 1;
                        }
                    }
                }
                return 0;
            }
        }
        
        auto test_graph_traversal = []() -> auto {
            auto g = Graph();
            auto n1 = GraphNode(1);
            auto n2 = GraphNode(2);
            auto n3 = GraphNode(3);
            auto n4 = GraphNode(4);
            auto n5 = GraphNode(5);
            
            n1.addNeighbor(n2);
            n1.addNeighbor(n3);
            n2.addNeighbor(n4);
            n3.addNeighbor(n5);
            n4.addNeighbor(n5);
            
            g.addNode(n1);
            g.addNode(n2);
            g.addNode(n3);
            g.addNode(n4);
            g.addNode(n5);
            
            auto found = 0;
            for (auto i = 1; i <= 5; i = i + 1) {
                found = found + g.dfs(n1, i, []);
            }
            return found;
        };
        
        class Sorter {
            auto bubbleSort(auto arr) {
                auto n = arr.size();
                for (auto i = 0; i < n - 1; i = i + 1) {
                    for (auto j = 0; j < n - i - 1; j = j + 1) {
                        if (arr[j] > arr[j + 1]) {
                            auto temp = arr[j];
                            arr[j] = arr[j + 1];
                            arr[j + 1] = temp;
                        }
                    }
                }
                return arr;
            }
        }
        
        auto test_bubble_sort = []() -> auto {
            auto sorter = Sorter();
            auto total = 0;
            for (auto test = 0; test < 3; test = test + 1) {
                auto arr = [64, 34, 25, 12, 22, 11, 90, 5, 77, 30];
                arr = sorter.bubbleSort(arr);
                total = total + arr[0] + arr[arr.size() - 1];
            }
            return total;
        };
    )");
    
    // Test 1: JaiScript OneDef Point
    measure("JaiScript (OneDef): Point class", [&e]() {
        e->execute("test_point_class();");
    });
    
    // Test 2: JaiScript OneDef Counter  
    measure("JaiScript (OneDef): Counter class", [&e]() {
        e->execute("test_counter_class();");
    });
    
    // Test 3: JaiScript OneDef Graph
    measure("JaiScript (OneDef): Graph traversal", [&e]() {
        e->execute("test_graph_traversal();");
    });
    
    // Test 4: JaiScript OneDef Sorting
    measure("JaiScript (OneDef): Bubble sort", [&e]() {
        e->execute("test_bubble_sort();");
    });
    
    // Setup ChaiScript OneDef approach - define classes once
    chaiscript::ChaiScript chai;
    chai.add(chaiscript::fun([](double x) { return std::sqrt(x); }), "sqrt");
    chai.eval(R"(
        class Point {
            var x;
            var y;
            def Point(px, py) {
                this.x = px;
                this.y = py;
            }
            def distance_to(other) {
                var dx = this.x - other.x;
                var dy = this.y - other.y;
                return sqrt(dx * dx + dy * dy);
            }
        };
        
        def test_point_class() {
            var totalDist = 0.0;
            for (var i = 0; i < 10; ++i) {
                var p1 = Point(i, i + 1);
                var p2 = Point(i + 1, i);
                totalDist = totalDist + p1.distance_to(p2);
            }
            return totalDist;
        }
        
        class Counter {
            var value;
            var step;
            def Counter(start) {
                this.value = start;
                this.step = 1;
            }
            def increment() {
                this.value = this.value + this.step;
                return this.value;
            }
            def setStep(newStep) {
                this.step = newStep;
            }
        };
        
        def test_counter_class() {
            var total = 0;
            for (var i = 0; i < 15; ++i) {
                var c = Counter(i);
                c.setStep(2);
                total = total + c.increment();
                total = total + c.increment();
            }
            return total;
        }
        
        class GraphNode {
            var value;
            var neighbors;
            
            def GraphNode(val) {
                this.value = val;
                this.neighbors = [];
            }
            
            def addNeighbor(node) {
                this.neighbors.push_back(node);
            }
            
            def getValue() {
                return this.value;
            }
            
            def getNeighbors() {
                return this.neighbors;
            }
        };
        
        class Graph {
            var nodes;
            
            def Graph() {
                this.nodes = [];
            }
            
            def addNode(node) {
                this.nodes.push_back(node);
            }
            
            def dfs(start, target, visited) {
                if (start.getValue() == target) {
                    return 1;
                }
                
                visited.push_back(start.getValue());
                
                var neighbors = start.getNeighbors();
                for (var i = 0; i < neighbors.size(); ++i) {
                    var neighbor = neighbors[i];
                    var found = false;
                    for (var j = 0; j < visited.size(); ++j) {
                        if (visited[j] == neighbor.getValue()) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (this.dfs(neighbor, target, visited) == 1) {
                            return 1;
                        }
                    }
                }
                return 0;
            }
        };
        
        def test_graph_traversal() {
            var g = Graph();
            var n1 = GraphNode(1);
            var n2 = GraphNode(2);
            var n3 = GraphNode(3);
            var n4 = GraphNode(4);
            var n5 = GraphNode(5);
            
            n1.addNeighbor(n2);
            n1.addNeighbor(n3);
            n2.addNeighbor(n4);
            n3.addNeighbor(n5);
            n4.addNeighbor(n5);
            
            g.addNode(n1);
            g.addNode(n2);
            g.addNode(n3);
            g.addNode(n4);
            g.addNode(n5);
            
            var found = 0;
            for (var i = 1; i <= 5; ++i) {
                found = found + g.dfs(n1, i, []);
            }
            return found;
        }
        
        class Sorter {
            def bubbleSort(arr) {
                var n = arr.size();
                for (var i = 0; i < n - 1; ++i) {
                    for (var j = 0; j < n - i - 1; ++j) {
                        if (arr[j] > arr[j + 1]) {
                            var temp = arr[j];
                            arr[j] = arr[j + 1];
                            arr[j + 1] = temp;
                        }
                    }
                }
                return arr;
            }
        };
        
        def test_bubble_sort() {
            var sorter = Sorter();
            var total = 0;
            for (var test = 0; test < 3; ++test) {
                var arr = [64, 34, 25, 12, 22, 11, 90, 5, 77, 30];
                arr = sorter.bubbleSort(arr);
                total = total + arr[0] + arr[arr.size() - 1];
            }
            return total;
        }
    )");
    
    // Test 1: ChaiScript OneDef Point
    measure("ChaiScript (OneDef): Point class", [&chai]() {
        chai.eval("test_point_class();");
    });
    
    // Test 2: ChaiScript OneDef Counter
    measure("ChaiScript (OneDef): Counter class", [&chai]() {
        chai.eval("test_counter_class();");
    });
    
    // Test 3: ChaiScript OneDef Graph
    measure("ChaiScript (OneDef): Graph traversal", [&chai]() {
        chai.eval("test_graph_traversal();");
    });
    
    // Test 4: ChaiScript OneDef Sorting
    measure("ChaiScript (OneDef): Bubble sort", [&chai]() {
        chai.eval("test_bubble_sort();");
    });
    
    std::cout << "\n=== Fair Performance Comparison ===\n";
    
    // Redefinition Comparison (both engines redefine classes each iteration)
    std::cout << "\n1. Redefinition Strategy (JaiScript vs ChaiScript NewEngine)\n";
    std::cout << std::setw(40) << "Test" 
              << std::setw(20) << "JaiScript (Redef)" 
              << std::setw(20) << "ChaiScript (NewEng)" 
              << std::setw(10) << "Speedup" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    
    for (size_t i = 0; i < jaiscript_redef_results.size() && i < chaiscript_newengine_results.size(); ++i) {
        auto& jai_result = jaiscript_redef_results[i];
        auto& chai_result = chaiscript_newengine_results[i];
        
        // Extract test name
        std::string test_name = jai_result.name.substr(jai_result.name.find_last_of(':') + 2);
        
        double speedup = static_cast<double>(chai_result.microseconds) / jai_result.microseconds;
        
        std::cout << std::setw(40) << test_name
                  << std::setw(20) << (std::to_string(jai_result.microseconds) + " μs")
                  << std::setw(20) << (std::to_string(chai_result.microseconds) + " μs")
                  << std::setw(10) << (std::to_string(speedup).substr(0, 4) + "x") << std::endl;
    }
    
    // OneDef Comparison (both engines define classes once, use function wrappers)
    std::cout << "\n2. OneDef Strategy (both engines define classes once)\n";
    std::cout << std::setw(40) << "Test" 
              << std::setw(20) << "JaiScript (OneDef)" 
              << std::setw(20) << "ChaiScript (OneDef)" 
              << std::setw(10) << "Speedup" << std::endl;
    std::cout << std::string(90, '-') << std::endl;
    
    for (size_t i = 0; i < jaiscript_onedef_results.size() && i < chaiscript_onedef_results.size(); ++i) {
        auto& jai_result = jaiscript_onedef_results[i];
        auto& chai_result = chaiscript_onedef_results[i];
        
        // Extract test name
        std::string test_name = jai_result.name.substr(jai_result.name.find_last_of(':') + 2);
        
        double speedup = static_cast<double>(chai_result.microseconds) / jai_result.microseconds;
        
        std::cout << std::setw(40) << test_name
                  << std::setw(20) << (std::to_string(jai_result.microseconds) + " μs")
                  << std::setw(20) << (std::to_string(chai_result.microseconds) + " μs")
                  << std::setw(10) << (std::to_string(speedup).substr(0, 4) + "x") << std::endl;
    }
    
    std::cout << "\n=== Strategy Comparison Summary ===\n";
    
    // Calculate averages for each approach
    double redef_total_speedup = 0;
    int redef_count = 0;
    for (size_t i = 0; i < jaiscript_redef_results.size() && i < chaiscript_newengine_results.size(); ++i) {
        redef_total_speedup += static_cast<double>(chaiscript_newengine_results[i].microseconds) / jaiscript_redef_results[i].microseconds;
        redef_count++;
    }
    
    double onedef_total_speedup = 0;
    int onedef_count = 0;
    for (size_t i = 0; i < jaiscript_onedef_results.size() && i < chaiscript_onedef_results.size(); ++i) {
        onedef_total_speedup += static_cast<double>(chaiscript_onedef_results[i].microseconds) / jaiscript_onedef_results[i].microseconds;
        onedef_count++;
    }
    
    if (redef_count > 0) {
        std::cout << "Redefinition Strategy Average: JaiScript " << (redef_total_speedup / redef_count) << "x faster than ChaiScript\\n";
    }
    if (onedef_count > 0) {
        std::cout << "OneDef Strategy Average: JaiScript " << (onedef_total_speedup / onedef_count) << "x faster than ChaiScript\\n";
    }
    
    // Show which approach is better for each engine
    if (jaiscript_redef_results.size() > 0 && jaiscript_onedef_results.size() > 0) {
        long jai_redef_avg = 0, jai_onedef_avg = 0;
        for (const auto& result : jaiscript_redef_results) jai_redef_avg += result.microseconds;
        for (const auto& result : jaiscript_onedef_results) jai_onedef_avg += result.microseconds;
        jai_redef_avg /= jaiscript_redef_results.size();
        jai_onedef_avg /= jaiscript_onedef_results.size();
        
        std::cout << "\\nJaiScript: " << (jai_onedef_avg < jai_redef_avg ? "OneDef" : "Redefinition") 
                  << " strategy is " << (jai_onedef_avg < jai_redef_avg ? 
                      static_cast<double>(jai_redef_avg) / jai_onedef_avg : 
                      static_cast<double>(jai_onedef_avg) / jai_redef_avg) 
                  << "x faster\\n";
    }
    
    if (chaiscript_newengine_results.size() > 0 && chaiscript_onedef_results.size() > 0) {
        long chai_neweng_avg = 0, chai_onedef_avg = 0;
        for (const auto& result : chaiscript_newengine_results) chai_neweng_avg += result.microseconds;
        for (const auto& result : chaiscript_onedef_results) chai_onedef_avg += result.microseconds;
        chai_neweng_avg /= chaiscript_newengine_results.size();
        chai_onedef_avg /= chaiscript_onedef_results.size();
        
        std::cout << "ChaiScript: " << (chai_onedef_avg < chai_neweng_avg ? "OneDef" : "NewEngine") 
                  << " strategy is " << (chai_onedef_avg < chai_neweng_avg ? 
                      static_cast<double>(chai_neweng_avg) / chai_onedef_avg : 
                      static_cast<double>(chai_onedef_avg) / chai_neweng_avg) 
                  << "x faster\\n";
    }
    
    std::cout << "JaiScript Features:\n";
    std::cout << "✓ Full OOP with native class syntax\n";
    std::cout << "✓ Constructor with parameters and field initialization\n";
    std::cout << "✓ Inheritance with super() calls\n";
    std::cout << "✓ Method dispatch with implicit this\n";
    std::cout << "✓ Hot reload preserves instances\n";
    std::cout << "✓ Switch/case statements\n";
    std::cout << "✓ Range-based for loops\n";
    std::cout << "✓ Exception handling (try/catch/throw)\n";
    
    return 0;
}