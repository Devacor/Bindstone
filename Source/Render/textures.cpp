#include "textures.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL_image.h>
#include "Utility/generalUtility.h"
#include "Render/points.h"
#include "sharedTextures.h"

#ifndef GL_BGR
	#define GL_BGR 0x80E0
#endif

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::TextureDefinition);
CEREAL_REGISTER_TYPE(MV::FileTextureDefinition);
CEREAL_REGISTER_TYPE(MV::DynamicTextureDefinition);
CEREAL_REGISTER_TYPE(MV::SurfaceTextureDefinition);

CEREAL_REGISTER_TYPE(MV::TextureHandle);

namespace MV {

	/**************************************\
	| ---BareSurfaceAndTextureFunctions--- |
	\**************************************/

	bool clearTexturePoints(std::vector<DrawPoint> &a_points) {
		if (a_points.size() == 4) {
			a_points[0].textureX = 0.0f; a_points[0].textureY = 0.0f;
			a_points[1].textureX = 0.0f; a_points[1].textureY = 1.0f;
			a_points[2].textureX = 1.0f; a_points[2].textureY = 1.0f;
			a_points[3].textureX = 1.0f; a_points[3].textureY = 0.0f;
			return true;
		}
		return false;
	}


	SDL_Surface* converToPowerOfTwo(SDL_Surface* surface) {
		int width = roundUpPowerOfTwo(surface->w);
		int height = roundUpPowerOfTwo(surface->h);

		SDL_Surface* pot_surface = SDL_CreateRGBSurface(0, width, height, 32,
			0x00ff0000, 0x0000ff00,
			0x000000ff, 0xff000000);
		SDL_Rect dstrect;
		dstrect.w = surface->w;
		dstrect.h = surface->h;
		dstrect.x = 0;
		dstrect.y = 0;
		SDL_SetSurfaceAlphaMod(surface, 0);
		SDL_BlitSurface(surface, NULL, pot_surface, &dstrect);
		SDL_FreeSurface(surface);

		return pot_surface;
	}

	GLenum getTextureFormat(SDL_Surface* img) {
		int nOfColors = img->format->BytesPerPixel;
		if (nOfColors == 4) {	  // contains an alpha channel
			if (img->format->Rmask == 0x000000ff) {
				return GL_RGBA;
			} else {
				return GL_BGRA;
			}
		} else if (nOfColors == 3) {	  // no alpha channel
			if (img->format->Rmask == 0x000000ff) {
				return GL_RGB;
			} else {
				return GL_BGR;
			}
		}
		return 0;
	}

	GLenum getInternalTextureFormat(SDL_Surface* img) {
		int nOfColors = img->format->BytesPerPixel;
		if (nOfColors == 4) {	  // contains an alpha channel
			return GL_RGBA;
		} else if (nOfColors == 3) {	  // no alpha channel
			return GL_RGB;
		}
		return 0;
	}

	SDL_Surface* convertToPowerOfTwoSurface(SDL_Surface *a_img) {
		if (a_img != nullptr && (!isPowerOfTwo(a_img->w) || !isPowerOfTwo(a_img->h))) {
			int widthPowerOfTwo = roundUpPowerOfTwo(a_img->w);
			int heightPowerOfTwo = roundUpPowerOfTwo(a_img->h);

			int bpp;
			Uint32 Rmask, Gmask, Bmask, Amask;
			SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
			SDL_Surface *surface = SDL_CreateRGBSurface(0, widthPowerOfTwo, heightPowerOfTwo, bpp, Rmask, Gmask, Bmask, Amask);

			SDL_SetSurfaceBlendMode(a_img, SDL_BLENDMODE_NONE);
			require<ResourceException>(SDL_BlitSurface(a_img, 0, surface, 0) == 0, "SDL_BlitSurface failed to copy!");

			SDL_FreeSurface(a_img);
			return surface;
		}
		return a_img;
	}

