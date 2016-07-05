#ifndef _MV_BOXAABB_H_
#define _MV_BOXAABB_H_

#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>

#include "points.h"
#include "matrix.h"
#include "Utility/generalUtility.h"

namespace MV {

	template <typename T = PointPrecision>
	class BoxAABB{
	public:
		BoxAABB(){ initialize(Point<T>()); }
		BoxAABB(const Point<T> &a_startPoint){ initialize(a_startPoint); }
		BoxAABB(const Size<T> &a_startSize){ initialize(a_startSize); }
		BoxAABB(const Point<T> &a_startPoint, const Point<T> &a_endPoint){ initialize(a_startPoint, a_endPoint); }
		BoxAABB(const Point<T> &a_startPoint, const Size<T> &a_size, bool a_center = false){ initialize(a_startPoint, a_size); }

		void initialize(const Point<T> &a_startPoint);
		void initialize(const Size<T> &a_startPoint);
		void initialize(const BoxAABB<T> &a_startBox);
		void initialize(const Point<T> &a_startPoint, const Point<T> &a_endPoint);
		void initialize(const Point<T> &a_startPoint, const Size<T> &a_endPoint, bool a_center = false);

		std::vector<BoxAABB<T>> removeFromBounds(const BoxAABB<T> &a_comparePoint);

		BoxAABB<T>& expandWith(const Point<T> &a_comparePoint);
		BoxAABB<T>& expandWith(const BoxAABB<T> &a_compareBox);

		T width() const{
			return (maxPoint - minPoint).x;
		}
		T height() const{
			return (maxPoint - minPoint).y;
		}

		void clear() { minPoint.clear(); maxPoint.clear(); }
		bool empty() const{ return minPoint == maxPoint; }
		bool flatWidth() const{ return equals(minPoint.x, maxPoint.x); }
		bool flatHeight() const{ return equals(minPoint.y, maxPoint.y); }

		Size<T> size() const{ return toSize(maxPoint - minPoint); }

		Point<PointPrecision> percent(const Point<T> &a_point) const;

		bool contains(const Point<T> &a_comparePoint, bool a_useDepth = false) const;
		bool contains(const BoxAABB<T>& a_other, bool a_useDepth = false) const;

		bool collides(const BoxAABB<T> &a_other, bool a_useDepth = false) const;

		void sanitize();

		Point<T> centerPoint() const { return minPoint + ((minPoint + maxPoint) / 2.0f); }

		Point<T> topLeftPoint() const { return minPoint; }
		Point<T> bottomLeftPoint() const { return point(minPoint.x, maxPoint.y); }
		Point<T> bottomRightPoint() const { return maxPoint; }
		Point<T> topRightPoint() const { return point(maxPoint.x, minPoint.y); }

		Point<T> operator[](size_t a_index) const{
			switch(a_index){
			case 0:
				return topLeftPoint();
			case 1:
				return bottomLeftPoint();
			case 2:
				return bottomRightPoint();
			case 3:
				return topRightPoint();
			}
			return{};
		}

		BoxAABB<T>& operator+=(const Point<T> &a_offset){
			minPoint += a_offset;
			maxPoint += a_offset;
			return *this;
		}
		BoxAABB<T>& operator-=(const Point<T> &a_offset){
			minPoint -= a_offset;
			maxPoint -= a_offset;
			return *this;
		}

		BoxAABB<T>& operator*=(const Scale &a_offset){
			minPoint *= a_offset;
			maxPoint *= a_offset;
			return *this;
		}
		BoxAABB<T>& operator/=(const Scale &a_offset){
			minPoint /= a_offset;
			maxPoint /= a_offset;
			return *this;
		}

		bool operator==(const BoxAABB<T> &a_bounds) const{
			return minPoint == a_bounds.minPoint && maxPoint == a_bounds.maxPoint;
		}

		bool operator!=(const BoxAABB<T> &a_bounds) const {
			return !(*this == a_bounds);
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(minPoint), CEREAL_NVP(maxPoint));
		}

