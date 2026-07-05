#pragma once

#ifndef __JAISCRIPT_DETAIL_EXECUTION_LIMITS_HPP__
#define __JAISCRIPT_DETAIL_EXECUTION_LIMITS_HPP__

#include <jaiscript/core/checked_result.hpp>
#include <jaiscript/core/runtime_errors.hpp>
#include <cstddef>
#include <cstdint>

namespace jai::detail {

	// Execution-limit state shared VERBATIM by both backends and the shared builtin
	// methods (parity by construction). The engine owns one instance; the active
	// backend caches a pointer to it, so reentrant executes share the outer run's
	// allowance. Instance state only - never static (zero-statics/multi-engine rule).
	struct execution_limits {
		// Terminal-error latch: while set, EVERY script catch handler is skipped -
		// try/catch, catch-all, nested, catches inside coroutines/lambdas, and catches
		// in outer scripts around a failing reentrant execute(). The failure unwinds
		// to the outermost host execute()/resume() boundary, which reports it normally.
		// Budget overruns latch this ALWAYS (scripts can never catch a timeout).
		// Cleared only by a non-reentrant prepare_for_execution or a host-level resume.
		bool terminal_error = false;

		// Approximate script memory accounting (engine::memory_cap). HIGH-WATER MARK:
		// charges only grow during one top-level execute (releases are not credited
		// back - v1 simplification) and the counter resets per top-level execute. On
		// each raise the counter re-arms to zero so a script that catches the first
		// denial can free its caches and keep working; the SECOND raise in the same
		// execute latches terminal_error (uncatchable).
		size_t memory_used = 0;
		size_t memory_limit = SIZE_MAX;     // effective limit (SIZE_MAX = unlimited)
		size_t memory_cap = 0;              // configured cap in bytes (0 = unlimited)
		uint32_t memory_raises = 0;
		uint64_t memory_cap_symbol_id = 0;  // interned decimal cap size for error text

		// Denial-style charge: false = the allocation would exceed the cap and nothing
		// was added (the value must not come into existence - the caller raises).
		bool memory_charge(size_t bytes) noexcept {
			if (memory_used + bytes > memory_limit) [[unlikely]] { return false; }
			memory_used += bytes;
			return true;
		}
		// Count-only charge for sites that cannot deny in place (environment growth,
		// internal clones); the raise happens at the next loop back-edge / call entry.
		void memory_charge_deferred(size_t bytes) noexcept { memory_used += bytes; }
		bool memory_tripped() const noexcept { return memory_used > memory_limit; }

		void reset_for_execute() noexcept {
			terminal_error = false;
			memory_used = 0;
			memory_raises = 0;
		}
	};

	// Style-matched to the budget error text; {0} resolves to the interned cap size.
	inline constexpr std::string_view k_memory_cap_message =
		"Script memory cap exceeded ({0} bytes) - raise engine::memory_cap or break up the work";

	// The single raise point for memory-cap errors (identical text/semantics on both
	// backends and in the shared builtin methods). Re-arms the counter so a catching
	// script gets a fresh allowance to clean up; the second raise per top-level execute
	// latches terminal (Dev ruling: ONE script catch per execute, then terminal).
	inline error_propagator raise_memory_cap(execution_limits& limits) noexcept {
		limits.memory_used = 0;
		if (++limits.memory_raises >= 2) { limits.terminal_error = true; }
		return error_propagator{ make_error_code(runtime_error_code::memory_cap_exceeded),
			k_memory_cap_message, limits.memory_cap_symbol_id };
	}

} // namespace jai::detail

#endif // __JAISCRIPT_DETAIL_EXECUTION_LIMITS_HPP__
