#include "texturePacker.h"

namespace MV{
	void TexturePack::add(const Size<> &a_shape) {
		auto newShape = shapeForBestRegion(a_shape);
		shapes.push_back(newShape);
		regions.emplace_back(Point<>{newShape.maxPoint.x, newShape.minPoint.y}, Point<>{static_cast<float>(std::numeric_limits<int>::max()), static_cast<float>(std::numeric_limits<int>::max())});
		regions.emplace_back(Point<>{newShape.minPoint.x, newShape.maxPoint.y}, Point<>{static_cast<float>(std::numeric_limits<int>::max()), static_cast<float>(std::numeric_limits<int>::max())});
		regions.erase(std::remove_if(regions.begin(), regions.end(), [&](BoxAABB& a_region){
			return std::find_if(shapes.begin(), shapes.end(), [&](BoxAABB& a_boxShape){return a_boxShape.collides(a_region); }) != shapes.end();
		}), regions.end());
		extent = {std::max(newShape.maxPoint.x, extent.width), std::max(newShape.maxPoint.y, extent.height)};
	}

	MV::BoxAABB TexturePack::shapeForBestRegion(const Size<> &a_shape) {
		BoxAABB foundRegion;
		Size<> foundExtentDifference(static_cast<float>(std::numeric_limits<int>::max()), static_cast<float>(std::numeric_limits<int>::max()));
		for(auto&& region : regions){
			if(region.size().contains(a_shape)){
				auto newExtent = pointToSize(region.minPoint) + a_shape;
				if(extent.contains(newExtent)){
					foundRegion = region;
					break;
				}
				auto extentDifference = newExtent - extent;
				if(foundRegion.empty() || (extentDifference.width * extentDifference.height) < (foundExtentDifference.width * foundExtentDifference.height)){
					foundRegion = region;
					foundExtentDifference = extentDifference;
				}
			}
		}
		return{foundRegion.minPoint, a_shape};
	}

	TexturePack::TexturePack() {
		regions.emplace_back(Size<>{static_cast<float>(std::numeric_limits<int>::max()), static_cast<float>(std::numeric_limits<int>::max())});
	}

	void TexturePack::print() const{
		std::cout << "EXTENT: " << extent << std::endl;
		for(auto&& shape : shapes){
			std::cout << "SHAPE: [" << shape << "]" << std::endl;
		}
		std::cout << "**************" << std::endl;
	}

}