		Point<T> minPoint, maxPoint;

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script, const std::string &a_postfix = "");
	};

	template <typename T>
	Point<PointPrecision> MV::BoxAABB<T>::percent(const Point<T> &a_point) const {
		Point<PointPrecision> percentResult;

		percentResult.x = std::min<PointPrecision>(std::max<PointPrecision>(static_cast<PointPrecision>(a_point.x - minPoint.x), 0.0f), static_cast<PointPrecision>(maxPoint.x - minPoint.x));
		percentResult.y = std::min<PointPrecision>(std::max<PointPrecision>(static_cast<PointPrecision>(a_point.y - minPoint.y), 0.0f), static_cast<PointPrecision>(maxPoint.y - minPoint.y));
		percentResult.z = std::min<PointPrecision>(std::max<PointPrecision>(static_cast<PointPrecision>(a_point.z - minPoint.z), 0.0f), static_cast<PointPrecision>(maxPoint.z - minPoint.z));

		percentResult /= cast<PointPrecision>(toPoint(size()));
		return percentResult;
	}

	template <typename T>
	std::ostream& operator<<(std::ostream& a_os, const BoxAABB<T>& a_box){
		a_os << "[" << a_box.minPoint << " - " << a_box.maxPoint << "]";
		return a_os;
	}

	template <typename T>
	std::istream& operator>>(std::istream& a_is, BoxAABB<T>& a_box){
		a_is >> a_box.minPoint >> a_box.maxPoint;
		return a_is;
	}

	template <typename T>
	BoxAABB<T> operator+(const BoxAABB<T> &a_lhs, const Point<T> &a_rhs){
		auto result = a_lhs;
		return result += a_rhs;
	}

	template <typename T>
	BoxAABB<T> operator-(const BoxAABB<T> &a_lhs, const Point<T> &a_rhs){
		auto result = a_lhs;
		return result -= a_rhs;
	}

	template <typename T>
	BoxAABB<T> operator*(const BoxAABB<T> &a_lhs, const Scale &a_rhs){
		auto result = a_lhs;
		return result *= a_rhs;
	}

	template <typename T>
	BoxAABB<T> operator/(const BoxAABB<T> &a_lhs, const Scale &a_rhs){
		auto result = a_lhs;
		return result /= a_rhs;
	}

	template <typename T>
	void BoxAABB<T>::initialize(const Point<T> &a_startPoint){
		minPoint = a_startPoint; maxPoint = a_startPoint;
	}

	template <typename T>
	void BoxAABB<T>::initialize(const Size<T> &a_size){
		minPoint.clear(); maxPoint = toPoint(a_size);
	}

	template <typename T>
	void BoxAABB<T>::initialize(const Point<T> &a_startPoint, const Point<T> &a_endPoint){
		initialize(a_startPoint);
		expandWith(a_endPoint);
	}

	template <typename T>
	void BoxAABB<T>::initialize(const Point<T> &a_startPoint, const Size<T> &a_size, bool a_center){
		initialize(a_startPoint);
		expandWith(a_startPoint + toPoint(a_size));
		if (a_center) {
			auto offset = toPoint<T>(a_size / static_cast<T>(2));
			minPoint -= offset;
			maxPoint -= offset;
		}
	}

	template <typename T>
	void BoxAABB<T>::initialize(const BoxAABB<T> &a_startBox){
		minPoint = a_startBox.minPoint; maxPoint = a_startBox.maxPoint;
	}

	template <typename T>
	BoxAABB<T>& BoxAABB<T>::expandWith(const Point<T> &a_comparePoint){
		minPoint.x = std::min(a_comparePoint.x, minPoint.x);
		minPoint.y = std::min(a_comparePoint.y, minPoint.y);
		minPoint.z = std::min(a_comparePoint.z, minPoint.z);

		maxPoint.x = std::max(a_comparePoint.x, maxPoint.x);
		maxPoint.y = std::max(a_comparePoint.y, maxPoint.y);
		maxPoint.z = std::max(a_comparePoint.z, maxPoint.z);
		return *this;
	}

	template <typename T>
	BoxAABB<T>& BoxAABB<T>::expandWith(const BoxAABB<T> &a_compareBox){
		expandWith(a_compareBox.minPoint);
		expandWith(a_compareBox.maxPoint);
		return *this;
	}

	template <typename T>
	std::vector<BoxAABB<T>> BoxAABB<T>::removeFromBounds(const BoxAABB<T> &a_compareBox){
		if(!collides(a_compareBox)){
			return{*this};
		} else if(a_compareBox.contains(*this)){
			return{};
		}

		//Slice a side down
		if(a_compareBox.contains(topLeftPoint()) && a_compareBox.contains(bottomLeftPoint())){
			return{{point(a_compareBox.maxPoint.x, minPoint.y), maxPoint}};
		} else if(a_compareBox.contains(topRightPoint()) && a_compareBox.contains(bottomRightPoint())){
			return{{minPoint, point(a_compareBox.minPoint.x, maxPoint.y)}};
		} else if(a_compareBox.contains(topLeftPoint()) && a_compareBox.contains(topRightPoint())){
			return{{point(minPoint.x, a_compareBox.maxPoint.y), maxPoint}};
		} else if(a_compareBox.contains(bottomLeftPoint()) && a_compareBox.contains(bottomRightPoint())){
			return{{minPoint, point(maxPoint.x, a_compareBox.minPoint.y)}};
		}

		//Corners
		if(a_compareBox.contains(topLeftPoint())){
			return {
				{point(a_compareBox.maxPoint.x, minPoint.y), maxPoint},
				{point(minPoint.x, a_compareBox.maxPoint.y), maxPoint},
			};
		} else if(a_compareBox.contains(topRightPoint())){
			return {
				{minPoint, point(a_compareBox.minPoint.x, maxPoint.y)},
				{point(minPoint.x, a_compareBox.maxPoint.y), maxPoint},
			};
		} else if(a_compareBox.contains(bottomRightPoint())){
			return {
				{minPoint, point(maxPoint.x, a_compareBox.minPoint.y)},
				{minPoint, point(a_compareBox.minPoint.x, maxPoint.y)},
			};
		} else if(a_compareBox.contains(bottomLeftPoint())){
			return {
				{minPoint, point(maxPoint.x, a_compareBox.minPoint.y)},
				{point(a_compareBox.maxPoint.x, minPoint.y), maxPoint},
			};
		}

		//Bisect
		if(minPoint.x < a_compareBox.minPoint.x && maxPoint.x > a_compareBox.maxPoint.x && (a_compareBox.minPoint.y <= minPoint.y || a_compareBox.maxPoint.y >= maxPoint.y)){
			std::vector<BoxAABB> regions = {
				{minPoint, point(a_compareBox.minPoint.x, maxPoint.y)},
				{point(a_compareBox.maxPoint.x, minPoint.y), maxPoint}
			};
			if(minPoint.y < a_compareBox.minPoint.y && !(maxPoint.y > a_compareBox.maxPoint.y)){
				regions.emplace_back(minPoint, point(maxPoint.x, a_compareBox.minPoint.y));
			} else if(maxPoint.y > a_compareBox.maxPoint.y){
				regions.emplace_back(point(minPoint.x, a_compareBox.maxPoint.y), maxPoint);
			}
			return regions;
		} else if(minPoint.y < a_compareBox.minPoint.y && maxPoint.y > a_compareBox.maxPoint.y && (a_compareBox.minPoint.x <= minPoint.x || a_compareBox.maxPoint.x >= maxPoint.x)){
			std::vector<BoxAABB> regions = {
				{minPoint, point(maxPoint.x, a_compareBox.minPoint.y)},
				{point(minPoint.x, a_compareBox.maxPoint.y), maxPoint}
			};
			if(minPoint.x < a_compareBox.minPoint.x && !(maxPoint.x > a_compareBox.maxPoint.x)){
				regions.emplace_back(minPoint, point(a_compareBox.minPoint.x, maxPoint.y));
			} else if(maxPoint.x > a_compareBox.maxPoint.x){
				regions.emplace_back(point(a_compareBox.maxPoint.x, minPoint.y), maxPoint);
			}
			return regions;
		}

		if(contains(a_compareBox)){
			return{
				{minPoint, point(a_compareBox.minPoint.x, maxPoint.y)},
				{minPoint, point(maxPoint.x, a_compareBox.minPoint.y)},
				{point(minPoint.x, a_compareBox.maxPoint.y), maxPoint},
				{point(a_compareBox.maxPoint.x, minPoint.y), maxPoint}
			};
		}

		std::cerr << "Unhandled removeFromBounds!" << std::endl;
		return{};
	}

	template <typename T>
	bool BoxAABB<T>::contains(const Point<T> &a_comparePoint, bool a_useDepth) const{
		return (a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y &&
			a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y) &&
			(!a_useDepth ||
				(a_comparePoint.z >= minPoint.z && a_comparePoint.z <= maxPoint.z)
			);
	}

	template <typename T>
	bool BoxAABB<T>::contains(const BoxAABB<T>& a_other, bool a_useDepth) const {
		return (minPoint.x <= a_other.minPoint.x && minPoint.y <= a_other.minPoint.y &&
			maxPoint.x >= a_other.maxPoint.x && maxPoint.y >= a_other.maxPoint.y) &&
			(!a_useDepth ||
				(minPoint.z <= a_other.minPoint.z && maxPoint.z >= a_other.maxPoint.z)
			);
	}

	template <typename T>
	void BoxAABB<T>::sanitize() {
		if(minPoint.x > maxPoint.x){ std::swap(minPoint.x, maxPoint.x); }
		if(minPoint.y > maxPoint.y){ std::swap(minPoint.y, maxPoint.y); }
		if(minPoint.z > maxPoint.z){ std::swap(minPoint.z, maxPoint.z); }
	}

	template <typename T>
	bool BoxAABB<T>::collides(const BoxAABB<T> &a_other, bool a_useDepth) const {
		return  !(maxPoint.x <= a_other.minPoint.x ||
			maxPoint.y <= a_other.minPoint.y ||
			minPoint.x >= a_other.maxPoint.x ||
			minPoint.y >= a_other.maxPoint.y) ||
			(a_useDepth &&
				!(maxPoint.z <= a_other.minPoint.z || minPoint.z >= a_other.maxPoint.z)
			);
	}

	template <class T>
	BoxAABB<T> boxaabb(const Point<T> &a_startPoint){
		return{a_startPoint};
	}

	template <class T>
	BoxAABB<T> boxaabb(const Size<T> &a_startSize){
		return{a_startSize};
	}

	template <class T>
	BoxAABB<T> boxaabb(const Point<T> &a_startPoint, const Point<T> &a_endPoint){
		return{a_startPoint, a_endPoint};
	}

	template <class T>
	BoxAABB<T> boxaabb(const Point<T> &a_startPoint, const Size<T> &a_size){
		return{a_startPoint, a_size};
	}

	template <class Target, class Origin>
	BoxAABB<Target> cast(const BoxAABB<Origin> &a_box){
		return BoxAABB<Target>(cast<Target>(a_box.minPoint), cast<Target>(a_box.maxPoint));
	}

	template <class Target, class Origin>
	BoxAABB<Target> round(const BoxAABB<Origin>& a_box) {
		return{ round<Target>(a_box.minPoint), round<Target>(a_box.maxPoint) };
	}

	class Draw2D;
	class PointVolume{
	public:
		void addPoint(const Point<> &a_newPoint);

		//ignores z
		bool pointContained(const Point<> &a_comparePoint);

		bool volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer, const TransformMatrix &a_matrix);
		Point<> getCenter();
		BoxAABB<> getAABB();

		std::vector<Point<>> points;
	private:
		//get angle within proper range between two points (-pi to +pi)
		PointPrecision getAngle(const Point<> &a_p1, const Point<> &a_p2);
	};

	AxisAngles angle(PointPrecision a_angle);

	template <class T>
	chaiscript::ChaiScript& BoxAABB<T>::hook(chaiscript::ChaiScript &a_script, const std::string &a_postfix) {
		a_script.add(chaiscript::user_type<BoxAABB<T>>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>()>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Size<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &, const Point<T> &)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &, const Size<T> &, bool)>(), "BoxAABB" + a_postfix);
		a_script.add(chaiscript::constructor<BoxAABB<T>(const Point<T> &, const Size<T> &)>(), "BoxAABB" + a_postfix);

		a_script.add(chaiscript::fun(&BoxAABB<T>::removeFromBounds), "removeFromBounds");
		a_script.add(chaiscript::fun(&BoxAABB<T>::width), "width");
		a_script.add(chaiscript::fun(&BoxAABB<T>::height), "height");
		a_script.add(chaiscript::fun(&BoxAABB<T>::clear), "clear");
		a_script.add(chaiscript::fun(&BoxAABB<T>::empty), "empty");
		a_script.add(chaiscript::fun(&BoxAABB<T>::flatWidth), "flatWidth");
		a_script.add(chaiscript::fun(&BoxAABB<T>::flatHeight), "flatHeight");
		a_script.add(chaiscript::fun(&BoxAABB<T>::size), "size");
		a_script.add(chaiscript::fun(&BoxAABB<T>::percent), "percent");
		a_script.add(chaiscript::fun(&BoxAABB<T>::topLeftPoint), "topLeftPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::bottomLeftPoint), "bottomLeftPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::bottomRightPoint), "bottomRightPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator[]), "[]");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator+=), "+=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator-=), "-=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator*=), "*=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator/=), "/=");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator==), "==");
		a_script.add(chaiscript::fun(&BoxAABB<T>::operator!=), "!=");

		a_script.add(chaiscript::fun(&BoxAABB<T>::minPoint), "minPoint");
		a_script.add(chaiscript::fun(&BoxAABB<T>::maxPoint), "maxPoint");

		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Size<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const BoxAABB<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &, const Point<T> &)>(&BoxAABB<T>::initialize)), "initialize");
		a_script.add(chaiscript::fun(static_cast<void(BoxAABB<T>::*)(const Point<T> &, const Size<T> &, bool)>(&BoxAABB<T>::initialize)), "initialize");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Point<T> &)>(MV::operator+<T>)), "+");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Point<T> &)>(MV::operator-<T>)), "-");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Scale &)>(MV::operator*<T>)), "*");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<T>(*)(const BoxAABB<T> &, const Scale &)>(MV::operator/<T>)), "/");

		return a_script;
	}
}

#endif