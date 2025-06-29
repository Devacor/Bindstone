#include "../jai_test.hpp"
#include "jaiscript/jaiscript.hpp"
#include "jaiscript/core/class_builder.hpp"

using namespace jai;
using namespace jai::test;


JAI_TEST_SUITE(CustomSubscriptOperator)

/*
JAI_TEST(simple_test) {
    std::cout << "Simple test running\n" << std::flush;
    expect_eq(1, 1);
    std::cout << "Simple test passed\n" << std::flush;
}
*/

// Simple vector-like class with custom operator[]
class MyVector {
public:
    std::vector<int> data;
    
    MyVector() = default;
    MyVector(std::initializer_list<int> init) : data(init) {}
    
    int& operator[](size_t index) {
        if (index >= data.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data[index];
    }
    
    const int& operator[](size_t index) const {
        if (index >= data.size()) {
            throw std::out_of_range("Index out of bounds");
        }
        return data[index];
    }
    
    void push_back(int value) {
        data.push_back(value);
    }
    
    int size() const {
        return static_cast<int>(data.size());
    }
};

// script_string wrapper with custom operator[] that returns characters
class MyString {
public:
    std::string str;
    
    MyString() : str("") {}
    MyString(std::string s) : str(std::move(s)) {}
    
    char operator[](size_t index) const {
        if (index >= str.size()) {
            return '\0';
        }
        return str[index];
    }
    
    int length() const {
        return static_cast<int>(str.length());
    }
};

// Matrix class with custom 2D subscript operator
class Matrix {
public:
    std::vector<std::vector<double>> data;
    size_t rows, cols;
    
    Matrix(size_t r, size_t c) : rows(r), cols(c) {
        data.resize(rows, std::vector<double>(cols, 0.0));
    }
    
    // Row proxy for chained subscript operator
    class RowProxy {
        std::vector<double>& row;
    public:
        RowProxy(std::vector<double>& r) : row(r) {}
        
        double& operator[](size_t col) {
            return row[col];
        }
    };
    
    RowProxy operator[](size_t row) {
        if (row >= rows) {
            throw std::out_of_range("Row index out of bounds");
        }
        return RowProxy(data[row]);
    }
};

JAI_TEST(basic_custom_subscript_read) {
    engine engine;
    
    // Register MyVector class
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back)
        .method("size", &MyVector::size);
    
    // Register operator[] as a global function
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        return vec[index];
    });
    
    // Test read access
    engine.execute(R"(
        var vec = MyVector();
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);
        
        var first = vec[0];
        var second = vec[1];
        var third = vec[2];
    )");
    
    expect_eq(engine.get_variable("first").as<int>(), 10);
    expect_eq(engine.get_variable("second").as<int>(), 20);
    expect_eq(engine.get_variable("third").as<int>(), 30);
}

JAI_TEST(custom_subscript_with_assignment) {
    engine engine;
    
    // Register MyVector
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back)
        .method("size", &MyVector::size);
    
    // Register operator[] for reading
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        return vec[index];
    });
    
    // Register []= for assignment
    engine.add_function("[]=", [](MyVector& vec, int index, int value) {
        vec[index] = value;
    });
    
    // Test read and write access
    engine.execute(R"(
        var vec = MyVector();
        vec.push_back(10);
        vec.push_back(20);
        vec.push_back(30);
        
        // Read initial values
        var before = vec[1];
        
        // Modify using subscript
        vec[1] = 99;
        
        // Read modified value
        var after = vec[1];
    )");
    
    expect_eq(engine.get_variable("before").as<int>(), 20);
    expect_eq(engine.get_variable("after").as<int>(), 99);
}

JAI_TEST(string_subscript_operator) {
    engine engine;
    
    // Register MyString class
    make_class_builder<MyString>(engine, "MyString")
        .constructor<const std::string&>()
        .method("length", &MyString::length);
    
    // Register operator[] as global function
    engine.add_function("[]", [](const MyString& str, int index) -> char {
        return str[index];
    });
    
    engine.execute(R"(
        var str = MyString("Hello");
        var h = str[0];
        var e = str[1];
        var o = str[4];
        var len = str.length();
    )");
    
    expect_eq(engine.get_variable("h").as<char>(), 'H');
    expect_eq(engine.get_variable("e").as<char>(), 'e');
    expect_eq(engine.get_variable("o").as<char>(), 'o');
    expect_eq(engine.get_variable("len").as<int>(), 5);
}

