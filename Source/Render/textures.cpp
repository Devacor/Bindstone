#include "textures.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <SDL_image.h>
#include "Utility/generalUtility.h"

#ifndef GL_BGR
	#define GL_BGR 0x80E0
#endif

CEREAL_REGISTER_TYPE(MV::TextureDefinition);
CEREAL_REGISTER_TYPE(MV::FileTextureDefinition);
CEREAL_REGISTER_TYPE(MV::DynamicTextureDefinition);
CEREAL_REGISTER_TYPE(MV::SurfaceTextureDefinition);

CEREAL_REGISTER_TYPE(MV::TextureHandle);

namespace MV {

	/**************************************\
	| ---BareSurfaceAndTextureFunctions--- |
	\**************************************/

	SDL_Surface* converToPowerOfTwo(SDL_Surface* surface)
	{
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

	GLenum getTextureFormat(SDL_Surface* img){
		int nOfColors = img->format->BytesPerPixel;
		if (nOfColors == 4){	  // contains an alpha channel
			if (img->format->Rmask == 0x000000ff){
				return GL_RGBA;
			} else{
				return GL_BGRA;
			}
		} else if (nOfColors == 3){	  // no alpha channel
			if (img->format->Rmask == 0x000000ff){
				return GL_RGB;
			} else{
				return GL_BGR;
			}
		}
		return 0;
	}

	GLenum getInternalTextureFormat(SDL_Surface* img){
		int nOfColors = img->format->BytesPerPixel;
		if (nOfColors == 4){	  // contains an alpha channel
			return GL_RGBA;
		} else if (nOfColors == 3){	  // no alpha channel
			return GL_RGB;
		}
		return 0;
	}

	SDL_Surface* convertToPowerOfTwoSurface(SDL_Surface *a_img){
		if(a_img != nullptr && (!isPowerOfTwo(a_img->w) || !isPowerOfTwo(a_img->h))){
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

	bool loadTextureFromFile(const std::string &a_file, GLuint &a_imageLoaded, Size<int> &a_size, bool a_repeat) {
		std::cout << "Loading: " << a_file << std::endl;
		SDL_Surface *img = IMG_Load(a_file.c_str());
		if (!img){
			std::cerr << "Failed to load texture: (" << a_file << ") " << SDL_GetError() << std::endl;
			return false;
		}

		bool result = loadTextureFromSurface(img, a_imageLoaded, a_size, a_repeat);

		return result;
	}

	//Load an opengl texture
	bool loadTextureFromSurface(SDL_Surface *a_img, GLuint &a_imageLoaded, Size<int> &a_size, bool a_repeat) {
		a_img = convertToPowerOfTwoSurface(a_img);

		if(a_img == nullptr){
			std::cerr << "ERROR: loadTextureFromSurface was provided a null SDL_Surface!" << std::endl;
			return false;
		}

		GLenum textureFormat = getTextureFormat(a_img);
		if(!textureFormat){
			std::cerr << "Unable to determine texture format!" << std::endl;
			return false;
		}

		glGenTextures(1, &a_imageLoaded);		// Generate texture ID
		glBindTexture(GL_TEXTURE_2D, a_imageLoaded);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (a_repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (a_repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, getInternalTextureFormat(a_img), a_img->w, a_img->h, 0, textureFormat, GL_UNSIGNED_BYTE, a_img->pixels);

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
		texture(0){
	}

	void TextureDefinition::load(){
		if(!loaded()){
			reloadImplementation();
			if(isShared){
				onReloadAction(shared_from_this());
			}
		}
	}

	GLuint TextureDefinition::textureId() const{
		return texture;
	}

	bool TextureDefinition::loaded() const{
		return texture != 0;
	}

	Size<int> TextureDefinition::size() const{
		require<ResourceException>(loaded(), "The texture hasn't actually loaded yet.  You may need to create a handle to implicitly force a texture load.");
		return textureSize;
	}

	Size<int> TextureDefinition::size(){
		if(!loaded()){
			load();
			auto ourSize = textureSize;
			cleanupOpenglTexture();
			return ourSize;
		} else{
			return textureSize;
		}
	}

	std::string TextureDefinition::name() const{
		return textureName;
	}

	void TextureDefinition::unload(){
		if (!handles.empty()){
			handles.erase(std::remove_if(handles.begin(), handles.end(), [](const std::weak_ptr<TextureHandle> &value){return value.expired(); }), handles.end());
			cleanupOpenglTexture();
		}
	}

	void TextureDefinition::unload(TextureHandle* toRemove){
		if(!handles.empty()){
			handles.erase(std::remove_if(handles.begin(), handles.end(), [&](const std::weak_ptr<TextureHandle> &value){return value.expired() || &(*value.lock()) == toRemove; }), handles.end());
			cleanupOpenglTexture();
		}
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeHandle() {
		if (handles.empty()){
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this()));
		handles.push_back(handle);
		return handle;
	}

	std::shared_ptr<TextureHandle> TextureDefinition::makeHandle(const Point<int> &a_position, const Size<int> &a_size) {
		if (handles.empty()){
			load();
		}
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this(), a_position, a_size));
		handles.push_back(handle);
		return handle;
	}

	TextureDefinition::~TextureDefinition() {
		handles.clear();
		cleanupOpenglTexture();
	}

	void TextureDefinition::cleanupOpenglTexture() {
		if(handles.empty()){
			glDeleteTextures(1, &texture);
			texture = 0; //just to be certain, glDeleteTextures may not set a texture id to 0.
			cleanupImplementation();
			textureSize.set(0, 0);
		}
	}

	void TextureDefinition::save(const std::string &a_fileName) {
		if(!loaded()){
			load();
			saveLoadedTexture(a_fileName, texture);
			cleanupOpenglTexture();
		} else{
			saveLoadedTexture(a_fileName, texture);
		}
	}


	/*****************************\
	| ---FileTextureDefinition--- |
	\*****************************/

	void FileTextureDefinition::reloadImplementation(){
		loadTextureFromFile(textureName, texture, textureSize, repeat);
	}

	/********************************\
	| ---DynamicTextureDefinition--- |
	\********************************/

	void DynamicTextureDefinition::reloadImplementation() {
		textureSize.width = roundUpPowerOfTwo(textureSize.width);
		textureSize.height = roundUpPowerOfTwo(textureSize.height);

		unsigned int* data;						// Stored Data
		unsigned int imageSize = (textureSize.width * textureSize.height) * 4 * sizeof(unsigned int);
		// Create Storage Space For Texture Data (128x128x4)
		data = (unsigned int*)new GLuint[(imageSize)];
		memset(data, backgroundColor.hex(), (imageSize));
		glGenTextures(1, &texture);					// Create 1 Texture
		glBindTexture(GL_TEXTURE_2D, texture);			// Bind The Texture
		// Build Texture Using Information In data
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureSize.width, textureSize.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

		delete[] data;							// Release data
	}

	/********************************\
	| ---SurfaceTextureDefinition--- |
	\********************************/

	void SurfaceTextureDefinition::reloadImplementation() {
		SDL_Surface* newSurface = surfaceGenerator();
		require<PointerException>(newSurface != nullptr, "SurfaceTextureDefinition::reloadImplementation was passed a null SDL_Surface pointer.");

		generatedSurfaceSize = Size<int>(newSurface->w, newSurface->h);

		//loads and frees
		loadTextureFromSurface(newSurface, texture, textureSize, false);
	}

	Size<int> SurfaceTextureDefinition::surfaceSize() const {
		return generatedSurfaceSize;
	}


	/*********************\
	| ---TextureHandle--- |
	\*********************/

	Size<int> TextureHandle::size() const {
		return handleSize;
	}

	Point<int> TextureHandle::position() const {
		return handlePosition;
	}

	std::shared_ptr<TextureDefinition> TextureHandle::texture() const {
		return textureDefinition;
	}

	TextureHandle::TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const Point<int> &a_position, const Size<int> &a_size) :
		sizeObserver(sizeChanges),
		textureDefinition(a_texture),
		handlePosition(a_position),
		handleSize((a_texture && a_texture->loaded() && a_size == Size<int>(-1, -1)) ? a_texture->size() : a_size),
		resizeToParent(a_size == Size<int>(-1, -1)),
		flipX(false),
		flipY(false),
		name(a_texture->name()){
		
		onParentReload = TextureDefinition::SignalType::make([&](std::shared_ptr<TextureDefinition> a_texture){
			if(resizeToParent){
				setBounds(handlePosition, a_texture->size());
			}
		});
		observeTextureReload();
		if(a_texture && a_texture->loaded()){
			handlePercentSize = cast<double>(size()) / cast<double>(textureDefinition->size());
			handlePercentPosition = cast<double>(position()) / toPoint(cast<double>(textureDefinition->size()));

			handlePercentTopLeft.x = handlePercentPosition.x;
			handlePercentBottomRight.x = handlePercentPosition.x + handlePercentSize.width;
			if(flipX){ std::swap(handlePercentTopLeft.x, handlePercentBottomRight.x); }

			handlePercentTopLeft.y = handlePercentPosition.y;
			handlePercentBottomRight.y = handlePercentPosition.x + handlePercentSize.height;
			if(flipY){ std::swap(handlePercentTopLeft.y, handlePercentBottomRight.y); }
		}
	}

	TextureHandle::~TextureHandle() {
		if(textureDefinition){
			textureDefinition->unload(this);
		}
	}

	void TextureHandle::observeTextureReload(){
		if(textureDefinition){
			textureDefinition->onReload.connect(onParentReload);
		}
	}

	void TextureHandle::updatePercentBounds(){
		handlePercentSize = cast<double>(size()) / cast<double>(textureDefinition->size());
		handlePercentPosition = cast<double>(position()) / toPoint(cast<double>(textureDefinition->size()));

		updatePercentCorners();
	}

	void TextureHandle::updateIntegralBounds(){
		handleSize = cast<int>(cast<double>(textureDefinition->size()) * handlePercentSize);
		handlePosition = cast<int>(toPoint(cast<double>(textureDefinition->size())) * handlePercentPosition);

		updatePercentCorners();
	}

	void TextureHandle::updatePercentCorners(){
		handlePercentTopLeft.x = handlePercentPosition.x;
		handlePercentBottomRight.x = handlePercentPosition.x + handlePercentSize.width;
		if (flipX){ std::swap(handlePercentTopLeft.x, handlePercentBottomRight.x); }

		handlePercentTopLeft.y = handlePercentPosition.y;
		handlePercentBottomRight.y = handlePercentPosition.x + handlePercentSize.height;
		if (flipY){ std::swap(handlePercentTopLeft.y, handlePercentBottomRight.y); }

		sizeChanges(shared_from_this());
	}

	double TextureHandle::percentLeft() const{
		return handlePercentTopLeft.x;
	}
	double TextureHandle::percentRight() const{
		return handlePercentBottomRight.x;
	}
	double TextureHandle::percentTop() const{
		return handlePercentTopLeft.y;
	}
	double TextureHandle::percentBottom() const{
		return handlePercentBottomRight.y;
	}

	void TextureHandle::setCorners(const Point<int> &a_topLeft, const Point<int> &a_bottomRight){
		handleSize = toSize(a_bottomRight - a_topLeft);
		handlePosition = a_topLeft;

		updatePercentBounds();
	}
	void TextureHandle::setCorners(const Point<double> &a_topLeft, const Point<double> &a_bottomRight){
		handlePercentSize = toSize(a_bottomRight - a_topLeft);
		handlePercentPosition = a_topLeft;

		updateIntegralBounds();
	}

	void TextureHandle::setBounds(const Point<int> &a_topLeft, const Size<int> &a_size){
		handleSize = a_size;
		handlePosition = a_topLeft;

		updatePercentBounds();
	}
	void TextureHandle::setBounds(const Point<double> &a_topLeft, const Size<double> &a_size){
		handlePercentSize = a_size;
		handlePercentPosition = a_topLeft;

		updateIntegralBounds();
	}

	void TextureHandle::setFlipX( bool a_flip ) {
		flipX = a_flip;
		updatePercentBounds();
	}

	void TextureHandle::setFlipY( bool a_flip ) {
		flipY = a_flip;
		updatePercentBounds();
	}

	bool TextureHandle::flippedX() const {
		return flipX;
	}

	bool TextureHandle::flippedY() const {
		return flipY;
	}

	Size<double> TextureHandle::percentSize() const {
		return handlePercentSize;
	}

	Point<double> TextureHandle::percentPosition() const {
		return handlePercentPosition;
	}

	Point<double> TextureHandle::percentTopLeft() const {
		return handlePercentTopLeft;
	}

	Point<double> TextureHandle::percentBottomRight() const {
		return handlePercentBottomRight;
	}

	/**********************\
	| ---SharedTextures--- |
	\**********************/

	std::shared_ptr<FileTextureDefinition> SharedTextures::getFileTexture( const std::string &a_filename, bool a_repeat ) {
		std::string identifier = a_filename + (a_repeat?"1":"0");
		auto foundDefinition = fileDefinitions.find(identifier);
		if(foundDefinition == fileDefinitions.end()){
			std::shared_ptr<FileTextureDefinition> newDefinition = FileTextureDefinition::make(a_filename, a_repeat);
			fileDefinitions[identifier] = newDefinition;
			return newDefinition;
		}else{
			return foundDefinition->second;
		}
	}

	std::shared_ptr<DynamicTextureDefinition> SharedTextures::getDynamicTexture( const std::string &a_name, const Size<int> &a_size ) {
		std::stringstream identifierMaker;
		identifierMaker << a_name << a_size;
		std::string identifier = identifierMaker.str();

		auto foundDefinition = dynamicDefinitions.find(identifier);
		if(foundDefinition == dynamicDefinitions.end()){
			std::shared_ptr<DynamicTextureDefinition> newDefinition = DynamicTextureDefinition::make(a_name, a_size, {0.0f, 0.0f, 0.0f, 0.0f});
			dynamicDefinitions[identifier] = newDefinition;
			return newDefinition;
		}else{
			return foundDefinition->second;
		}
	}

	std::shared_ptr<SurfaceTextureDefinition> SharedTextures::getSurfaceTexture(const std::string &a_identifier, std::function<SDL_Surface*()> a_surfaceGenerator) {
		auto foundDefinition = surfaceDefinitions.find(a_identifier);
		if(foundDefinition == surfaceDefinitions.end()){
			std::shared_ptr<SurfaceTextureDefinition> newDefinition = SurfaceTextureDefinition::make(a_identifier, a_surfaceGenerator);
			surfaceDefinitions[a_identifier] = newDefinition;
			return newDefinition;
		} else{
			return foundDefinition->second;
		}
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

	std::shared_ptr<DynamicTextureDefinition> SharedTextures::defaultTexture = nullptr;
	std::shared_ptr<TextureHandle> SharedTextures::defaultHandle = nullptr;


}
