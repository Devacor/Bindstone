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
		friend cereal::access;
	public:
		struct ShapeDefinition {
			std::string id;
			BoxAABB<int> bounds;
			BoxAABB<float> slice;
			std::shared_ptr<TextureDefinition> texture;

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const a_version) {
				archive(CEREAL_NVP(id), CEREAL_NVP(bounds));
				if (a_version > 0) {
					archive(CEREAL_NVP(slice));
				}
				archive(CEREAL_NVP(texture));
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

		void consolidate(const std::string &a_fileName, SharedTextures *a_shared);

        static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script);

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

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if(packedTexture.expired()){
				packedTexture.reset();
			}
			if (version > 0) {
				archive(CEREAL_NVP(id));
			}
			archive(CEREAL_NVP(packedTexture), CEREAL_NVP(shapes), CEREAL_NVP(maximumExtent), CEREAL_NVP(contentExtent), CEREAL_NVP(containers), CEREAL_NVP(consolidatedTexture));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TexturePack> &construct, std::uint32_t const version){
			auto& services = cereal::get_user_data<MV::Services>(archive);
			auto* renderer = services.get<MV::Draw2D>();

			std::string id;
			if (version > 0) {
				archive(cereal::make_nvp("id", id));
			}
			construct(id, renderer);
			archive(cereal::make_nvp("packedTexture", construct->packedTexture),
				cereal::make_nvp("shapes", construct->shapes), 
				cereal::make_nvp("maximumExtent", construct->maximumExtent), cereal::make_nvp("contentExtent", construct->contentExtent),
				cereal::make_nvp("containers", construct->containers), cereal::make_nvp("consolidatedTexture", construct->consolidatedTexture));

			construct->initializeAfterLoad();
		}

		void initializeAfterLoad();
	};

	class PackedTextureDefinition : public DynamicTextureDefinition {
		friend cereal::access;
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

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(CEREAL_NVP(texturePack), cereal::make_nvp("base", cereal::base_class<DynamicTextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<PackedTextureDefinition> &construct, std::uint32_t const /*version*/){
			std::shared_ptr<TexturePack> texturePack;
			archive(cereal::make_nvp("texturePack", texturePack));
			construct("", texturePack, Size<int>(), Color());
			archive(cereal::make_nvp("base", cereal::base_class<DynamicTextureDefinition>(construct.ptr())));
		}

		void reloadImplementation() override {
			DynamicTextureDefinition::reloadImplementation();
			texturePack->reloadedPackedTextureChild();
		}

		std::shared_ptr<TexturePack> texturePack;
	};
}

CEREAL_CLASS_VERSION(MV::TexturePack::ShapeDefinition, 1);

#endif
