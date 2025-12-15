$file = 'Source/JaiScript/include/jaiscript/core/class_builder.hpp'
$content = Get-Content $file -Raw

$searchPattern = @'
            return script_value(std::monostate{}, engine_weak); // null
        });

        return *this;
    }

public:
    // Add property with getter/setter
'@

$replacement = @'
            return script_value(std::monostate{}, engine_weak); // null
        });

        // Register bound_cpp_vector<T> if this property is a std::vector
        if constexpr (detail::is_specialization_v<P, std::vector>) {
            using element_type = typename P::value_type;
            std::string wrapper_type_name = std::string("bound_cpp_vector<") + typeid(element_type).name() + ">";

            // Check if already registered
            auto existing = engine_.get_class_definition_by_type(std::type_index(typeid(bound_cpp_vector<element_type>)));
            if (!existing) {
                // Register bound_cpp_vector<element_type> with array-like methods
                class_builder<bound_cpp_vector<element_type>>(engine_, wrapper_type_name)
                    .method("size", &bound_cpp_vector<element_type>::size)
                    .method("empty", &bound_cpp_vector<element_type>::empty)
                    .method("clear", &bound_cpp_vector<element_type>::clear)
                    .method("push_back", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back))
                    .method("push", static_cast<void(bound_cpp_vector<element_type>::*)(const element_type&)>(&bound_cpp_vector<element_type>::push_back)) // Alias
                    .method("pop_back", &bound_cpp_vector<element_type>::pop_back)
                    .method("pop", &bound_cpp_vector<element_type>::pop_back) // Alias
                    .build();
            }
        }

        return *this;
    }

public:
    // Add property with getter/setter
'@

if ($content -match [regex]::Escape($searchPattern)) {
    $content = $content -replace [regex]::Escape($searchPattern), $replacement
    $content | Set-Content $file -NoNewline
    Write-Host "Successfully added bound_cpp_vector registration!"
} else {
    Write-Host "Pattern not found - searching for alternative..."
}
