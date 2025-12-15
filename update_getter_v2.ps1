$file = 'Source/JaiScript/include/jaiscript/core/class_builder.hpp'

# Read content as array of lines
$lines = Get-Content $file

# Find the lines to replace (around line 1654)
$replaced = $false
$count = 0
for ($i = 0; $i < $lines.Count; $i++) {
    if ($lines[$i] -match '^\s+if \(auto eng = engine_weak\.lock\(\)\) \{$' -and
        $lines[$i+1] -match '^\s+return detail::value_converter<P>::to\(cpp_obj\.get\(\)->') {

        # Replace these 3 lines with the new code
        $indent = '            '
        $lines[$i] = "$indent if (auto eng = engine_weak.lock()) {"
        $lines[$i+1] = "$indent    // Special handling for std::vector<T> - wrap in bound_cpp_vector for zero-copy access"
        $lines[$i+2] = "$indent    if constexpr (detail::is_specialization_v<P, std::vector>) {"

        # Insert new lines after current position
        $newLines = @(
            "$indent        using element_type = typename P::value_type;",
            "$indent        // Create bound_cpp_vector wrapper that references the C++ vector directly",
            "$indent        auto wrapper = std::make_shared<bound_cpp_vector<element_type>>(",
            "$indent            cpp_obj.get()->*member, eng);",
            "$indent        return eng->make_object(wrapper);",
            "$indent    } else {",
            "$indent        return detail::value_converter<P>::to(cpp_obj.get()->*member, eng.get());",
            "$indent    }",
            "$indent}"
        )

        # Remove the old closing brace line
        $lines = $lines[0..$i] + $newLines + $lines[($i+4)..($lines.Count-1)]
        $replaced = $true
        $count++

        # Skip ahead to avoid replacing the same pattern twice in one iteration
        $i += $newLines.Count + 3

        if ($count -ge 2) {
            break
        }
    }
}

if ($replaced) {
    $lines | Set-Content $file
    Write-Host "Successfully updated $count property getter(s)!"
} else {
    Write-Host "Pattern not found"
}
