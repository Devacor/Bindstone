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
	class PackedTextureDefinition;
	class TexturePack : public std::enable_shared_from_this<TexturePack> {
		friend cereal::access;
	public:
		static std::shared_ptr<TexturePack> make(MV::Draw2D* a_renderer, const Color &a_color = Color(0.0f, 0.0f, 0.0f, 0.0f), const Size<int> &a_maximumExtent = Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max())){
			return std::shared_ptr<TexturePack>(new TexturePack(a_renderer, a_color, a_maximumExtent));
		}
		bool add(const std::string &a_id, const std::shared_ptr<TextureDefinition> &a_shape, PointPrecision a_scale = 1.0f);
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

		std::shared_ptr<TextureHandle> handle(const std::string &a_id);
		std::vector<std::string> handleIds() const;

		std::shared_ptr<TextureDefinition> texture() {
			return fullHandle()->texture();
		}

		void consolidate(const std::string &a_fileName);
	private:
		TexturePack(MV::Draw2D* a_renderer, const Color &a_color = Color(0.0f, 0.0f, 0.0f, 0.0f), const Size<int> &a_maximumExtent = Size<int>(std::numeric_limits<int>::max(), std::numeric_limits<int>::max()));
		struct ShapeDefinition {
			std::string id;
			BoxAABB<int> bounds;
			std::shared_ptr<TextureDefinition> texture;

			template <class Archive>
			void serialize(Archive & archive){
				archive(CEREAL_NVP(id), CEREAL_NVP(bounds), CEREAL_NVP(texture));
			}
		};

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

		MV::Draw2D* renderer;

		bool dirty;

		template <class Archive>
		void serialize(Archive & archive){
			if(packedTexture.expired()){
				packedTexture.reset();
			}
			archive(CEREAL_NVP(packedTexture), CEREAL_NVP(shapes), CEREAL_NVP(maximumExtent), CEREAL_NVP(contentExtent), CEREAL_NVP(containers), CEREAL_NVP(consolidatedTexture));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TexturePack> &construct){
			Draw2D *renderer = nullptr;
			archive.extract(cereal::make_nvp("renderer", renderer));
			MV::require<PointerException>(renderer != nullptr, "Null renderer in Node::load_and_construct.");
			construct(renderer);
			archive(cereal::make_nvp("packedTexture", construct->packedTexture),
				cereal::make_nvp("shapes", construct->shapes), 
				cereal::make_nvp("maximumExtent", construct->maximumExtent), cereal::make_nvp("contentExtent", construct->contentExtent),
				cereal::make_nvp("containers", construct->containers), cereal::make_nvp("consolidatedTexture", construct->consolidatedTexture));

			if(!construct->packedTexture.expired() && construct->packedTexture.lock()->loaded()){
				auto scene = construct->makeScene();
				auto framebuffer = renderer->makeFramebuffer({}, construct->contentExtent, construct->packedTexture.lock()->textureId(), construct->background)->start();
				scene->draw();
			}
		}
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
		void serialize(Archive & archive){
			archive(CEREAL_NVP(texturePack), cereal::make_nvp("base", cereal::base_class<DynamicTextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<PackedTextureDefinition> &construct){
			std::shared_ptr<TexturePack> texturePack;
			archive(cereal::make_nvp("texturePack", texturePack));
			construct("", texturePack, Size<int>(), Color());
			archive(cereal::make_nvp("base", cereal::base_class<DynamicTextureDefinition>(construct.ptr())));
			texturePack->reloadedPackedTextureChild();
		}

		std::shared_ptr<TexturePack> texturePack;
	};
}

#endif