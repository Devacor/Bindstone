#ifndef _MV_TEXTURES_H_
#define _MV_TEXTURES_H_

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <atomic>

#include "render.h"
#include "device.h"   // Render::Device / Render::BoundTexture (held by value in LoadedTextureData)
#include "boxaabb.h"

#include <jaiscript/signals/signal_decl.hpp>
#include "MV/Serialization/serialize.h"

namespace MV {
	class SharedTextures;
	class TextureHandle;
	struct LoadedTextureData;
	struct TextureParameters;

	GLenum getTextureFormat(SDL_Surface* img);
	GLenum getInternalTextureFormat(SDL_Surface* img);

	void saveLoadedTexture(const std::string &a_fileName, GLuint a_texture, GLuint a_width, GLuint a_height);

	//Requires a 4 point vector. IE: Shrink to 4 before calling this if you use 9-slicing etc.
	bool clearTexturePoints(std::vector<DrawPoint> &a_points);

	struct OwnedSurface {
	public:
		static std::shared_ptr<OwnedSurface> make(SDL_Surface* a_surface) {
			return std::shared_ptr<OwnedSurface>(new OwnedSurface(a_surface));
		}
		~OwnedSurface() {
			if (ourSurface != nullptr) {
				SDL_FreeSurface(ourSurface);
			}
		}
		SDL_Surface* get() {
			return ourSurface;
		}
	private:
		SDL_Surface* ourSurface;
		OwnedSurface(SDL_Surface* a_surface) :ourSurface(a_surface) {}
		OwnedSurface() = delete;
		OwnedSurface(const OwnedSurface&) = delete;
		OwnedSurface& operator=(const OwnedSurface&) = delete;
	};

	//converting to a power of two surface may free the original surface, but returns an unfreed surface.
	std::shared_ptr<OwnedSurface> convertToPowerOfTwoSurface(const std::shared_ptr<OwnedSurface> &a_img);
	std::shared_ptr<OwnedSurface> convertToBGRSurface(const std::shared_ptr<OwnedSurface> &a_img);
	// a_device: upload the GPU texture through this Render::Device; null keeps the legacy raw-GL path.
	std::unique_ptr<LoadedTextureData> loadTextureFromSurface(const std::shared_ptr<OwnedSurface> &img, const TextureParameters &file, Render::Device* a_device = nullptr);

	//required to allow forward declared MV::SharedTextures
	MV::SharedTextures* getSharedTextureFromServices(MV::Services& a_services);

	// Canonicalize a texture path (strip leading "Assets/", normalize separators) so different
	// spellings of the same file dedupe to one GPU texture in globalLookup. Case preserved so the
	// on-disk open path stays valid via sdlFileHandle()'s "Assets/" fallback.
	inline std::string normalizeTexturePath(std::string a_path) {
		for (auto &c : a_path) { if (c == '\\') c = '/'; }
		static const char prefix[] = "assets/";
		if (a_path.size() >= 7) {
			bool match = true;
			for (size_t i = 0; i < 7; ++i) {
				char a = a_path[i];
				if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
				if (a != prefix[i]) { match = false; break; }
			}
			if (match) { a_path.erase(0, 7); }
		}
		return a_path;
	}

	// Lightweight scene-load texture profiling (decode vs GL upload). Atomic because decode
	// now runs on the thread pool (parallel preload); near-zero cost when unused.
	struct TextureLoadProfile {
		std::atomic<int64_t> loadFileCalls{ 0 };   // == decodes (cache misses)
		std::atomic<int64_t> uploadCalls{ 0 };     // glTexImage2D calls (non-headless)
		std::atomic<int64_t> uploadBytes{ 0 };     // total RGBA bytes uploaded
		std::atomic<int64_t> decodeMicros{ 0 };    // summed IMG_Load_RW time (CPU, across threads)
		std::atomic<int64_t> uploadMicros{ 0 };    // summed glTexImage2D time (main thread)
		void reset() { loadFileCalls = 0; uploadCalls = 0; uploadBytes = 0; decodeMicros = 0; uploadMicros = 0; }
	};
	TextureLoadProfile& textureLoadProfile();

