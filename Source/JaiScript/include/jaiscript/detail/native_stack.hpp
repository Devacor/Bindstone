#pragma once

#ifndef __JAISCRIPT_DETAIL_NATIVE_STACK_HPP__
#define __JAISCRIPT_DETAIL_NATIVE_STACK_HPP__

#include <jaiscript/core/types.hpp>
#include <cstddef>
#include <cstdint>

namespace jai::detail {

	// True when less than 256 KiB of the calling thread's stack remains (Windows asks the thread's bounds; elsewhere false):
	// the guard of a native recursion per script call, which the call-depth cap otherwise bounds
	bool native_stack_low() noexcept;

	// The same question with a smaller margin, so a call's own guard fires first inside a recursion of calls
	bool native_stack_nesting_low() noexcept;

	inline constexpr std::string_view k_nesting_too_deep_listing = "nesting too deep for the host thread's stack";
	inline constexpr std::string_view k_nesting_too_deep = "Nesting too deep for the host thread's stack";

	// Source nested past the host thread's stack: a walk over it unwinds to its phase's entry, which reports it once
	class nesting_exhausted : public runtime_error {
	public:
		explicit nesting_exhausted(const source_location& at) : runtime_error(std::string(k_nesting_too_deep), at) {}
	};
	[[noreturn]] JAI_NOINLINE void throw_nesting_exhausted(const source_location& at);

	// A recursive walk over source (the parser, the checker, the slot pass, the compilers, the interpreter's evaluation) arms
	// this on entry, on the thread that runs it. Windows measures the thread's own stack; elsewhere the walk may take
	// k_native_stack_budget bytes below the frame that armed it.
	class native_stack_mark {
	public:
		native_stack_mark() noexcept { arm(); }
		void arm() noexcept {
			char probe;
			const std::uintptr_t here = reinterpret_cast<std::uintptr_t>(&probe);
			limit_ = here > k_native_stack_budget ? here - k_native_stack_budget : 0;
		}
		bool low() const noexcept {
#if defined(_WIN32)
			return native_stack_nesting_low();
#else
			char probe;
			return reinterpret_cast<std::uintptr_t>(&probe) < limit_;
#endif
		}
		void guard(const source_location& at) const {
			if (low()) [[unlikely]] { throw_nesting_exhausted(at); }
		}

		static constexpr std::size_t k_native_stack_budget = 1024 * 1024;

	private:
		std::uintptr_t limit_ = 0;
	};

}

#endif // __JAISCRIPT_DETAIL_NATIVE_STACK_HPP__
