#include "sliced.h"

CEREAL_REGISTER_TYPE(MV::Scene::Sliced);

namespace MV {
	namespace Scene {

		std::shared_ptr<Sliced> Sliced::make(Draw2D* a_renderer) {
			auto sliced = std::shared_ptr<Sliced>(new Sliced(a_renderer));
			sliced->registerShader();
			return sliced;
		}

		std::shared_ptr<Sliced> Sliced::make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const Size<> &a_size, bool a_center) {
			auto sliced = std::shared_ptr<Sliced>(new Sliced(a_renderer));
			sliced->registerShader();
			return sliced->sliceDimensions(a_sliceDimensions)->size(a_size, a_center);
		}

		std::shared_ptr<Sliced> Sliced::make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const Size<> &a_size, const Point<> &a_centerPoint) {
			auto sliced = std::shared_ptr<Sliced>(new Sliced(a_renderer));
			sliced->registerShader();
			return sliced->sliceDimensions(a_sliceDimensions)->size(a_size, a_centerPoint);
		}

		std::shared_ptr<Sliced> Sliced::make(Draw2D* a_renderer, const SliceDimensions &a_sliceDimensions, const BoxAABB<> &a_boxAABB) {
			auto sliced = std::shared_ptr<Sliced>(new Sliced(a_renderer));
			sliced->registerShader();
			return sliced->sliceDimensions(a_sliceDimensions)->bounds(a_boxAABB);
		}

		std::shared_ptr<Sliced> Sliced::size(const Size<> &a_size, const Point<> &a_centerPoint){
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;

			return bounds({topLeft, bottomRight});
		}

		std::shared_ptr<Sliced> Sliced::size(const Size<> &a_size, bool a_center) {
			return size(a_size, (a_center) ? point(a_size.width / 2.0f, a_size.height / 2.0f) : point(0.0f, 0.0f));
		}

		std::shared_ptr<Sliced> MV::Scene::Sliced::bounds(const BoxAABB<> &a_bounds) {
			dimensions = a_bounds;
			auto notifyOnChanged = makeScopedDepthChangeNote(this);

			if(dimensions.size().width < slicedDimensions.leftSlice() + slicedDimensions.rightSlice()){
				dimensions.maxPoint.x = dimensions.minPoint.x + slicedDimensions.leftSlice() + slicedDimensions.rightSlice();
			}
			if(dimensions.size().height < slicedDimensions.topSlice() + slicedDimensions.bottomSlice()){
				dimensions.maxPoint.y = dimensions.minPoint.y + slicedDimensions.topSlice() + slicedDimensions.bottomSlice();
			}

			//Top
			points[0] = Point<>(dimensions.minPoint.x, dimensions.minPoint.y, dimensions.minPoint.z);
			points[1] = Point<>(dimensions.minPoint.x + slicedDimensions.leftSlice(), dimensions.minPoint.y, dimensions.minPoint.z*.75f + dimensions.maxPoint.z*.25f);
			points[2] = Point<>(dimensions.maxPoint.x - slicedDimensions.rightSlice(), dimensions.minPoint.y, dimensions.minPoint.z*.25f + dimensions.maxPoint.z*.75f);
			points[3] = Point<>(dimensions.maxPoint.x, dimensions.minPoint.y, dimensions.maxPoint.z);

			//Top Middle
			points[4] = Point<>(dimensions.minPoint.x, dimensions.minPoint.y + slicedDimensions.topSlice(), dimensions.minPoint.z);
			points[5] = Point<>(dimensions.minPoint.x + slicedDimensions.leftSlice(), dimensions.minPoint.y + slicedDimensions.topSlice(), dimensions.minPoint.z*.75f + dimensions.maxPoint.z*.25f);
			points[6] = Point<>(dimensions.maxPoint.x - slicedDimensions.rightSlice(), dimensions.minPoint.y + slicedDimensions.topSlice(), dimensions.minPoint.z*.25f + dimensions.maxPoint.z*.75f);
			points[7] = Point<>(dimensions.maxPoint.x, dimensions.minPoint.y + slicedDimensions.topSlice(), dimensions.maxPoint.z);

			//Bottom Middle
			points[8] = Point<>(dimensions.minPoint.x, dimensions.maxPoint.y - slicedDimensions.bottomSlice(), dimensions.minPoint.z);
			points[9] = Point<>(dimensions.minPoint.x + slicedDimensions.leftSlice(), dimensions.maxPoint.y - slicedDimensions.bottomSlice(), dimensions.minPoint.z*.75f + dimensions.maxPoint.z*.25f);
			points[10] = Point<>(dimensions.maxPoint.x - slicedDimensions.rightSlice(), dimensions.maxPoint.y - slicedDimensions.bottomSlice(), dimensions.minPoint.z*.25f + dimensions.maxPoint.z*.75f);
			points[11] = Point<>(dimensions.maxPoint.x, dimensions.maxPoint.y - slicedDimensions.bottomSlice(), dimensions.maxPoint.z);

			//Bottom
			points[12] = Point<>(dimensions.minPoint.x, dimensions.maxPoint.y, dimensions.minPoint.z);
			points[13] = Point<>(dimensions.minPoint.x + slicedDimensions.leftSlice(), dimensions.maxPoint.y, dimensions.minPoint.z*.75f + dimensions.maxPoint.z*.25f);
			points[14] = Point<>(dimensions.maxPoint.x - slicedDimensions.rightSlice(), dimensions.maxPoint.y, dimensions.minPoint.z*.25f + dimensions.maxPoint.z*.75f);
			points[15] = Point<>(dimensions.maxPoint.x, dimensions.maxPoint.y, dimensions.maxPoint.z);

			alertParent(VisualChange::make(shared_from_this()));
			return std::static_pointer_cast<Sliced>(shared_from_this());
		}


		std::shared_ptr<Sliced> Sliced::sliceDimensions(const SliceDimensions &a_sliceDimensions) {
			slicedDimensions = a_sliceDimensions;

			Point<PointPrecision> textureOffset;
			Size<PointPrecision> textureScale;
			if(ourTexture){
				auto bounds = ourTexture->percentBounds();
				textureOffset = bounds.minPoint;
				textureScale = bounds.size();
			}

			//Top
			points[0].textureX = 0.0f; points[0].textureY = 0.0f;
			points[1].textureX = slicedDimensions.leftSlicePercent(); points[1].textureY = 0.0f;
			points[2].textureX = slicedDimensions.rightSlicePercent(); points[2].textureY = 0.0f;
			points[3].textureX = 1.0f; points[2].textureY = 0.0f;

			//Top Middle
			points[4].textureX = 0.0f; points[4].textureY = slicedDimensions.topSlicePercent();
			points[5].textureX = slicedDimensions.leftSlicePercent(); points[5].textureY = slicedDimensions.topSlicePercent();
			points[6].textureX = slicedDimensions.rightSlicePercent(); points[6].textureY = slicedDimensions.topSlicePercent();
			points[7].textureX = 1.0f; points[7].textureY = slicedDimensions.topSlicePercent();

			//Bottom Middle
			points[8].textureX = 0.0f; points[8].textureY = slicedDimensions.bottomSlicePercent();
			points[9].textureX = slicedDimensions.leftSlicePercent(); points[9].textureY = slicedDimensions.bottomSlicePercent();
			points[10].textureX = slicedDimensions.rightSlicePercent(); points[10].textureY = slicedDimensions.bottomSlicePercent();
			points[11].textureX = 1.0f; points[11].textureY = slicedDimensions.bottomSlicePercent();

			//Bottom
			points[12].textureX = 0.0f; points[12].textureY = 1.0f;
			points[13].textureX = slicedDimensions.leftSlicePercent(); points[13].textureY = 1.0f;
			points[14].textureX = slicedDimensions.rightSlicePercent(); points[14].textureY = 1.0f;
			points[15].textureX = 1.0f; points[15].textureY = 1.0f;

			for(auto & point : points){
				point.textureX *= static_cast<PointPrecision>(textureScale.width);
				point.textureY *= static_cast<PointPrecision>(textureScale.height);

				point.textureX += static_cast<PointPrecision>(textureOffset.x);
				point.textureY += static_cast<PointPrecision>(textureOffset.y);
			}
			
			bounds(dimensions);
			return std::static_pointer_cast<Sliced>(shared_from_this());
		}

		void Sliced::clearTextureCoordinates(){
			sliceDimensions(slicedDimensions);
			alertParent(VisualChange::make(shared_from_this(), false));
		}

		void Sliced::updateTextureCoordinates(){
			sliceDimensions(slicedDimensions);
		}

		SliceDimensions Sliced::sliceDimensions() const {
			return slicedDimensions;
		}

		MV::BoxAABB<> Sliced::bounds() const {
			return dimensions;
		}

		void Sliced::drawImplementation() {
			defaultDraw(GL_TRIANGLE_STRIP);
		}

	}
}