	// Decodes an image file into an SDL surface (CPU only, thread-safe — no GL). Split out of
	// LoadedTexture::loadFile so the texture system can decode many files in parallel and then
	// upload them on the GL thread.
	std::shared_ptr<OwnedSurface> decodeTextureSurface(const TextureParameters& a_parameters);

	struct TextureParameters {
		TextureParameters(std::string a_path, bool a_powerTwo, bool a_repeat, bool a_pixel):
			path(normalizeTexturePath(std::move(a_path))),
			powerTwo(a_powerTwo),
			repeat(a_repeat),
			pixel(a_pixel),
			cleared(false){
		}

		TextureParameters(bool a_powerTwo, bool a_repeat, bool a_pixel) :
			powerTwo(a_powerTwo),
			repeat(a_repeat),
			pixel(a_pixel),
			cleared(false){
		}
		TextureParameters() : cleared(true) {}

		void clear() {
			cleared = true;
		}

		void set(std::string a_path, bool a_powerTwo, bool a_repeat, bool a_pixel) {
			path = normalizeTexturePath(std::move(a_path));
			powerTwo = a_powerTwo;
			repeat = a_repeat;
			pixel = a_pixel;
			cleared = false;
		}

		void set(bool a_powerTwo, bool a_repeat, bool a_pixel) {
			powerTwo = a_powerTwo;
			repeat = a_repeat;
			pixel = a_pixel;
			cleared = false;
		}

		auto tie() const {
			return std::tie(path, powerTwo, repeat, pixel);
		}

		bool operator<(const TextureParameters &a_rhs) const {
			return tie() < a_rhs.tie();
		}

		bool operator==(const TextureParameters &a_rhs) const {
			return cleared ? false : tie() == a_rhs.tie();
		}
		bool operator!=(const TextureParameters &a_rhs) const {
			return cleared ? true : tie() != a_rhs.tie();
		}

		std::string path;
		bool powerTwo = true;
		bool repeat = false;
		bool pixel = false;

		bool cleared = false;
	};

	class OpenGlTextureId {
	public:
		OpenGlTextureId() {
			glGenTextures(1, &textureId);
		}
		OpenGlTextureId(GLuint a_textureId) :
			textureId(a_textureId) {
		}
		OpenGlTextureId(OpenGlTextureId&& a_rhs) :
			textureId(a_rhs.textureId){
			a_rhs.textureId = 0;
		}
		OpenGlTextureId& operator=(OpenGlTextureId&& a_rhs) noexcept {
			if (this != &a_rhs) {
				if (textureId != 0) { glDeleteTextures(1, &textureId); }
				textureId = a_rhs.textureId;
				a_rhs.textureId = 0;
			}
			return *this;
		}
		~OpenGlTextureId() {
			if (textureId != 0) {
				glDeleteTextures(1, &textureId);
			}
		}

		operator GLuint() const {
			return textureId;
		}
		inline GLuint id() const {
			return textureId;
		}
	private:
		OpenGlTextureId(const OpenGlTextureId& a_rhs) = delete;
		OpenGlTextureId& operator=(const OpenGlTextureId& a_rhs) = delete;

		GLuint textureId = 0;
	};

	class LoadedTexture;
	// A texture's GPU object: a device BoundTexture, or a legacy raw-GL name until a device exists. id() resolves whichever is active.
	struct LoadedTextureData {
		friend LoadedTexture;
	public:
		LoadedTextureData() {}
		LoadedTextureData(GLuint a_textureId) : textureId(a_textureId){}
		LoadedTextureData(Render::Device* a_device, Render::BoundTexture a_bound, Render::BoundSampler a_sampler = {}) :
			renderDevice(a_device), boundTexture(a_bound), boundSampler(a_sampler) {}
		LoadedTextureData(LoadedTextureData&& a_rhs) = default;
		~LoadedTextureData();

