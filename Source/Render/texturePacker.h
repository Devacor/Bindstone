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
		TexturePack(MV::Draw2D* a_renderer, const Color &a_color = Color(0.0f, 0.0f, 0.0f, 0.0f), const Size<int> &a_maximumExtent = Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
		bool add(const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale = 1.0f);

		std::string print() const;

		std::shared_ptr<MV::Scene::Node> makeScene() const;

		std::shared_ptr<TextureDefinition> texture();
	private:
		void updateContainers(const BoxAABB<int> &a_newShape);
		BoxAABB<int> positionShape(const Size<int> &a_shape);

		std::vector<std::pair<BoxAABB<int>, std::shared_ptr<TextureDefinition>>> shapes;
		std::vector<BoxAABB<int>> containers;

		Size<int> maximumExtent;
		Size<int> contentExtent;
		std::shared_ptr<DynamicTextureDefinition> packedTexture;

		MV::Draw2D* renderer;

		Color background;

		bool dirty;
	};

}

#endif