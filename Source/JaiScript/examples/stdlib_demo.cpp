#include <jaiscript/jaiscript.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <iostream>

int main() {
    std::cout << "=== JaiScript Standard Library Demo ===" << std::endl;
    
    auto engine = jai::createEngine();
    
    // Register standard library functions
    jai::stdlib::register_all(*engine);
    
    // Add some sample data
    engine->add_global("version", jai::script_value(jai::version()));
    engine->add_global("pi", jai::script_value(3.14159));
    
    std::cout << "\n--- I/O Functions ---" << std::endl;
    
    // Test print function
    engine->eval("print('Hello from JaiScript!');");
    engine->eval("print('Version:', version);");
    engine->eval("print('Pi value:', pi);");
    
    // Test print with multiple values
    engine->eval("print('Multiple', 'values', 'in', 'one', 'line');");
    engine->eval("print('Numbers:', 1, 2, 3, 4, 5);");
    engine->eval("print('Mixed types:', 42, true, 'string', null);");
    
    // Test write function (no newline)
    engine->eval("write('Line '); write('without '); write('newlines '); print('until now');");
    
    // Test type_of function
    engine->eval("print('\\nType information:');");
    engine->eval("print('Type of 42:', type_of(42));");
    engine->eval("print('Type of 3.14:', type_of(3.14));");
    engine->eval("print('Type of \"hello\":', type_of('hello'));");
    engine->eval("print('Type of true:', type_of(true));");
    engine->eval("print('Type of null:', type_of(null));");
    engine->eval("print('Type of [1,2,3]:', type_of([1,2,3]));");
    engine->eval("print('Type of {a: 1}:', type_of({a: 1}));");
    
    // Test to_string function
    engine->eval("print('\\nString conversion:');");
    engine->eval("var num_str = to_string(42); print('Number as string:', num_str);");
    engine->eval("var bool_str = to_string(true); print('Bool as string:', bool_str);");
    
    std::cout << "\n--- JSON Functions ---" << std::endl;
    
    // Test JSON functions
    engine->eval(R"(
        var data = {
            name: 'JaiScript',
            version: version,
            features: ['scripting', 'embedding', 'JSON'],
            metrics: {
                performance: 'fast',
                ease_of_use: 'high'
            }
        };
        
        print('Original data:', data);
        
        // Convert to JSON (compact)
        var json_compact = to_json(data);
        print('\nCompact JSON:');
        print(json_compact);
        
        // Convert to JSON (pretty-printed with 2-space indent)
        var json_pretty = to_json(data, 2);
        print('\nPretty JSON:');
        print(json_pretty);
        
        // Parse JSON back
        var parsed = from_json(json_compact);
        print('\nParsed data:', parsed);
        print('Parsed name:', parsed.name);
        print('First feature:', parsed.features[0]);
    )");
    
    std::cout << "\n--- Array Methods ---" << std::endl;
    
    // Test array builtin methods
    engine->eval(R"(
        var arr = [10, 20, 30];
        print('Initial array:', arr);
        print('Array size:', arr.size());
        
        arr.push(40);
        print('After push(40):', arr);
        
        var last = arr.pop();
        print('Popped value:', last);
        print('After pop:', arr);
        
        print('Front element:', arr.front());
        print('Back element:', arr.back());
        print('Is empty?', arr.empty());
        
        arr.clear();
        print('After clear:', arr);
        print('Is empty now?', arr.empty());
    )");
    
    std::cout << "\n--- Map Methods ---" << std::endl;
    
    // Test map builtin methods
    engine->eval(R"(
        var map = {a: 1, b: 2, c: 3};
        print('Initial map:', map);
        print('Map size:', map.size());
        
        print('Contains "b"?', map.contains('b'));
        print('Contains "d"?', map.contains('d'));
        
        print('Keys:', map.keys());
        print('Values:', map.values());
        
        map.erase('b');
        print('After erasing "b":', map);
        
        print('Is empty?', map.empty());
        map.clear();
        print('After clear:', map);
        print('Is empty now?', map.empty());
    )");
    
    std::cout << "\n=== Demo Complete ===" << std::endl;
    
    return 0;
}