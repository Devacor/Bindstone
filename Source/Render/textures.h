#ifndef __MV_TEXTURES_H__
#define __MV_TEXTURES_H__

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>
#include <memory>

#include "render.h"

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
	public:
		virtual ~TextureDefinition(){}
		std::shared_ptr<TextureHandle> makeHandle();
		std::shared_ptr<TextureHandle> makeHandle(const Point<int> &a_position, const Size<int> &a_size);

		void setOnReload(std::function< void (std::shared_ptr<TextureDefinition>) > a_onReload);
		void clearOnReload();

		GLuint textureId() const;
		std::string name() const;
		Size<int> size() const;

		//bookkeeping
		void reload();
		void cleanup();
	protected:
		TextureDefinition(const std::string &a_name);

		std::string textureName;
		Size<int> textureSize;
		GLuint texture;

	private:
		virtual void reloadImplementation() = 0;
		virtual void cleanupImplementation(){}

		std::vector< std::weak_ptr<TextureHandle> > handles;
		std::function< void (std::shared_ptr<TextureDefinition>) > onReload;
	};

	class FileTextureDefinition : public TextureDefinition {
	public:
		static std::shared_ptr<FileTextureDefinition> make(const std::string &a_filename, bool a_repeat = false){
			return std::shared_ptr<FileTextureDefinition>(new FileTextureDefinition(a_filename, a_repeat));
		}

	private:
		FileTextureDefinition(const std::string &a_filename, bool a_repeat):
			TextureDefinition(a_filename),
			repeat(a_repeat){
		}
		virtual void reloadImplementation();

		bool repeat;
	};

	class DynamicTextureDefinition : public TextureDefinition {
	public:
		static std::shared_ptr<DynamicTextureDefinition> make(const std::string &a_name, const Size<int> &a_size){
			return std::shared_ptr<DynamicTextureDefinition>(new DynamicTextureDefinition(a_name, a_size));
		}
	private:
		DynamicTextureDefinition(const std::string &a_name, const Size<int> &a_size):
			TextureDefinition(a_name){
			textureSize = a_size;
		}
		virtual void reloadImplementation();
	};

	class SurfaceTextureDefinition : public TextureDefinition {
	public:
		static std::shared_ptr<SurfaceTextureDefinition> make(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator){
			return std::shared_ptr<SurfaceTextureDefinition>(new SurfaceTextureDefinition(a_name, a_surfaceGenerator));
		}

		Size<int> surfaceSize() const;
	private:
		SurfaceTextureDefinition(const std::string &a_name, std::function<SDL_Surface*()> a_surfaceGenerator):
			TextureDefinition(a_name),
			surfaceGenerator(a_surfaceGenerator){
		}
		virtual void reloadImplementation();
		std::function<SDL_Surface*()> surfaceGenerator;
		Size<int> generatedSurfaceSize;
	};

	class TextureHandle : public std::enable_shared_from_this<TextureHandle> {
		friend TextureDefinition;
		Socket<void(std::shared_ptr<TextureHandle>)> sizeChanges;
	public:
		~TextureHandle();
		typedef Signal<void (std::shared_ptr<TextureHandle>)> Signal;

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
		void setCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight);

		void setBounds(const Point<int> &a_topLeft, const Size<int> &a_size);
		void setBounds(const Point<> &a_topLeft, const Size<> &a_size);

		void setFlipX(bool a_flip);
		void setFlipY(bool a_flip);

		bool flippedX() const;
		bool flippedY() const;

		std::shared_ptr<TextureDefinition> texture() const;

		SocketRegister<void(std::shared_ptr<TextureHandle>)> sizeObserver;
	private:
		TextureHandle(std::shared_ptr<TextureDefinition> a_texture, const Point<int> &a_position = Point<int>(0, 0), const Size<int> &a_size = Size<int>(-1, -1));

		void updatePercentBounds();
		void updateIntegralBounds();
		void updatePercentCorners();

		Size<int> handleSize;
		Point<int> handlePosition;

		Size<double> handlePercentSize;
		Point<double> handlePercentPosition;

		Point<double> handlePercentTopLeft;
		Point<double> handlePercentBottomRight;

		bool flipX;
		bool flipY;

		std::shared_ptr<TextureDefinition> textureDefinition;
	};

	class SharedTextures {
	public:
		std::shared_ptr<FileTextureDefinition> getFileTexture(const std::string &a_filename, bool a_repeat = false);
		std::shared_ptr<DynamicTextureDefinition> getDynamicTexture(const std::string &a_identifier, const Size<int> &a_size);
		std::shared_ptr<SurfaceTextureDefinition> getSurfaceTexture(const std::string &a_identifier, std::function<SDL_Surface*()> a_surfaceGenerator);

	private:
		std::map<std::string, std::shared_ptr<FileTextureDefinition>> fileDefinitions;
		std::map<std::string, std::shared_ptr<DynamicTextureDefinition>> dynamicDefinitions;
		std::map<std::string, std::shared_ptr<SurfaceTextureDefinition>> surfaceDefinitions;
	};

}
#endif
