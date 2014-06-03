#ifndef _MV_TEXTURES_H_
#define _MV_TEXTURES_H_

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "render.h"

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

	//These methods are all destructive to the original surface.  Do not rely on SDL_Surface being viable after calling.
	SDL_Surface* convertToPowerOfTwoSurface(SDL_Surface *a_img);
	bool loadTextureFromFile(const std::string &file, GLuint &imageLoaded, Size<int> &size, bool repeat);
	bool loadTextureFromSurface(SDL_Surface *img, GLuint &imageLoaded, Size<int> &size, bool repeat);

	class TextureHandle;
	class TextureDefinition : public std::enable_shared_from_this<TextureDefinition> {
		Slot<void(std::shared_ptr<TextureDefinition>)> onReloadAction;
	public:
		SlotRegister<void(std::shared_ptr<TextureDefinition>)> onReload;
		typedef Signal<void(std::shared_ptr<TextureDefinition>)> SignalType;

		virtual ~TextureDefinition(){}
		std::shared_ptr<TextureHandle> makeHandle();
		std::shared_ptr<TextureHandle> makeHandle(const Point<int> &a_position, const Size<int> &a_size);

		GLuint textureId() const;
		std::string name() const;
		Size<int> size() const;

		//bookkeeping
		bool loaded() const;
		void reload();
		void cleanup();
		void cleanup(TextureHandle* toRemove);

		template <class Archive>
		void serialize(Archive & archive){
			archive(cereal::make_nvp("name", textureName), cereal::make_nvp("size", textureSize), CEREAL_NVP(handles));
		}
	protected:
		TextureDefinition(const std::string &a_name);

		std::string textureName;
		Size<int> textureSize;
		GLuint texture;

		std::vector< std::weak_ptr<TextureHandle> > handles;

	private:
		virtual void reloadImplementation() = 0;
		virtual void cleanupImplementation(){}
	};

	class FileTextureDefinition : public TextureDefinition {
		friend cereal::access;
	public:
		static std::shared_ptr<FileTextureDefinition> make(const std::string &a_filename, bool a_repeat = false){
			return std::shared_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_repeat));
		}

		FileTextureDefinition():FileTextureDefinition("", false){} //only for cereal. DO NOT USE.
	private:
		FileTextureDefinition(const std::string &a_filename, bool a_repeat):
			TextureDefinition(a_filename),
			repeat(a_repeat){
		}
		virtual void reloadImplementation();

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(repeat), cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<FileTextureDefinition> &construct){
			bool repeat = false;
			archive(cereal::make_nvp("repeat", repeat));
			construct("", repeat);
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			if(!construct->handles.empty()){
				construct->reloadImplementation();
			}
		}

		bool repeat;
	};

	class DynamicTextureDefinition : public TextureDefinition {
		friend cereal::access;
	public:
		static std::shared_ptr<DynamicTextureDefinition> make(const std::string &a_name, const Size<int> &a_size){
			return std::shared_ptr<DynamicTextureDefinition>(new DynamicTextureDefinition(a_name, a_size));
		}
		
		DynamicTextureDefinition():DynamicTextureDefinition("", Size<int>()){} //only for cereal. DO NOT USE.
	private:
		DynamicTextureDefinition(const std::string &a_name, const Size<int> &a_size):
			TextureDefinition(a_name){
			textureSize = a_size;
		}

		template <class Archive>
		void serialize(Archive & archive){
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(this)));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<DynamicTextureDefinition> &construct){
			construct("", Size<int>());
			archive(cereal::make_nvp("base", cereal::base_class<TextureDefinition>(construct.ptr())));
			if(!construct->handles.empty()){
				construct->reloadImplementation();
			}
		}

		virtual void reloadImplementation();
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
				reload();
			}
		}

	private:
		SurfaceTextureDefinition(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator):
			TextureDefinition(a_name),
			surfaceGenerator(a_surfaceGenerator){
		}

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

		Size<int> size() const;
		Point<int> position() const;

		Size<double> percentSize() const;
		Point<double> percentPosition() const;

		Point<double> percentTopLeft() const;
		Point<double> percentBottomRight() const;

		double percentLeft() const;
		double percentRight() const;
		double percentTop() const;
		double percentBottom() const;

		void setCorners(const Point<int> &a_topLeft, const Point<int> &a_bottomRight);
		void setCorners(const Point<double> &a_topLeft, const Point<double> &a_bottomRight);

		void setBounds(const Point<int> &a_topLeft, const Size<int> &a_size);
		void setBounds(const Point<double> &a_topLeft, const Size<double> &a_size);

		void setFlipX(bool a_flip);
		void setFlipY(bool a_flip);

		bool flippedX() const;
		bool flippedY() const;

		std::shared_ptr<TextureDefinition> texture() const;

		SlotRegister<void(std::shared_ptr<TextureHandle>)> sizeObserver;
		std::string name; //REMOVE THIS
	private:
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const Point<int> &a_position = Point<int>(0, 0), const Size<int> &a_size = Size<int>(-1, -1));

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(handleSize), CEREAL_NVP(handlePosition),
				CEREAL_NVP(handlePercentSize), CEREAL_NVP(handlePercentPosition),
				CEREAL_NVP(handlePercentTopLeft), CEREAL_NVP(handlePercentBottomRight),
				CEREAL_NVP(flipX), CEREAL_NVP(flipY),
				CEREAL_NVP(textureDefinition), CEREAL_NVP(name));
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextureHandle> &construct){
			std::shared_ptr<TextureDefinition> textureDefinition;
			archive(CEREAL_NVP(textureDefinition));
			construct(textureDefinition);
			archive(
				cereal::make_nvp("handleSize", construct->handleSize),
				cereal::make_nvp("handlePosition", construct->handlePosition),
				cereal::make_nvp("handlePercentPosition", construct->handlePercentPosition),
				cereal::make_nvp("handlePercentSize", construct->handlePercentSize),
				cereal::make_nvp("handlePercentTopLeft", construct->handlePercentTopLeft),
				cereal::make_nvp("handlePercentBottomRight", construct->handlePercentBottomRight),
				cereal::make_nvp("flipX", construct->flipX),
				cereal::make_nvp("flipY", construct->flipY),
				cereal::make_nvp("name", construct->name)
			);
			construct->observeTextureReload();
		}

		void updatePercentBounds();
		void updateIntegralBounds();
		void updatePercentCorners();
		void observeTextureReload();

		Size<int> handleSize;
		Point<int> handlePosition;

		Size<double> handlePercentSize;
		Point<double> handlePercentPosition;

		Point<double> handlePercentTopLeft;
		Point<double> handlePercentBottomRight;

		bool flipX;
		bool flipY;

		bool resizeToParent;

		std::shared_ptr<TextureDefinition> textureDefinition;

		TextureDefinition::SignalType::SharedType onParentReload;
	};

	class SharedTextures {
	public:
		std::shared_ptr<FileTextureDefinition> getFileTexture(const std::string &a_filename, bool a_repeat = false);
		std::shared_ptr<DynamicTextureDefinition> getDynamicTexture(const std::string &a_identifier, const Size<int> &a_size);
		std::shared_ptr<SurfaceTextureDefinition> getSurfaceTexture(const std::string &a_identifier, std::function<SDL_Surface*()> a_surfaceGenerator);

		template <class Archive>
		void serialize(Archive & archive){
			archive(CEREAL_NVP(fileDefinitions), CEREAL_NVP(dynamicDefinitions), CEREAL_NVP(surfaceDefinitions));
		}
	private:
		std::map<std::string, std::shared_ptr<FileTextureDefinition>> fileDefinitions;
		std::map<std::string, std::shared_ptr<DynamicTextureDefinition>> dynamicDefinitions;
		std::map<std::string, std::shared_ptr<SurfaceTextureDefinition>> surfaceDefinitions;
	};

}

#endif