		GLuint id() const;
		operator GLuint() const { return id(); }

		Render::BoundTexture bound() const { return boundTexture; }
		Render::BoundSampler sampler() const { return boundSampler; }

		// Build the GPU binding from the retained surface (no re-decode), then release it. No-op without
		// a device, if already device-backed, or if no surface was retained.
		void bindToDevice(Render::Device* a_device);
		void uploadLegacyGl(SDL_Surface* a_surf, const TextureParameters& a_parameters);

		OpenGlTextureId textureId;                  // legacy raw-GL path only
		Render::Device* renderDevice = nullptr;
		Render::BoundTexture boundTexture;
		Render::BoundSampler boundSampler;
		Size<int> size;
		Size<int> originalSize;

		// Decoded pixels retained so the binding rebuilds without re-reading the file; released once
		// device-backed. Null for raw-pixel textures (dynamic/atlas).
		std::shared_ptr<OwnedSurface> sourceSurface;
		TextureParameters sourceParameters;

	private:
		LoadedTextureData(const LoadedTextureData& a_rhs) = delete;
		LoadedTextureData& operator=(const LoadedTextureData& a_rhs) = delete;

		std::atomic<int> useCount = 0;   // live holders of the shared globalLookup entry; last one erases it.
	};

	class LoadedTexture {
	public:
		// a_device: upload the GPU texture through this device; null keeps the legacy raw-GL path.
		LoadedTexture(const TextureParameters& a_parameters, Render::Device* a_device = nullptr) {
			if (a_parameters.cleared) {
				locallyOwnedParametersValue = std::make_unique<TextureParameters>(a_parameters);
				locallyOwnedDataValue = std::make_unique<LoadedTextureData>();
			} else {
				std::scoped_lock guard(lock);
				auto found = globalLookup.find(a_parameters);
				if (found == globalLookup.end()) {
					bool success = false;
					std::tie(found, success) = globalLookup.insert({ a_parameters, std::move(loadFile(a_parameters, a_device)) });
				}
				// Guard: a failed load() can insert a null unique_ptr.
				if (found->second) { ++found->second->useCount; }
				dataValue = found->second.get();
				parametersValue = &found->first;
			}
		}
		LoadedTexture(std::unique_ptr<LoadedTextureData> a_locallyOwnedDataValue) :
			locallyOwnedDataValue(a_locallyOwnedDataValue.release()){
		}
		// Null the source's shared-cache pointers so its dtor doesn't double-decrement the entry this now owns.
		LoadedTexture(LoadedTexture&& a_rhs) noexcept :
			dataValue(a_rhs.dataValue),
			parametersValue(a_rhs.parametersValue),
			locallyOwnedDataValue(std::move(a_rhs.locallyOwnedDataValue)),
			locallyOwnedParametersValue(std::move(a_rhs.locallyOwnedParametersValue)) {
			a_rhs.dataValue = nullptr;
			a_rhs.parametersValue = nullptr;
		}
		~LoadedTexture() {
			// Only the shared-cache path refcounts; locally-owned textures leave dataValue null.
			if (!locallyOwnedDataValue && dataValue) {
				if (--dataValue->useCount == 0) {
					std::scoped_lock guard(lock);
					globalLookup.erase(parameters());
				}
			}
		}

		const TextureParameters& parameters() const {
			return locallyOwnedParametersValue ? *locallyOwnedParametersValue : *parametersValue;
		}
		LoadedTextureData& data() {
			return locallyOwnedDataValue ? *locallyOwnedDataValue : *dataValue;
		}
		GLuint id() const {
			return locallyOwnedDataValue ? locallyOwnedDataValue->id() : dataValue->id();
		}
		Render::BoundTexture bound() const {
			return locallyOwnedDataValue ? locallyOwnedDataValue->bound() : dataValue->bound();
		}
		Render::BoundSampler sampler() const {
			return locallyOwnedDataValue ? locallyOwnedDataValue->sampler() : dataValue->sampler();
		}

