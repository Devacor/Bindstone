#include "texturePacker.h"

#include "Scene/sprite.h"

CEREAL_REGISTER_TYPE(MV::PackedTextureDefinition);

namespace MV{
	bool TexturePack::add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale) {
		auto shapeSize = a_shape->contentSize() * Scale(a_scale);
		auto newShape = positionShape(shapeSize);
		if(newShape.minPoint.x >= 0){
			dirty = true;
			shapes.push_back({a_id, newShape, a_shape});
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
			out << "			ctx.fillRect(" << shape.bounds << ");\n";
		}
		out << "		</script>\n";
		out << "	</body>\n";
		out << "</html>" << std::endl;
		return out.str();
	}

	TexturePack::TexturePack(MV::Draw2D* a_renderer, const Color &a_color, const Size<int> &a_maximumExtent):
		renderer(a_renderer),
		maximumExtent(a_maximumExtent),
		background(a_color),
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
		auto scene = Scene::Node::make(*renderer);
		for(auto&& shape : shapes){
			if(shape.texture){
				scene->make()->attach<MV::Scene::Sprite>()->bounds(cast<PointPrecision>(shape.bounds))->texture(shape.texture->makeHandle(shape.texture->contentSize()))->shader(PREMULTIPLY_ID);
			}
		}
		if(consolidatedTexture){
			scene->make()->attach<MV::Scene::Sprite>()->bounds(cast<PointPrecision>(consolidatedTexture->size()))->texture(consolidatedTexture->makeHandle())->shader(PREMULTIPLY_ID);
		}
		return scene;
	}

	std::shared_ptr<TextureHandle> TexturePack::contentHandle() {
		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty || !sharedPackedTexture || !sharedPackedTexture->loaded()){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			auto handle = sharedPackedTexture->makeHandle({contentExtent});

			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, sharedPackedTexture->textureId(), background)->start();
			scene->draw();

			return handle;
		} else{
			return sharedPackedTexture->makeHandle({contentExtent});
		}
	}

	std::shared_ptr<TextureHandle> TexturePack::fullHandle() {
		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty || !sharedPackedTexture || !sharedPackedTexture->loaded()){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			auto handle = sharedPackedTexture->makeHandle();

			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, sharedPackedTexture->textureId(), background)->start();
			scene->draw();

			return handle;
		} else{
			return sharedPackedTexture->makeHandle();
		}
	}


	std::shared_ptr<TextureHandle> TexturePack::handle(size_t a_index) {
		if (a_index > shapes.size()) {
			std::cerr << "Failed to find handle index: " << a_index << std::endl;
			return fullHandle();
		}

		auto& foundShape = shapes[a_index];

		auto sharedPackedTexture = getOrMakeTexture();
		if (dirty || !sharedPackedTexture || !sharedPackedTexture->loaded()) {
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			auto handle = sharedPackedTexture->makeHandle(foundShape.bounds);

			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, sharedPackedTexture->textureId(), background)->start();
			scene->draw();

			return handle;
		} else {
			return sharedPackedTexture->makeHandle(foundShape.bounds);
		}
	}

	std::shared_ptr<TextureHandle> TexturePack::handle(const std::string & a_id){
		auto foundShape = std::find_if(shapes.begin(), shapes.end(), [&](ShapeDefinition& a_shape){
			return a_shape.id == a_id;
		});
		if(foundShape == shapes.end()){
			std::cerr << "Failed to find handle: " << a_id << std::endl;
			return fullHandle();
		}
		
		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty || !sharedPackedTexture || !sharedPackedTexture->loaded()){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			auto handle = sharedPackedTexture->makeHandle(foundShape->bounds);

			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, sharedPackedTexture->textureId(), background)->start();
			scene->draw();

			return handle;
		} else{
			return sharedPackedTexture->makeHandle(foundShape->bounds);
		}
	}

	void TexturePack::consolidate(const std::string & a_fileName){
		fullHandle()->texture()->save(a_fileName);
		consolidatedTexture = FileTextureDefinition::make(a_fileName);
		for(auto&& shape : shapes){
			shape.texture = nullptr;
		}
	}

	std::vector<std::string> TexturePack::handleIds() const {
		std::vector<std::string> keys;
		std::transform(shapes.begin(), shapes.end(), std::back_inserter(keys), [](const ShapeDefinition &shape){
			return shape.id;
		});
		return keys;
	}

	std::shared_ptr<PackedTextureDefinition> TexturePack::getOrMakeTexture() {
		if(packedTexture.expired()){
			auto sharedPackedTexture = PackedTextureDefinition::make("TexturePack", shared_from_this(), maximumExtent, background);
			packedTexture = sharedPackedTexture;
			return sharedPackedTexture;
		} else{
			return packedTexture.lock();
		}
	}

	void TexturePack::reloadedPackedTextureChild() {
		if(!packedTexture.expired() && packedTexture.lock()->loaded()){
			auto scene = makeScene();
			auto framebuffer = renderer->makeFramebuffer({}, contentExtent, packedTexture.lock()->textureId(), background)->start();
			scene->draw();
		}
	}

}