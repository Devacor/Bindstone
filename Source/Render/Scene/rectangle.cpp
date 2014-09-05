#include "rectangle.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Rectangle);

namespace MV {
	namespace Scene {
		/*************************\
		| -------Rectangle------- |
		\*************************/

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->registerShader();
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size, bool a_center) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->registerShader();
			return rectangle->size(a_size, a_center);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->registerShader();
			return rectangle->size(a_size, a_centerPoint);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const BoxAABB<> &a_boxAABB) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->registerShader();
			return rectangle->bounds(a_boxAABB);
		}


		std::shared_ptr<Rectangle> Rectangle::bounds(const BoxAABB<> &a_bounds){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;

			return std::static_pointer_cast<Rectangle>(shared_from_this());
		}

		std::shared_ptr<Rectangle> Rectangle::size(const Size<> &a_size, const Point<> &a_centerPoint){
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;

			return bounds({topLeft, bottomRight});
		}

		std::shared_ptr<Rectangle> Rectangle::size(const Size<> &a_size, bool a_center) {
			return size(a_size, (a_center) ? point(a_size.width / 2.0f, a_size.height / 2.0f) : point(0.0f, 0.0f));
		}

		void Rectangle::clearTextureCoordinates(){
			clearTexturePoints(points);
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Rectangle::updateTextureCoordinates(){
			if(ourTexture != nullptr){
				ourTexture->apply(points);
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
