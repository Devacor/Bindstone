#include "textures.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL_image.h>
#include "MV/Utility/generalUtility.h"
#include "MV/Render/points.h"
#include "sharedTextures.h"

#ifndef GL_BGR
	#define GL_BGR 0x80E0
#endif

#ifndef GL_BGRA
	#define GL_BGRA 0x80E1
#endif

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::TextureDefinition);
CEREAL_REGISTER_TYPE(MV::FileTextureDefinition);
CEREAL_REGISTER_TYPE(MV::DynamicTextureDefinition);
CEREAL_REGISTER_TYPE(MV::SurfaceTextureDefinition);

CEREAL_REGISTER_TYPE(MV::TextureHandle);

CEREAL_CLASS_VERSION(MV::TextureHandle, 2);

CEREAL_REGISTER_DYNAMIC_INIT(mv_scenetextures);

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

			return surface;
		}
		return a_img;
	}

	LoadedTextureData& TextureIndex::loadFile(const TextureParameters &a_parameters) {
		if (a_parameters.cleared) {
			std::cerr << "Warning: Passing in a cleared texture parameter! [" << a_parameters.path << "]" << std::endl;
		}

		std::lock_guard<std::recursive_mutex> guard(lock);
		auto& found = data[a_parameters];
		found.useCount++;
		if (found.textureId != 0) {
			return found;
		}

		MV::info("Loading Image: ", a_parameters.path);
		SDL_RWops* sdlIO = sdlFileHandle(a_parameters.path);
		if (!sdlIO) {
			MV::warning("Failed to load image [", a_parameters.path, "]");
			return found;
		}
		SDL_Surface *img = IMG_Load_RW(sdlIO, 1);
		if (!img) {
			MV::error("Failed to load image [", a_parameters.path, "] [",SDL_GetError(),"]");
			return found;
		}

		found = loadTextureFromSurface(img, a_parameters);
		SDL_FreeSurface(img);
		return found;
	}

	void TextureIndex::releaseFile(const TextureParameters &a_parameters) {
		if (a_parameters.cleared) { return; }

		auto& found = data[a_parameters];
		if (--found.useCount == 0 && found.textureId != 0) {
			decrement(found.textureId);
			found.textureId = 0;
		}
		if (found.useCount < 0) {
			std::cerr << "TextureIndex::releaseFile underflow: (" << a_parameters.path << ")" << std::endl;
		}
	}

	//Load an opengl texture
	LoadedTextureData loadTextureFromSurface(SDL_Surface *a_img, const TextureParameters &a_parameters) {
		if (a_img == nullptr) {
			std::cerr << "ERROR: loadTextureFromSurface was provided a null SDL_Surface!" << std::endl;
			return {};
		}

		GLenum textureFormat = getTextureFormat(a_img);
		if (!textureFormat) {
			std::cerr << "Unable to determine texture format!" << std::endl;
			return {};
		}

		LoadedTextureData results;
		results.originalSize.width = a_img->w;
		results.originalSize.height = a_img->h;

		SDL_Surface* surfaceToWorkWith = a_img;
		if (a_parameters.powerTwo) {
			surfaceToWorkWith = convertToPowerOfTwoSurface(a_img);
		}
		SCOPE_EXIT{ if (surfaceToWorkWith != a_img) { SDL_FreeSurface(surfaceToWorkWith); } };

		if (!RUNNING_IN_HEADLESS) {
			glGenTextures(1, &results.textureId);		// Generate texture ID
			TextureIndex::increment(results.textureId);
			glBindTexture(GL_TEXTURE_2D, results.textureId);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (a_parameters.pixel) ? GL_NEAREST : GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (a_parameters.pixel) ? GL_NEAREST : GL_LINEAR);

			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (a_parameters.repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (a_parameters.repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

			glTexImage2D(GL_TEXTURE_2D, 0, getInternalTextureFormat(surfaceToWorkWith), surfaceToWorkWith->w, surfaceToWorkWith->h, 0, textureFormat, GL_UNSIGNED_BYTE, surfaceToWorkWith->pixels);
		}

		results.size.width = surfaceToWorkWith->w;
		results.size.height = surfaceToWorkWith->h;

		return results;
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

	void TextureDefinition::registerHandle(std::weak_ptr<TextureHandle> a_newHandle) {
		if (handles.empty()) {
			load();
		}
		handles.push_back(a_newHandle);
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
			TextureIndex::decrement(texture);
			texture = 0;
			cleanupImplementation();
		}
	}

	void TextureDefinition::save(const std::string &a_fileName) {
		if (RUNNING_IN_HEADLESS) { return; }

		if (!loaded()) {
			load();
			saveLoadedTexture(a_fileName, texture, textureSize.width, textureSize.height);
			cleanupOpenglTexture();
		} else {
			saveLoadedTexture(a_fileName, texture, textureSize.width, textureSize.height);
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
		loadedTexture = std::make_unique<LoadedTexture>(TextureParameters{ textureName, powerTwo, repeat, pixel });
		textureSize = loadedTexture->Data().size;
		desiredSize = loadedTexture->Data().originalSize;
		texture = loadedTexture->Data().textureId;
	}

	void FileTextureDefinition::cleanupOpenglTexture() {
		loadedTexture.release();
		texture = 0;
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
		TextureIndex::increment(texture);
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
		auto results = loadTextureFromSurface(newSurface, TextureParameters(true, false, false));
		texture = results.textureId;
		textureSize = results.size;
		desiredSize = results.originalSize;

		SDL_FreeSurface(newSurface);
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

	const BoxAABB<PointPrecision> &TextureHandle::rawPercent() const {
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

	MV::SharedTextures* getSharedTextureFromServices(MV::Services& a_services) {
		return a_services.get<MV::SharedTextures>();
	}

	void TextureHandle::postLoadInitialize(SharedTextures* a_sharedTextures) {
		bool needsLoad = true;
		if (a_sharedTextures && !packId.empty()) {
			auto packFound = a_sharedTextures->pack(packId);
			if (packFound) {
				auto ids = packFound->handleIds();
				if (std::find(ids.begin(), ids.end(), debugName) != ids.end()) {
					if (debugName == "CONTENT_HANDLE") {
						handlePercent = packFound->contentPercent();
					} else if (debugName == "FULL_HANDLE") {
						handlePercent = { MV::point(0.0f, 0.0f), MV::size(1.0f, 1.0f) };
					} else {
						auto shapeFound = packFound->shape(debugName);
						slicePercent = shapeFound.slice;
						handlePercent = packFound->percentBounds(shapeFound.bounds);
					}
					auto textureFound = packFound->texture();
					if (textureFound) {
						textureDefinition = textureFound;
						textureDefinition->registerHandle(shared_from_this());
						needsLoad = false;
					}
				}
			}
		}
		if (needsLoad) {
			textureDefinition->load();
		}
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

	void saveLoadedTexture(const std::string &a_fileName, GLuint a_texture, GLuint a_width, GLuint a_height) {
		GLint prev_fbo = 0;
		GLuint fbo = 0;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		std::vector<char> pixels(a_width*a_height * 4);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a_texture, 0);
		GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
		if (status != GL_FRAMEBUFFER_COMPLETE) {
			std::cerr << "failed to make complete framebuffer object" << status << '\n';
		}
		glReadPixels(0, 0, a_width, a_height, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);
		glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
		glDeleteFramebuffers(1, &fbo);

		SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(&pixels[0], a_width, a_height, 32, a_width * 4, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
		IMG_SavePNG(surf, a_fileName.c_str());
		SDL_FreeSurface(surf);
	}

	void TextureIndex::increment(GLuint a_id) {
		MV::require<MV::ResourceException>(a_id != 0, "Null texture attempted to increment in TextureUnloader!");
		std::lock_guard<std::recursive_mutex> guard(lock);
		handles[a_id]++;
	}

	bool TextureIndex::decrement(GLuint a_id) {
		MV::require<MV::ResourceException>(a_id != 0, "Null texture attempted to decrement in TextureUnloader!");
		std::lock_guard<std::recursive_mutex> guard(lock);
		auto handleCount = --handles[a_id];
		MV::require<MV::ResourceException>(handleCount >= 0, "TextureUnloader: Handle underflow for GLuint id: ", a_id);
		if (handleCount == 0) {
			glDeleteTextures(1, &a_id);
			return true;
		}
		return false;
	}

	std::recursive_mutex TextureIndex::lock;
	std::map<GLuint, int> TextureIndex::handles;
	std::map<MV::TextureParameters, MV::LoadedTextureData> TextureIndex::data;

	LoadedTexture::LoadedTexture(TextureParameters a_parameters) :
		ourParameters(a_parameters),
		loadedData(TextureIndex::loadFile(a_parameters)){
	}

	LoadedTexture::~LoadedTexture() {
		TextureIndex::releaseFile(ourParameters);
	}

}