	bool loadTextureFromFile(const std::string &a_file, GLuint &a_imageLoaded, Size<int> &a_size, Size<int> &a_originalSize, bool a_powerTwo, bool a_repeat, bool a_pixel) {
		std::cout << "Loading: " << a_file << std::endl;
		SDL_Surface *img = IMG_Load(a_file.c_str());
		if (!img) {
			std::cerr << "Failed to load texture: (" << a_file << ") " << SDL_GetError() << std::endl;
			return false;
		}

		bool result = loadTextureFromSurface(img, a_imageLoaded, a_size, a_originalSize, a_powerTwo, a_repeat, a_pixel);

		return result;
	}

	//Load an opengl texture
	bool loadTextureFromSurface(SDL_Surface *a_img, GLuint &a_imageLoaded, Size<int> &a_size, Size<int> &a_originalSize, bool a_powerTwo, bool a_repeat, bool a_pixel) {
		a_originalSize.width = a_img->w;
		a_originalSize.height = a_img->h;
		if (a_powerTwo) {
			a_img = convertToPowerOfTwoSurface(a_img);
		}

		if (a_img == nullptr) {
			std::cerr << "ERROR: loadTextureFromSurface was provided a null SDL_Surface!" << std::endl;
			return false;
		}

		GLenum textureFormat = getTextureFormat(a_img);
		if (!textureFormat) {
			std::cerr << "Unable to determine texture format!" << std::endl;
			return false;
		}

		if (!RUNNING_IN_HEADLESS) {
			glGenTextures(1, &a_imageLoaded);		// Generate texture ID
			TextureUnloader::increment(a_imageLoaded);
			glBindTexture(GL_TEXTURE_2D, a_imageLoaded);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (a_pixel) ? GL_NEAREST : GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (a_pixel) ? GL_NEAREST : GL_LINEAR);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (a_repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (a_repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, getInternalTextureFormat(a_img), a_img->w, a_img->h, 0, textureFormat, GL_UNSIGNED_BYTE, a_img->pixels);
		}

		a_size.width = a_img->w;
		a_size.height = a_img->h;

		SDL_FreeSurface(a_img);

		return true;
	}

	/*************************\
	| ---TextureDefinition--- |
	\*************************/

	TextureDefinition::TextureDefinition(const std::string &a_name, bool a_isShared) :
		isShared(a_isShared),
		onReload(onReloadAction),
		textureName(a_name),
		texture(0) {
	}

	void TextureDefinition::load() {
		if (!loaded() && !RUNNING_IN_HEADLESS) {
			reloadImplementation();
			if (isShared) {
				onReloadAction(shared_from_this());
			}
		}
	}

	GLuint TextureDefinition::textureId() const {
		return texture;
	}

	bool TextureDefinition::loaded() const {
		return texture != 0;
	}

	Size<int> TextureDefinition::size() const {
		require<ResourceException>(loaded(), "size:: The texture hasn't actually loaded yet.  You may need to create a handle to implicitly force a texture load.");
		return textureSize;
	}

	Size<int> TextureDefinition::size() {
		if (!loaded()) {
			load();
			auto ourSize = textureSize;
			cleanupOpenglTexture();
			return ourSize;
		} else {
			return textureSize;
		}
	}

	Size<int> TextureDefinition::contentSize() const {
		require<ResourceException>(loaded(), "contentSize:: The texture hasn't actually loaded yet.  You may need to create a handle to implicitly force a texture load.");
		return desiredSize;
	}

	Size<int> TextureDefinition::contentSize() {
		if (!loaded()) {
			load();
			auto ourSize = desiredSize;
			cleanupOpenglTexture();
			return ourSize;
		} else {
			return desiredSize;
		}
	}

	std::string TextureDefinition::name() const {
		return textureName;
	}

	void TextureDefinition::unload() {
		cleanupOpenglTexture();
	}

