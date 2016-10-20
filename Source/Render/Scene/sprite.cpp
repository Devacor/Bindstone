#include "sprite.h"
#include <numeric>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Sprite);

namespace MV {
	namespace Scene {
		/*Vertex Indices*\
			0 15  14  3
			8  4   7 13
			9  5   6 12
			1 10  11  2
		\*Vertex Indices*/
		void Sprite::updateSlice() {
			if (hasSlice()) {
				if (points.size() != 16) {
					points.resize(16);
					updateSliceColorsFromCorners();
					vertexIndices.clear();
					appendNineSliceVertexIndices(vertexIndices, 0);
					ourTexture->apply(points);
				} else {
					ourTexture->applySlicePosition(points);
				}
			} else {
				clearSlice();
			}
		}

		void Sprite::updateSliceColorsFromCorners() {
			if (hasSlice()) {
				points[4].copyColor(points[0]);
				points[8].copyColor(points[0]);
				points[15].copyColor(points[0]);

				points[5].copyColor(points[1]);
				points[9].copyColor(points[1]);
				points[10].copyColor(points[1]);

				points[6].copyColor(points[2]);
				points[11].copyColor(points[2]);
				points[12].copyColor(points[2]);

				points[7].copyColor(points[3]);
				points[14].copyColor(points[3]);
				points[13].copyColor(points[3]);
			}
		}

		void Sprite::clearSlice() {
			if (points.size() == 16) {
				points.resize(4);
				vertexIndices.clear();
				appendQuadVertexIndices(vertexIndices, 0);
			}
		}

		//no need to iterate over everything for a sprite. Also, update slice.
		void Sprite::refreshBounds() {
			auto originalBounds = localBounds;
			localBounds = BoxAABB<>(points[0], points[2]);
			if (originalBounds != localBounds) {
				for (auto&& childAnchor : childAnchors) {
					childAnchor->apply();
				}
				updateSlice();
				notifyParentOfBoundsChange();
			}
		}

		void Sprite::updateTextureCoordinates() {
			if (ourTexture != nullptr) {
				updateSlice();
				//If we didn't have a slice, then we need to manually apply. Otherwise this was handled already.
				if(!hasSlice()) {
					ourTexture->apply(points);
				}
				notifyParentOfComponentChange();
			} else {
				clearTextureCoordinates();
			}
		}

	}
}
