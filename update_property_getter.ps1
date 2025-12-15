$file = 'Source/JaiScript/include/jaiscript/core/class_builder.hpp'
$content = Get-Content $file -Raw

$oldText = @'
            if (auto eng = engine_weak.lock()) {
                return detail::value_converter<P>::to(cpp_obj.get()->*member, eng.get());
            }
            throw runtime_error("Engine no longer exists");
        });

        class_def_->add_method("_set_" + name
'@

$newText = @'
            if (auto eng = engine_weak.lock()) {
                // Special handling for std::vector<T> - wrap in bound_cpp_vector for zero-copy access
                if constexpr (detail::is_specialization_v<P, std::vector>) {
                    using element_type = typename P::value_type;
                    // Create bound_cpp_vector wrapper that references the C++ vector directly
                    auto wrapper = std::make_shared<bound_cpp_vector<element_type>>(
                        cpp_obj.get()->*member, eng);
                    return eng->make_object(wrapper);
                } else {
                    return detail::value_converter<P>::to(cpp_obj.get()->*member, eng.get());
                }
            }
            throw runtime_error("Engine no longer exists");
        });

        class_def_->add_method("_set_" + name
'@

# Find the first occurrence and replace it
$index = $content.IndexOf($oldText)
if ($index -ge 0) {
    $content = $content.Substring(0, $index) + $newText + $content.Substring($index + $oldText.Length)
    $content | Set-Content $file -NoNewline
    Write-Host "Successfully updated property getter!"
} else {
    Write-Host "Pattern not found in file"
}
