#pragma once

#include <cstddef>
#include <string>

namespace jai {

	enum class field_migration_outcome { preserved, converted, reset, added, removed };

	// Aggregates actual field operations, not per-instance values or declared-schema changes.
	// A report owns metadata strings only. Type names describe the stored values before/after.
	struct field_migration_report {
		std::string class_name;
		std::string field_name;
		std::string old_type;
		std::string new_type;
		field_migration_outcome outcome{field_migration_outcome::preserved};
		std::size_t count{0};
	};
}
