#include "boxaabb.h"
#include <numeric>
#include <algorithm>

#include "render.h"

namespace MV {
	/*************************\
	| ------BoundingBox------ |
	\*************************/

	void BoxAABB::initialize(const Point<> &a_startPoint){
		minPoint = a_startPoint; maxPoint = a_startPoint;
	}

	void BoxAABB::initialize(const Point<> &a_startPoint, const Point<> &a_endPoint){
		initialize(a_startPoint);
		expandWith(a_endPoint);
	}

	void BoxAABB::initialize(const BoxAABB &a_startBox){
		minPoint = a_startBox.minPoint; maxPoint = a_startBox.maxPoint;
	}

	BoxAABB& BoxAABB::expandWith(const Point<> &a_comparePoint){
		minPoint.x = std::min(a_comparePoint.x, minPoint.x);
		minPoint.y = std::min(a_comparePoint.y, minPoint.y);
		minPoint.z = std::min(a_comparePoint.z, minPoint.z);

		maxPoint.x = std::max(a_comparePoint.x, maxPoint.x);
		maxPoint.y = std::max(a_comparePoint.y, maxPoint.y);
		maxPoint.z = std::max(a_comparePoint.z, maxPoint.z);
		return *this;
	}

	BoxAABB& BoxAABB::expandWith(const BoxAABB &a_compareBox){
		expandWith(a_compareBox.minPoint);
		expandWith(a_compareBox.maxPoint);
		return *this;
	}

	bool BoxAABB::pointContainedZ(const Point<> &a_comparePoint) const{
		if(a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y && a_comparePoint.z >= minPoint.z){
			if(a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y && a_comparePoint.z <= maxPoint.z){
				return true;
			}
		}
		return false;
	}

	bool BoxAABB::pointContained(const Point<> &a_comparePoint) const{
		if((a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y) && (a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y)){
			return true;
		}
		return false;
	}

	void BoxAABB::sanitize() {
		if(minPoint.x > maxPoint.x){ std::swap(minPoint.x, maxPoint.x); }
		if(minPoint.y > maxPoint.y){ std::swap(minPoint.y, maxPoint.y); }
		if(minPoint.z > maxPoint.z){ std::swap(minPoint.z, maxPoint.z); }
	}

	std::ostream& operator<<(std::ostream& a_os, const BoxAABB& a_box){
		a_os << "[" << a_box.minPoint << " - " << a_box.maxPoint << "]";
		return a_os;
	}

	std::istream& operator>>(std::istream& a_is, BoxAABB& a_box){
		a_is >> a_box.minPoint >> a_box.maxPoint;
		return a_is;
	}

	/*************************\
	| ------PointVolume------ |
	\*************************/

	bool PointVolume::pointContained(const Point<> &a_comparePoint){
		int i;
		PointPrecision angle = 0;
		Point<> p1, p2;
		int totalPoints = (int)points.size();
		for(i = 0; i<totalPoints; i++) {
			p1 = points[i] - a_comparePoint;
			p2 = points[(i + 1) % totalPoints] - a_comparePoint;
			angle += getAngle(p1, p2);
		}
		if(angle < 0){ angle *= -1; }
		if(angle < PIE){
			return false;
		}
		return true;
	}

	void PointVolume::addPoint(const Point<> &a_newPoint){
		points.push_back(a_newPoint);
	}

	PointPrecision PointVolume::getAngle(const Point<> &a_p1, const Point<> &a_p2){
		PointPrecision theta1 = atan2(a_p1.y, a_p1.x);
		PointPrecision theta2 = atan2(a_p2.y, a_p2.x);
		PointPrecision dtheta = theta2 - theta1;

		while(dtheta > static_cast<PointPrecision>(PIE)){
			dtheta -= static_cast<PointPrecision>(PIE*2.0);
		}
		while(dtheta < -PIE){
			dtheta += static_cast<PointPrecision>(PIE*2.0);
		}

		return(dtheta);
	}

	Point<> PointVolume::getCenter(){
		int totalPoints = (int)points.size();
		Point<> average = std::accumulate(points.begin(), points.end(), Point<>());
		average /= static_cast<PointPrecision>(totalPoints);
		return average;
	}

	bool PointVolume::volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer){
		require<PointerException>(a_renderer != nullptr, "PointVolume::volumeCollision was passed a null renderer.");
		Point<> point1 = getCenter();
		Point<> point2 = a_compareVolume.getCenter();

		PointPrecision angle = getAngle(point1, point2);
		angle = angle * static_cast<PointPrecision>(180.0 / PIE);
		angle += 90.0f;

		PointVolume tmpVolume1, tmpVolume2;
		int totalPoints;
		a_renderer->modelviewMatrix().push().makeIdentity().rotateZ(angle);

		totalPoints = (int)points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume1.addPoint(a_renderer->worldFromLocal(points[i]));
		}

		totalPoints = (int)a_compareVolume.points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume2.addPoint(a_renderer->worldFromLocal(a_compareVolume.points[i]));
		}

		a_renderer->modelviewMatrix().pop();

		BoxAABB box1 = tmpVolume1.getAABB();
		BoxAABB box2 = tmpVolume2.getAABB();

		if((box2.minPoint.x > box1.minPoint.x && box2.maxPoint.x > box1.maxPoint.x) ||
			(box2.minPoint.x < box1.minPoint.x && box2.maxPoint.x < box1.maxPoint.x)) {
			return false;
		}
		return true;
	}

	BoxAABB PointVolume::getAABB(){
		BoxAABB result;
		int totalPoints = (int)points.size();
		if(totalPoints > 0){
			result.initialize(points[0]);
			for(int i = 1; i < totalPoints; i++){
				result.expandWith(points[i]);
			}
		}
		return result;
	}

	MV::AxisAngles angle(PointPrecision a_angle) {
		return{0.0f, 0.0f, a_angle};
	}

}
