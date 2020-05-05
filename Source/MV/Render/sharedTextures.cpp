#include "sharedTextures.h"
#include <algorithm>
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/stringUtility.h"

#include "MV/Utility/stopwatch.h"

using namespace boost::filesystem;

namespace MV {
	/**********************\
	| ---SharedTextures--- |
	\**********************/

	std::shared_ptr<TexturePack> SharedTextures::pack(const std::string &a_name) {
		std::string identifier = a_name;
		auto foundDefinition = texturePacks.find(identifier);
		if (foundDefinition == texturePacks.end()) {
			return std::shared_ptr<TexturePack>();
		} else {
			return foundDefinition->second;
		}
	}

	std::shared_ptr<TexturePack> SharedTextures::pack(const std::string &a_name, Draw2D* a_renderer) {
		std::string identifier = a_name;
		auto foundDefinition = texturePacks.find(identifier);
		if (foundDefinition == texturePacks.end()) {
			require<PointerException>(a_renderer, "No renderer supplied in creation of pack: ", a_name);
			auto newDefinition = TexturePack::make(a_name, a_renderer);
			texturePacks[identifier] = newDefinition;
			return newDefinition;
		} else {
			return foundDefinition->second;
		}
	}

	std::shared_ptr<FileTextureDefinition> SharedTextures::file(const std::string &a_filename, bool a_repeat, bool a_pixel) {
		std::string identifier = fileId(a_filename, a_repeat, a_pixel);
		auto foundDefinition = fileDefinitions.find(identifier);
		if(foundDefinition == fileDefinitions.end()){
			std::shared_ptr<FileTextureDefinition> newDefinition = FileTextureDefinition::make(a_filename, true, a_repeat, a_pixel);
			fileDefinitions[identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	void SharedTextures::files(const std::string &a_rootDirectory, bool a_repeat, bool a_pixel) {
		path directory(a_rootDirectory);
		if (exists(directory)) {
			for (auto&& imagePath = directory_iterator(directory); imagePath != directory_iterator(); ++imagePath) {
				if (exists(*imagePath) && is_regular_file(*imagePath)) {
					file(imagePath->path().string(), a_repeat, a_pixel);
				}
			}
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

	std::shared_ptr<SurfaceTextureDefinition> SharedTextures::surface(const std::string &a_identifier, std::function<std::shared_ptr<OwnedSurface>()> a_surfaceGenerator) {
		auto foundDefinition = surfaceDefinitions.find(a_identifier);
		if(foundDefinition == surfaceDefinitions.end()){
			std::shared_ptr<SurfaceTextureDefinition> newDefinition = SurfaceTextureDefinition::make(a_identifier, a_surfaceGenerator);
			surfaceDefinitions[a_identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
	}

	std::vector<std::pair<std::string, bool>> SharedTextures::fileIds() const {
		std::vector<std::pair<std::string, bool>> keys;
		std::transform(fileDefinitions.begin(), fileDefinitions.end(), std::back_inserter(keys), [](const std::map<std::string, std::shared_ptr<FileTextureDefinition>>::value_type &pair) {
			if (pair.first.size() < 1) {
				return std::pair<std::string, bool>{"", false};
			}
			std::string fileName = pair.first.substr(0, pair.first.size() - 1);
			bool repeating = pair.first[pair.first.size()-2] == '1';
			return std::pair<std::string, bool>{fileName, repeating};
		});
		return keys;
	}

	std::vector<std::string> SharedTextures::packIds() const {
		std::vector<std::string> keys;
		std::transform(texturePacks.begin(), texturePacks.end(), std::back_inserter(keys), [](const std::map<std::string, std::shared_ptr<TexturePack>>::value_type &pair){
			return pair.first;
		});
		return keys;
	}

	void SharedTextures::assemblePacks(const std::string &a_rootDirectory, Draw2D* a_renderer) {
		path directory(a_rootDirectory);
		if(exists(directory)){
			for(auto&& atlas = directory_iterator(directory); atlas != directory_iterator();++atlas){
				if(is_directory(*atlas)){
					assemblePack(atlas->path().string(), a_renderer);
				}
			}
		}
	}

	std::vector<std::string> SharedTextures::getImagesInFolder(const std::string &a_packPath) const {
		std::vector<std::string> imageFiles;
		for (auto&& imagePath = directory_iterator(path(a_packPath)); imagePath != directory_iterator(); ++imagePath) {
			if (exists(*imagePath) && is_regular_file(*imagePath)) {
				std::string lowerCaseImageExtension = imagePath->path().extension().string();
				toLowerInPlace(lowerCaseImageExtension);
				if (std::find(validExtensions.begin(), validExtensions.end(), lowerCaseImageExtension) != validExtensions.end()) {
					imageFiles.push_back(imagePath->path().string());
				}
			}
		}
		return imageFiles;
	}

	std::vector<SharedTextures::PackItem> SharedTextures::getSortedPackItems(std::vector<std::string> imagePaths) const {
		std::vector<PackItem> packItems;
		for (auto&& filePath : imagePaths) {
			std::string contents = fileContents(path(filePath).replace_extension("slc").string());
			std::stringstream stream(contents);
			BoxAABB<float> sliceBounds;
			if (stream) {
				stream >> sliceBounds.minPoint.x >> sliceBounds.minPoint.y >> sliceBounds.maxPoint.x >> sliceBounds.maxPoint.y;
			}
			packItems.push_back({ path(filePath).filename().string(), FileTextureDefinition::make(filePath, true, false), sliceBounds });
		}

		std::sort(packItems.begin(), packItems.end(), std::greater<>());
		return packItems;
	}

	std::shared_ptr<TexturePack> SharedTextures::assemblePack(const std::string &a_packPath, Draw2D* a_renderer) {
		Stopwatch timer;
		timer.start();

		std::vector<PackItem> packItems = getSortedPackItems(getImagesInFolder(a_packPath));
		if (packItems.empty()) {
			MV::error("Texture Pack Directory Is Empty: [", a_packPath, "]");
			return {};
		}

		auto newPack = pack(path(a_packPath).filename().string(), a_renderer);
		for (auto&& packItem : packItems) {
			newPack->add(packItem.id, packItem.texture, packItem.sliceBounds);
		}
		auto combinedSavePath = a_packPath;
		if(a_packPath.back() == '\\' || a_packPath.back() == '/'){
			combinedSavePath.pop_back();
		}
		combinedSavePath += ".png";
		newPack->consolidate(combinedSavePath, this);
		MV::info("Texture Pack Saved [", a_packPath, "]: ", timer.stop());

		return newPack;
	}
}
