/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for   |
| lists of points is start at the top left corner and go   |
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)                                            |
\**********************************************************/

#ifndef _MV_SCENE_RECTANGLE_H_
#define _MV_SCENE_RECTANGLE_H_

#include "Render/Scene/node.h"

namespace MV {
	namespace Scene {

#define RECTANGLE_OVERRIDES(T) \
	std::shared_ptr<T> size(const Size<> &a_size, bool a_center = false){ return std::static_pointer_cast<T>(Rectangle::size(a_size, a_center)); }\
	std::shared_ptr<T> size(const Size<> &a_size, const Point<> &a_centerPoint){ return std::static_pointer_cast<T>(Rectangle::size(a_size, a_centerPoint)); }\
	std::shared_ptr<T> bounds(const BoxAABB<> &a_bounds){ return std::static_pointer_cast<T>(Rectangle::bounds(a_bounds)); }


		class Rectangle : public Node{
			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Rectangle)

			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const BoxAABB<> &a_boxAABB);

			virtual ~Rectangle(){ }

			std::shared_ptr<Rectangle> size(const Size<> &a_size, bool a_center = false);
			std::shared_ptr<Rectangle> size(const Size<> &a_size, const Point<> &a_centerPoint);

			std::shared_ptr<Rectangle> bounds(const BoxAABB<> &a_bounds);

			template<typename PointAssign>
			void applyToCorners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

			virtual void clearTextureCoordinates() override;
			virtual void updateTextureCoordinates() override;
		protected:
			Rectangle(Draw2D *a_renderer):Node(a_renderer){
				points.resize(4);

				clearTexturePoints(points);
				appendQuadVertexIndices(vertexIndices, 0);
			}

			virtual void drawImplementation() override;
		private:

			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Rectangle> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Rectangle node.");
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
