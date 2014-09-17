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

#include "cereal/cereal.hpp"
#include "cereal/types/map.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/memory.hpp"
#include "cereal/types/string.hpp"
#include "cereal/types/base_class.hpp"

#include "cereal/archives/json.hpp"
#include "cereal/types/polymorphic.hpp"

namespace MV {
	SDL_Surface* converToPowerOfTwo(SDL_Surface* surface);
	GLenum getTextureFormat(SDL_Surface* img);
	GLenum getInternalTextureFormat(SDL_Surface* img);

	void saveLoadedTexture(const std::string &a_fileName, GLuint a_texture);

	bool clearTexturePoints(std::vector<DrawPoint> &a_points);

	//These methods are all destructive to the original surface.  Do not rely on SDL_Surface being viable after calling.
	SDL_Surface* convertToPowerOfTwoSurface(SDL_Surface *a_img);
	bool loadTextureFromFile(const std::string &file, GLuint &imageLoaded, Size<int> &size, Size<int> &originalSize, bool powerTwo, bool repeat);
	bool loadTextureFromSurface(SDL_Surface *img, GLuint &imageLoaded, Size<int> &size, Size<int> &originalSize, bool powerTwo, bool repeat);

	class TextureHandle;
	class TextureDefinition : public std::enable_shared_from_this<TextureDefinition> {
		friend cereal::access;
		Slot<void(std::shared_ptr<TextureDefinition>)> onReloadAction;
	public:
		SlotRegister<void(std::shared_ptr<TextureDefinition>)> onReload;
		typedef Signal<void(std::shared_ptr<TextureDefinition>)> SignalType;

		virtual ~TextureDefinition();
		std::shared_ptr<TextureHandle> makeHandle();
		std::shared_ptr<TextureHandle> makeHandle(const BoxAABB<int> &a_bounds);

		GLuint textureId() const;
		std::string name() const;
		Size<int> size() const;
		Size<int> size();
		Size<int> contentSize() const;
		Size<int> contentSize();
		//bookkeeping
		bool loaded() const;
		void load();
		void unload();
		void unload(TextureHandle* a_toRemove);

		void reload();

		void save(const std::string &a_fileName);
	protected:
		void cleanupOpenglTexture();
		TextureDefinition(const std::string &a_name, bool a_isShared = true);

		std::string textureName;
		Size<int> textureSize;
		Size<int> desiredSize;
		GLuint texture;

		std::vector< std::weak_ptr<TextureHandle> > handles;
		bool isShared;

	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(cereal::make_nvp("name", textureName), cereal::make_nvp("size", textureSize), cereal::make_nvp("contentSize", desiredSize), CEREAL_NVP(handles));
			if(!handles.empty() && !texture){
				reloadImplementation();
			}
		}

		virtual void reloadImplementation() = 0;
		virtual void cleanupImplementation(){}
	};

	class FileTextureDefinition : public TextureDefinition {
		friend cereal::access;
	public:
		static std::shared_ptr<FileTextureDefinition> make(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false){
			return std::shared_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat));
		}
		static std::unique_ptr<FileTextureDefinition> makeUnmanaged(const std::string &a_filename, bool a_powerTwo = true, bool a_repeat = false){
			return std::unique_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_powerTwo, a_repeat, false));
		}



	protected:
		FileTextureDefinition(const std::string &a_filename, bool a_powerTwo, bool a_repeat, bool a_isShared = true):
			TextureDefinition(a_filename, a_isShared),
			repeat(a_repeat),
			powerTwo(a_powerTwo){
		}

	private:
		virtual void reloadImplementation();

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(powerTwo), CEREAL_NVP(repeat), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<FileTextureDefinition> &construct){
			bool repeat = false;
			bool powerTwo = true;
			archive(cereal::make_nvp("powerTwo", powerTwo), cereal::make_nvp("repeat", repeat));
			construct("", powerTwo, repeat);
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			if(!construct->handles.empty()){
				construct->reloadImplementation();
			}
		}
		bool powerTwo;
		bool repeat;
	};

	class DynamicTextureDefinition : public TextureDefinition {
		friend cereal::access;
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

	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<DynamicTextureDefinition> &construct){
			construct("", Size<int>(), Color());
			Color backgroundColor;
			archive(cereal::make_nvp("backgroundColor", backgroundColor), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			construct->backgroundColor = backgroundColor;
			if(!construct->handles.empty()){
				construct->reloadImplementation();
			}
		}

		virtual void reloadImplementation();
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
	protected:
		SurfaceTextureDefinition(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator):
			TextureDefinition(a_name),
			surfaceGenerator(a_surfaceGenerator){
		}

	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
			//Must manually call setSurfaceGenerator; We can assume whatever owns this texture definition knows how to reconstitute it.
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<SurfaceTextureDefinition> &construct){
			construct("", std::function<SDL_Surface*()>());
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
		}

		virtual void reloadImplementation();

		std::function<SDL_Surface*()> surfaceGenerator;
		Size<int> generatedSurfaceSize;
	};

	class TextureHandle : public std::enable_shared_from_this<TextureHandle> {
		friend cereal::access;
		friend TextureDefinition;
		Slot<void(std::shared_ptr<TextureHandle>)> sizeChanges;
	public:
		virtual ~TextureHandle();
		typedef Signal<void (std::shared_ptr<TextureHandle>)> SignalType;

		std::shared_ptr<TextureHandle> bounds(const BoxAABB<int> &a_bounds);
		BoxAABB<int> bounds() const;

		std::shared_ptr<TextureHandle> percentBounds(const BoxAABB<PointPrecision> &a_bounds);
		BoxAABB<PointPrecision> percentBounds() const;

		std::shared_ptr<TextureHandle> flipX(bool a_flip);
		bool flipX() const;

		std::shared_ptr<TextureHandle> flipY(bool a_flip);
		bool flipY() const;

		bool apply(std::vector<DrawPoint> &a_points) const;

		std::shared_ptr<TextureDefinition> texture() const;

		SlotRegister<void(std::shared_ptr<TextureHandle>)> sizeObserver;

		std::string name() const;
	private:
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const BoxAABB<int> &a_bounds = BoxAABB<int>(point(-1, -1)));

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(handleRegion), CEREAL_NVP(handlePercent),
				CEREAL_NVP(resizeToParent),
				cereal::make_nvp("flipX", flipTextureX),
				cereal::make_nvp("flipY", flipTextureY),
				CEREAL_NVP(textureDefinition),
				cereal::make_nvp("name", debugName));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextureHandle> &construct){
			std::shared_ptr<TextureDefinition> textureDefinition;
			archive(CEREAL_NVP(textureDefinition));
			construct(textureDefinition);
			archive(
				cereal::make_nvp("handleRegion", construct->handleRegion),
				cereal::make_nvp("handlePercent", construct->handlePercent),
				cereal::make_nvp("resizeToParent", construct->resizeToParent),
				cereal::make_nvp("flipX", construct->flipTextureX),
				cereal::make_nvp("flipY", construct->flipTextureY),
				cereal::make_nvp("name", construct->debugName)
			);
			construct->observeTextureReload();
		}

		void updatePercentBounds();
		void updateIntegralBounds();
		void updatePercentCorners();
		void observeTextureReload();

		BoxAABB<int> handleRegion;
		BoxAABB<PointPrecision> handlePercent;

		bool flipTextureX;
		bool flipTextureY;

		bool resizeToParent;

		std::string debugName;

		std::shared_ptr<TextureDefinition> textureDefinition;

		TextureDefinition::SignalType::SharedType onParentReload;
	};
}

#endif
