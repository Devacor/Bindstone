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
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Pixel node."));
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
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Line node."));
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

		class Rectangle : public Node{
			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Rectangle)

			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const BoxAABB &a_boxAABB);

			virtual ~Rectangle(){ }

			std::shared_ptr<Rectangle> size(const Size<> &a_size, bool a_center = false);
			std::shared_ptr<Rectangle> size(const Size<> &a_size, const Point<> &a_centerPoint);

			std::shared_ptr<Rectangle> bounds(const BoxAABB &a_bounds);

			template<typename PointAssign>
			void applyToCorners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

			virtual void clearTextureCoordinates();
			virtual void updateTextureCoordinates();
		protected:
			Rectangle(Draw2D *a_renderer):Node(a_renderer){
				points.resize(4);

				points[0].textureX = 0.0; points[0].textureY = 0.0;
				points[1].textureX = 0.0; points[1].textureY = 1.0;
				points[2].textureX = 1.0; points[2].textureY = 1.0;
				points[3].textureX = 1.0; points[3].textureY = 0.0;
			}

			virtual void drawImplementation();
		private:

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Rectangle> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require(renderer != nullptr, MV::PointerException("Error: Failed to load a renderer for Rectangle node."));
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}
		};

		template<typename PointAssign>
		void Rectangle::applyToCorners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft){
			auto notifyOnChanged = makeScopedDepthChangeNote(this);
			points[0] = a_TopLeft;
			points[1] = a_BottomLeft;
			points[2] = a_BottomRight;
			points[3] = a_TopRight;
		}
	}

}

#endif
