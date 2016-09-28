#include "sprite.h"
#include <numeric>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Sprite);

namespace MV {
	namespace Scene {

		void Sprite::updateSlice() {
			if (hasSlice()) {
				if (points.size() != 16) {
					points.resize(16);
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

		void Sprite::clearSlice() {
			if (points.size() == 16) {
				points.resize(4);
				vertexIndices.clear();
				appendQuadVertexIndices(vertexIndices, 0);
			}
		}

		//no need to iterate over everything for a sprite. Also, update slice.
		void Sprite::refreshBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
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
				if (hasSlice()) {
					updateSlice();
				} else {
					ourTexture->apply(points);
				}
				notifyParentOfComponentChange();
			} else {
				clearTextureCoordinates();
			}
		}

	}
}
