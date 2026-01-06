#pragma once
#ifndef MV_JAI_SERIALIZATION_HPP
#define MV_JAI_SERIALIZATION_HPP

// JaiScript serialization support for MV types
// Include this header after both MV types and JaiScript serialization are available
//
// This provides save() and load() free functions for MV types that can be used
// with jai::property<T> serialization via ADL (Argument Dependent Lookup)

#include <jaiscript/serialization/archive.hpp>
#include "MV/Render/points.h"
#include "MV/Render/boxaabb.h"

namespace MV {

	// ============================================================
	// Point<T> serialization
	// ============================================================
	template<typename T>
	void save(jai::serialization::archive_writer& ar, const Point<T>& p) {
		if constexpr (std::is_same_v<T, float>) {
			ar.write_float32(p.x);
			ar.write_float32(p.y);
			ar.write_float32(p.z);
		} else if constexpr (std::is_same_v<T, double>) {
			ar.write_float64(p.x);
			ar.write_float64(p.y);
			ar.write_float64(p.z);
		} else if constexpr (std::is_integral_v<T>) {
			ar.write_int64(static_cast<int64_t>(p.x));
			ar.write_int64(static_cast<int64_t>(p.y));
			ar.write_int64(static_cast<int64_t>(p.z));
		}
	}

	template<typename T>
	void load(jai::serialization::archive_reader& ar, Point<T>& p) {
		if constexpr (std::is_same_v<T, float>) {
			p.x = ar.read_float32();
			p.y = ar.read_float32();
			p.z = ar.read_float32();
		} else if constexpr (std::is_same_v<T, double>) {
			p.x = ar.read_float64();
			p.y = ar.read_float64();
			p.z = ar.read_float64();
		} else if constexpr (std::is_integral_v<T>) {
			p.x = static_cast<T>(ar.read_int64());
			p.y = static_cast<T>(ar.read_int64());
			p.z = static_cast<T>(ar.read_int64());
		}
	}

	// ============================================================
	// Size<T> serialization
	// ============================================================
	template<typename T>
	void save(jai::serialization::archive_writer& ar, const Size<T>& s) {
		if constexpr (std::is_same_v<T, float>) {
			ar.write_float32(s.width);
			ar.write_float32(s.height);
			ar.write_float32(s.depth);
		} else if constexpr (std::is_same_v<T, double>) {
			ar.write_float64(s.width);
			ar.write_float64(s.height);
			ar.write_float64(s.depth);
		} else if constexpr (std::is_integral_v<T>) {
			ar.write_int64(static_cast<int64_t>(s.width));
			ar.write_int64(static_cast<int64_t>(s.height));
			ar.write_int64(static_cast<int64_t>(s.depth));
		}
	}

	template<typename T>
	void load(jai::serialization::archive_reader& ar, Size<T>& s) {
		if constexpr (std::is_same_v<T, float>) {
			s.width = ar.read_float32();
			s.height = ar.read_float32();
			s.depth = ar.read_float32();
		} else if constexpr (std::is_same_v<T, double>) {
			s.width = ar.read_float64();
			s.height = ar.read_float64();
			s.depth = ar.read_float64();
		} else if constexpr (std::is_integral_v<T>) {
			s.width = static_cast<T>(ar.read_int64());
			s.height = static_cast<T>(ar.read_int64());
			s.depth = static_cast<T>(ar.read_int64());
		}
	}

	// ============================================================
	// Scale serialization
	// ============================================================
	inline void save(jai::serialization::archive_writer& ar, const Scale& s) {
		ar.write_float32(s.x);
		ar.write_float32(s.y);
		ar.write_float32(s.z);
	}

	inline void load(jai::serialization::archive_reader& ar, Scale& s) {
		s.x = ar.read_float32();
		s.y = ar.read_float32();
		s.z = ar.read_float32();
	}

	// ============================================================
	// Color serialization
	// ============================================================
	inline void save(jai::serialization::archive_writer& ar, const Color& c) {
		ar.write_float32(c.R);
		ar.write_float32(c.G);
		ar.write_float32(c.B);
		ar.write_float32(c.A);
	}

	inline void load(jai::serialization::archive_reader& ar, Color& c) {
		c.R = ar.read_float32();
		c.G = ar.read_float32();
		c.B = ar.read_float32();
		c.A = ar.read_float32();
	}

	// ============================================================
	// TexturePoint serialization
	// ============================================================
	inline void save(jai::serialization::archive_writer& ar, const TexturePoint& tp) {
		ar.write_float32(tp.textureX);
		ar.write_float32(tp.textureY);
	}

	inline void load(jai::serialization::archive_reader& ar, TexturePoint& tp) {
		tp.textureX = ar.read_float32();
		tp.textureY = ar.read_float32();
	}

	// ============================================================
	// BoxAABB<T> serialization
	// ============================================================
	template<typename T>
	void save(jai::serialization::archive_writer& ar, const BoxAABB<T>& box) {
		save(ar, box.minPoint);
		save(ar, box.maxPoint);
	}

	template<typename T>
	void load(jai::serialization::archive_reader& ar, BoxAABB<T>& box) {
		load(ar, box.minPoint);
		load(ar, box.maxPoint);
	}

} // namespace MV

// ============================================================
// Scene namespace types
// ============================================================
namespace MV {
namespace Scene {

	// Forward declarations
	class Node;
	class Drawable;
	class Anchors;

	// Anchors serialization requires friend access - declared here, defined in drawable.cpp
	// These are declared as friends in the Anchors class
	void save(jai::serialization::archive_writer& ar, const Anchors& anchors);
	void load(jai::serialization::archive_reader& ar, Anchors& anchors);

} // namespace Scene
} // namespace MV

// ============================================================
// weak_ptr serialization (in std namespace for ADL)
// Note: weak_ptr serialization stores whether it's valid and
// relies on the object being reconstructed separately.
// For scene graphs, this typically means storing an ID string.
// ============================================================
namespace std {

	// weak_ptr save - stores validity flag only
	// Full object reference reconstruction must be handled by the containing class
	template<typename T>
	void save(jai::serialization::archive_writer& ar, const std::weak_ptr<T>& wp) {
		bool valid = !wp.expired();
		ar.write_bool(valid);
		// Note: The actual object reference must be reconstructed by the parent
		// class using IDs or other means. weak_ptr cannot own the object.
	}

	template<typename T>
	void load(jai::serialization::archive_reader& ar, std::weak_ptr<T>& wp) {
		bool valid = ar.read_bool();
		// weak_ptr will be default (expired) - parent class must reconnect it
		wp.reset();
		(void)valid; // Parent class should use stored ID to reconnect
	}

} // namespace std

#endif // MV_JAI_SERIALIZATION_HPP
