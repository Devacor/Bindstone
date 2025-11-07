#pragma once

#ifndef __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__
#define __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__

#include <unordered_map>
#include <deque>
#include <string>
#include <string_view>
#include <cstdint>
#include <limits>

namespace jai {

	struct sv_hash {
		using is_transparent = void; // enable heterogeneous lookup
		[[nodiscard]] size_t operator()(std::string_view sv) const noexcept {
			return std::hash<std::string_view>{}(sv);
		}
	};

	struct sv_equal {
		using is_transparent = void;
		[[nodiscard]] bool operator()(std::string_view a, std::string_view b) const noexcept {
			return a == b;
		}
	};

	// String symbolizer (local-only, non-deterministic IDs).
	class string_symbolizer {
	public:
		using id_type = std::uint64_t;

		string_symbolizer() {
			index_.reserve(256);
			// Pre-intern "this" to avoid a lazy branch in hot paths
			this_id_ = intern("this");
		}

		// Intern or fetch an existing ID. No string allocation on hit.
		[[nodiscard]] id_type intern(std::string_view sv) noexcept {
			if (auto it = index_.find(sv); it != index_.end())
				return it->second;

			// Copy once into deque (stable address), then map a string_view to it (no second copy).
			auto& stored = storage_.emplace_back(sv);
			const std::string_view key{ stored.data(), stored.size() };

			// Note: size() fits into 32-bit id by design; guard if you need >4B strings.
			const id_type id = static_cast<id_type>(ids_.size());
			ids_.push_back(key);
			index_.emplace(key, id);
			return id;
		}

		// Fast path for known keys (no allocation). Returns empty view if OOB.
		[[nodiscard]] inline std::string_view get_string(id_type id) const noexcept {
			return id < ids_.size() ? ids_[id] : std::string_view{};
		}

		[[nodiscard]] inline id_type get_this_id() const noexcept { return this_id_; }

		// Optional: batch intern to amortize rehashes.
		template <class Range>
		void intern_many(const Range& r) {
			// Heuristic: avoid frequent rehash; grow to near final size.
			index_.reserve(index_.size() + size_hint(r));
			ids_.reserve(ids_.size() + size_hint(r));
			for (std::string_view s : r) (void)intern(s);
		}

	private:
		// Owning storage with stable addresses (push_back/push_front do not invalidate references)
		std::deque<std::string> storage_;

		// Map from string_view (into storage_) to ID (no second copy)
		std::unordered_map<std::string_view, id_type, sv_hash, sv_equal> index_;

		// ID -> string_view (into storage_) for reverse lookup
		std::vector<std::string_view> ids_;

		id_type this_id_{ std::numeric_limits<id_type>::max() };

		// Tiny helper to allow generic ranges without forcing size()
		template <class Range>
		static size_t size_hint(const Range& r) {
			if constexpr (requires { r.size(); }) return r.size();
			return 0;
		}
	};

} // namespace jai

#endif // __JAISCRIPT_DETAIL_STRING_SYMBOLIZER_HPP__
