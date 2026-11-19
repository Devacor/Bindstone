#pragma once

#include <string_view>

namespace jai::detail {

// Library names that have one better spelling now: a call nothing declares reports what to write instead of a bare
// "undeclared" (internal/diagnostics.md "Retired names"). The checker asks only after every declaration has had its say, so a
// script's or a host's own function of the name keeps working.
struct retired_name {
	std::string_view name;          // as a script calls it: a global's name, or a namespace member's qualified name
	std::string_view replacement;   // what must exist for the advice to hold on this engine; empty when the language always has it
	std::string_view text;
};

inline constexpr retired_name k_retired_names[] = {
	{"file_data", "file::data", "JaiScript has no 'file_data': write file::data(path)"},
	{"read_file", "file::read", "JaiScript has no 'read_file': write file::read(path)"},
	{"write_file", "file::write", "JaiScript has no 'write_file': write file::write(path, text)"},
	{"file_exists", "file::exists", "JaiScript has no 'file_exists': write file::exists(path)"},
	{"delete_file", "file::remove", "JaiScript has no 'delete_file': write file::remove(path)"},
	{"move_file", "file::move", "JaiScript has no 'move_file': write file::move(from, to)"},
	{"dir_exists", "file::directory_exists", "JaiScript has no 'dir_exists': write file::directory_exists(path)"},
	{"make_dirs", "file::make_directories", "JaiScript has no 'make_dirs': write file::make_directories(path)"},
	{"list_dir", "file::list_directory", "JaiScript has no 'list_dir': write file::list_directory(path)"},
	{"writable_dir", "file::ensure_writable_directory", "JaiScript has no 'writable_dir': write file::ensure_writable_directory(path)"},
	{"type_of", "", "JaiScript has no 'type_of': write reflect::type_of(x)"},
	{"is_registered_type", "", "JaiScript has no 'is_registered_type': write reflect::is_registered_type(x)"},
	{"to_base64", "base64_encode", "JaiScript has no 'to_base64': write base64_encode(to_binary(value))"},
	{"from_base64", "base64_decode", "JaiScript has no 'from_base64': write from_binary(base64_decode(text))"},
	{"time::now", "time::engine_elapsed", "JaiScript has no 'time::now': write time::engine_elapsed()"},
	{"scoped_template", "", "JaiScript has no 'scoped_template': write untrusted_template(path, map)"},
	{"scoped_template_text", "", "JaiScript has no 'scoped_template_text': write untrusted_template_text(text, map)"},
	{"reflect::scoped_template_dirs", "reflect::untrusted_template_dirs", "JaiScript has no 'reflect::scoped_template_dirs': write reflect::untrusted_template_dirs()"},
	{"reflect::scoped_template_output_limit", "reflect::untrusted_template_output_limit",
	 "JaiScript has no 'reflect::scoped_template_output_limit': write reflect::untrusted_template_output_limit()"},
	{"jai::permissions::scoped_template", "jai::permissions::untrusted_template",
	 "JaiScript has no 'jai::permissions::scoped_template': write jai::permissions::untrusted_template"},
};

inline constexpr const retired_name* find_retired_name(std::string_view name) noexcept {
	for (const retired_name& entry : k_retired_names) {
		if (entry.name == name) { return &entry; }
	}
	return nullptr;
}

} // namespace jai::detail
