#ifndef _MV_TEXTURES_H_
#define _MV_TEXTURES_H_

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "render.h"
#include "boxaabb.h"

#include "Utility/cerealUtility.h"

#include "chaiscript/chaiscript.hpp"

namespace MV {
	SDL_Surface* converToPowerOfTwo(SDL_Surface* surface);
	GLenum getTextureFormat(SDL_Surface* img);
	GLenum getInternalTextureFormat(SDL_Surface* img);

	void saveLoadedTexture(const std::string &a_fileName, GLuint a_texture);

	//Requires a 4 point vector. IE: Shrink to 4 before calling this if you use 9-slicing etc.
	bool clearTexturePoints(std::vector<DrawPoint> &a_points);

	//These methods are all destructive to the original surface.  Do not rely on SDL_Surface being viable after calling.
	SDL_Surface* convertToPowerOfTwoSurface(SDL_Surface *a_img);
	bool loadTextureFromFile(const std::string &file, GLuint &imageLoaded, Size<int> &size, Size<int> &originalSize, bool powerTwo, bool repeat, bool pixel);
	bool loadTextureFromSurface(SDL_Surface *img, GLuint &imageLoaded, Size<int> &size, Size<int> &originalSize, bool powerTwo, bool repeat, bool pixel);

	class TextureUnloader {
	public:
		static void increment(GLuint a_id);
		static bool decrement(GLuint a_id);
	private:
		static std::mutex lock;
		static std::map<GLuint, int> handles;
	};

	class SharedTextures;
	class TextureHandle;
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

		static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
			a_script.add(chaiscript::user_type<TextureDefinition>(), "TextureDefinition");
			
			a_script.add(chaiscript::fun(&TextureDefinition::textureId), "textureId");
			a_script.add(chaiscript::fun(&TextureDefinition::name), "name");
			a_script.add(chaiscript::fun(&TextureDefinition::loaded), "loaded");
			a_script.add(chaiscript::fun(&TextureDefinition::load), "load");
			a_script.add(chaiscript::fun(&TextureDefinition::reload), "reload");
			a_script.add(chaiscript::fun(&TextureDefinition::save), "save");

			a_script.add(chaiscript::fun(static_cast<Size<int>(TextureDefinition::*)()>(&TextureDefinition::size)), "size");
			a_script.add(chaiscript::fun(static_cast<Size<int>(TextureDefinition::*)()>(&TextureDefinition::contentSize)), "contentSize");

			a_script.add(chaiscript::fun(static_cast<Scale(TextureDefinition::*)() const>(&TextureDefinition::scale)), "scale");
			a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)(const Scale &)>(&TextureDefinition::scale)), "scale");

			a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)()>(&TextureDefinition::unload)), "unload");
			a_script.add(chaiscript::fun(static_cast<void(TextureDefinition::*)(TextureHandle*)>(&TextureDefinition::unload)), "unload");

			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)()>(&TextureDefinition::makeHandle)), "makeHandle");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<int> &)>(&TextureDefinition::makeHandle)), "makeHandle");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<PointPrecision> &)>(&TextureDefinition::makeHandle)), "makeHandle");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureDefinition::*)(const BoxAABB<PointPrecision> &)>(&TextureDefinition::makeRawHandle)), "makeRawHandle");

			return a_script;
		}
	protected:
		void cleanupOpenglTexture();
		TextureDefinition(const std::string &a_name, bool a_isShared = true);

		std::string textureName;
		Size<int> textureSize;
		Size<int> desiredSize;
		Scale logicalScale;
		GLuint texture;

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
		virtual void cleanupImplementation(){}
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
			repeat(a_repeat),
			pixel(a_pixel),
			powerTwo(a_powerTwo){
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

			construct("", powerTwo, repeat, pixel);
			archive.extract(cereal::make_nvp("texture", construct->textures));
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
		static std::shared_ptr<SurfaceTextureDefinition> make(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator){
			return std::shared_ptr<SurfaceTextureDefinition>(new SurfaceTextureDefinition(a_name, a_surfaceGenerator));
		}

		Size<int> surfaceSize() const;

		void setSurfaceGenerator(std::function<SDL_Surface*()> a_surfaceGenerator){
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
		SurfaceTextureDefinition(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator):
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
			construct("", std::function<SDL_Surface*()>());
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
		}

		std::function<SDL_Surface*()> surfaceGenerator;
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
		BoxAABB<PointPrecision> rawPercent() const;

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
			
			a_script.add(chaiscript::fun(static_cast<BoxAABB<PointPrecision>(TextureHandle::*)() const>(&TextureHandle::rawPercent)), "rawPercent");
			a_script.add(chaiscript::fun(static_cast<std::shared_ptr<TextureHandle>(TextureHandle::*)(const BoxAABB<PointPrecision> &)>(&TextureHandle::rawPercent)), "rawPercent");

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
					archive.extract(cereal::make_nvp("texture", sharedTextures));
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

CEREAL_CLASS_VERSION(MV::TextureHandle, 2);

#endif
