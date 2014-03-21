#include "primitives.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Pixel);
CEREAL_REGISTER_TYPE(MV::Scene::Line);
CEREAL_REGISTER_TYPE(MV::Scene::Rectangle);

namespace MV {
	namespace Scene {
		/*************************\
		| ---------Pixel--------- |
		\*************************/

		void Pixel::setPoint(const DrawPoint &a_point){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			points[0] = a_point;
		}

		void Pixel::drawImplementation(){
			defaultDraw(GL_POINTS);
		}

		std::shared_ptr<Pixel> Pixel::make(Draw2D* a_renderer, const DrawPoint &a_point /*= DrawPoint()*/) {
			auto point = std::shared_ptr<Pixel>(new Pixel(a_renderer));
			point->setPoint(a_point);
			return point;
		}


		/*************************\
		| ----------Line--------- |
		\*************************/

		void Line::setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			points[0] = a_startPoint;
			points[1] = a_endPoint;
		}

		void Line::drawImplementation(){
			defaultDraw(GL_LINES);
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer) {
			return std::shared_ptr<Line>(new Line(a_renderer));
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint) {
			auto line = std::shared_ptr<Line>(new Line(a_renderer));
			line->setEnds(a_startPoint, a_endPoint);
			return line;
		}



		/*************************\
		| -------Rectangle------- |
		\*************************/

		void Rectangle::setTwoCorners(const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			DrawPoint topLeft = a_topLeft, bottomRight = a_bottomRight;
			topLeft.x = std::min(a_topLeft.x, a_bottomRight.x);
			bottomRight.x = std::max(a_topLeft.x, a_bottomRight.x);
			topLeft.y = std::min(a_topLeft.y, a_bottomRight.y);
			bottomRight.y = std::max(a_topLeft.y, a_bottomRight.y);
			topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

			points[0] = topLeft;
			points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
			points[2] = bottomRight;
			points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
		}

		void Rectangle::setTwoCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			Point<> topLeft = a_topLeft, bottomRight = a_bottomRight;
			topLeft.x = std::min(a_topLeft.x, a_bottomRight.x);
			bottomRight.x = std::max(a_topLeft.x, a_bottomRight.x);
			topLeft.y = std::min(a_topLeft.y, a_bottomRight.y);
			bottomRight.y = std::max(a_topLeft.y, a_bottomRight.y);
			topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

			points[0] = topLeft;
			points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
			points[2] = bottomRight;
			points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
		}

		void Rectangle::setTwoCorners(const BoxAABB &a_bounds){
			setTwoCorners(a_bounds.minPoint, a_bounds.maxPoint);
		}

		void Rectangle::setSizeAndCenterPoint(const Point<> &a_centerPoint, const Size<> &a_size){
			double halfWidth = a_size.width / 2, halfHeight = a_size.height / 2;

			Point<> topLeft(-halfWidth, -halfHeight);
			Point<> bottomRight(halfWidth, halfHeight);

			setTwoCorners(topLeft, bottomRight);
			position(a_centerPoint);
		}

		void Rectangle::setSizeAndCornerPoint(const Point<> &a_topLeft, const Size<> &a_size){
			Point<> bottomRight(pointFromSize(a_size));

			setTwoCorners(Point<>(), bottomRight);
			position(a_topLeft);
		}

		void Rectangle::clearTextureCoordinates(){
			points[0].textureX = 0.0; points[0].textureY = 0.0;
			points[1].textureX = 0.0; points[1].textureY = 1.0;
			points[2].textureX = 1.0; points[2].textureY = 1.0;
			points[3].textureX = 1.0; points[3].textureY = 0.0;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Rectangle::updateTextureCoordinates(){
			if(ourTexture != nullptr){
				points[0].textureX = ourTexture->percentLeft(); points[0].textureY = ourTexture->percentTop();
				points[1].textureX = ourTexture->percentLeft(); points[1].textureY = ourTexture->percentBottom();
				points[2].textureX = ourTexture->percentRight(); points[2].textureY = ourTexture->percentBottom();
				points[3].textureX = ourTexture->percentRight(); points[3].textureY = ourTexture->percentTop();
				alertParent(VisualChange::make(shared_from_this()));
			} else {
				clearTextureCoordinates();
			}
		}

		void Rectangle::drawImplementation(){
			defaultDraw(GL_TRIANGLE_FAN);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer) {
			return std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setTwoCorners(a_topLeft, a_bottomRight);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setTwoCorners(a_topLeft, a_bottomRight);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Point<> &a_point, const Size<> &a_size, bool a_center) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			if(a_center){
				rectangle->setSizeAndCenterPoint(a_point, a_size);
			} else{
				rectangle->setSizeAndCornerPoint(a_point, a_size);
			}
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setSize(a_size);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const BoxAABB &a_boxAABB) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setTwoCorners(a_boxAABB);
			return rectangle;
		}

		void Rectangle::setSize(const Size<> &a_size) {
			points[2] = points[0] + static_cast<DrawPoint>(pointFromSize(a_size));
			points[1].y = points[2].y;
			points[3].x = points[2].x;
			alertParent(VisualChange::make(shared_from_this()));
		}

	}
}