		// Parallel-preload support. cacheDecodedSurface uploads an already-decoded surface on the
		// calling (GL) thread into globalLookup, so a later LoadedTexture(params) is a hit, not a re-decode.
		static bool isCached(const TextureParameters& a_parameters);
		static void cacheDecodedSurface(const TextureParameters& a_parameters, const std::shared_ptr<OwnedSurface>& a_surface, Render::Device* a_device = nullptr);
		// Device-back the shared cache entry for these params in place (every live holder upgrades at
		// once; nothing erased, so no raw dataValue is invalidated). No-op if absent or already backed.
		static bool deviceBackInPlace(const TextureParameters& a_parameters, Render::Device* a_device);
	private:
		LoadedTexture(const LoadedTexture&) = delete;
		LoadedTexture& operator=(const LoadedTexture&) = delete;

		std::unique_ptr<LoadedTextureData> loadFile(const TextureParameters& a_parameters, Render::Device* a_device);
		//static void releaseFile(const TextureParameters& a_parameters);
		
		LoadedTextureData* dataValue = nullptr;          // shared-cache path only (null when locally-owned).
		const TextureParameters* parametersValue = nullptr;
		std::unique_ptr<LoadedTextureData> locallyOwnedDataValue;
		std::unique_ptr<TextureParameters> locallyOwnedParametersValue;
		static std::mutex lock;
		static std::map<TextureParameters, std::unique_ptr<LoadedTextureData>> globalLookup;
	};


	class TextureDefinition : public std::enable_shared_from_this<TextureDefinition> {
		friend cereal::access;
		friend jai::access;
		friend class jai::serialization::access;
		friend class FileTextureDefinition;
		friend class DynamicTextureDefinition;
		friend class SurfaceTextureDefinition;
		friend class SharedTextures;   // drives streaming load + the loading-placeholder state
	protected:
		jai::signal_emitter<void(std::shared_ptr<TextureDefinition>)> onReloadAction;
	public:
		// True while streaming-loading; textureId() then reports the loading-placeholder checker.
		bool isLoadPending() const { return loadPending; }

		jai::signal<void(std::shared_ptr<TextureDefinition>)> onReload;
		typedef jai::receiver<void(std::shared_ptr<TextureDefinition>)> SignalType;

		virtual ~TextureDefinition();
		std::shared_ptr<TextureHandle> makeHandle();
		std::shared_ptr<TextureHandle> makeHandle(const BoxAABB<int> &a_bounds);
		std::shared_ptr<TextureHandle> makeHandle(const BoxAABB<PointPrecision> &a_bounds);
		void registerHandle(std::weak_ptr<TextureHandle> a_newHandle);
		std::shared_ptr<TextureHandle> makeRawHandle(const BoxAABB<PointPrecision> &a_bounds);

		GLuint textureId() const;
		// The GPU texture as an opaque RHI handle (textureId() is the transition shim). Null until
		// loaded, or when loaded via the legacy raw-GL path.
		Render::BoundTexture boundTexture() const;
		Render::BoundSampler boundSampler() const;
		// Borrowed; injected by SharedTextures so uploads route through the active device (null = legacy raw-GL).
		void renderDevice(Render::Device* a_device) { ourDevice = a_device; }
		Render::Device* renderDevice() const { return ourDevice; }
		// Draw-time back-fill when the device appears after load, so the RHI draw path can bind it. No-op once backed.
		void ensureDeviceBacked(Render::Device* a_device);
		std::string name() const;
		Size<int> size() const;
		Size<int> size();
		Size<int> contentSize() const;
		Size<int> contentSize();

