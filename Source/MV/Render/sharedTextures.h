#ifndef _MV_SHAREDTEXTURES_H_
#define _MV_SHAREDTEXTURES_H_

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include <boost/filesystem.hpp>

#include "texturePacker.h"

#include "MV/Utility/cerealUtility.h"

namespace MV {
	class SharedTextures {
		friend FileTextureDefinition;
	public:
		inline static const std::vector<std::string> validExtensions { ".jpg", ".png", ".bmp", ".tga", ".gif", ".webp" };
		struct PackItem {
			std::string id;
			std::shared_ptr<FileTextureDefinition> texture;
			BoxAABB<float> sliceBounds;

			bool operator<(const PackItem& a_rhs) const {
				return texture->size().area() > a_rhs.texture->size().area();
			}

			bool operator>(const PackItem& a_rhs) const {
				return texture->size().area() > a_rhs.texture->size().area();
			}
		};

		std::shared_ptr<TexturePack> pack(const std::string &a_name);
		std::shared_ptr<TexturePack> pack(const std::string &a_name, Draw2D* a_renderer);
		std::shared_ptr<FileTextureDefinition> file(const std::string &a_filename, bool a_repeat = false, bool a_pixel = false);
		std::shared_ptr<DynamicTextureDefinition> dynamic(const std::string &a_identifier, const Size<int> &a_size);
		std::shared_ptr<SurfaceTextureDefinition> surface(const std::string &a_identifier, std::function<std::shared_ptr<OwnedSurface>()> a_surfaceGenerator);

		void files(const std::string &a_rootDirectory, bool a_repeat = false, bool a_pixel = false);

		std::vector<std::pair<std::string, bool>> fileIds() const;
		std::vector<std::string> packIds() const;

		void assemblePacks(const std::string &a_rootDirectory, Draw2D* a_renderer);
		std::shared_ptr<TexturePack> assemblePack(const std::string &a_packPath, Draw2D* a_renderer);

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(CEREAL_NVP(texturePacks), CEREAL_NVP(fileDefinitions), CEREAL_NVP(dynamicDefinitions), CEREAL_NVP(surfaceDefinitions));
		}

		static std::shared_ptr<TextureHandle> white(){
			static std::shared_ptr<DynamicTextureDefinition> defaultTexture;
			static std::shared_ptr<TextureHandle> defaultHandle;
			if(!defaultTexture){
				defaultTexture = DynamicTextureDefinition::make("defaultTexture", {1, 1}, {1.0f, 1.0f, 1.0f, 1.0f});
				defaultHandle = defaultTexture->makeHandle();
			}
			return defaultHandle;
		}

		static std::string fileId(const std::string &a_filename, bool a_repeat, bool a_pixel = false) {
			return a_filename + (a_repeat ? "1" : "0") + (a_pixel ? "1" : "");
		}
	private:
		std::vector<std::string> getImagesInFolder(const std::string& a_packPath) const;
		std::vector<SharedTextures::PackItem> getSortedPackItems(std::vector<std::string> imagePaths) const;

		std::map<std::string, std::shared_ptr<TexturePack>> texturePacks;
		std::map<std::string, std::shared_ptr<FileTextureDefinition>> fileDefinitions;
		std::map<std::string, std::shared_ptr<DynamicTextureDefinition>> dynamicDefinitions;
		std::map<std::string, std::shared_ptr<SurfaceTextureDefinition>> surfaceDefinitions;
	};
}

#endif
