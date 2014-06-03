/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for   |
| lists of points is start at the top left corner and go   |
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)                                            |
\**********************************************************/

#ifndef _MV_BOXAABB_H_
#define _MV_BOXAABB_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <boost/lexical_cast.hpp>

#include "points.h"
#include "Utility/package.h"

namespace MV {
	class BoxAABB{
		friend cereal::access;
	public:
		BoxAABB(){ initialize(Point<>()); }
		BoxAABB(const Point<> &a_startPoint){ initialize(a_startPoint); }
		BoxAABB(const Point<> &a_startPoint, const Point<> &a_endPoint){ initialize(a_startPoint, a_endPoint); }

		void initialize(const Point<> &a_startPoint);
		void initialize(const BoxAABB &a_startBox);
		void initialize(const Point<> &a_startPoint, const Point<> &a_endPoint);

		BoxAABB& expandWith(const Point<> &a_comparePoint);
		BoxAABB& expandWith(const BoxAABB &a_compareBox);

		PointPrecision width() const{
			return (maxPoint - minPoint).x;
		}
		PointPrecision height() const{
			return (maxPoint - minPoint).y;
		}

		bool isEmpty() const{ return minPoint == maxPoint; }
		bool flatWidth() const{ return equals(minPoint.x, maxPoint.x); }
		bool flatHeight() const{ return equals(minPoint.y, maxPoint.y); }

		Size<> size() const{ return sizeFromPoint(maxPoint - minPoint); }

		//includes z
		bool pointContainedZ(const Point<> &a_comparePoint) const;

		//ignores z
		bool pointContained(const Point<> &a_comparePoint) const;

		void sanitize();

		Point<> centerPoint() const { return minPoint + ((minPoint + maxPoint) / 2.0f); }

		Point<> topLeftPoint() const { return minPoint; }
		Point<> topRightPoint() const { return point(maxPoint.x, minPoint.y); }
		Point<> bottomLeftPoint() const { return point(minPoint.x, maxPoint.y); }
		Point<> bottomRightPoint() const { return maxPoint; }

		BoxAABB& operator+=(const Point<> &a_offset){
			minPoint += a_offset;
			maxPoint += a_offset;
			return *this;
		}
		BoxAABB& operator-=(const Point<> &a_offset){
			minPoint -= a_offset;
			maxPoint -= a_offset;
			return *this;
		}

		Point<> minPoint, maxPoint;
	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(minPoint), CEREAL_NVP(maxPoint));
		}
	};

	std::ostream& operator<<(std::ostream& a_os, const BoxAABB& a_point);
	std::istream& operator>>(std::istream& a_is, BoxAABB& a_point);

	class Draw2D;
	class PointVolume{
		friend cereal::access;
	public:
		void addPoint(const Point<> &a_newPoint);

		//ignores z
		bool pointContained(const Point<> &a_comparePoint);

		bool volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer);
		Point<> getCenter();
		BoxAABB getAABB();

		std::vector<Point<>> points;
	private:
		//get angle within proper range between two points (-pi to +pi)
		PointPrecision getAngle(const Point<> &a_p1, const Point<> &a_p2);

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(points));
		}
	};

	typedef Point<> AxisAngles;
	typedef Point<> AxisMagnitude;
}

#endif
