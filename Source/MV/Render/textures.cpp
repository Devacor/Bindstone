#include "textures.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <future>
#include <set>
#include <SDL_image.h>
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/threadPool.hpp"
#include "MV/Render/points.h"
#include "sharedTextures.h"

#ifndef GL_BGR
	#define GL_BGR 0x80E0
#endif

#ifndef GL_BGRA
	#define GL_BGRA 0x80E1
#endif

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/serialization/polymorphic.hpp>
static jai::registrar<MV::FileTextureDefinition, MV::Services> _regFileTexDef("FileTextureDefinition");
static jai::registrar<MV::DynamicTextureDefinition, MV::Services> _regDynTexDef("DynamicTextureDefinition");
static jai::registrar<MV::SurfaceTextureDefinition, MV::Services> _regSurfTexDef("SurfaceTextureDefinition");
static jai::registrar<MV::TextureHandle, MV::Services> _regTexHandle("TextureHandle");
static jai::serialization::register_relation<MV::FileTextureDefinition, MV::TextureDefinition> _relFileTexDef;
static jai::serialization::register_relation<MV::DynamicTextureDefinition, MV::TextureDefinition> _relDynTexDef;
static jai::serialization::register_relation<MV::SurfaceTextureDefinition, MV::TextureDefinition> _relSurfTexDef;

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
		if (img->format->format == SDL_PIXELFORMAT_ABGR8888 && img->format->BytesPerPixel == 4) {
			return GL_RGBA;
		}

		if (img->format->BytesPerPixel == 4) {	  // contains an alpha channel
			if (img->format->Rmask == 0x000000ff) {
				return GL_RGBA;
			} else {
				return GL_BGRA;
			}
		} else if (img->format->BytesPerPixel == 3) {	  // no alpha channel
			if (img->format->Rmask == 0x000000ff) {
				return GL_RGB;
			} else {
				return GL_BGR;
			}
		}
		return 0;
	}

	GLenum getInternalTextureFormat(SDL_Surface* img) {
		if (img->format->BytesPerPixel == 4) {	  // contains an alpha channel
			return GL_RGBA;
		} else if (img->format->BytesPerPixel == 3) {	  // no alpha channel
			return GL_RGB;
		}
		return 0;
	}

	std::shared_ptr<OwnedSurface> normalizeSurface(const std::shared_ptr<OwnedSurface> &a_img, bool convertPowerOfTwo) {
		if (convertPowerOfTwo) {
			return convertToPowerOfTwoSurface(a_img);
		} else {
			return convertToBGRSurface(a_img);
		}
	}

	std::shared_ptr<OwnedSurface> convertToPowerOfTwoSurface(const std::shared_ptr<OwnedSurface>& a_img) {
		static const auto FORMAT = SDL_PIXELFORMAT_ABGR8888;
		if (a_img != nullptr && a_img->get() != nullptr && (!isPowerOfTwo(a_img->get()->w) || !isPowerOfTwo(a_img->get()->h) || a_img->get()->format->format != FORMAT)) {
			int widthPowerOfTwo = roundUpPowerOfTwo(a_img->get()->w);
			int heightPowerOfTwo = roundUpPowerOfTwo(a_img->get()->h);
			int bpp;
			Uint32 Rmask, Gmask, Bmask, Amask;
			SDL_PixelFormatEnumToMasks(FORMAT, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
			auto surface = OwnedSurface::make(SDL_CreateRGBSurface(0, widthPowerOfTwo, heightPowerOfTwo, bpp, Rmask, Gmask, Bmask, Amask));
			
			SDL_SetSurfaceBlendMode(a_img->get(), SDL_BLENDMODE_NONE);
			require<ResourceException>(SDL_BlitSurface(a_img->get(), 0, surface->get(), 0) == 0, "SDL_BlitSurface failed to copy!");

			return surface;
		}
		return a_img;
	}

	std::shared_ptr<OwnedSurface> convertToBGRSurface(const std::shared_ptr<OwnedSurface>& a_img) {
		static const auto FORMAT = SDL_PIXELFORMAT_ABGR8888;
		if (a_img != nullptr && a_img->get() != nullptr && a_img->get()->format->format != FORMAT) {
			int bpp;
			Uint32 Rmask, Gmask, Bmask, Amask;
			SDL_PixelFormatEnumToMasks(FORMAT, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
			auto surface = OwnedSurface::make(SDL_CreateRGBSurface(0, a_img->get()->w, a_img->get()->h, bpp, Rmask, Gmask, Bmask, Amask));

			SDL_SetSurfaceBlendMode(a_img->get(), SDL_BLENDMODE_NONE);
			require<ResourceException>(SDL_BlitSurface(a_img->get(), 0, surface->get(), 0) == 0, "SDL_BlitSurface failed to copy!");

			return surface;
		}
		return a_img;
	}

	TextureLoadProfile& textureLoadProfile() {
		static TextureLoadProfile profile;
		return profile;
	}

	// CPU-only image decode (no GL) — safe to run on the thread pool.
	std::shared_ptr<OwnedSurface> decodeTextureSurface(const TextureParameters &a_parameters) {
		SDL_RWops* sdlIO = sdlFileHandle(a_parameters.path);
		if (!sdlIO) {
			MV::warning("Failed to load image [", a_parameters.path, "]");
			return nullptr;
		}
		++textureLoadProfile().loadFileCalls;
		auto decodeStart = std::chrono::steady_clock::now();
		SDL_Surface *img = IMG_Load_RW(sdlIO, 1);
		textureLoadProfile().decodeMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - decodeStart).count();
		if (!img) {
			MV::error("Failed to load image [", a_parameters.path, "] [", SDL_GetError(), "]");
			return nullptr;
		}
		return OwnedSurface::make(img);
	}

	std::unique_ptr<LoadedTextureData> LoadedTexture::loadFile(const TextureParameters &a_parameters, Render::Device* a_device) {
		if (a_parameters.cleared) {
			warning("Warning: Passing in a cleared texture parameter! [", a_parameters.path, "]");
		}

		MV::info("Loading Image: ", a_parameters.path);
		auto surface = decodeTextureSurface(a_parameters);
		if (!surface) { return {}; }
		return loadTextureFromSurface(surface, a_parameters, a_device);
	}

	bool LoadedTexture::isCached(const TextureParameters &a_parameters) {
		std::scoped_lock guard(lock);
		return globalLookup.find(a_parameters) != globalLookup.end();
	}

	bool LoadedTexture::deviceBackInPlace(const TextureParameters &a_parameters, Render::Device* a_device) {
		if (!a_device || RUNNING_IN_HEADLESS) { return false; }
		std::scoped_lock guard(lock);
		auto found = globalLookup.find(a_parameters);
		if (found == globalLookup.end() || !found->second) { return false; } // not cached -> caller load()s fresh
		LoadedTextureData* existing = found->second.get();
		// Upgrade the shared entry in place from its retained surface; every holder shares it, nothing is erased.
		existing->bindToDevice(a_device);
		return existing->boundTexture.valid();
	}

	void LoadedTexture::cacheDecodedSurface(const TextureParameters &a_parameters, const std::shared_ptr<OwnedSurface> &a_surface, Render::Device* a_device) {
		if (!a_surface || !a_surface->get()) { return; }
		std::scoped_lock guard(lock);
		if (globalLookup.find(a_parameters) != globalLookup.end()) { return; }
		auto data = loadTextureFromSurface(a_surface, a_parameters, a_device);
		if (data) {
			globalLookup.insert({ a_parameters, std::move(data) });
		}
	}

	GLuint SharedTextures::loadingPlaceholderId() {
		if (RUNNING_IN_HEADLESS) { return 0; }
		// Singleton 2x2 magenta/black checker, created once on the GL thread. Nearest-filtered
		// and repeating so a stretched sprite shows obvious "still loading" squares.
		static GLuint checkerId = 0;
		if (checkerId == 0) {
			const uint32_t magenta = 0xFFFF00FFu;   // RGBA bytes FF 00 FF FF (little-endian)
			const uint32_t black   = 0xFF000000u;   // RGBA bytes 00 00 00 FF
			uint32_t pixels[4] = { magenta, black, black, magenta };
			glGenTextures(1, &checkerId);
			glBindTexture(GL_TEXTURE_2D, checkerId);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
		}
		return checkerId;
	}

	void SharedTextures::loadFile(const std::shared_ptr<FileTextureDefinition>& a_definition, bool a_blocking, std::function<void()> a_onLoaded) {
		if (!a_definition) { return; }
		if (a_definition->loaded()) { if (a_onLoaded) { a_onLoaded(); } return; }

		const TextureParameters params = a_definition->fileTextureParameters();
		const bool canStream = !a_blocking && ourLoadThreadPool && !RUNNING_IN_HEADLESS && !LoadedTexture::isCached(params);

		if (!canStream) {
			a_definition->load();
			if (a_onLoaded) { a_onLoaded(); }
			return;
		}

		a_definition->loadPending = true;
		++ourPendingTextureLoads;

		// Coalesce by params: register this definition as a waiter. If a decode for the same
		// image is already in flight, just wait for it (no redundant decode); otherwise kick it.
		auto& waiters = ourInFlightLoads[params];
		waiters.push_back(PendingTextureLoad{ a_definition, std::move(a_onLoaded) });
		if (waiters.size() > 1) {
			return;
		}

		auto surface = std::make_shared<std::shared_ptr<OwnedSurface>>();
		SharedTextures* self = this;
		ourLoadThreadPool->task(ThreadPool::Job(
			[params, surface]() {
				try { *surface = decodeTextureSurface(params); } catch (...) { *surface = nullptr; }
			},
			[self, params, surface]() {
				// Main thread: upload through the active RHI device (matches the synchronous path).
				LoadedTexture::cacheDecodedSurface(params, *surface, self->device());
				auto it = self->ourInFlightLoads.find(params);
				if (it == self->ourInFlightLoads.end()) { return; }
				std::vector<PendingTextureLoad> waiters = std::move(it->second);
				self->ourInFlightLoads.erase(it);
				for (auto& waiter : waiters) {
					waiter.definition->loadPending = false;
					waiter.definition->renderDevice(self->device());   // ensure the waiter resolves via the device too
					waiter.definition->load();   // globalLookup hit -> sets loadedTexture, fires onReload
					--self->ourPendingTextureLoads;
					if (waiter.onLoaded) { waiter.onLoaded(); }
				}
			},
			ThreadPool::Job::Continue::MAIN_THREAD));
	}

	GLuint LoadedTextureData::id() const {
		// Device path: native GL name behind the BoundTexture (transition shim). Legacy: owned name.
		if (renderDevice && boundTexture.valid()) {
			return static_cast<GLuint>(renderDevice->nativeTextureId(boundTexture));
		}
		return textureId.id();
	}
	LoadedTextureData::~LoadedTextureData() {
		// Only the device-backed handle frees here; the legacy OpenGlTextureId member frees via its own dtor.
		if (renderDevice && boundTexture.valid()) {
			renderDevice->destroyTexture(boundTexture);
		}
	}

	void LoadedTexture::releaseDeviceBackedEntries(Render::Device* a_device) {
		if (!a_device) { return; }
		std::scoped_lock guard(lock);
		for (auto&& entry : globalLookup) {
			auto& data = entry.second;
			if (data && data->renderDevice == a_device) {
				if (data->boundTexture.valid()) {
					a_device->destroyTexture(data->boundTexture);
				}
				data->boundTexture = Render::BoundTexture();
				data->renderDevice = nullptr;
			}
		}
	}

	static Render::PixelFormat rhiFormatForSurface(SDL_Surface* a_surface); // defined below

	void LoadedTextureData::bindToDevice(Render::Device* a_device) {
		if (!a_device || RUNNING_IN_HEADLESS) { return; }
		if (boundTexture.valid() && renderDevice == a_device) { return; } // already device-backed
		if (!sourceSurface || !sourceSurface->get()) { return; }          // no retained data to bind from

		SDL_Surface* surf = sourceSurface->get();
		Render::TextureDesc desc;
		desc.width = surf->w;
		desc.height = surf->h;
		desc.format = rhiFormatForSurface(surf);
		desc.dimension = Render::TextureDimension::Tex2D;
		desc.usage = Render::TextureUsage::Sampled | Render::TextureUsage::CopyDst;

		auto uploadStart = std::chrono::steady_clock::now();
		Render::BoundTexture bound = a_device->createTexture(desc, surf->pixels);
		auto& prof = textureLoadProfile();
		prof.uploadMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - uploadStart).count();
		++prof.uploadCalls;
		prof.uploadBytes += static_cast<int64_t>(surf->w) * surf->h * 4;

		Render::SamplerDesc samplerDesc;
		samplerDesc.min = samplerDesc.mag = sourceParameters.pixel ? Render::Filter::Nearest : Render::Filter::Linear;
		samplerDesc.mip = Render::MipFilter::None;
		samplerDesc.u = samplerDesc.v = sourceParameters.repeat ? Render::AddressMode::Repeat : Render::AddressMode::ClampToEdge;

		renderDevice = a_device;
		boundTexture = bound;
		boundSampler = a_device->createSampler(samplerDesc);
		textureId = OpenGlTextureId(0u);   // drop any legacy GL name
		sourceSurface.reset();             	}

	void LoadedTextureData::uploadLegacyGl(SDL_Surface* a_surf, const TextureParameters& a_parameters) {
		glBindTexture(GL_TEXTURE_2D, textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (a_parameters.pixel) ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (a_parameters.pixel) ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (a_parameters.repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (a_parameters.repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

		auto uploadStart = std::chrono::steady_clock::now();
		glTexImage2D(GL_TEXTURE_2D, 0, getInternalTextureFormat(a_surf), a_surf->w, a_surf->h, 0, getTextureFormat(a_surf), GL_UNSIGNED_BYTE, a_surf->pixels);
		auto& prof = textureLoadProfile();
		prof.uploadMicros += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - uploadStart).count();
		++prof.uploadCalls;
		prof.uploadBytes += static_cast<int64_t>(a_surf->w) * a_surf->h * 4;
	}

	static Render::PixelFormat rhiFormatForSurface(SDL_Surface* a_surface) {
		return (getTextureFormat(a_surface) == GL_BGRA) ? Render::PixelFormat::BGRA8_UNORM
		                                                : Render::PixelFormat::RGBA8_UNORM;
	}

	//Load an opengl texture
	std::unique_ptr<LoadedTextureData> loadTextureFromSurface(const std::shared_ptr<OwnedSurface> &a_img, const TextureParameters &a_parameters, Render::Device* a_device) {
		if (a_img == nullptr || a_img->get() == nullptr) {
			error("ERROR: loadTextureFromSurface was provided a null SDL_Surface!");
			return {};
		}

		GLenum textureFormat = getTextureFormat(a_img->get());
		if (!textureFormat) {
			error("Unable to determine texture format!");
			return {};
		}
		auto surfaceToWorkWith = normalizeSurface(a_img, a_parameters.powerTwo);

		// Retain the normalized surface so a later device back-fill (bindToDevice) never re-decodes the file.
		std::unique_ptr<LoadedTextureData> results =
			RUNNING_IN_HEADLESS ? std::make_unique<LoadedTextureData>(0) : std::make_unique<LoadedTextureData>();

		results->originalSize.width = a_img->get()->w;
		results->originalSize.height = a_img->get()->h;
		results->size.width = surfaceToWorkWith->get()->w;
		results->size.height = surfaceToWorkWith->get()->h;
		results->sourceSurface = surfaceToWorkWith;
		results->sourceParameters = a_parameters;

		if (a_device && !RUNNING_IN_HEADLESS) {
			results->bindToDevice(a_device);
		} else if (!RUNNING_IN_HEADLESS) {
			results->uploadLegacyGl(surfaceToWorkWith->get(), a_parameters); // transitional raw-GL upload
		}
		return results;
	}

	/*************************\
	| ---TextureDefinition--- |
	\*************************/

	TextureDefinition::TextureDefinition(const std::string &a_name, bool a_isShared) :
		isShared(a_isShared),
		onReload(onReloadAction),
		textureName(a_name) {
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
		if (loadedTexture) { return loadedTexture->id(); }
		if (loadPending) { return SharedTextures::loadingPlaceholderId(); }
		return 0;
	}

	Render::BoundTexture TextureDefinition::boundTexture() const {
		return loadedTexture ? loadedTexture->bound() : Render::BoundTexture{};
	}

	void TextureDefinition::ensureDeviceBacked(Render::Device* a_device) {
		// Back-fill a BoundTexture at draw time when the device appears after the texture loaded. Idempotent once bound.
		if (!a_device || RUNNING_IN_HEADLESS) { return; }
		if (loadedTexture && loadedTexture->bound().valid() && ourDevice == a_device) { return; }
		ourDevice = a_device;
		if (!loaded()) { return; }

		if (deviceBackSharedCache(a_device)) {
			if (isShared) { onReloadAction(shared_from_this()); }
			return;
		}

		if (loadedTexture && loadedTexture->data().sourceSurface) {
			loadedTexture->data().bindToDevice(a_device);
			if (isShared) { onReloadAction(shared_from_this()); }
			return;
		}

		loadedTexture.reset();
		reloadImplementation();
		if (isShared) { onReloadAction(shared_from_this()); }
	}

	Render::BoundSampler TextureDefinition::boundSampler() const {
		return loadedTexture ? loadedTexture->sampler() : Render::BoundSampler{};
	}

	bool TextureDefinition::loaded() const {
		return textureId() != 0;
	}

	Size<int> TextureDefinition::size() const {
		require<ResourceException>(loaded(), "size:: The texture hasn't actually loaded yet.  You may need to create a handle to implicitly force a texture load.");
		return textureSize;
	}

	void TextureDefinition::cleanupOpenglTexture() {
		// reset() (NOT release()): runs ~LoadedTexture so the shared-cache refcount drops and the GPU texture frees.
		loadedTexture.reset();
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
		// The ctor stores a_bounds as the percent directly; calling bounds(a_bounds) here
		// re-interpreted the percent box as PIXELS (x contentSize / size) - double-applying
		// the half-texel inset, and corrupting handlePercent to +/-INF when the texture
		// size isn't loaded yet (headless tools).
		auto handle = std::shared_ptr<TextureHandle>(new TextureHandle(shared_from_this(), a_bounds));
		handles.push_back(handle);
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
	}

	void TextureDefinition::save(const std::string &a_fileName) {
		if (RUNNING_IN_HEADLESS) { return; }

		if (!loaded()) {
			load();
			saveLoadedTexture(a_fileName, textureId(), textureSize.width, textureSize.height);
			cleanupOpenglTexture();
		} else {
			saveLoadedTexture(a_fileName, textureId(), textureSize.width, textureSize.height);
		}
	}

	void TextureDefinition::reload() {
		if (RUNNING_IN_HEADLESS) { return; }

		if (loaded()) {
			load();
		}
	}


	/*****************************\
	| ---FileTextureDefinition--- |
	\*****************************/

	void FileTextureDefinition::reloadImplementation() {
		loadedTexture = std::make_unique<LoadedTexture>(TextureParameters{ textureName, powerTwo, repeat, pixel }, ourDevice);
		if (loadedTexture->id() != 0 || loadedTexture->bound().valid()) {
			textureSize = loadedTexture->data().size;
			desiredSize = loadedTexture->data().originalSize;
		} else {
			loadedTexture.reset();
		}
	}

	/********************************\
	| ---DynamicTextureDefinition--- |
	\********************************/

	void DynamicTextureDefinition::reloadImplementation() {
		textureSize.width = roundUpPowerOfTwo(textureSize.width);
		textureSize.height = roundUpPowerOfTwo(textureSize.height);
		if (RUNNING_IN_HEADLESS) { return; }

		std::vector<unsigned char> pixels(static_cast<size_t>(textureSize.width) * textureSize.height * 4);
		memset(pixels.data(), backgroundColor.hex(), pixels.size() * sizeof(unsigned char));

		if (ourDevice) {
			Render::TextureDesc desc;
			desc.width = textureSize.width;
			desc.height = textureSize.height;
			desc.format = Render::PixelFormat::RGBA8_UNORM;
			desc.dimension = Render::TextureDimension::Tex2D;
			desc.usage = Render::TextureUsage::Sampled | Render::TextureUsage::CopyDst | Render::TextureUsage::ColorTarget;
			Render::BoundTexture bound = ourDevice->createTexture(desc, pixels.data());

			Render::SamplerDesc samplerDesc; // dynamic textures used LINEAR + CLAMP_TO_EDGE in the legacy path.
			Render::BoundSampler sampler = ourDevice->createSampler(samplerDesc);

			auto data = std::make_unique<LoadedTextureData>(ourDevice, bound, sampler);
			data->originalSize = { desiredSize.width, desiredSize.height };
			data->size = { textureSize.width, textureSize.height };
			loadedTexture = std::make_unique<LoadedTexture>(std::move(data));
			return;
		}

		// Legacy raw-GL path (no device; transitional).
		loadedTexture = std::make_unique<LoadedTexture>(TextureParameters());
		loadedTexture->data().originalSize.width = desiredSize.width;
		loadedTexture->data().originalSize.height = desiredSize.height;
		loadedTexture->data().size.width = textureSize.width;
		loadedTexture->data().size.height = textureSize.height;

		glBindTexture(GL_TEXTURE_2D, loadedTexture->id());			// Bind The Texture
		// Build Texture Using Information In data
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureSize.width, textureSize.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
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
		if (!surfaceGenerator) {
			return; // deserialized without an owner reattaching via setSurfaceGenerator (stale asset glyphs)
		}
		auto newSurface = surfaceGenerator();
		require<PointerException>(newSurface != nullptr && newSurface->get() != nullptr, "SurfaceTextureDefinition::reloadImplementation was passed a null SDL_Surface pointer.");

		generatedSurfaceSize = Size<int>(newSurface->get()->w, newSurface->get()->h);

		loadedTexture = std::make_unique<LoadedTexture>(loadTextureFromSurface(newSurface, TextureParameters(true, false, false), ourDevice));
		if (loadedTexture->id() != 0 || loadedTexture->bound().valid()) {
			textureSize = loadedTexture->data().size;
			desiredSize = loadedTexture->data().originalSize;
		} else {
			loadedTexture.reset();
		}
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
			if (a_sharedTextures) {
				// Route GPU upload through the active device (the definition may have been created via
				// a path that didn't inject it — e.g. deserialization).
				if (textureDefinition && !textureDefinition->renderDevice()) {
					textureDefinition->renderDevice(a_sharedTextures->device());
				}
				if (auto fileDefinition = std::dynamic_pointer_cast<FileTextureDefinition>(textureDefinition)) {
					a_sharedTextures->loadFile(fileDefinition);
					return;
				}
			}
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
		if(a_points.size() >= 4){
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

		std::vector<uint8_t> pixels(a_width*a_height * 4);
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

	std::mutex LoadedTexture::lock;
	std::map<MV::TextureParameters, std::unique_ptr<LoadedTextureData>> LoadedTexture::globalLookup;

}
