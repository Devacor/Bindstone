#\!/bin/bash

# Replace value_type with script_value_type
find . \( -name "*.hpp" -o -name "*.cpp" \) -not -path "./TestSuite/*" -not -name "*.backup" -not -name "*.backup*" -exec sed -i.bak \
    -e 's/\bvalue_type::/script_value_type::/g' \
    -e 's/enum value_type/enum script_value_type/g' \
    -e 's/value_type \([a-zA-Z_][a-zA-Z0-9_]*\) =/script_value_type \1 =/g' \
    -e 's/value_type base_type/script_value_type base_type/g' \
    -e 's/(value_type/(script_value_type/g' \
    -e 's/<value_type>/<script_value_type>/g' \
    -e 's/value_type type/script_value_type type/g' \
    {} \;

# Clean up backup files
find . -name "*.bak" -delete
