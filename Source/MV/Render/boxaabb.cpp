#include "boxaabb.h"
#include <numeric>
#include <algorithm>

#include "render.h"

namespace MV {
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

		return wrap(static_cast<PointPrecision>(-PIE), static_cast<PointPrecision>(PIE), dtheta);
	}

	Point<> PointVolume::getCenter(){
		int totalPoints = (int)points.size();
		Point<> average = std::accumulate(points.begin(), points.end(), Point<>());
		average /= static_cast<PointPrecision>(totalPoints);
		return average;
	}

	bool PointVolume::volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer, int32_t a_cameraId, const TransformMatrix &a_matrix){
		require<PointerException>(a_renderer != nullptr, "PointVolume::volumeCollision was passed a null renderer.");
		Point<> point1 = getCenter();
		Point<> point2 = a_compareVolume.getCenter();

		PointPrecision angle = getAngle(point1, point2);
		angle = angle * static_cast<PointPrecision>(180.0 / PIE);
		angle += 90.0f;

		PointVolume tmpVolume1, tmpVolume2;
		int totalPoints;
		TransformMatrix rotatedMatrix = a_matrix;
		rotatedMatrix.rotateZ(angle);

		totalPoints = (int)points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume1.addPoint(a_renderer->worldFromLocal(points[i], a_cameraId, rotatedMatrix));
		}

		totalPoints = (int)a_compareVolume.points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume2.addPoint(a_renderer->worldFromLocal(a_compareVolume.points[i], a_cameraId, rotatedMatrix));
		}

		BoxAABB<> box1 = tmpVolume1.getAABB();
		BoxAABB<> box2 = tmpVolume2.getAABB();

		return box1.collides(box2);
	}

	BoxAABB<> PointVolume::getAABB(){
		BoxAABB<> result;
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
