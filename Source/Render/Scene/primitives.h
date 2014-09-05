/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for   |
| lists of points is start at the top left corner and go   |
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)                                            |
\**********************************************************/

#ifndef _MV_SCENE_PRIMITIVES_H_
#define _MV_SCENE_PRIMITIVES_H_

#include "Render/Scene/node.h"

namespace MV {
	namespace Scene {
		class Pixel : public Node{
			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Pixel)

				static std::shared_ptr<Pixel> make(Draw2D* a_renderer, const DrawPoint &a_point = DrawPoint());
			virtual ~Pixel(){}

			void setPoint(const DrawPoint &a_point);

			template<typename PointAssign>
			void applyToPoint(const PointAssign &a_values);

		protected:
			Pixel(Draw2D *a_renderer):Node(a_renderer){
				points.resize(1);
			}

		private:
			virtual void drawImplementation();

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Pixel> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Pixel node.");
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}
		};

		template<typename PointAssign>
		void Pixel::applyToPoint(const PointAssign &a_values){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);
			points[0] = a_values;
		}

		class Line : public Node{
			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Line)

				static std::shared_ptr<Line> make(Draw2D* a_renderer);
			static std::shared_ptr<Line> make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

			virtual ~Line(){}

			void setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

			template<typename PointAssign>
			void applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint);

		protected:
			Line(Draw2D *a_renderer):
				Node(a_renderer){

				points.resize(2);
			}
		private:
			virtual void drawImplementation();

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Line> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Line node.");
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}
		};

		template<typename PointAssign>
		void Line::applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);
			points[0] = a_startPoint;
			points[1] = a_endPoint;
		}
	}

}

#endif
