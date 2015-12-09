#include "path.h"
#include "cereal/archives/json.hpp"
CEREAL_REGISTER_TYPE(MV::Scene::PathMap);
CEREAL_REGISTER_TYPE(MV::Scene::PathAgent);

namespace MV {
	namespace Scene {

		const std::vector<Color> PathMap::alternatingDebugTiles{ {0.1f, 0.7f, 0.1f, 0.5f}, { 0.1f, 0.1f, 0.7f, 0.5f } };
		const Color PathMap::staticBlockedDebugTile{ 0.7f, 0.1f, 0.1f, 0.5f };

		PathMap::PathMap(const std::weak_ptr<Node> &a_owner, const Size<> &a_size, const Size<int> &a_gridSize, bool a_useCorners /*= true*/) :
			Drawable(a_owner),
			map(Map::make(a_gridSize, a_useCorners)),
			cellDimensions(a_size) {

			shouldDraw = false;
		}

		void PathMap::repositionDebugDrawPoints() {
			std::vector<Point<>> cornerOffsets = { {0, 0}, {0, cellDimensions.height}, {cellDimensions.width, cellDimensions.height}, {cellDimensions.width, 0} };
			int squareIndex = 0;
			for (int x = 0; x < map->size().width; ++x) {
				for (int y = 0; y < map->size().height; ++y) {
					int index = (x * map->size().height) + y;
					for (int i = 0; i < 4; ++i) {
						points[(index * 4) + i] = MV::point(x * cellDimensions.width, y * cellDimensions.height) + cornerOffsets[i] + topLeftOffset;
						points[(index * 4) + i] = (*map)[x][y].staticallyBlocked() ? staticBlockedDebugTile : alternatingDebugTiles[(x + y) % 2];
					}
				}
			}
			refreshBounds();
		}

	}
}