	void TextureDefinition::unload(TextureHandle* toRemove) {
		handles.erase(std::remove_if(handles.begin(), handles.end(), [&](const std::weak_ptr<TextureHandle> &value) {return value.expired() || &(*value.lock()) == toRemove; }), handles.end());
		if (handles.empty()) {
			cleanupOpenglTexture();
		}
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeHandle() {
		if (handles.empty()) {
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this()));
		handles.push_back(handle);
		return handle;
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeHandle(const BoxAABB<int> &a_bounds) {
		if (handles.empty()) {
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this(), a_bounds));
		handles.push_back(handle);
		return handle;
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeHandle(const BoxAABB<PointPrecision> &a_bounds) {
		if (handles.empty()) {
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this(), a_bounds));
		handles.push_back(handle);
		handle->bounds(a_bounds);
		return handle;
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeRawHandle(const BoxAABB<PointPrecision> &a_bounds) {
		if (handles.empty()) {
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this(), a_bounds));
		handles.push_back(handle);
		return handle;
	}

	TextureDefinition::~TextureDefinition() {
		handles.clear();
		cleanupOpenglTexture();
	}

	void TextureDefinition::cleanupOpenglTexture() {
		if (texture) {
			TextureUnloader::decrement(texture);
			texture = 0;
			cleanupImplementation();
			textureSize.set(0, 0);
		}
	}

	void TextureDefinition::save(const std::string &a_fileName) {
		if (RUNNING_IN_HEADLESS) { return; }

		if (!loaded()) {
			load();
			saveLoadedTexture(a_fileName, texture);
			cleanupOpenglTexture();
		} else {
			saveLoadedTexture(a_fileName, texture);
		}
	}

	void TextureDefinition::reload() {
		if (RUNNING_IN_HEADLESS) { return; }

		if (loaded()) {
			cleanupOpenglTexture();
			load();
		}
	}


	/*****************************\
	| ---FileTextureDefinition--- |
	\*****************************/

	void FileTextureDefinition::reloadImplementation() {
		if (textures) {
			auto textureId = SharedTextures::fileId(textureName, repeat);
			auto foundShared = textures->fileDefinitions.find(textureId);
			bool justLoaded = false;
			if (foundShared != textures->fileDefinitions.end()) {
				if (foundShared->second->texture) {
					texture = foundShared->second->texture;
					TextureUnloader::increment(texture);
					textureSize = foundShared->second->textureSize;
					desiredSize = foundShared->second->desiredSize;
					powerTwo = foundShared->second->powerTwo;
					justLoaded = true;
				} else {
					loadTextureFromFile(textureName, texture, textureSize, desiredSize, powerTwo, repeat, pixel);
					foundShared->second->texture = texture;
					TextureUnloader::increment(texture);
					foundShared->second->textureSize = textureSize;
					foundShared->second->desiredSize = desiredSize;
					foundShared->second->powerTwo = powerTwo;
					foundShared->second->onReloadAction(foundShared->second);
				}
			}else{
				loadTextureFromFile(textureName, texture, textureSize, desiredSize, powerTwo, repeat, pixel);
				textures->fileDefinitions[textureId] = std::static_pointer_cast<FileTextureDefinition>(shared_from_this());
			}
		} else {
			loadTextureFromFile(textureName, texture, textureSize, desiredSize, powerTwo, repeat, pixel);
		}
	}

	/********************************\
	| ---DynamicTextureDefinition--- |
	\********************************/

	void DynamicTextureDefinition::reloadImplementation() {
		textureSize.width = roundUpPowerOfTwo(textureSize.width);
		textureSize.height = roundUpPowerOfTwo(textureSize.height);

		if (RUNNING_IN_HEADLESS) { return; }
		unsigned int* data;						// Stored Data
		unsigned int imageSize = (textureSize.width * textureSize.height) * 4 * sizeof(unsigned int);
		// Create Storage Space For Texture Data (128x128x4)
		data = (unsigned int*)new GLuint[(imageSize)];
		memset(data, backgroundColor.hex(), (imageSize));
		glGenTextures(1, &texture);					// Create 1 Texture
		TextureUnloader::increment(texture);
		glBindTexture(GL_TEXTURE_2D, texture);			// Bind The Texture
		// Build Texture Using Information In data
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureSize.width, textureSize.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

		delete[] data;							// Release data
	}

