#pragma once
#ifndef MV_JAI_SERIALIZATION_FWD_HPP
#define MV_JAI_SERIALIZATION_FWD_HPP

// Forward declarations for JaiScript serialization support for MV types
// This header declares save/load functions without including heavy dependencies,
// allowing it to be included early in the include chain for ADL detection.

// Forward declare JaiScript archive types
namespace jai {
namespace serialization {
    class archive_writer;
    class archive_reader;
}
}

namespace MV {

	// Forward declarations
	template<typename T> class Point;
	template<typename T> class Size;
	class Scale;
	class Color;
	class TexturePoint;
	template<typename T> class BoxAABB;

	// save/load function declarations for ADL
	template<typename T> void save(jai::serialization::archive_writer& ar, const Point<T>& p);
	template<typename T> void load(jai::serialization::archive_reader& ar, Point<T>& p);

	template<typename T> void save(jai::serialization::archive_writer& ar, const Size<T>& s);
	template<typename T> void load(jai::serialization::archive_reader& ar, Size<T>& s);

	void save(jai::serialization::archive_writer& ar, const Scale& s);
	void load(jai::serialization::archive_reader& ar, Scale& s);

	void save(jai::serialization::archive_writer& ar, const Color& c);
	void load(jai::serialization::archive_reader& ar, Color& c);

	void save(jai::serialization::archive_writer& ar, const TexturePoint& tp);
	void load(jai::serialization::archive_reader& ar, TexturePoint& tp);

	template<typename T> void save(jai::serialization::archive_writer& ar, const BoxAABB<T>& box);
	template<typename T> void load(jai::serialization::archive_reader& ar, BoxAABB<T>& box);

} // namespace MV

namespace MV {
namespace Scene {

	// Forward declarations
	class Node;
	class Drawable;
	class Anchors;

	// Anchors save/load declarations
	void save(jai::serialization::archive_writer& ar, const Anchors& anchors);
	void load(jai::serialization::archive_reader& ar, Anchors& anchors);

} // namespace Scene
} // namespace MV

// weak_ptr save/load declarations
namespace std {
	template<typename T> void save(jai::serialization::archive_writer& ar, const std::weak_ptr<T>& wp);
	template<typename T> void load(jai::serialization::archive_reader& ar, std::weak_ptr<T>& wp);
}

#endif // MV_JAI_SERIALIZATION_FWD_HPP
