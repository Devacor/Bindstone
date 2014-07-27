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
			a_renderer->registerShader(point);
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
			auto line = std::shared_ptr<Line>(new Line(a_renderer));
			a_renderer->registerShader(line);
			return line;
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint) {
			auto line = std::shared_ptr<Line>(new Line(a_renderer));
			a_renderer->registerShader(line);
			line->setEnds(a_startPoint, a_endPoint);
			return line;
		}



		/*************************\
		| -------Rectangle------- |
		\*************************/

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			a_renderer->registerShader(rectangle);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size, bool a_center) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			a_renderer->registerShader(rectangle);
			return rectangle->size(a_size, a_center);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			a_renderer->registerShader(rectangle);
			return rectangle->size(a_size, a_centerPoint);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const BoxAABB &a_boxAABB) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			a_renderer->registerShader(rectangle);
			return rectangle->bounds(a_boxAABB);
		}


		std::shared_ptr<Rectangle> Rectangle::bounds(const BoxAABB &a_bounds){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);
			
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (equals(a_bounds.maxPoint.z, 0.0f) && equals(a_bounds.minPoint.z, 0.0f))? 0.0f :(a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;
			
			return std::static_pointer_cast<Rectangle>(shared_from_this());
		}

		std::shared_ptr<Rectangle> Rectangle::size(const Size<> &a_size, const Point<> &a_centerPoint){
			Point<> topLeft;
			Point<> bottomRight = sizeToPoint(a_size);
			
			topLeft-=a_centerPoint;
			bottomRight-=a_centerPoint;
			
			return bounds({topLeft, bottomRight});
		}
		
		std::shared_ptr<Rectangle> Rectangle::size(const Size<> &a_size, bool a_center) {
			return size(a_size, (a_center)?point(a_size.width/2.0f, a_size.height/2.0f):point(0.0f, 0.0f));
		}

		void Rectangle::clearTextureCoordinates(){
			points[0].textureX = 0.0f; points[0].textureY = 0.0f;
			points[1].textureX = 0.0f; points[1].textureY = 1.0f;
			points[2].textureX = 1.0f; points[2].textureY = 1.0f;
			points[3].textureX = 1.0f; points[3].textureY = 0.0f;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Rectangle::updateTextureCoordinates(){
			if(ourTexture != nullptr){
				points[0].textureX = static_cast<PointPrecision>(ourTexture->percentLeft()); points[0].textureY = static_cast<PointPrecision>(ourTexture->percentTop());
				points[1].textureX = static_cast<PointPrecision>(ourTexture->percentLeft()); points[1].textureY = static_cast<PointPrecision>(ourTexture->percentBottom());
				points[2].textureX = static_cast<PointPrecision>(ourTexture->percentRight()); points[2].textureY = static_cast<PointPrecision>(ourTexture->percentBottom());
				points[3].textureX = static_cast<PointPrecision>(ourTexture->percentRight()); points[3].textureY = static_cast<PointPrecision>(ourTexture->percentTop());
				alertParent(VisualChange::make(shared_from_this(), false));
			} else {
				clearTextureCoordinates();
			}
		}

		void Rectangle::drawImplementation(){
			defaultDraw(GL_TRIANGLES);
		}

	}
}