	void DynamicTextureDefinition::resize(const Size<int> &a_size) {
		textureSize = a_size;
		desiredSize = a_size;
		reload();
	}

	/********************************\
	| ---SurfaceTextureDefinition--- |
	\********************************/

	void SurfaceTextureDefinition::reloadImplementation() {
		SDL_Surface* newSurface = surfaceGenerator();
		require<PointerException>(newSurface != nullptr, "SurfaceTextureDefinition::reloadImplementation was passed a null SDL_Surface pointer.");

		generatedSurfaceSize = Size<int>(newSurface->w, newSurface->h);

		//loads and frees
		loadTextureFromSurface(newSurface, texture, textureSize, desiredSize, true, false, false);
	}

	Size<int> SurfaceTextureDefinition::surfaceSize() const {
		return generatedSurfaceSize;
	}


	/*********************\
	| ---TextureHandle--- |
	\*********************/

	std::shared_ptr<TextureHandle> TextureHandle::bounds(const BoxAABB<int> &a_bounds) {
		boundsNoSignal(a_bounds);
		auto self = shared_from_this();
		sizeChangeSignal(self);
		return self;
	}

	std::shared_ptr<TextureHandle> TextureHandle::bounds(const BoxAABB<PointPrecision> &a_bounds) {
		boundsNoSignal(cast<int>(a_bounds * toScale(textureDefinition->contentSize())));
		auto self = shared_from_this();
		sizeChangeSignal(self);
		return self;
	}

	BoxAABB<int> TextureHandle::bounds() const {
		return cast<int>(handlePercent * toScale(textureDefinition->size()));
	}

	std::shared_ptr<TextureHandle> TextureHandle::rawPercent(const BoxAABB<PointPrecision> &a_bounds) {
		handlePercent = a_bounds;
		auto self = shared_from_this();
		sizeChangeSignal(self);
		return self;
	}

	BoxAABB<PointPrecision> TextureHandle::rawPercent() const {
		return handlePercent;
	}

	std::shared_ptr<TextureDefinition> TextureHandle::texture() const {
		return textureDefinition;
	}