		void scale(const Scale &a_newScale){
			logicalScale = a_newScale;
		}
		Scale scale() const{
			return logicalScale;
		}
		//bookkeeping
		bool loaded() const;
		void load();
		void unload();
		void unload(TextureHandle* a_toRemove);

		void reload();

		void save(const std::string &a_fileName);

	protected:
		virtual void cleanupOpenglTexture();
		TextureDefinition(const std::string &a_name, bool a_isShared = true);
		std::unique_ptr<LoadedTexture> loadedTexture;
		std::string textureName;
		Size<int> textureSize;
		Size<int> desiredSize;
		Scale logicalScale;

		std::vector< std::weak_ptr<TextureHandle> > handles;
		bool isShared;
		bool loadPending = false;   // streaming load in flight (set/cleared by SharedTextures)
		Render::Device* ourDevice = nullptr;   // borrowed; active RHI device for uploads (null = legacy raw-GL).

	private:
		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if constexpr (jai::serialization::jai_archive<Archive>) {
				archive(
					jai::serialization::make_nvp("name", textureName),
					jai::serialization::make_nvp("size", textureSize),
					jai::serialization::make_nvp("contentSize", desiredSize),
					jai::serialization::make_nvp("scale", logicalScale),
					JAI_NVP(handles)
				);
			} else {
				archive(
					cereal::make_nvp("name", textureName),
					cereal::make_nvp("size", textureSize),
					cereal::make_nvp("contentSize", desiredSize)
				);

				if(version > 0){
					archive(cereal::make_nvp("scale", logicalScale));
				}

				archive(
					CEREAL_NVP(handles)
				);
			}
		}

