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

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/types/polymorphic.hpp"

namespace MV {
	class SharedTextures {
	public:
		std::shared_ptr<TexturePack> pack(const std::string &a_name, Draw2D* a_renderer = nullptr);
		std::shared_ptr<FileTextureDefinition> file(const std::string &a_filename, bool a_repeat = false);
		std::shared_ptr<DynamicTextureDefinition> dynamic(const std::string &a_identifier, const Size<int> &a_size);
		std::shared_ptr<SurfaceTextureDefinition> surface(const std::string &a_identifier, std::function<SDL_Surface*()> a_surfaceGenerator);

		void files(const std::string &a_rootDirectory, bool a_repeat = false);

		std::vector<std::pair<std::string, bool>> fileIds() const;
		std::vector<std::string> packIds() const;

		void assemblePacks(const std::string &a_rootDirectory, Draw2D* a_renderer);
		std::shared_ptr<TexturePack> assemblePack(const std::string &a_packPath, Draw2D* a_renderer);

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(texturePacks), CEREAL_NVP(fileDefinitions), CEREAL_NVP(dynamicDefinitions), CEREAL_NVP(surfaceDefinitions));
		}

		static std::shared_ptr<TextureHandle> white(){
			if(!defaultTexture){
				defaultTexture = DynamicTextureDefinition::make("defaultTexture", {1, 1}, {1.0f, 1.0f, 1.0f, 1.0f});
				defaultHandle = defaultTexture->makeHandle();
			}
			return defaultHandle;
		}
	private:
		std::map<std::string, std::shared_ptr<TexturePack>> texturePacks;
		std::map<std::string, std::shared_ptr<FileTextureDefinition>> fileDefinitions;
		std::map<std::string, std::shared_ptr<DynamicTextureDefinition>> dynamicDefinitions;
		std::map<std::string, std::shared_ptr<SurfaceTextureDefinition>> surfaceDefinitions;

		static std::shared_ptr<DynamicTextureDefinition> defaultTexture;
		static std::shared_ptr<TextureHandle> defaultHandle;
	};
}

#endif
