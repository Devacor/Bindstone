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
	class SharedTextures;
	namespace Scene {
		class Node;
	}
	class PackedTextureDefinition;
	class TexturePack : public std::enable_shared_from_this<TexturePack> {
		friend jai::access;
		friend class jai::serialization::access;
	public:
		struct ShapeDefinition {
			std::string id;
			BoxAABB<int> bounds;
			BoxAABB<float> slice;
			std::shared_ptr<TextureDefinition> texture;

			template<class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					jai::serialization::make_nvp("id", id),
					jai::serialization::make_nvp("bounds", bounds),
					jai::serialization::make_nvp("slice", slice),
					jai::serialization::make_nvp("texture", texture));
			}
		};


		static std::shared_ptr<TexturePack> make(const std::string &a_id, MV::Draw2D* a_renderer, const Color &a_color, const Size<int> &a_maximumExtent);
		static std::shared_ptr<TexturePack> make(const std::string &a_id, MV::Draw2D* a_renderer, const Color &a_color);
		static std::shared_ptr<TexturePack> make(const std::string &a_id, MV::Draw2D* a_renderer, const Size<int> &a_maximumExtent);
		static std::shared_ptr<TexturePack> make(const std::string &a_id, MV::Draw2D* a_renderer);

		bool add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale = 1.0f);
		bool add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, const MV::BoxAABB<float> &a_slice, PointPrecision a_scale = 1.0f);
		std::string print() const;

		std::shared_ptr<MV::Scene::Node> makeScene() const;

		Size<int> maximumBounds() const{
			return maximumExtent;
		}
		Size<int> contentBounds() const{
			return contentExtent;
		}

		std::shared_ptr<TextureHandle> contentHandle();

		std::shared_ptr<TextureHandle> fullHandle();

		std::shared_ptr<TextureHandle> handle(size_t a_index);
		size_t size() const { return shapes.size(); }
		std::shared_ptr<TextureHandle> handle(const std::string &a_id);
		std::vector<std::string> handleIds() const;

		ShapeDefinition shape(const std::string &a_id) const;
		ShapeDefinition shape(size_t a_index) const;

		BoxAABB<PointPrecision> contentPercent() const {
			return percentBounds(contentExtent);
		}

		BoxAABB<PointPrecision> percentBounds(const BoxAABB<int> &a_shape) const;

		std::shared_ptr<TextureDefinition> texture() {
			return consolidatedTexture;
		}

		// The active RHI device, via the pack's renderer (set at make() and on load). Lets
		// PackedTextureDefinition route its GPU upload through the device like every other texture.
		Render::Device* device() const { return renderer ? renderer->device() : nullptr; }

		void consolidate(const std::string &a_fileName, SharedTextures *a_shared);

		std::string identifier() const{
			return id;
		}

		void identifier(const std::string &a_id) {
			id = a_id;
		}

	private:
		TexturePack(const std::string & a_id, MV::Draw2D* a_renderer, const Color &a_color = Color(0.0f, 0.0f, 0.0f, 0.0f), const Size<int> &a_maximumExtent = Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));

		std::shared_ptr<PackedTextureDefinition> getOrMakeTexture();

		void updateContainers(const BoxAABB<int> &a_newShape);
		BoxAABB<int> positionShape(const Size<int> &a_shape);

		friend PackedTextureDefinition;
		void reloadedPackedTextureChild();

		std::vector<ShapeDefinition> shapes;
		std::vector<BoxAABB<int>> containers;

		Size<int> maximumExtent;
		Size<int> contentExtent;

		std::weak_ptr<PackedTextureDefinition> packedTexture;

		std::shared_ptr<FileTextureDefinition> consolidatedTexture;
		
		Color background;
		std::string id;

		MV::Draw2D* renderer;

		bool dirty;

		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			if(packedTexture.expired()){
				packedTexture.reset();
			}
			archive(
				jai::serialization::make_nvp("id", id),
				jai::serialization::make_nvp("packedTexture", packedTexture),
				jai::serialization::make_nvp("shapes", shapes),
				jai::serialization::make_nvp("maximumExtent", maximumExtent),
				jai::serialization::make_nvp("contentExtent", contentExtent),
				jai::serialization::make_nvp("containers", containers),
				jai::serialization::make_nvp("consolidatedTexture", consolidatedTexture));
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<TexturePack>& construct) {
			auto* services = ar.template get_user_context<MV::Services>();
			auto* renderer = services->template get<MV::Draw2D>();
			std::string id;
			ar.serialize("id", id);
			construct(id, renderer);
			ar.serialize("packedTexture", construct->packedTexture);
			ar.serialize("shapes", construct->shapes);
			ar.serialize("maximumExtent", construct->maximumExtent);
			ar.serialize("contentExtent", construct->contentExtent);
			ar.serialize("containers", construct->containers);
			ar.serialize("consolidatedTexture", construct->consolidatedTexture);
			construct->initializeAfterLoad();
		}

		void initializeAfterLoad();
	};

	class PackedTextureDefinition : public DynamicTextureDefinition {
		friend jai::access;
		friend class jai::serialization::access;
	public:
		static std::shared_ptr<PackedTextureDefinition> make(const std::string &a_name, const std::shared_ptr<TexturePack> &a_texturePack, const Size<int> &a_size, const Color &a_backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f}){
			return std::shared_ptr<PackedTextureDefinition>(new PackedTextureDefinition(a_name, a_texturePack, a_size, a_backgroundColor));
		}

		std::shared_ptr<TexturePack> pack() const{
			return texturePack;
		}
	private:
		PackedTextureDefinition(const std::string &a_name, const std::shared_ptr<TexturePack> &a_texturePack, const Size<int> &a_size, const Color &a_backgroundColor):
			DynamicTextureDefinition(a_name, a_size, a_backgroundColor),
			texturePack(a_texturePack){
		}

		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			archive(jai::serialization::make_nvp("texturePack", texturePack));
			DynamicTextureDefinition::serialize(archive, version);
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<PackedTextureDefinition>& construct) {
			std::shared_ptr<TexturePack> texturePack;
			ar.serialize("texturePack", texturePack);
			Color backgroundColor;
			ar.serialize("backgroundColor", backgroundColor);
			construct("", texturePack, Size<int>(), backgroundColor);
			ar.serialize("name", construct->textureName);
			ar.serialize("size", construct->textureSize);
			ar.serialize("contentSize", construct->desiredSize);
			ar.serialize("scale", construct->logicalScale);
			ar.serialize("handles", construct->handles);
		}

		void reloadImplementation() override {
			// Packed textures are created via make(), not SharedTextures::dynamic(), so inject the
			// device here (the one chokepoint) so the base upload routes through the RHI instead of raw GL.
			if (!renderDevice() && texturePack) { renderDevice(texturePack->device()); }
			DynamicTextureDefinition::reloadImplementation();
			texturePack->reloadedPackedTextureChild();
		}

		std::shared_ptr<TexturePack> texturePack;
	};
}

#endif
