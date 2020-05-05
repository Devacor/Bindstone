#include "texturePacker.h"

#include "Scene/sprite.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include "sharedTextures.h"

CEREAL_REGISTER_TYPE(MV::PackedTextureDefinition);
CEREAL_CLASS_VERSION(MV::PackedTextureDefinition, 1);
CEREAL_CLASS_VERSION(MV::TexturePack::ShapeDefinition, 1);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenetexturepacker);

namespace MV{

	std::shared_ptr<MV::TexturePack> TexturePack::make(const std::string &a_id, MV::Draw2D* a_renderer, const Color &a_color, const Size<int> &a_maximumExtent) {
		return std::shared_ptr<TexturePack>(new TexturePack(a_id, a_renderer, a_color, a_maximumExtent));
	}

	std::shared_ptr<MV::TexturePack> TexturePack::make(const std::string &a_id, MV::Draw2D* a_renderer, const Size<int> &a_maximumExtent) {
		return std::shared_ptr<TexturePack>(new TexturePack(a_id, a_renderer, Color(0.0f, 0.0f, 0.0f, 0.0f), a_maximumExtent));
	}

	std::shared_ptr<MV::TexturePack> TexturePack::make(const std::string &a_id, MV::Draw2D* a_renderer, const Color &a_color) {
		return std::shared_ptr<TexturePack>(new TexturePack(a_id, a_renderer, a_color, Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max())));
	}

	std::shared_ptr<MV::TexturePack> TexturePack::make(const std::string &a_id, MV::Draw2D* a_renderer) {
		return std::shared_ptr<TexturePack>(new TexturePack(a_id, a_renderer, Color(0.0f, 0.0f, 0.0f, 0.0f), Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max())));
	}

	bool TexturePack::add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale) {
		return add(a_id, a_shape, MV::BoxAABB<float>(), a_scale);
	}

	bool TexturePack::add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, const MV::BoxAABB<float> &a_slice, PointPrecision a_scale) {
		auto shapeSize = a_shape->contentSize() * Scale(a_scale);
		auto newShape = positionShape(shapeSize);
		if(newShape.minPoint.x >= 0){
			dirty = true;
			shapes.push_back({a_id, newShape, a_slice, a_shape});
			updateContainers(newShape);

			contentExtent = {std::max(newShape.maxPoint.x, contentExtent.width), std::max(newShape.maxPoint.y, contentExtent.height)};
			return true;
		}
		return false;
	}

	class ContainerComparer {
	public:
		ContainerComparer(const BoxAABB<int>& a_container, const Size<int> & a_maximumExtent, const Size<int>& a_shape, const MV::Size<int> &a_currentExtent, const MV::Size<int> &a_currentPower2Extent) :
			ourContainer(a_container),
			spaceLeft(a_container.size().area() - a_shape.area()) {
			auto newExtent = toSize(a_container.minPoint) + a_shape;
			isValid = a_maximumExtent.contains(newExtent) && a_container.size().contains(a_shape);

			if (isValid) {
				requiresExpansion = !a_currentExtent.contains(newExtent);
				requiresSquareExpansion = !a_currentPower2Extent.contains(newExtent);

				expandsMaxDimension = requiresExpansion &&
					((a_currentExtent.width < a_currentExtent.height && newExtent.height > a_currentExtent.height) ||
						(a_currentExtent.width > a_currentExtent.height && newExtent.width > a_currentExtent.width));
			}
		}

		bool valid() const {
			return isValid;
		}

		BoxAABB<int> container() const {
			return ourContainer;
		}

		bool operator<(const ContainerComparer& a_rhs) const {
			if (!requiresSquareExpansion && a_rhs.requiresSquareExpansion) {
				return true;
			}
			if (!expandsMaxDimension && a_rhs.expandsMaxDimension) {
				return true;
			}
			if (!requiresExpansion && a_rhs.requiresExpansion) {
				return true;
			}
			if (requiresSquareExpansion && !a_rhs.requiresSquareExpansion) {
				return false;
			}
			if (expandsMaxDimension && !a_rhs.expandsMaxDimension) {
				return false;
			}
			if (requiresExpansion && !a_rhs.requiresExpansion) {
				return false;
			}
			return spaceLeft < a_rhs.spaceLeft;
		}
	private:
		BoxAABB<int> ourContainer;
		int spaceLeft = std::numeric_limits<int>::max();
		bool requiresExpansion = true;
		bool requiresSquareExpansion = true;
		bool expandsMaxDimension = true;
		bool isValid = false;
	};

	BoxAABB<int> TexturePack::positionShape(const Size<int> &a_shape) {
		int squarePower2Size = MV::roundUpPowerOfTwo(std::max(contentExtent.width, contentExtent.height));
		const MV::Size<int> squarePower2ContentExtent{ squarePower2Size, squarePower2Size };

		std::vector<ContainerComparer> comparers;
		for (auto&& container : containers) {
			ContainerComparer comparer(container, maximumExtent, a_shape, contentExtent, squarePower2ContentExtent);
			if (comparer.valid()) {
				MV::insertSorted(comparers, comparer);
			}
		}

		if (comparers.empty()) {
			return { Point<int>(-1, -1, -1), Point<int>(-1, -1, -1) };
		} else {
			return { comparers.front().container().minPoint, a_shape };
		}
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
			Color tmp(static_cast<float>(randomInteger(0, 200)), static_cast<float>(randomInteger(0, 200)), static_cast<float>(randomInteger(0, 200)), .25f);
			//Color tmp(0.0f, 0.0f, 0.0f, .25f);
			out << "			ctx.fillStyle=\"rgba" << tmp << "\";\n";
			out << "			ctx.fillRect(" << shape.bounds << ");\n";
		}
		out << "		</script>\n";
		out << "	</body>\n";
		out << "</html>" << std::endl;
		return out.str();
	}

	TexturePack::TexturePack(const std::string &a_id, MV::Draw2D* a_renderer, const Color &a_color, const Size<int> &a_maximumExtent):
		renderer(a_renderer),
		id(a_id),
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
			} else {
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
		if (!dirty && consolidatedTexture) {
			return consolidatedTexture->makeHandle(percentBounds({contentExtent}))->pack(id)->name("CONTENT_HANDLE");
		}

		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			return sharedPackedTexture->makeHandle(percentBounds({contentExtent}))->pack(id)->name("CONTENT_HANDLE");
		} else{
			return sharedPackedTexture->makeHandle(percentBounds({contentExtent}))->pack(id)->name("CONTENT_HANDLE");
		}
	}

	std::shared_ptr<TextureHandle> TexturePack::fullHandle() {
		if (!dirty && consolidatedTexture) {
			return consolidatedTexture->makeHandle()->pack(id)->name("FULL_HANDLE");
		}

		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			return sharedPackedTexture->makeHandle()->pack(id)->name("FULL_HANDLE");
		} else{
			return sharedPackedTexture->makeHandle()->pack(id)->name("FULL_HANDLE");
		}
	}

	TexturePack::ShapeDefinition TexturePack::shape(const std::string &a_id) const {
		auto foundShape = std::find_if(shapes.cbegin(), shapes.cend(), [&](const ShapeDefinition& a_shape) {
			return a_shape.id == a_id;
		});
		require<ResourceException>(foundShape != shapes.cend(), "No shape found for id: [", a_id, "]");
		return *foundShape;
	}
	TexturePack::ShapeDefinition TexturePack::shape(size_t a_index) const {
		require<ResourceException>(a_index <= shapes.size(), "No shape found for index: [", a_index, "]");
		return shapes[a_index];
	}

	std::shared_ptr<TextureHandle> TexturePack::handle(size_t a_index) {
		if (a_index > shapes.size()) {
			std::cerr << "Failed to find handle index: " << a_index << std::endl;
			return fullHandle();
		}

		auto& foundShape = shapes[a_index];

		if (!dirty && consolidatedTexture) {
			return consolidatedTexture->makeHandle(percentBounds(foundShape.bounds))->slice(foundShape.slice)->pack(id)->name(foundShape.id);
		}

		auto sharedPackedTexture = getOrMakeTexture();
		if (dirty) {
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			return sharedPackedTexture->makeHandle(percentBounds(foundShape.bounds))->slice(foundShape.slice)->pack(id)->name(foundShape.id);
		} else {
			return sharedPackedTexture->makeHandle(percentBounds(foundShape.bounds))->pack(id)->name(foundShape.id);
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
		
		if (!dirty && consolidatedTexture) {
			return consolidatedTexture->makeHandle(percentBounds(foundShape->bounds))->slice(foundShape->slice)->pack(id)->name(a_id);
		}

		auto sharedPackedTexture = getOrMakeTexture();
		if(dirty){
			dirty = false;
			sharedPackedTexture->resize(contentExtent);
			return sharedPackedTexture->makeHandle(percentBounds(foundShape->bounds))->slice(foundShape->slice)->pack(id)->name(a_id);
		} else {
			return sharedPackedTexture->makeHandle(percentBounds(foundShape->bounds))->slice(foundShape->slice)->pack(id)->name(a_id);
		}
	}

	void TexturePack::consolidate(const std::string & a_fileName, SharedTextures* a_shared){
		fullHandle()->texture()->save(a_fileName);
		consolidatedTexture = a_shared->file(a_fileName);
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
		if (auto lockedTexture = packedTexture.lock()) {
			return lockedTexture;
		} else {
			auto sharedPackedTexture = PackedTextureDefinition::make("TexturePack", shared_from_this(), maximumExtent, background);
			packedTexture = sharedPackedTexture;
			return sharedPackedTexture;
		}
	}

	void TexturePack::reloadedPackedTextureChild() {
		if (auto lockedTexture = packedTexture.lock()) {
			if (lockedTexture->loaded()) {
				auto scene = makeScene();
				auto framebuffer = renderer->makeFramebuffer({}, contentExtent, lockedTexture->textureId(), background)->start();
				scene->draw();
			}
		}
	}

	BoxAABB<PointPrecision> TexturePack::percentBounds(const BoxAABB<int> &a_shape) const {
		return cast<PointPrecision>(a_shape) / Scale(static_cast<MV::PointPrecision>(roundUpPowerOfTwo(contentExtent.width)), static_cast<MV::PointPrecision>(roundUpPowerOfTwo(contentExtent.height)));
	}

	void TexturePack::initializeAfterLoad() {
		if (auto lockedTexture = packedTexture.lock()) {
			if (lockedTexture->loaded()) {
				auto scene = makeScene();
				auto framebuffer = renderer->makeFramebuffer({}, contentExtent, lockedTexture->textureId(), background)->start();
				scene->draw();
			}
		}
	}
}
