#include "texturePacker.h"

#include "Scene/rectangle.h"

namespace MV{
	bool TexturePack::add(const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale) {
		auto shapeSize = a_shape->size() * Scale(a_scale);
		auto newShape = positionShape(shapeSize);
		if(newShape.minPoint.x >= 0){
			dirty = true;
			shapes.push_back({newShape, a_shape});
			updateContainers(newShape);

			contentExtent = {std::max(newShape.maxPoint.x, contentExtent.width), std::max(newShape.maxPoint.y, contentExtent.height)};
			return true;
		}
		return false;
	}

	BoxAABB<int> TexturePack::positionShape(const Size<int> &a_shape) {
		BoxAABB<int> bestShape;
		bestShape.minPoint.x = -1;

		int bestSpaceLeft = maximumExtent.area();
		auto bestContainer = containers.end();
		bool bestRequiresExpansion = true;

		for(auto container = containers.begin(); container != containers.end(); ++container){
			auto spaceLeft = container->size().area() - a_shape.area();
			auto newExtent = toSize(container->minPoint) + a_shape;
			bool requiresExpansion = !contentExtent.contains(newExtent);
			if(maximumExtent.contains(newExtent) && ((bestRequiresExpansion && (spaceLeft < bestSpaceLeft || !requiresExpansion)) || (!bestRequiresExpansion && !requiresExpansion && spaceLeft < bestSpaceLeft)) && container->size().contains(a_shape)){
				bestShape = {container->minPoint, a_shape};
				bestSpaceLeft = spaceLeft;
				bestContainer = container;
				bestRequiresExpansion = requiresExpansion;
				if(bestSpaceLeft < 15 && !requiresExpansion){
					break;
				}
			}
		}

		return bestShape;
	}

	std::string TexturePack::print() const{
		std::stringstream out;
		out << "<html>\n";
		out << "	<head>\n";
		out << "	</head>\n";
		out << "	<body>\n";
		out << " <b>Shapes: " << shapes.size() << ",  " << contentExtent << "<br/></b>\n\n";
		out << "		<canvas id=\"myCanvas\" width=\"" << contentExtent.width << "\" height=\"" << contentExtent.height << "\" style=\"border:1px solid #d3d3d3; background:#ffffff; \"></canvas>\n";
		out << "		<script>\n";
		out << "			var c = document.getElementById(\"myCanvas\");\n";
		out << "			var ctx = c.getContext(\"2d\");\n\n";
		for(auto&& shape : shapes){
			Color tmp(static_cast<float>(randomNumber(0, 200)), static_cast<float>(randomNumber(0, 200)), static_cast<float>(randomNumber(0, 200)), .25f);
			//Color tmp(0.0f, 0.0f, 0.0f, .25f);
			out << "			ctx.fillStyle=\"rgba" << tmp << "\";\n";
			out << "			ctx.fillRect(" << shape.first.minPoint.x << "," << shape.first.minPoint.y << "," << shape.first.size().width << "," << shape.first.size().height << ");\n";
		}
		out << "		</script>\n";
		out << "	</body>\n";
		out << "</html>" << std::endl;
		return out.str();
	}

	TexturePack::TexturePack(MV::Draw2D* a_renderer, const Color &a_color, const Size<int> &a_maximumExtent):
		renderer(a_renderer),
		maximumExtent(a_maximumExtent),
		packedTexture(DynamicTextureDefinition::make("TexturePack", Size<int>(), a_color)),
		dirty(true){
		containers.emplace_back(a_maximumExtent);
	}

	void TexturePack::updateContainers(const BoxAABB<int> &a_newShape){
		for(size_t i = 0; i < containers.size();){
			if(containers[i].collides(a_newShape)){
				auto newContainers = containers[i].removeFromBounds(a_newShape);
				containers.erase(containers.begin() + i);

				newContainers.erase(std::remove_if(newContainers.begin(), newContainers.end(), [&](BoxAABB<int>& newContainer){
					return std::find_if(containers.begin(), containers.end(), [&](BoxAABB<int>& compare){
						return compare.contains(newContainer);
					}) != containers.end();
				}), newContainers.end());

				moveAppend(containers, newContainers);
			} else{
				++i;
			}
		}
	}

	std::shared_ptr<MV::Scene::Node> TexturePack::makeScene() const {
		auto scene = Scene::Node::make(renderer);
		for(auto&& shape : shapes){
			scene->make<MV::Scene::Rectangle>(cast<PointPrecision>(shape.first))->texture(shape.second->makeHandle())->shader(PREMULTIPLY_ID);
		}
		return scene;
	}

	std::shared_ptr<TextureDefinition> TexturePack::texture() {
		if(dirty){
			packedTexture->resize(contentExtent);
			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, packedTexture->textureId(), background)->start();
			renderer->backgroundColor(background);
			renderer->clearScreen();
			scene->draw();
		}
		return packedTexture;
	}

}