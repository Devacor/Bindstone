#ifndef _MV_TEXTUREPACKER_H_
#define _MV_TEXTUREPACKER_H_

#include <vector>
#include <algorithm>
#include <limits>

#include "textures.h"
#include "points.h"

namespace MV {

	class TexturePack {
	public:
		TexturePack();

		void add(const Size<> &a_shape);

		void print() const;

	private:
		BoxAABB shapeForBestRegion(const Size<> &a_shape);

		std::vector<BoxAABB> regions;
		std::vector<BoxAABB> shapes;
		Size<> extent;
	};

}

#endif