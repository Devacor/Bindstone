#include <algorithm>
#include <boost/algorithm/string.hpp>

#include "sharedTextures.h"

using namespace boost::filesystem;

namespace MV {
	/**********************\
	| ---SharedTextures--- |
	\**********************/

	std::shared_ptr<TexturePack> SharedTextures::pack(const std::string &a_name, Draw2D* a_renderer){
		std::string identifier = a_name;
		auto foundDefinition = texturePacks.find(identifier);
		if(foundDefinition == texturePacks.end()){
			require<PointerException>(a_renderer, "No renderer supplied in creation of pack: ", a_name);
			auto newDefinition = TexturePack::make(a_renderer);
			texturePacks[identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	std::shared_ptr<FileTextureDefinition> SharedTextures::file(const std::string &a_filename, bool a_repeat) {
		std::string identifier = a_filename + (a_repeat ? "1" : "0");
		auto foundDefinition = fileDefinitions.find(identifier);
		if(foundDefinition == fileDefinitions.end()){
			std::shared_ptr<FileTextureDefinition> newDefinition = FileTextureDefinition::make(a_filename, true, a_repeat);
			fileDefinitions[identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	std::shared_ptr<DynamicTextureDefinition> SharedTextures::dynamic(const std::string &a_name, const Size<int> &a_size) {
		std::stringstream identifierMaker;
		identifierMaker << a_name << a_size;
		std::string identifier = identifierMaker.str();

		auto foundDefinition = dynamicDefinitions.find(identifier);
		if(foundDefinition == dynamicDefinitions.end()){
			std::shared_ptr<DynamicTextureDefinition> newDefinition = DynamicTextureDefinition::make(a_name, a_size, {0.0f, 0.0f, 0.0f, 0.0f});
			dynamicDefinitions[identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	std::shared_ptr<SurfaceTextureDefinition> SharedTextures::surface(const std::string &a_identifier, std::function<SDL_Surface*()> a_surfaceGenerator) {
		auto foundDefinition = surfaceDefinitions.find(a_identifier);
		if(foundDefinition == surfaceDefinitions.end()){
			std::shared_ptr<SurfaceTextureDefinition> newDefinition = SurfaceTextureDefinition::make(a_identifier, a_surfaceGenerator);
			surfaceDefinitions[a_identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	std::vector<std::string> SharedTextures::packIds() const {
		std::vector<std::string> keys;
		std::transform(texturePacks.begin(), texturePacks.end(), std::back_inserter(keys), [](const std::map<std::string, std::shared_ptr<TexturePack>>::value_type &pair){
			return pair.first;
		});
		return keys;
	}

	void SharedTextures::loadPacks(const std::string &a_rootDirectory, Draw2D* a_renderer) {
		path directory(a_rootDirectory);
		if(exists(directory)){
			for(auto&& atlas = directory_iterator(directory); atlas != directory_iterator();++atlas){
				if(is_directory(*atlas)){
					loadPack(atlas->path().string(), a_renderer);
				}
			}
		}
	}

	std::shared_ptr<TexturePack> SharedTextures::loadPack(const std::string &a_packPath, Draw2D* a_renderer) {
		auto newPack = pack(path(a_packPath).filename().string(), a_renderer);

		std::vector<std::string> validExtensions {".jpg", ".png", ".bmp", ".tga", ".gif"};

		for(auto&& imagePath = directory_iterator(path(a_packPath)); imagePath != directory_iterator();++imagePath){
			if(exists(*imagePath) && is_regular_file(*imagePath)){
				std::string lowerCaseImageExtension = imagePath->path().extension().string();
				boost::algorithm::to_lower(lowerCaseImageExtension);
				if(std::find(validExtensions.begin(), validExtensions.end(), lowerCaseImageExtension) != validExtensions.end()){
					newPack->add(imagePath->path().filename().string(), file(imagePath->path().string()));
				}
			}
		}

		return newPack;
	}

	std::shared_ptr<DynamicTextureDefinition> SharedTextures::defaultTexture = nullptr;
	std::shared_ptr<TextureHandle> SharedTextures::defaultHandle = nullptr;


}
