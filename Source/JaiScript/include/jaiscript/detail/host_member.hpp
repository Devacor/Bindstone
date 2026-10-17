#pragma once
#ifndef __JAISCRIPT_DETAIL_HOST_MEMBER_HPP__
#define __JAISCRIPT_DETAIL_HOST_MEMBER_HPP__

#include <cstddef>
#include <cstdint>

namespace jai {
class script_value;
class engine;
}

namespace jai::detail {

// A bound C++ data member read and written in place: registered once per bound field, it reaches the member through
// its address instead of the _get_/_set_ call. read produces exactly what the bound getter returns; write takes only
// values the setter would store unchanged and answers false otherwise, so the setter keeps every conversion and text.
struct host_member_lane {
	script_value (*read)(const void* address, engine* eng) = nullptr;
	bool (*write)(void* address, const script_value& value) = nullptr;   // null: the member is read-only
	// the member's address in the receiver's C++ object, or null where the lane cannot reach it (never throws)
	void* (*locate)(const host_member_lane& lane, const script_value& receiver) = nullptr;
};

// A located member for one receiver layout: the member sits at stored pointer + delta while the receiver's C++
// object is held under the same registered type and, for a polymorphic one, has the same dynamic type (vptr)
struct host_member_hit {
	const host_member_lane* lane = nullptr;
	std::ptrdiff_t delta = 0;
	uint64_t stored_type = UINT64_MAX;
	const void* vptr = nullptr;
};

} // namespace jai::detail

#endif
