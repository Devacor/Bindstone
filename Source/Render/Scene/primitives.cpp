#include "primitives.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Pixel);
CEREAL_REGISTER_TYPE(MV::Scene::Line);

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
			point->registerShader();
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
			line->registerShader();
			return line;
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint) {
			auto line = std::shared_ptr<Line>(new Line(a_renderer));
			line->registerShader();
			line->setEnds(a_startPoint, a_endPoint);
			return line;
		}

	}
}
