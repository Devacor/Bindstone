#ifndef _MV_TEXTUREPACKER_H_
#define _MV_TEXTUREPACKER_H_

#include <vector>
#include <algorithm>
#include <limits>
#include <string>

#include "boxaabb.h"
#include "points.h"
#include "textures.h"

namespace MV {
	namespace Scene {
		class Node;
	}
	class TexturePack {
	public:
		TexturePack(const Size<int> &a_maximumExtent = Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
		bool add(const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale = 1.0f);

		std::string print() const;

		void addToScene(std::shared_ptr<MV::Scene::Node> scene);
	private:
		void updateContainers(const BoxAABB<int> &a_newShape);
		BoxAABB<int> positionShape(const Size<int> &a_shape);

		std::vector<std::pair<BoxAABB<int>, std::shared_ptr<TextureDefinition>>> shapes;
		std::vector<BoxAABB<int>> containers;

		Size<int> maximumExtent;
		Size<int> contentExtent;
	};

}

#endif