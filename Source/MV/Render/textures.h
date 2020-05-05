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
#include "boxaabb.h"

#include "MV/Utility/signal.hpp"
#include "MV/Utility/cerealUtility.h"

#include "MV/Utility/chaiscriptUtility.h"

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
	std::unique_ptr<LoadedTextureData> loadTextureFromSurface(const std::shared_ptr<OwnedSurface> &img, const TextureParameters &file);

	//required to allow forward declared MV::SharedTextures
	MV::SharedTextures* getSharedTextureFromServices(MV::Services& a_services);

	struct TextureParameters {
		TextureParameters(std::string a_path, bool a_powerTwo, bool a_repeat, bool a_pixel):
			path(a_path),
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
			path = a_path;
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
	struct LoadedTextureData {
		friend LoadedTexture;
	public:
		LoadedTextureData() {}
		LoadedTextureData(GLuint a_textureId) : textureId(a_textureId){}
		LoadedTextureData(LoadedTextureData&& a_rhs) = default;

		operator GLuint() const {
			return textureId;
		}

		OpenGlTextureId textureId;
		Size<int> size;
		Size<int> originalSize;

	private:
		LoadedTextureData(const LoadedTextureData& a_rhs) = delete;
		LoadedTextureData& operator=(const LoadedTextureData& a_rhs) = delete;

		std::atomic<int> useCount = 0;
	};

	class LoadedTexture {
	public:
		LoadedTexture(const TextureParameters& a_parameters) {
			if (a_parameters.cleared) {
				locallyOwnedParametersValue = std::make_unique<TextureParameters>(a_parameters);
				locallyOwnedDataValue = std::make_unique<LoadedTextureData>();
			} else {
				std::scoped_lock guard(lock);
				auto found = globalLookup.find(a_parameters);
				if (found != globalLookup.end()) {
					++found->second->useCount;
				} else {
					bool success = false;
					std::tie(found, success) = globalLookup.insert({ a_parameters, std::move(loadFile(a_parameters)) });
				}
				dataValue = found->second.get();
				parametersValue = &found->first;
			}
		}
		LoadedTexture(std::unique_ptr<LoadedTextureData> a_locallyOwnedDataValue) :
			locallyOwnedDataValue(a_locallyOwnedDataValue.release()){
		}
		LoadedTexture(LoadedTexture&&) = default;
		~LoadedTexture() {
			if (--dataValue->useCount == 0) {
				std::scoped_lock guard(lock);
				globalLookup.erase(parameters());
			}
		}

		const TextureParameters& parameters() const {
			return locallyOwnedParametersValue ? *locallyOwnedParametersValue : *parametersValue;
		}
		LoadedTextureData& data() {
			return locallyOwnedDataValue ? *locallyOwnedDataValue : *dataValue;
		}
		GLuint id() const {
			return locallyOwnedDataValue ? locallyOwnedDataValue->textureId : dataValue->textureId;
		}
	private:
		LoadedTexture(const LoadedTexture&) = delete;
		LoadedTexture& operator=(const LoadedTexture&) = delete;

		std::unique_ptr<LoadedTextureData> loadFile(const TextureParameters& a_parameters);
		//static void releaseFile(const TextureParameters& a_parameters);
		
		LoadedTextureData* dataValue;
		const TextureParameters* parametersValue;
		std::unique_ptr<LoadedTextureData> locallyOwnedDataValue;
		std::unique_ptr<TextureParameters> locallyOwnedParametersValue;
		static std::mutex lock;
		static std::map<TextureParameters, std::unique_ptr<LoadedTextureData>> globalLookup;
	};


	class TextureDefinition : public std::enable_shared_from_this<TextureDefinition> {
		friend cereal::access;
	protected:
		Signal<void(std::shared_ptr<TextureDefinition>)> onReloadAction;
	public:
		SignalRegister<void(std::shared_ptr<TextureDefinition>)> onReload;
		typedef Receiver<void(std::shared_ptr<TextureDefinition>)> SignalType;

		virtual ~TextureDefinition();
		std::shared_ptr<TextureHandle> makeHandle();
		std::shared_ptr<TextureHandle> makeHandle(const BoxAABB<int> &a_bounds);
		std::shared_ptr<TextureHandle> makeHandle(const BoxAABB<PointPrecision> &a_bounds);
		void registerHandle(std::weak_ptr<TextureHandle> a_newHandle);
		std::shared_ptr<TextureHandle> makeRawHandle(const BoxAABB<PointPrecision> &a_bounds);

		GLuint textureId() const;
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

	private:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
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

		virtual void reloadImplementation() = 0;
	};

	class FileTextureDefinition : public TextureDefinition {
		friend cereal::access;
	public:
		static std::shared_ptr<FileTextureDefinition> make(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false, bool a_pixel = false){
			return std::shared_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat, a_pixel));
		}
		static std::unique_ptr<FileTextureDefinition> makeUnmanaged(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false, bool a_pixel = false){
			return std::unique_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat, a_pixel, false));
		}

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<FileTextureDefinition>(), "FileTextureDefinition");
			a_script.add(chaiscript::base_class<TextureDefinition, FileTextureDefinition>());

			a_script.add(chaiscript::fun(&FileTextureDefinition::make), "FileTextureDefinition_make");
			a_script.add(chaiscript::fun(&FileTextureDefinition::makeUnmanaged), "FileTextureDefinition_makeUnmanaged");

			return a_script;
		}
	protected:
		FileTextureDefinition(const std::string &a_filename, bool a_powerTwo, bool a_repeat, bool a_pixel, bool a_isShared = true):
			TextureDefinition(a_filename, a_isShared),
			powerTwo(a_powerTwo),
			repeat(a_repeat),
			pixel(a_pixel){
		}

		void reloadImplementation() override;

	private:

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(CEREAL_NVP(powerTwo), CEREAL_NVP(repeat), CEREAL_NVP(pixel), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
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

		SharedTextures *textures = nullptr;
		bool powerTwo;
		bool repeat;
		bool pixel;
	};

	class DynamicTextureDefinition : public TextureDefinition {
		friend cereal::access;
	public:
		static std::shared_ptr<DynamicTextureDefinition> make(const std::string &a_name, const Size<int> &a_size, const Color &a_backgroundColor = {0.0f, 0.0f, 0.0f, 0.0f}){
			return std::shared_ptr<DynamicTextureDefinition>(new DynamicTextureDefinition(a_name, a_size, a_backgroundColor));
		}

		void resize(const Size<int> &a_size);
		
		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<DynamicTextureDefinition>(), "DynamicTextureDefinition");
			a_script.add(chaiscript::base_class<TextureDefinition, DynamicTextureDefinition>());

			a_script.add(chaiscript::fun(&DynamicTextureDefinition::make), "DynamicTextureDefinition_make");
			a_script.add(chaiscript::fun(&DynamicTextureDefinition::resize), "resize");

			return a_script;
		}
	protected:
		DynamicTextureDefinition(const std::string &a_name, const Size<int> &a_size, const Color &a_backgroundColor):
			TextureDefinition(a_name),
			backgroundColor(a_backgroundColor){
			textureSize = a_size;
			desiredSize = a_size;
		}

		virtual void reloadImplementation() override;

	private:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(CEREAL_NVP(backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<DynamicTextureDefinition> &construct, std::uint32_t const /*version*/){
			construct("", Size<int>(), Color());
			Color backgroundColor;
			archive(cereal::make_nvp("backgroundColor", backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			construct->backgroundColor = backgroundColor;
		}

		Color backgroundColor;
	};

	class SurfaceTextureDefinition : public TextureDefinition {
		friend cereal::access;
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

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<SurfaceTextureDefinition>(), "SurfaceTextureDefinition");
			a_script.add(chaiscript::base_class<TextureDefinition, SurfaceTextureDefinition>());

			a_script.add(chaiscript::fun(&SurfaceTextureDefinition::make), "SurfaceTextureDefinition_make");
			a_script.add(chaiscript::fun(&SurfaceTextureDefinition::surfaceSize), "surfaceSize");
			a_script.add(chaiscript::fun(&SurfaceTextureDefinition::setSurfaceGenerator), "setSurfaceGenerator");

			return a_script;
		}
	protected:
		SurfaceTextureDefinition(const std::string &a_name, std::function<std::shared_ptr<OwnedSurface> ()> a_surfaceGenerator):
			TextureDefinition(a_name),
			surfaceGenerator(a_surfaceGenerator){
		}

		void reloadImplementation() override;

	private:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
			//Must manually call setSurfaceGenerator; We can assume whatever owns this texture definition knows how to reconstitute it.
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<SurfaceTextureDefinition> &construct, std::uint32_t const /*version*/){
			construct("", std::function<std::shared_ptr<OwnedSurface> ()>());
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
		}

		std::function<std::shared_ptr<OwnedSurface> ()> surfaceGenerator;
		Size<int> generatedSurfaceSize;
	};

	class TextureHandle : public std::enable_shared_from_this<TextureHandle> {
		friend cereal::access;
		friend TextureDefinition;
		Signal<void(std::shared_ptr<TextureHandle>)> sizeChangeSignal;
	public:
		virtual ~TextureHandle();
		typedef Receiver<void (std::shared_ptr<TextureHandle>)> SignalType;

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

		SignalRegister<void(std::shared_ptr<TextureHandle>)> sizeChange;

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

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<TextureHandle>(), "TextureHandle");

			a_script.add(chaiscript::fun(&TextureHandle::apply), "apply");
			a_script.add(chaiscript::fun([](TextureHandle &a_self) { return a_self.name(); }), "name");
			a_script.add(chaiscript::fun([](TextureHandle &a_self, const std::string &a_name) { return a_self.name(a_name); }), "name");
			a_script.add(chaiscript::fun(&TextureHandle::texture), "texture");
			a_script.add(chaiscript::fun(&TextureHandle::clone), "clone");

			a_script.add(chaiscript::fun(&TextureHandle::clearSlice), "clearSlice");
			a_script.add(chaiscript::fun(&TextureHandle::hasSlice), "hasSlice");

			a_script.add(chaiscript::fun(&TextureHandle::hasSlice), "logicalSize");
			a_script.add(chaiscript::fun(&TextureHandle::hasSlice), "logicalSlice");

			a_script.add(chaiscript::fun(&TextureHandle::sizeChange), "sizeChange");

			a_script.add(chaiscript::fun(static_cast<BoxAABB<int>(TextureHandle::*)() const>(&TextureHandle::bounds)), "bounds");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(const BoxAABB<int> &)>(&TextureHandle::bounds)), "bounds");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(const BoxAABB<PointPrecision> &)>(&TextureHandle::bounds)), "bounds");
			
			a_script.add(chaiscript::fun(static_cast<BoxAABB<PointPrecision>(TextureHandle::*)() const>(&TextureHandle::slice)), "slice");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(const BoxAABB<int> &)>(&TextureHandle::slice)), "slice");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(const BoxAABB<PointPrecision> &)>(&TextureHandle::slice)), "slice");

			a_script.add(chaiscript::fun([](TextureHandle &a_self) { return a_self.rawPercent(); }), "rawPercent");
			a_script.add(chaiscript::fun([](TextureHandle &a_self, const BoxAABB<PointPrecision> &a_rawPercent) { return a_self.rawPercent(a_rawPercent); }), "rawPercent");

			a_script.add(chaiscript::fun(static_cast<bool(TextureHandle::*)() const>(&TextureHandle::flipX)), "flipX");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(bool)>(&TextureHandle::flipX)), "flipX");

			a_script.add(chaiscript::fun(static_cast<bool(TextureHandle::*)() const>(&TextureHandle::flipY)), "flipY");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(bool)>(&TextureHandle::flipY)), "flipY");

			return a_script;
		}
	protected:
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<PointPrecision> &a_bounds = BoxAABB<PointPrecision>(Point<PointPrecision>(), Size<PointPrecision>(1.0f, 1.0f)));
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<int> &a_bounds);

		void boundsNoSignal(const BoxAABB<int> &a_bounds);

		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const version){
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

		template <class Archive>
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