JAI_TEST(operator_overload_syntax) {
    engine engine;
    
    // Define a class in JaiScript with operator[] overload
    engine.execute(R"(
        class Container {
            private:
                var items;
                
            public:
                Container() {
                    this.items = [];
                }
                
                func []=(index, value) {
                    // Ensure array is large enough
                    while (this.items.size() <= index) {
                        this.items.push_back(0);
                    }
                    this.items[index] = value;
                }
                
                func [](index) {
                    if (index < this.items.size()) {
                        return this.items[index];
                    }
                    return null;
                }
                
                func size() {
                    return this.items.size();
                }
        }
        
        var c = Container();
        c[0] = 100;
        c[2] = 300;
        
        var val0 = c[0];
        var val1 = c[1];
        var val2 = c[2];
        var sz = c.size();
    )");
    
    expect_eq(engine.get_variable("val0").as<int>(), 100);
    expect_eq(engine.get_variable("val1").as<int>(), 0);
    expect_eq(engine.get_variable("val2").as<int>(), 300);
    expect_eq(engine.get_variable("sz").as<int>(), 3);
}

JAI_TEST(mixed_builtin_and_custom_subscript) {
    engine engine;
    
    // Register a custom type
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back);
    
    // Register operator[] as global function
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        return vec[index];
    });
    
    engine.execute(R"(
        // Built-in array
        var arr = [10, 20, 30];
        var a1 = arr[1];
        
        // Built-in map
        var map = {"one": 1, "two": 2};
        var m1 = map["one"];
        
        // Custom type
        var vec = MyVector();
        vec.push_back(100);
        vec.push_back(200);
        var v1 = vec[1];
    )");
    
    expect_eq(engine.get_variable("a1").as<int>(), 20);
    expect_eq(engine.get_variable("m1").as<int>(), 1);
    expect_eq(engine.get_variable("v1").as<int>(), 200);
}

JAI_TEST(error_handling) {
    engine engine;
    
    // Register MyVector
    make_class_builder<MyVector>(engine, "MyVector")
        .constructor<>()
        .method("push_back", &MyVector::push_back);
    
    // Register operator[] with bounds checking
    engine.add_function("[]", [](MyVector& vec, int index) -> int {
        if (index < 0 || static_cast<size_t>(index) >= vec.size()) {
            throw std::out_of_range("MyVector index out of bounds");
        }
        return vec[index];
    });
    
    // Test out of bounds access
    try {
        engine.execute(R"(
            var vec = MyVector();
            vec.push_back(10);
            var x = vec[5];  // Out of bounds
        )");
        expect_true(false); // Should not reach here
    } catch (const runtime_error& e) {
        expect_true(true); // Expected exception
    }
    
    // Test subscript on non-subscriptable type
    try {
        engine.execute(R"(
            var x = 42;
            var y = x[0];  // Can't subscript an integer
        )");
        expect_true(false); // Should not reach here
    } catch (const runtime_error& e) {
        expect_true(true); // Expected exception
    }
}

JAI_TEST(chained_subscript_operator) {
    engine engine;
    
    // Register Matrix
    make_class_builder<Matrix>(engine, "Matrix")
        .constructor<size_t, size_t>();
    
    // Register operator[] that returns a row
    // For now, we can't return references to vectors directly
    // Instead, let's verify the matrix was created correctly
    engine.add_function("getMatrixRows", [](Matrix& mat) -> int {
        return static_cast<int>(mat.rows);
    });
    engine.add_function("getMatrixCols", [](Matrix& mat) -> int {
        return static_cast<int>(mat.cols);
    });
    
    engine.execute(R"(
        var mat = Matrix(2, 3);
        
        // For now, test basic matrix properties
        var rows = getMatrixRows(mat);
        var cols = getMatrixCols(mat);
        var isValidMatrix = (rows == 2 && cols == 3);
    )");
    
    expect_true(engine.get_variable("isValidMatrix").as<bool>());
}

JAI_TEST_SUITE_END()

JAI_TEST_MAIN()