		virtual void reloadImplementation() = 0;
		// ensureDeviceBacked() hook: device-back this def's shared globalLookup entry in place (so sibling
		// holders upgrade too). Non-shared defs return false and let ensureDeviceBacked rebuild locally.
		virtual bool deviceBackSharedCache(Render::Device* /*a_device*/) { return false; }
	};

	class FileTextureDefinition : public TextureDefinition {
		friend cereal::access;
		friend jai::access;
		friend class jai::serialization::access;
	public:
		static std::shared_ptr<FileTextureDefinition> make(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false, bool a_pixel = false){
			return std::shared_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat, a_pixel));
		}
		static std::unique_ptr<FileTextureDefinition> makeUnmanaged(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false, bool a_pixel = false){
			return std::unique_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat, a_pixel, false));
		}

		// The exact key this definition loads under, so the parallel preload can decode it off-thread.
		TextureParameters fileTextureParameters() const {
			return TextureParameters{ textureName, powerTwo, repeat, pixel };
		}

	protected:
		FileTextureDefinition(const std::string &a_filename, bool a_powerTwo, bool a_repeat, bool a_pixel, bool a_isShared = true):
			TextureDefinition(a_filename, a_isShared),
			powerTwo(a_powerTwo),
			repeat(a_repeat),
			pixel(a_pixel){
		}

		void reloadImplementation() override;
		// Device-back this file's shared cache entry in place (upgrades every holder at once).
		bool deviceBackSharedCache(Render::Device* a_device) override {
			return LoadedTexture::deviceBackInPlace(
				TextureParameters{ textureName, powerTwo, repeat, pixel }, a_device);
		}

	private:

		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if constexpr (jai::serialization::jai_archive<Archive>) {
				archive(JAI_NVP(powerTwo), JAI_NVP(repeat), JAI_NVP(pixel));
				TextureDefinition::serialize(archive, version);
			} else {
				archive(CEREAL_NVP(powerTwo), CEREAL_NVP(repeat), CEREAL_NVP(pixel), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
			}
		}

		template<class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<FileTextureDefinition> &construct, std::uint32_t const /*version*/){
			bool repeat = false;
			bool pixel = false;
			bool powerTwo = true;

			archive(cereal::make_nvp("powerTwo", powerTwo), cereal::make_nvp("repeat", repeat), cereal::make_nvp("pixel", pixel));

			MV::Services& services = cereal::get_user_data<MV::Services>(archive);

			construct("", powerTwo, repeat, pixel);
			construct->textures = services.get<MV::SharedTextures>();
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<FileTextureDefinition>& construct) {
			bool powerTwo = true, repeat = false, pixel = false;
			ar.serialize("powerTwo", powerTwo);
			ar.serialize("repeat", repeat);
			ar.serialize("pixel", pixel);
			construct("", powerTwo, repeat, pixel);
			auto* services = ar.get_user_context<MV::Services>();
			if (services) construct->textures = services->get<MV::SharedTextures>();
			ar.serialize("name", construct->textureName);
			ar.serialize("size", construct->textureSize);
			ar.serialize("contentSize", construct->desiredSize);
			ar.serialize("scale", construct->logicalScale);
			ar.serialize("handles", construct->handles);
		}

		SharedTextures *textures = nullptr;
		bool powerTwo;
		bool repeat;
		bool pixel;
	};

	class DynamicTextureDefinition : public TextureDefinition {
		friend cereal::access;
		friend jai::access;
		friend class jai::serialization::access;
	public:
		static std::shared_ptr<DynamicTextureDefinition> make(const std::string &a_name, const Size<int> &a_size, const Color &a_backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f}){
			return std::shared_ptr<DynamicTextureDefinition>(new DynamicTextureDefinition(a_name, a_size, a_backgroundColor));
		}

		void resize(const Size<int> &a_size);
		
	protected:
		DynamicTextureDefinition(const std::string &a_name, const Size<int> &a_size, const Color &a_backgroundColor):
			TextureDefinition(a_name),
			backgroundColor(a_backgroundColor){
			textureSize = a_size;
			desiredSize = a_size;
		}

		virtual void reloadImplementation() override;

	private:
		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if constexpr (jai::serialization::jai_archive<Archive>) {
				archive(JAI_NVP(backgroundColor));
				TextureDefinition::serialize(archive, version);
			} else {
				archive(CEREAL_NVP(backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
			}
		}

		template<class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<DynamicTextureDefinition> &construct, std::uint32_t const /*version*/){
			construct("", Size<int>(), Color());
			Color backgroundColor;
			archive(cereal::make_nvp("backgroundColor", backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			construct->backgroundColor = backgroundColor;
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<DynamicTextureDefinition>& construct) {
			construct("", Size<int>(), Color());
			ar.serialize("backgroundColor", construct->backgroundColor);
			ar.serialize("name", construct->textureName);
			ar.serialize("size", construct->textureSize);
			ar.serialize("contentSize", construct->desiredSize);
			ar.serialize("scale", construct->logicalScale);
			ar.serialize("handles", construct->handles);
		}

		Color backgroundColor;
	};

	class SurfaceTextureDefinition : public TextureDefinition {
		friend cereal::access;
		friend jai::access;
		friend class jai::serialization::access;
	public:
		static std::shared_ptr<SurfaceTextureDefinition> make(const std::string &a_name, std::function<std::shared_ptr<OwnedSurface> ()> a_surfaceGenerator){
			return std::shared_ptr<SurfaceTextureDefinition>(new SurfaceTextureDefinition(a_name, a_surfaceGenerator));
		}

		Size<int> surfaceSize() const;

		void setSurfaceGenerator(std::function<std::shared_ptr<OwnedSurface> ()> a_surfaceGenerator){
			surfaceGenerator = a_surfaceGenerator;
			if(!handles.empty()){
				load();
			}
		}

	protected:
		SurfaceTextureDefinition(const std::string &a_name, std::function<std::shared_ptr<OwnedSurface> ()> a_surfaceGenerator):
			TextureDefinition(a_name),
			surfaceGenerator(a_surfaceGenerator){
		}

		void reloadImplementation() override;

	private:
		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if constexpr (jai::serialization::jai_archive<Archive>) {
				TextureDefinition::serialize(archive, version);
			} else {
				archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
			}
			//Must manually call setSurfaceGenerator; We can assume whatever owns this texture definition knows how to reconstitute it.
		}

		template<class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<SurfaceTextureDefinition> &construct, std::uint32_t const /*version*/){
			construct("", std::function<std::shared_ptr<OwnedSurface> ()>());
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<SurfaceTextureDefinition>& construct) {
			construct("", std::function<std::shared_ptr<OwnedSurface>()>());
			ar.serialize("name", construct->textureName);
			ar.serialize("size", construct->textureSize);
			ar.serialize("contentSize", construct->desiredSize);
			ar.serialize("scale", construct->logicalScale);
			ar.serialize("handles", construct->handles);
		}

		std::function<std::shared_ptr<OwnedSurface> ()> surfaceGenerator;
		Size<int> generatedSurfaceSize;
	};

	class TextureHandle : public std::enable_shared_from_this<TextureHandle> {
		friend cereal::access;
		friend jai::access;
		friend class jai::serialization::access;
		friend TextureDefinition;
		jai::signal_emitter<void(std::shared_ptr<TextureHandle>)> sizeChangeSignal;
	public:
		virtual ~TextureHandle();
		typedef jai::receiver<void (std::shared_ptr<TextureHandle>)> SignalType;

		//0 to texturePixels
		std::shared_ptr<TextureHandle> bounds(const BoxAABB<int> &a_bounds);
		//in texturePixels
		BoxAABB<int> bounds() const;

		//0.0 to 1.0 (of the texture's contentSize)
		std::shared_ptr<TextureHandle> bounds(const BoxAABB<PointPrecision> &a_bounds);

		//0.0 to 1.0 of the whole texture... OpenGL coordinates.
		std::shared_ptr<TextureHandle> rawPercent(const BoxAABB<PointPrecision> &a_bounds);
		//OpenGL coordinates.
		const BoxAABB<PointPrecision> &rawPercent() const;

		//logical size == bounds.size() * texture scale
		Size<PointPrecision> logicalSize() const;
		//logical slice == percentSlice * logicalSize
		BoxAABB<PointPrecision> logicalSlice() const;

		//relative to the size of the int bounds
		//a texture handle at position(128, 128), size(64, 64) would expect a sliceBounds of range x: 0-64, y: 0-64
		std::shared_ptr<TextureHandle> slice(const BoxAABB<int> &a_sliceBounds);
		//percent of bounds, IE: 0-1 of bounds, not 0-1 of texture.
		std::shared_ptr<TextureHandle> slice(const BoxAABB<PointPrecision> &a_sliceBounds);
		BoxAABB<PointPrecision> slice() const;

		std::shared_ptr<TextureHandle> clearSlice();
		bool hasSlice() const;

		//RawSlice == percent of texture from 0-1 of texture size. Used for whole texture OpenGL
		BoxAABB<PointPrecision> rawSlice() const;

		std::shared_ptr<TextureHandle> flipX(bool a_flip);
		bool flipX() const;

		std::shared_ptr<TextureHandle> flipY(bool a_flip);
		bool flipY() const;

		bool apply(std::vector<DrawPoint> &a_points) const;
		bool applySlicePosition(std::vector<DrawPoint> &a_points) const;

		std::shared_ptr<TextureDefinition> texture() const;

		jai::signal<void(std::shared_ptr<TextureHandle>)> sizeChange;

		std::shared_ptr<TextureHandle> name(const std::string &a_name) {
			debugName = a_name;
			return shared_from_this();
		}
		std::string name() const;

		std::shared_ptr<TextureHandle> pack(const std::string &a_pack) {
			packId = a_pack;
			return shared_from_this();
		}
		std::string pack() const {
			return packId;
		}

		std::shared_ptr<TextureHandle> clone();

	protected:
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<PointPrecision> &a_bounds = BoxAABB<PointPrecision>(Point<PointPrecision>(), Size<PointPrecision>(1.0f, 1.0f)));
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<int> &a_bounds);

		void boundsNoSignal(const BoxAABB<int> &a_bounds);

		template<class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
			if constexpr (jai::serialization::jai_archive<Archive>) {
				archive(
					jai::serialization::make_nvp("texture", textureDefinition),
					jai::serialization::make_nvp("handle", handlePercent),
					jai::serialization::make_nvp("slice", slicePercent),
					jai::serialization::make_nvp("name", debugName),
					JAI_NVP(packId)
				);
			} else {
				if (version == 0) {
					BoxAABB<int> oldIntegralBounds;
					bool oldFlipValues;
					archive(
						cereal::make_nvp("handleRegion", oldIntegralBounds),
						cereal::make_nvp("flipX", oldFlipValues),
						cereal::make_nvp("flipY", oldFlipValues),
						CEREAL_NVP(textureDefinition),
						CEREAL_NVP(handlePercent)
					);
				}else{
					archive(
						cereal::make_nvp("texture", textureDefinition),
						cereal::make_nvp("handle", handlePercent),
						cereal::make_nvp("slice", slicePercent),
						cereal::make_nvp("name", debugName)
					);
					if (version > 1) {
						archive(cereal::make_nvp("packId", packId));
					}
				}
			}
		}

		template<class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextureHandle> &construct, std::uint32_t const version){
			std::shared_ptr<TextureDefinition> textureDefinition;
			if(version == 0){
				archive(cereal::make_nvp("textureDefinition", textureDefinition));
				construct(textureDefinition);
				BoxAABB<int> oldIntegralBounds;
				bool oldFlipValues;
				archive(
					cereal::make_nvp("handleRegion", oldIntegralBounds),
					cereal::make_nvp("flipX", oldFlipValues),
					cereal::make_nvp("flipY", oldFlipValues),
					cereal::make_nvp("handlePercent", construct->handlePercent),
					cereal::make_nvp("name", construct->debugName)
				);
			}else{
				archive(cereal::make_nvp("texture", textureDefinition));
				construct(textureDefinition);
				archive(
					cereal::make_nvp("handle", construct->handlePercent),
					cereal::make_nvp("slice", construct->slicePercent),
					cereal::make_nvp("name", construct->debugName)
				);
				if (version > 1) {
					archive(cereal::make_nvp("packId", construct->packId));
				}
				MV::SharedTextures* sharedTextures = nullptr;
				if (!construct->packId.empty()) {
					MV::Services& services = cereal::get_user_data<MV::Services>(archive);
					sharedTextures = getSharedTextureFromServices(services);
				}
				construct->postLoadInitialize(sharedTextures);
			}
		}

		template<typename Archive>
		static void load_and_construct(Archive& ar, jai::serialization::construct<TextureHandle>& construct) {
			std::shared_ptr<TextureDefinition> textureDefinition;
			ar.serialize("texture", textureDefinition);
			construct(textureDefinition);
			ar.serialize("handle", construct->handlePercent);
			ar.serialize("slice", construct->slicePercent);
			ar.serialize("name", construct->debugName);
			ar.serialize("packId", construct->packId);
			// Always resolve SharedTextures (not only for packs): postLoadInitialize uses it to
			// route the load through the parallel deferred-load queue when one is active.
			MV::SharedTextures* sharedTextures = nullptr;
			if (auto* services = ar.get_user_context<MV::Services>()) {
				sharedTextures = getSharedTextureFromServices(*services);
			}
			construct->postLoadInitialize(sharedTextures);
		}

		void postLoadInitialize(SharedTextures* a_sharedTextures);

		std::shared_ptr<TextureDefinition> textureDefinition;

		BoxAABB<PointPrecision> slicePercent;
		BoxAABB<PointPrecision> handlePercent;

		std::string debugName;
		std::string packId;
	};
}

CEREAL_FORCE_DYNAMIC_INIT(mv_scenetextures);

#endif
