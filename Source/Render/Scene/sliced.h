#ifndef _MV_SCENE_SLICED_H_
#define _MV_SCENE_SLICED_H_

#include "Render/Scene/rectangle.h"

namespace MV {
	namespace Scene {
		//☐==☐
		//||X||
		//☐==☐

		class SliceDimensions {
		public:
			SliceDimensions(const Size<> &a_sliceSize, const Size<> a_targetSize):
				slices(toPoint(a_sliceSize), toPoint(a_sliceSize)),
				targetSize(a_targetSize){
			}
			SliceDimensions(const BoxAABB<> &a_slices, const Size<> &a_targetSize):
				slices(a_slices),
				targetSize(a_targetSize){
			}
			SliceDimensions():
				slices(){
			}

			Size<> sliceSize() const{
				return slices.size();
			}

			Point<> topLeftSlice() const{
				return slices.minPoint;
			}

			Point<> bottomRightSlice() const{
				return slices.maxPoint;
			}

			PointPrecision leftSlice() const{
				return slices.minPoint.x;
			}
			PointPrecision rightSlice() const{
				return slices.maxPoint.x;
			}
			PointPrecision topSlice() const{
				return slices.minPoint.y;
			}
			PointPrecision bottomSlice() const{
				return slices.maxPoint.y;
			}

			Size<> target() const{
				return targetSize;
			}

			PointPrecision leftSlicePercent() const{
				return slices.minPoint.x / targetSize.width;
			}

			PointPrecision rightSlicePercent() const{
				return 1.0f - slices.maxPoint.x / targetSize.width;
			}

			PointPrecision topSlicePercent() const{
				return slices.minPoint.y / targetSize.height;
			}

			PointPrecision bottomSlicePercent() const{
				return 1.0f - slices.minPoint.y / targetSize.height;
			}
		private:

			BoxAABB<> slices;
			Size<> targetSize;
		};

		class Sliced :
			public Node{

			friend cereal::access;
			friend Node;
		public:
			SCENE_MAKE_FACTORY_METHODS(Sliced)

			static std::shared_ptr<Sliced> make(Draw2D* a_renderer);
			static std::shared_ptr<Sliced> make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const Size<> &a_size, bool a_center = false);
			static std::shared_ptr<Sliced> make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const Size<> &a_size, const Point<>& a_centerPoint);
			static std::shared_ptr<Sliced> make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const BoxAABB<> &a_boxAABB);

			std::shared_ptr<Sliced> sliceDimensions(const SliceDimensions &a_sliceDimensions);
			SliceDimensions sliceDimensions() const;

			std::shared_ptr<Sliced> size(const Size<> &a_size, bool a_center = false);
			std::shared_ptr<Sliced> size(const Size<> &a_size, const Point<> &a_centerPoint);

			BoxAABB<> bounds() const;
			std::shared_ptr<Sliced> bounds(const BoxAABB<> &a_bounds);

			virtual void clearTextureCoordinates();
			virtual void updateTextureCoordinates();
		protected:
			Sliced(Draw2D *a_renderer):
				Node(a_renderer){
				points.resize(16);
				vertexIndices = {0, 4, 1, 5, 2, 6, 3, 7, 7, 11, 6, 10, 5, 9, 4, 8, 8, 12, 9, 13, 10, 14, 11, 15};
			}
			virtual void drawImplementation();
		private:
			template <class Archive>
			void serialize(Archive & archive){
				archive(cereal::make_nvp("node", cereal::base_class<Node>(this)));
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Sliced> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				require<PointerException>(renderer != nullptr, "Error: Failed to load a renderer for Sliced node.");
				construct(renderer);
				archive(cereal::make_nvp("node", cereal::base_class<Node>(construct.ptr())));
			}
			BoxAABB<> dimensions;
			SliceDimensions slicedDimensions;
		};
	}
}

#endif