	TextureHandle::TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<PointPrecision> &a_bounds) :
		sizeChange(sizeChangeSignal),
		textureDefinition(a_texture),
		handlePercent(a_bounds),
		debugName(a_texture->name()) {
	}

	TextureHandle::TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<int> &a_bounds) :
		sizeChange(sizeChangeSignal),
		textureDefinition(a_texture),
		debugName(a_texture->name()) {

		boundsNoSignal(a_bounds);
	}

	void TextureHandle::boundsNoSignal(const BoxAABB<int> &a_bounds) {
		auto floatBounds = cast<PointPrecision>(a_bounds);
		floatBounds.minPoint += point(.25f, .25f);
		floatBounds.maxPoint -= point(.25f, .25f);
		handlePercent = floatBounds / toScale(textureDefinition->size());
	}

	TextureHandle::~TextureHandle() {
		if (textureDefinition) {
			textureDefinition->unload(this);
		}
	}

	std::shared_ptr<TextureHandle> TextureHandle::flipX( bool a_flip ) {
		auto self = shared_from_this();
		if (flipX() != a_flip) {
			std::swap(handlePercent.minPoint.x, handlePercent.maxPoint.x);
			sizeChangeSignal(self);
		}
		return self;
	}

	std::shared_ptr<TextureHandle> TextureHandle::flipY(bool a_flip) {
		auto self = shared_from_this();
		if(flipY() != a_flip){
			std::swap(handlePercent.minPoint.y, handlePercent.maxPoint.y);
			sizeChangeSignal(self);
		}
		sizeChangeSignal(self);
		return self;
	}

	bool TextureHandle::flipX() const {
		return handlePercent.minPoint.x > handlePercent.maxPoint.x;
	}

	bool TextureHandle::flipY() const {
		return handlePercent.minPoint.y > handlePercent.maxPoint.y;
	}


	std::string TextureHandle::name() const {
		return debugName;
	}

	/*Vertex Indices*\
		0 15  14  3
		8  4   7 13
		9  5   6 12
		1 10  11  2
	\*Vertex Indices*/
	bool TextureHandle::apply(std::vector<DrawPoint> &a_points) const {
		if(a_points.size() == 4 || a_points.size() == 16){
			a_points[0].textureX = handlePercent.minPoint.x; a_points[0].textureY = handlePercent.minPoint.y;
			a_points[1].textureX = handlePercent.minPoint.x; a_points[1].textureY = handlePercent.maxPoint.y;
			a_points[2].textureX = handlePercent.maxPoint.x; a_points[2].textureY = handlePercent.maxPoint.y;
			a_points[3].textureX = handlePercent.maxPoint.x; a_points[3].textureY = handlePercent.minPoint.y;
		}
		if(a_points.size() == 16){
			auto finalSlice = rawSlice();
			a_points[4].textureX = finalSlice.minPoint.x; a_points[4].textureY = finalSlice.minPoint.y;
			a_points[5].textureX = finalSlice.minPoint.x; a_points[5].textureY = finalSlice.maxPoint.y;
			a_points[6].textureX = finalSlice.maxPoint.x; a_points[6].textureY = finalSlice.maxPoint.y;
			a_points[7].textureX = finalSlice.maxPoint.x; a_points[7].textureY = finalSlice.minPoint.y;

			a_points[8].textureX = handlePercent.minPoint.x; a_points[8].textureY = finalSlice.minPoint.y;
			a_points[9].textureX = handlePercent.minPoint.x; a_points[9].textureY = finalSlice.maxPoint.y;

			a_points[10].textureX = finalSlice.minPoint.x; a_points[10].textureY = handlePercent.maxPoint.y;
			a_points[11].textureX = finalSlice.maxPoint.x; a_points[11].textureY = handlePercent.maxPoint.y;

			a_points[12].textureX = handlePercent.maxPoint.x; a_points[12].textureY = finalSlice.maxPoint.y;
			a_points[13].textureX = handlePercent.maxPoint.x; a_points[13].textureY = finalSlice.minPoint.y;

			a_points[14].textureX = finalSlice.maxPoint.x; a_points[14].textureY = handlePercent.minPoint.y;
			a_points[15].textureX = finalSlice.minPoint.x; a_points[15].textureY = handlePercent.minPoint.y;

			applySlicePosition(a_points);
		}
		return a_points.size() == 4 || a_points.size() == 16;
	}

	/*Vertex Indices*\
		0 15  14  3
		8  4   7 13
		9  5   6 12
		1 10  11  2
	\*Vertex Indices*/
	bool TextureHandle::applySlicePosition(std::vector<DrawPoint> &a_points) const {
		if(a_points.size() == 16){
			auto parentBounds = BoxAABB<>(a_points[0].point(), a_points[2].point());
			auto sliceBounds = logicalSlice();
			
			sliceBounds.maxPoint = parentBounds.maxPoint - (toPoint(logicalSize()) - sliceBounds.maxPoint);
			sliceBounds.minPoint = parentBounds.minPoint + sliceBounds.minPoint;

			auto intermediateSliceBounds = sliceBounds;
			if (intermediateSliceBounds.maxPoint.x < intermediateSliceBounds.minPoint.x) {
				sliceBounds.maxPoint.x = parentBounds.minPoint.x + (parentBounds.width() * (slicePercent.minPoint.x + (slicePercent.width() / 2.0f)));
				sliceBounds.minPoint.x = sliceBounds.maxPoint.x;
			}

			if (intermediateSliceBounds.minPoint.y > intermediateSliceBounds.maxPoint.y) {
				sliceBounds.maxPoint.y = parentBounds.minPoint.y + (parentBounds.height() * (slicePercent.minPoint.y + (slicePercent.height() / 2.0f)));
				sliceBounds.minPoint.y = sliceBounds.maxPoint.y;
			}

// 			if (intermediateSliceBounds.maxPoint.x < parentBounds.minPoint.x) {
// 				sliceBounds.maxPoint.x = parentBounds.minPoint.x + (parentBounds.width() * slicePercent.maxPoint.x);
// 			}
// 			if (intermediateSliceBounds.maxPoint.y < parentBounds.minPoint.y || intermediateSliceBounds.maxPoint.y < intermediateSliceBounds.minPoint.y) {
// 				sliceBounds.maxPoint.y = parentBounds.minPoint.y + (parentBounds.height() * slicePercent.maxPoint.y);
// 			}
// 
// 			if (intermediateSliceBounds.minPoint.x > parentBounds.maxPoint.x || intermediateSliceBounds.minPoint.x > intermediateSliceBounds.maxPoint.x) {
// 				sliceBounds.minPoint.x = parentBounds.minPoint.x + (parentBounds.width() * slicePercent.minPoint.x);
// 			}
// 			if (intermediateSliceBounds.minPoint.y > parentBounds.maxPoint.y || intermediateSliceBounds.minPoint.y > intermediateSliceBounds.maxPoint.y) {
// 				sliceBounds.minPoint.y = parentBounds.minPoint.y + (parentBounds.height() * slicePercent.minPoint.y);
// 			}

			a_points[4] = sliceBounds.minPoint;
			a_points[5] = point(sliceBounds.minPoint.x, sliceBounds.maxPoint.y);
			a_points[6] = sliceBounds.maxPoint;
			a_points[7] = point(sliceBounds.maxPoint.x, sliceBounds.minPoint.y);

			a_points[8] = point(parentBounds.minPoint.x, sliceBounds.minPoint.y);
			a_points[9] = point(parentBounds.minPoint.x, sliceBounds.maxPoint.y);

			a_points[10] = point(sliceBounds.minPoint.x, parentBounds.maxPoint.y);
			a_points[11] = point(sliceBounds.maxPoint.x, parentBounds.maxPoint.y);

			a_points[12] = point(parentBounds.maxPoint.x, sliceBounds.maxPoint.y);
			a_points[13] = point(parentBounds.maxPoint.x, sliceBounds.minPoint.y);

			a_points[14] = point(sliceBounds.maxPoint.x, parentBounds.minPoint.y);
			a_points[15] = point(sliceBounds.minPoint.x, parentBounds.minPoint.y);

// 			for (int i = 0; i < 4; ++i) {
// 				a_points[i] = Color(1.0f, 1.0f, 1.0f, 1.0f);
// 			}
// 			for (int i = 4; i < 8; ++i) {
// 				a_points[i] = Color(1.0f, 0.0f, 1.0f, 1.0f);
// 			}
// 			for (int i = 8; i < 10; ++i) {
// 				a_points[i] = Color(0.0f, 0.0f, 1.0f, 1.0f);
// 			}
// 			for (int i = 10; i < 12; ++i) {
// 				a_points[i] = Color(1.0f, 0.0f, 0.0f, 1.0f);
// 			}
// 			for (int i = 12; i < 14; ++i) {
// 				a_points[i] = Color(0.0f, 1.0f, 0.0f, 1.0f);
// 			}
// 			for (int i = 14; i < 16; ++i) {
// 				a_points[i] = Color(0.0f, 1.0f, 1.0f, 1.0f);
// 			}
			return true;
		}
		return false;
	}

	std::shared_ptr<TextureHandle> TextureHandle::clone() {
		auto result = textureDefinition->makeHandle();
		result->debugName = debugName;
		result->handlePercent = handlePercent;
		result->slicePercent = slicePercent;
		return result;
	}

	//logical size == bounds.size() * texture scale
	Size<PointPrecision> TextureHandle::logicalSize() const{
		auto sanitizedBounds = handlePercent;
		sanitizedBounds.sanitize();
		return (sanitizedBounds.size() * cast<PointPrecision>(textureDefinition->contentSize())) * textureDefinition->scale();
	}
	//logical slice == percentSlice * logicalSize
	BoxAABB<PointPrecision> TextureHandle::logicalSlice() const{
		return slicePercent * toScale(logicalSize());
	}

	//relative to the size of the int bounds
	//a texture handle at position(128, 128), size(64, 64) would expect a sliceBounds of range x: 0-64, y: 0-64
	std::shared_ptr<TextureHandle> TextureHandle::slice(const BoxAABB<int> &a_sliceBounds) {
		slicePercent = cast<PointPrecision>(a_sliceBounds) / toScale(bounds().size());
		auto self = shared_from_this();
		sizeChangeSignal(self);
		return self;
	}
	//percent of bounds, IE: 0-1 of bounds, not 0-1 of texture.
	std::shared_ptr<TextureHandle> TextureHandle::slice(const BoxAABB<PointPrecision> &a_sliceBounds){
		slicePercent = a_sliceBounds;
		auto self = shared_from_this();
		sizeChangeSignal(self);
		return self;
	}
	BoxAABB<PointPrecision> TextureHandle::slice() const{
		return slicePercent;
	}
	std::shared_ptr<TextureHandle> TextureHandle::clearSlice() {
		slicePercent.clear();
		return shared_from_this();
	}
	bool TextureHandle::hasSlice() const {
		return !slicePercent.empty();
	}

	MV::BoxAABB<MV::PointPrecision> TextureHandle::rawSlice() const {
		auto finalSlice = slicePercent * toScale(handlePercent.size());
		finalSlice += handlePercent.minPoint;
		return finalSlice;
	}

	void saveLoadedTexture(const std::string &a_fileName, GLuint a_texture) {
		require<ResourceException>(a_texture, "saveLoadedOpenglTexture was supplied a null texture id: ", a_fileName);
		glBindTexture(GL_TEXTURE_2D, a_texture);
		GLint textureWidth, textureHeight;
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &textureWidth);
		glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &textureHeight);

		require<ResourceException>(textureWidth > 0 && textureHeight > 0, "saveLoadedOpenglTexture encountered a 0 dimension image: ", a_fileName);

		std::vector<char> pixels(textureWidth*textureHeight * 4);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);

		SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(&pixels[0], textureWidth, textureHeight, 32, textureWidth * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		IMG_SavePNG(surf, a_fileName.c_str());
		SDL_FreeSurface(surf);
	}


	void TextureUnloader::increment(GLuint a_id) {
		MV::require<MV::ResourceException>(a_id != 0, "Null texture attempted to increment in TextureUnloader!");
		std::lock_guard<std::mutex> guard(lock);
		handles[a_id]++;
	}

	bool TextureUnloader::decrement(GLuint a_id) {
		MV::require<MV::ResourceException>(a_id != 0, "Null texture attempted to decrement in TextureUnloader!");
		std::lock_guard<std::mutex> guard(lock);
		auto handleCount = --handles[a_id];
		MV::require<MV::ResourceException>(handleCount >= 0, "TextureUnloader: Handle underflow for GLuint id: ", a_id);
		if (handleCount == 0) {
			glDeleteTextures(1, &a_id);
			return true;
		}
		return false;
	}

	std::mutex TextureUnloader::lock;
	std::map<GLuint, int> TextureUnloader::handles;

}
