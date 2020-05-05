#include "MV/Utility/generalUtility.h"
#include "MV/Utility/scopeGuard.hpp"
#include "chaiscript/chaiscript.hpp"
#include "chaiscript/utility/utility.hpp"

#include "MV/Utility/chaiscriptStdLib.h"
#include "chaiscript/chaiscript_stdlib.hpp"

#include "boxaabb.h"
#include "sharedTextures.h"

namespace MV {
	template <class T>
	void hookBoxAABB(chaiscript::ChaiScript &a_script, const std::string &a_postfix) {
		a_script.add(chaiscript::user_type<BoxAABB<T>>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>()>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Size<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Size<T> &, bool)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &, const Point<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &, const Size<T> &)>(), "BoxAABB" + a_postfix);

		a_script.add(chaiscript::fun(&BoxAABB<T>::removeFromBounds), "removeFromBounds");
		a_script.add(chaiscript::fun(&BoxAABB<T>::set), "set");
		a_script.add(chaiscript::fun(&BoxAABB<T>::width), "width");
		a_script.add(chaiscript::fun(&BoxAABB<T>::height), "height");
		a_script.add(chaiscript::fun(&BoxAABB<T>::clear), "clear");
		a_script.add(chaiscript::fun(&BoxAABB<T>::empty), "empty");
		a_script.add(chaiscript::fun(&BoxAABB<T>::flatWidth), "flatWidth");
		a_script.add(chaiscript::fun(&BoxAABB<T>::flatHeight), "flatHeight");
		a_script.add(chaiscript::fun(&BoxAABB<T>::size), "size");
		a_script.add(chaiscript::fun(&BoxAABB<T>::percent), "percent");
		a_script.add(chaiscript::fun(&BoxAABB<T>::centerPoint), "centerPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::topLeftPoint), "topLeftPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::bottomLeftPoint), "bottomLeftPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::bottomRightPoint), "bottomRightPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator[]), "[]");
		a_script.add(chaiscript::fun([](BoxAABB<T> &a_self, const Point<T> &a_amount) {
			return a_self -= a_amount;
		}), "-=");
		a_script.add(chaiscript::fun([](BoxAABB<T> &a_self, const BoxAABB<T> &a_amount) {
			return a_self -= a_amount;
		}), "-=");
		a_script.add(chaiscript::fun([](BoxAABB<T> &a_self, const Point<T> &a_amount) {
			return a_self += a_amount;
		}), "+=");
		a_script.add(chaiscript::fun([](BoxAABB<T> &a_self, const BoxAABB<T> &a_amount) {
			return a_self += a_amount;
		}), "+=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator*=), "*=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator/=), "/=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator==), "==");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator!=), "!=");

		a_script.add(chaiscript::fun(&BoxAABB<T>::minPoint), "minPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::maxPoint), "maxPoint");

		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Size<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Size<T> &, bool)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const BoxAABB<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &, const Point<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &, const Size<T> &)>(&BoxAABB<T>::initialize)), "initialize");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Point<T> &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Point<T> &)>(MV::operator-<T>)), "-");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Scale &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Scale &)>(MV::operator/<T>)), "/");
	}

	void hookTexturePoint(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<TexturePoint>(), "TexturePoint");
		a_script.add(chaiscript::constructor<TexturePoint()>(), "TexturePoint");
		a_script.add(chaiscript::constructor<TexturePoint(PointPrecision, PointPrecision)>(), "TexturePoint");
		a_script.add(chaiscript::fun(&TexturePoint::textureX), "textureX");
		a_script.add(chaiscript::fun(&TexturePoint::textureY), "textureY");
		a_script.add(chaiscript::fun(&TexturePoint::operator==), "==");
		a_script.add(chaiscript::fun(&TexturePoint::operator!=), "!=");
	}

	void hookColor(chaiscript::ChaiScript &a_script) {
		a_script.add(chaiscript::user_type<Color::HSV>(), "HSV");
		a_script.add(chaiscript::constructor<Color::HSV(float, float, float, float)>(), "HSV");
		a_script.add(chaiscript::constructor<Color::HSV(float, float, float)>(), "HSV");
		a_script.add(chaiscript::constructor<Color::HSV(float, float)>(), "HSV");
		a_script.add(chaiscript::constructor<Color::HSV(float)>(), "HSV");
		a_script.add(chaiscript::constructor<Color::HSV()>(), "HSV");
		a_script.add(chaiscript::fun(&Color::HSV::H), "H");
		a_script.add(chaiscript::fun(&Color::HSV::S), "S");
		a_script.add(chaiscript::fun(&Color::HSV::V), "V");
		a_script.add(chaiscript::fun(&Color::HSV::A), "A");
		a_script.add(chaiscript::fun(static_cast<float(Color::HSV::*)() const>(&Color::HSV::percentHue)), "percentHue");
		a_script.add(chaiscript::fun(static_cast<float(Color::HSV::*)(float)>(&Color::HSV::percentHue)), "percentHue");
		a_script.add(chaiscript::fun(static_cast<float(Color::HSV::*)() const>(&Color::HSV::invertedPercentHue)), "invertedPercentHue");
		a_script.add(chaiscript::fun(static_cast<float(Color::HSV::*)(float)>(&Color::HSV::invertedPercentHue)), "invertedPercentHue");

		a_script.add(chaiscript::user_type<Color>(), "Color");
		a_script.add(chaiscript::constructor<Color()>(), "Color");
		a_script.add(chaiscript::constructor<Color(uint32_t, bool)>(), "Color");
		a_script.add(chaiscript::constructor<Color(float, float, float, float)>(), "Color");
		a_script.add(chaiscript::constructor<Color(int, int, int, int)>(), "Color");
		a_script.add(chaiscript::constructor<Color(float, float, float)>(), "Color");
		a_script.add(chaiscript::constructor<Color(int, int, int)>(), "Color");
		a_script.add(chaiscript::constructor<Color(uint32_t)>(), "Color");
		a_script.add(chaiscript::fun(&Color::R), "R");
		a_script.add(chaiscript::fun(&Color::G), "G");
		a_script.add(chaiscript::fun(&Color::B), "B");
		a_script.add(chaiscript::fun(&Color::A), "A");
		a_script.add(chaiscript::fun(&Color::normalize), "normalize");

		a_script.add(chaiscript::fun(&Color::operator==), "==");
		a_script.add(chaiscript::fun(&Color::operator!=), "!=");
			
		a_script.add(chaiscript::fun(&Color::operator+=), "+=");
		a_script.add(chaiscript::fun(&Color::operator-=), "-=");

		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(const Color&)>(&Color::operator*=)), "*=");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(float)>(&Color::operator*=<float>)), "*=");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(double)>(&Color::operator*=<double>)), "*=");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(const Color&)>(&Color::operator/=)), "/=");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(float)>(&Color::operator/=<float>)), "/=");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(double)>(&Color::operator/=<double>)), "/=");

		a_script.add(chaiscript::fun(&Color::getHsv), "getHsv");
		a_script.add(chaiscript::fun(static_cast<Color::HSV (Color::*)() const>(&Color::hsv)), "hsv");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(Color::HSV)>(&Color::hsv)), "hsv");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(Color::HSV)>(&Color::set)), "set");

		a_script.add(chaiscript::fun(static_cast<uint32_t (Color::*)() const>(&Color::hex)), "hex");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(uint32_t, bool)>(&Color::hex)), "hex");
		a_script.add(chaiscript::fun([](Color& a_self, uint32_t a_color) {return a_self.hex(a_color); }), "hex");
		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(uint32_t, bool)>(&Color::set)), "set");

		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(float, float, float, float)>(&Color::set)), "set");
		a_script.add(chaiscript::fun([](Color & a_self, float a_R, float a_G, float a_B) {return a_self.set(a_R, a_G, a_B); }), "set");

		a_script.add(chaiscript::fun(static_cast<Color&(Color::*)(int, int, int, int)>(&Color::set)), "set");
		a_script.add(chaiscript::fun([](Color & a_self, int a_R, int a_G, int a_B) {return a_self.set(a_R, a_G, a_B); }), "set");
	}

	template <class T>
	void hookPoint(chaiscript::ChaiScript &a_script, const std::string &a_postfix) {
		a_script.add(chaiscript::user_type<Point<T>>(), "Point" + a_postfix);
		a_script.add(chaiscript::constructor<Point<T>()>(), "Point" + a_postfix);
		a_script.add(chaiscript::constructor<Point<T>(T, T, T)>(), "Point" + a_postfix);
		a_script.add(chaiscript::constructor<Point<T>(T, T)>(), "Point" + a_postfix);
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T, T)>(&Point<T>::set)), "set");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T)>(&Point<T>::set)), "set");
		a_script.add(chaiscript::fun(&Point<T>::scale), "scale");
		a_script.add(chaiscript::fun(&Point<T>::atOrigin), "atOrigin");
		a_script.add(chaiscript::fun(&Point<T>::normalized), "normalized");
		a_script.add(chaiscript::fun(&Point<T>::magnitude), "magnitude");
		a_script.add(chaiscript::fun(&Point<T>::clear), "clear");
		a_script.add(chaiscript::fun(static_cast<PointPrecision (*)(const Point<T> &, const Point<T> &)>(&MV::angle2D<T>)), "angle2D");
		a_script.add(chaiscript::fun(static_cast<PointPrecision (*)(const Point<T> &, const Point<T> &)>(&MV::distance<T>)), "distance");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T, T)>(&Point<T>::locate)), "locate");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T)>(&Point<T>::locate)), "locate");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T, T)>(&Point<T>::translate)), "translate");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(T, T)>(&Point<T>::translate)), "translate");
		a_script.add(chaiscript::fun(&Point<T>::x), "x");
		a_script.add(chaiscript::fun(&Point<T>::y), "y");
		a_script.add(chaiscript::fun(&Point<T>::z), "z");

		a_script.add(chaiscript::fun(&Point<T>::ignoreZ), "ignoreZ");

		a_script.add(chaiscript::fun(&Point<T>::operator+=), "+=");
		a_script.add(chaiscript::fun(&Point<T>::operator-=), "-=");

		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const Point<T> &)>(&Point<T>::operator*=)), "*=");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const T&)>(&Point<T>::operator*=)), "*=");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const Scale&)>(&Point<T>::operator*=)), "*=");

		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const Point<T> &)>(&Point<T>::operator/=)), "/=");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const T&)>(&Point<T>::operator/=)), "/=");
		a_script.add(chaiscript::fun(static_cast<Point<T>&(Point<T>::*)(const Scale&)>(&Point<T>::operator/=)), "/=");

		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const Point<T> &) const>(&Point<T>::operator==)), "==");
		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const T&) const>(&Point<T>::operator==)), "==");

		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const Point<T> &) const>(&Point<T>::operator!=)), "!=");
		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const T&) const>(&Point<T>::operator!=)), "!=");

		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const Point<T> &) const>(&Point<T>::operator<)), "<");
		a_script.add(chaiscript::fun(static_cast<bool(Point<T>::*)(const Point<T> &) const>(&Point<T>::operator>)), ">");

		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const Point<T> &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const T &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const T &, const Point<T> &)>(MV::operator+<T>)), "+");

		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const Point<T> &)>(MV::operator-<T>)), "-");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const T &)>(MV::operator-<T>)), "-");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const T &, const Point<T> &)>(MV::operator-<T>)), "-");

		a_script.add(chaiscript::fun(static_cast<Point<T> (*)(const Point<T> &, const Point<T> &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<Point<T> (*)(const Point<T> &, const T &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<Point<T> (*)(const Point<T> &, const Scale &)>(MV::operator*<T>)), "*");

		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const Point<T> &)>(MV::operator/<T>)), "/");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const T &)>(MV::operator/<T>)), "/");
		a_script.add(chaiscript::fun(static_cast<Point<T>(*)(const Point<T> &, const Scale &)>(MV::operator/<T>)), "/");

		a_script.add(chaiscript::fun([](const Point<T> &a_point) {return to_string(a_point); }), "to_string");
		a_script.add(chaiscript::fun([](const Point<T> &a_point) {return to_string(a_point); }), "toString");
	}

	template <class T>
	void hookSize(chaiscript::ChaiScript &a_script, const std::string &a_postfix){
		a_script.add(chaiscript::user_type<Size<T>>(), "Size" + a_postfix);
		a_script.add(chaiscript::constructor<Size<T>()>(), "Size" + a_postfix);
		a_script.add(chaiscript::constructor<Size<T>(T, T, T)>(), "Size" + a_postfix);
		a_script.add(chaiscript::constructor<Size<T>(T, T)>(), "Size" + a_postfix);

		a_script.add(chaiscript::fun([](Size<T> & a_self, const Size<T> &a_other, bool a_useDepth) {return a_self.contains(a_other, a_useDepth); }), "contains");
		a_script.add(chaiscript::fun([](Size<T> & a_self, const Size<T> &a_other) {return a_self.contains(a_other, false); }), "contains");

		a_script.add(chaiscript::fun([](Size<T> & a_self, bool a_useDepth) {return a_self.area(a_useDepth); }), "area");
		a_script.add(chaiscript::fun([](Size<T> & a_self) {return a_self.area(false); }), "contains");

		a_script.add(chaiscript::fun(&Size<T>::operator>), ">");
		a_script.add(chaiscript::fun(&Size<T>::operator<), "<");
		a_script.add(chaiscript::fun(&Size<T>::width), "width");
		a_script.add(chaiscript::fun(&Size<T>::height), "height");
		a_script.add(chaiscript::fun(&Size<T>::depth), "depth");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(T, T, T)>(&Size<T>::set)), "set");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(T, T)>(&Size<T>::set)), "set");

		a_script.add(chaiscript::fun(static_cast<bool (*)(const Size<T> &, const Size<T> &)>(&operator==<T>)), "==");
		a_script.add(chaiscript::fun(static_cast<bool(*)(const Size<T> &, const Size<T> &)>(&operator!=<T>)), "!=");

		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Size<T> &)>(&Size<T>::operator+=)), "+=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const T&)>(&Size<T>::operator+=)), "+=");

		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Size<T> &)>(&Size<T>::operator-=)), "-=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const T&)>(&Size<T>::operator-=)), "-=");

		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Size<T> &)>(&Size<T>::operator*=)), "*=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Scale &)>(&Size<T>::operator*=)), "*=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const T&)>(&Size<T>::operator*=)), "*=");

		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Size<T> &)>(&Size<T>::operator/=)), "/=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const Scale &)>(&Size<T>::operator/=)), "/=");
		a_script.add(chaiscript::fun(static_cast<Size<T>&(Size<T>::*)(const T&)>(&Size<T>::operator/=)), "/=");

		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Size<T> &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const T &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const T &, const Size<T> &)>(MV::operator+<T>)), "+");

		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Size<T> &)>(MV::operator-<T>)), "-");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const T &)>(MV::operator-<T>)), "-");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const T &, const Size<T> &)>(MV::operator-<T>)), "-");

		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Size<T> &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const T &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Scale &)>(MV::operator*<T>)), "*");

		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Size<T> &)>(MV::operator/<T>)), "/");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const T &)>(MV::operator/<T>)), "/");
		a_script.add(chaiscript::fun(static_cast<Size<T>(*)(const Size<T> &, const Scale &)>(MV::operator/<T>)), "/");

		a_script.add(chaiscript::fun([](const Size<T> &a_size) {return to_string(a_size); }), "to_string");
		a_script.add(chaiscript::fun([](const Size<T> &a_size) {return to_string(a_size); }), "toString");

		return a_script;
	}

	void hookSharedTextures(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<SharedTextures>(), "SharedTextures");

		a_script.add(chaiscript::fun(&SharedTextures::assemblePacks), "assemblePacks");
		a_script.add(chaiscript::fun(&SharedTextures::assemblePack), "assemblePack");
		a_script.add(chaiscript::fun(&SharedTextures::white), "white");
		a_script.add(chaiscript::fun(&SharedTextures::file), "file");
		a_script.add(chaiscript::fun(&SharedTextures::dynamic), "dynamic");
		a_script.add(chaiscript::fun(&SharedTextures::surface), "surface");
		a_script.add(chaiscript::fun(&SharedTextures::files), "files");
		a_script.add(chaiscript::fun(&SharedTextures::fileId), "fileId");
		a_script.add(chaiscript::fun(&SharedTextures::fileIds), "fileIds");
		a_script.add(chaiscript::fun(&SharedTextures::packIds), "packIds");

		a_script.add(chaiscript::fun([](SharedTextures& a_self, const std::string& a_name, Draw2D* a_renderer) {return a_self.pack(a_name, a_renderer); }), "pack");
		a_script.add(chaiscript::fun([](SharedTextures& a_self, const std::string& a_name) {return a_self.pack(a_name); }), "pack");
	}

	void hookTexturePack(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<TexturePack>(), "TexturePack");

		a_script.add(chaiscript::fun(&TexturePack::print), "print");
		a_script.add(chaiscript::fun(&TexturePack::makeScene), "makeScene");
		a_script.add(chaiscript::fun(&TexturePack::maximumBounds), "maximumBounds");
		a_script.add(chaiscript::fun(&TexturePack::contentBounds), "contentBounds");

		a_script.add(chaiscript::fun(&TexturePack::consolidate), "consolidate");
		a_script.add(chaiscript::fun(&TexturePack::fullHandle), "fullHandle");
		a_script.add(chaiscript::fun(&TexturePack::contentHandle), "contentHandle");
		a_script.add(chaiscript::fun(&TexturePack::texture), "texture");

		a_script.add(chaiscript::fun(&TexturePack::size), "size");
		a_script.add(chaiscript::fun(&TexturePack::handleIds), "handleIds");
		a_script.add(chaiscript::fun([](TexturePack& a_self) {return a_self.identifier(); }), "identifier");

		a_script.add(chaiscript::fun([](TexturePack& a_self, size_t a_index) {return a_self.handle(a_index); }), "handle");
		a_script.add(chaiscript::fun([](TexturePack& a_self, const std::string& a_id) {return a_self.handle(a_id); }), "handle");
	}

	void hookTextureDefinition(chaiscript::ChaiScript& a_script) {
		a_script.add(chaiscript::user_type<TextureDefinition>(), "TextureDefinition");

		a_script.add(chaiscript::fun(&TextureDefinition::textureId), "textureId");
		a_script.add(chaiscript::fun(&TextureDefinition::name), "name");
		a_script.add(chaiscript::fun(&TextureDefinition::loaded), "loaded");
		a_script.add(chaiscript::fun(&TextureDefinition::load), "load");
		a_script.add(chaiscript::fun(&TextureDefinition::reload), "reload");
		a_script.add(chaiscript::fun(&TextureDefinition::save), "save");

		a_script.add(chaiscript::fun(static_cast<Size<int>(TextureDefinition::*)()>(&TextureDefinition::size)), "size");
		a_script.add(chaiscript::fun(static_cast<Size<int>(TextureDefinition::*)()>(&TextureDefinition::contentSize)), "contentSize");

		a_script.add(chaiscript::fun(static_cast<Scale(TextureDefinition::*)() const>(&TextureDefinition::scale)), "scale");
		a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)(const Scale&)>(&TextureDefinition::scale)), "scale");

		a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)()>(&TextureDefinition::unload)), "unload");
		a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)(TextureHandle*)>(&TextureDefinition::unload)), "unload");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)()>(&TextureDefinition::makeHandle)), "makeHandle");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<int>&)>(&TextureDefinition::makeHandle)), "makeHandle");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<PointPrecision>&)>(&TextureDefinition::makeHandle)), "makeHandle");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<PointPrecision>&)>(&TextureDefinition::makeRawHandle)), "makeRawHandle");
	}

	void hookRenderFolder(chaiscript::ChaiScript &a_script){
		hookBoxAABB<MV::PointPrecision>(a_script, "");
		hookBoxAABB<int>(a_script, "i");
		hookTexturePoint(a_script);
		hookPoint<MV::PointPrecision>(a_script, "");
		hookPoint<int>(a_script, "i");
		hookSize<MV::PointPrecision>(a_script, "");
		hookSize<int>(a_script, "i");
		hookSharedTextures(a_script);
		hookTexturePack(a_script);
		hookTextureDefinition(a_script);
	}
}