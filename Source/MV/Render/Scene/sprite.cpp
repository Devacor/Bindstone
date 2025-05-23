#include "sprite.h"
#include <numeric>

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Sprite);
CEREAL_CLASS_VERSION(MV::Scene::Sprite, 2);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenesprite);

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
				if (points->size() != 16) {
					points->resize(16);
					updateSliceColorsFromCorners();
					vertexIndices->clear();
					appendNineSliceVertexIndices(*vertexIndices, 0);
					ourTextures[0]->apply(*points);
				} else {
					ourTextures[0]->applySlicePosition(*points);
				}
			} else {
				clearSlice();
			}
		}

		void Sprite::updateSliceColorsFromCorners() {
			if (hasSlice()) {
				dirtyVertexBuffer = true;
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

		void Sprite::updateSubdivision() {
			if (!hasSlice()) {
				dirtyVertexBuffer = true;
				if (ourSubdivisions == 0) {
					points->resize(4);
					vertexIndices->clear();
					appendQuadVertexIndices(*vertexIndices, 0);
				} else {
					size_t newSize = (2 + ourSubdivisions) * (2 + ourSubdivisions);
					points->resize(newSize + 1);
					vertexIndices->clear();
					auto boundExtent = points[2].point() - points[0].point();
					bool colorsMatch = points[0].color() == points[1].color() && points[0].color() == points[2].color();

					auto minTexturePoint = points[0].texturePoint();
					auto maxTexturePoint = points[2].texturePoint();

					for (size_t x = 0; x <= ourSubdivisions; ++x) {
						for (size_t y = 0; y <= ourSubdivisions; ++y) {
							GLuint topLeft = static_cast<GLuint>((x == 0 && y == 0) ? 0 : x > 0 ? ((2 + ourSubdivisions) * x) + y + 2 : 3 + y);
							GLuint bottomLeft = static_cast<GLuint>((x == 0 && y == ourSubdivisions) ? 1 : x > 0 ? ((2 + ourSubdivisions) * x) + (y + 1) + 2 : 3 + (y + 1));
							GLuint bottomRight = static_cast<GLuint>((x == ourSubdivisions && y == ourSubdivisions) ? 2 : ((2 + ourSubdivisions) * (x + 1)) + (y + 1) + 2 - (x == ourSubdivisions ? 1 : 0));
							GLuint topRight = static_cast<GLuint>((x == ourSubdivisions && y == 0) ? 3 : ((2 + ourSubdivisions) * (x + 1)) + y + 2 - (x == ourSubdivisions ? 1 : 0));

							PointPrecision leftPercent = static_cast<PointPrecision>(x) / static_cast<PointPrecision>(ourSubdivisions + 1);
							PointPrecision topPercent = static_cast<PointPrecision>(y) / static_cast<PointPrecision>(ourSubdivisions + 1);
							PointPrecision rightPercent = static_cast<PointPrecision>(x + 1) / static_cast<PointPrecision>(ourSubdivisions + 1);
							PointPrecision bottomPercent = static_cast<PointPrecision>(y + 1) / static_cast<PointPrecision>(ourSubdivisions + 1);

							if (topLeft != 0) {
								points[topLeft] = (boundExtent * Scale(leftPercent, topPercent)) + points[0];
								if (!colorsMatch) {
									points[topLeft] = mix(mix(points[0].color(), points[1].color(), topPercent), mix(points[2].color(), points[1].color(), topPercent), leftPercent);
								}
							}
							if (bottomLeft != 1) {
								points[bottomLeft] = (boundExtent * Scale(leftPercent, bottomPercent)) + points[0];
								if (!colorsMatch) {
									points[bottomLeft] = mix(mix(points[0].color(), points[1].color(), bottomPercent), mix(points[2].color(), points[1].color(), bottomPercent), leftPercent);
								}
							}
							if (bottomRight != 2) {
								points[bottomRight] = (boundExtent * Scale(rightPercent, bottomPercent)) + points[0];
								if (!colorsMatch) {
									points[bottomRight] = mix(mix(points[0].color(), points[1].color(), bottomPercent), mix(points[2].color(), points[1].color(), bottomPercent), rightPercent);
								}
							}
							if (topRight != 3) {
								points[topRight] = (boundExtent * Scale(rightPercent, topPercent)) + points[0];
								if (!colorsMatch) {
									points[topRight] = mix(mix(points[0].color(), points[1].color(), topPercent), mix(points[2].color(), points[1].color(), topPercent), rightPercent);
								}
							}

							PointPrecision leftTexturePercent = mix(minTexturePoint.textureX, maxTexturePoint.textureX, leftPercent);
							PointPrecision topTexturePercent = mix(minTexturePoint.textureY, maxTexturePoint.textureY, topPercent);
							PointPrecision rightTexturePercent = mix(minTexturePoint.textureX, maxTexturePoint.textureX, rightPercent);
							PointPrecision bottomTexturePercent = mix(minTexturePoint.textureY, maxTexturePoint.textureY, bottomPercent);

							points[topLeft] = TexturePoint(leftTexturePercent, topTexturePercent);
							points[bottomLeft] = TexturePoint(leftTexturePercent, bottomTexturePercent);
							points[bottomRight] = TexturePoint(rightTexturePercent, bottomTexturePercent);
							points[topRight] = TexturePoint(rightTexturePercent, topTexturePercent);

							auto toInsert = std::vector<GLuint>{ topLeft, bottomLeft, bottomRight, bottomRight, topRight, topLeft };
							vertexIndices->insert(vertexIndices.end(), toInsert.begin(), toInsert.end());
						}
					}
				}
			}
		}

		void Sprite::updateSubdivisionTexture() {
			if (!hasSlice() && ourSubdivisions > 0) {
				dirtyVertexBuffer = true;
				auto minTexturePoint = points[0].texturePoint();
				auto maxTexturePoint = points[2].texturePoint();

				for (size_t x = 0; x <= ourSubdivisions; ++x) {
					for (size_t y = 0; y <= ourSubdivisions; ++y) {
						GLuint topLeft = static_cast<GLuint>((x == 0 && y == 0) ? 0 : x > 0 ? ((2 + ourSubdivisions) * x) + y + 2 : 3 + y);
						GLuint bottomLeft = static_cast<GLuint>((x == 0 && y == ourSubdivisions) ? 1 : x > 0 ? ((2 + ourSubdivisions) * x) + (y + 1) + 2 : 3 + (y + 1));
						GLuint bottomRight = static_cast<GLuint>((x == ourSubdivisions && y == ourSubdivisions) ? 2 : ((2 + ourSubdivisions) * (x + 1)) + (y + 1) + 2 - (x == ourSubdivisions ? 1 : 0));
						GLuint topRight = static_cast<GLuint>((x == ourSubdivisions && y == 0) ? 3 : ((2 + ourSubdivisions) * (x + 1)) + y + 2 - (x == ourSubdivisions ? 1 : 0));

						PointPrecision leftPercent = static_cast<PointPrecision>(x) / static_cast<PointPrecision>(ourSubdivisions + 1);
						PointPrecision topPercent = static_cast<PointPrecision>(y) / static_cast<PointPrecision>(ourSubdivisions + 1);
						PointPrecision rightPercent = static_cast<PointPrecision>(x + 1) / static_cast<PointPrecision>(ourSubdivisions + 1);
						PointPrecision bottomPercent = static_cast<PointPrecision>(y + 1) / static_cast<PointPrecision>(ourSubdivisions + 1);

						PointPrecision leftTexturePercent = mix(minTexturePoint.textureX, maxTexturePoint.textureX, leftPercent);
						PointPrecision topTexturePercent = mix(minTexturePoint.textureY, maxTexturePoint.textureY, topPercent);
						PointPrecision rightTexturePercent = mix(minTexturePoint.textureX, maxTexturePoint.textureX, rightPercent);
						PointPrecision bottomTexturePercent = mix(minTexturePoint.textureY, maxTexturePoint.textureY, bottomPercent);

						points[topLeft] = TexturePoint(leftTexturePercent, topTexturePercent);
						points[bottomLeft] = TexturePoint(leftTexturePercent, bottomTexturePercent);
						points[bottomRight] = TexturePoint(rightTexturePercent, bottomTexturePercent);
						points[topRight] = TexturePoint(rightTexturePercent, topTexturePercent);
					}
				}
			}
		}

		void Sprite::clearSlice() {
			if (points->size() == 16 && hasSlice()) {
				updateSubdivision();
			}
		}

		//no need to iterate over everything for a sprite. Also, update slice.
		void Sprite::refreshBounds() {
			dirtyVertexBuffer = true;
			auto originalBounds = *localBounds;
			localBounds = BoxAABB<>(points[0], points[2]);
			if (originalBounds != localBounds) {
				for (auto&& childAnchor : childAnchors) {
					childAnchor->apply();
				}
				updateSubdivision();
				updateSlice();
				notifyParentOfBoundsChange();
			}
		}

		void Sprite::updateTextureCoordinates(size_t a_textureId) {
			if (a_textureId == 0) {
				dirtyVertexBuffer = true;
				updateSlice();
				//If we didn't have a slice, then we need to manually apply. Otherwise this was handled already.
				if (!hasSlice()) {
					ourTextures[a_textureId]->apply(*points);
					updateSubdivisionTexture();
				}
				notifyParentOfComponentChange();
			}
		}

	}
}
