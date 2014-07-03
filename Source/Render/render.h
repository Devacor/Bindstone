/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _RENDER_H_
#define _RENDER_H_

#define NOMINMAX 1

#if defined(WIN32) && !defined(GL_BGR)
#define GL_BGR GL_BGR_EXT
#endif

#if !defined(WIN32) && !defined(GLNULL)
#define GLNULL GLuint(NULL)
#endif

#ifndef GLNULL
#define GLNULL 0
#endif

#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <fstream>
#include "Render/points.h"
#include "Render/matrix.h"

#ifdef __APPLE__
	#import "TargetConditionals.h" 
	#ifdef TARGET_OS_IPHONE
		#define __IPHONEOS__ 1
	#endif
#endif

#if defined(TARGET_OS_IPHONE) || defined(__ANDROID__)
	#define HAVE_OPENGLES 1
#endif

#include <SDL.h>

#ifdef HAVE_OPENGLES
	#include <SDL_opengl.h>
	#include <SDL_opengles.h>
	typedef GLfloat GLdouble; //opengles has no GLdouble
#else
	#include "gl3w/include/GL/gl3w.h"
#endif

#ifdef GL_DEPTH_COMPONENT24
	#define GL_DEPTH_COMPONENT_DEFAULT GL_DEPTH_COMPONENT24
#elif GL_DEPTH_COMPONENT24_OES
	#define GL_DEPTH_COMPONENT_DEFAULT GL_DEPTH_COMPONENT24_OES
#else
	#define GL_DEPTH_COMPONENT_DEFAULT GL_DEPTH_COMPONENT16
#endif

#include <string>
#include <iostream>

namespace MV {
	namespace Scene {
		class Node;
	}
	class Draw2D;

	extern const std::string DEFAULT_ID;

	#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	  const Uint32 SDL_RMASK = 0xff000000;
	  const Uint32 SDL_GMASK = 0x00ff0000;
	  const Uint32 SDL_BMASK = 0x0000ff00;
	  const Uint32 SDL_AMASK = 0x000000ff;
	#else
	  const Uint32 SDL_RMASK = 0x000000ff;
	  const Uint32 SDL_GMASK = 0x0000ff00;
	  const Uint32 SDL_BMASK = 0x00ff0000;
	  const Uint32 SDL_AMASK = 0xff000000;
	#endif

	class glExtensionBlendMode{
	public:
		glExtensionBlendMode();
		bool blendModeExtensionEnabled(){return initialized;}
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha);
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB);
		void setBlendEquation(GLenum a_rgbBlendFunc, GLenum a_alphaBlendFunc);
	protected:
		void loadExtensionBlendMode(char *a_extensionsList);
	private:
		bool initialized;
	};

	class glExtensionFramebufferObject;
	class Framebuffer : public std::enable_shared_from_this<Framebuffer> {
	public:
		~Framebuffer();

		void start();
		void stop();

		void setTextureId(GLuint a_texture){
			texture = a_texture;
		}
		GLuint textureId() const{
			return texture;
		}

		void setSize(const Size<int> &a_size){
			frameSize = a_size;
		}
		Size<int> size() const{
			return frameSize;
		}
		void setPosition(const Point<int> &a_position){
			framePosition = a_position;
		}
		Point<int> position() const{
			return framePosition;
		}
	private:
		friend glExtensionFramebufferObject;
		Framebuffer(Draw2D *a_renderer, GLuint a_framebuffer, GLuint a_renderbuffer, GLuint a_depthbuffer, GLuint a_texture, const Size<int> &a_size, const Point<int> &a_position);

		GLuint framebuffer;
		GLuint renderbuffer;
		GLuint depthbuffer;
		GLuint texture;
		Size<int> frameSize;
		Point<int> framePosition;

		Draw2D *renderer;
	};

	class glExtensionFramebufferObject{
		friend Framebuffer;
	public:
		glExtensionFramebufferObject(Draw2D *a_renderer);

		bool framebufferObjectExtensionEnabled(){return initialized;}
		std::shared_ptr<Framebuffer> makeFramebuffer(const Point<int> &a_position, const Size<int> &a_size, GLuint a_texture);

		void startUsingFramebuffer(std::shared_ptr<Framebuffer> a_framebuffer, bool a_push = true);
		void stopUsingFramebuffer();
	protected:
		GLint originalFramebufferId;
		GLint originalRenderbufferId;
		std::vector< std::shared_ptr<Framebuffer> > activeFramebuffers;

		void loadExtensionFramebufferObject(char* a_extensionsList);
		void initializeOriginalBufferIds();
	private:
		void deleteFramebuffer(Framebuffer &a_framebuffer);
		Draw2D *renderer;
		bool initialized;
		Color savedClearColor;
	};

	class glExtensions :
		public glExtensionBlendMode,
		public glExtensionFramebufferObject
	{
	public:
		glExtensions(Draw2D *a_renderer):
			glExtensionFramebufferObject(a_renderer){
		}
	protected:
		void initializeExtensions(){
			char* extensionsList = (char*) glGetString(GL_EXTENSIONS);
			if(!extensionsList){
				std::cerr << "ERROR: Could not load extensions list from glGetString(GL_EXTENSIONS)" << std::endl;
			}else{
				loadExtensionBlendMode(extensionsList);
				loadExtensionFramebufferObject(extensionsList);
			}
		}
	};

	class Draw2D;
	struct ProjectionDetails {
		ProjectionDetails(const Draw2D &a_renderer):
			renderer(a_renderer){
		}
			
		Point<int> projectScreen(const Point<> &a_point);
		Point<> projectWorld(const Point<> &a_point);
		Point<> unProjectScreen(const Point<int> &a_point);
		Point<> unProjectWorld(const Point<> &a_point);

		const Draw2D &renderer;
	};

	class Window {
		friend Draw2D;
	public:
		~Window();
		void setTitle(const std::string &a_title);

		void resize(const Size<int> &a_size);
		Window& allowUserResize(bool a_maintainProportions = true, const Size<int> &a_minSize = Size<int>(1, 1), const Size<int> &a_maxSize = Size<int>(1000000, 1000000));
		Window& lockUserResize();

		Window& windowedMode();
		void fullScreenMode();
		void fullScreenWindowedMode();

		Window& borderless();
		Window& bordered();

		int height() const;
		int width() const;
		Size<int> size() const;

		bool handleEvent(const SDL_Event &event);
	private:
		Window(Draw2D &a_renderer);
		bool initialize();
		void ensureValidGLContext();
		void refreshContext();
		void updateScreen();
		void updateWindowResizeLimits();
		void conformToAspectRatio(int &a_width, int &a_height) const;

		SDL_GLContext glcontext;
		bool initialized;

		void updateAspectRatio();
		bool maintainProportions;
		Size<int> windowSize;
		PointPrecision aspectRatio;
		uint32_t SDLflags;
		std::string title;

		bool vsync;
		bool userCanResize;
		Size<int> minSize;
		Size<int> maxSize;

		SDL_Window *window;
		Draw2D &renderer;
	};

	class RenderWorld {
		friend Draw2D;
	public:
		void resize(const Size<> &a_size);

		PointPrecision height() const;
		PointPrecision width() const;
		Size<> size() const;
	private:
		RenderWorld(Draw2D& a_renderer);
		Size<> worldSize;

		Draw2D &renderer;
	};

	class TextureDefinition;
	class TextureHandle;
	class Shader {
	public:
		Shader(const std::string &a_stringId, GLuint a_id):
			stringId(a_stringId),
			programId(a_id){
			if(!glIsProgram(a_id)){
				std::cerr << "GL Program Id IS NOT A PROGRAM: " << a_id << std::endl;
			} else{
				int total = -1;
				glGetProgramiv(programId, GL_ACTIVE_UNIFORMS, &total);
				std::cout << "Shader Id: " << programId << std::endl;
				for(int i = 0; i < total; ++i)  {
					int name_len = -1, num = -1;
					GLenum type = GL_ZERO;
					char name[256];
					glGetActiveUniform(programId, GLuint(i), sizeof(name)-1, &name_len, &num, &type, name);
					name[name_len] = 0;
					GLuint location = glGetUniformLocation(programId, name);
					std::cout << "Shader Uniform: [" << name << "] = " << location << std::endl;
					variables[name] = location;
				}
				std::cout << "_" << std::endl;
			}

		}

		std::string id() const{
			return stringId;
		}

		void use(){
			glUseProgram(programId);
		}

		void set(std::string a_variableName, const std::shared_ptr<TextureHandle> &a_texture, GLuint a_textureBindIndex = 0);
		void set(std::string a_variableName, const std::shared_ptr<TextureDefinition> &a_texture, GLuint a_textureBindIndex = 0);
		void set(std::string a_variableName, GLuint a_texture, GLuint a_textureBindIndex = 0);

		void set(std::string a_variableName, PointPrecision a_value);

		void set(std::string a_variableName, const TransformMatrix &a_matrix);
	private:
		GLint variableOffset(const std::string &a_variableName){
			auto found = variables.find(a_variableName);
			if(found != variables.end()){
				return found->second;
			} else{
				auto foundLocation = glGetUniformLocation(programId, a_variableName.c_str());
				if(foundLocation >= 0){
					variables[a_variableName] = foundLocation;
					return foundLocation;
				}
			}
			return -1;
		}
		std::string stringId;
		GLuint programId;
		std::map<std::string, GLuint> variables;
	};

	//If attempting to make multiple instances of Draw2D bear in mind it modifies global state in the
	//projection matrices, OpenGL, and SDL.
	class Draw2D : public glExtensions {
		friend Window;
		friend RenderWorld;
	public:
		Draw2D();
		~Draw2D();

		Window& window();
		const Window& window() const;

		RenderWorld& world();
		const RenderWorld& world() const;

		MatrixStack& projectionMatrix(){
			return contextProjectionMatrix;
		}

		MatrixStack& modelviewMatrix(){
			return contextModelviewMatrix;
		}

		const MatrixStack& projectionMatrix() const{
			return contextProjectionMatrix;
		}

		const MatrixStack& modelviewMatrix() const{
			return contextModelviewMatrix;
		}

		//call for every event to handle window actions correctly
		bool handleEvent(const SDL_Event &event);

		bool initialize(Size<int> a_window, Size<> a_world = Size<>(-1, -1), bool a_requireExtensions = false, bool a_summarize = false);

		Color backgroundColor(Color a_newColor);
		Color backgroundColor() const;
		
		void clearScreen();
		void updateScreen();

		Point<> worldFromLocal(const Point<> &a_localPoint ) const;
		Point<int> screenFromLocal(const Point<> &a_localPoint ) const;

		Point<> localFromWorld(const Point<> &a_worldPoint) const;
		Point<> localFromScreen(const Point<int> &a_screenPoint) const;

		Point<int> screenFromWorld(const Point<> &a_worldPoint) const;
		Point<> worldFromScreen(const Point<int> &a_screenPoint) const;
		
		void summarizeDisplayMode() const;

		void defaultBlendFunction();

		Shader* loadShader(const std::string &a_id, const std::string &a_vertexShaderFilename, const std::string &a_fragmentShaderFilename);
		Shader* loadShaderCode(const std::string &a_id, const std::string &a_vertexShaderCode, const std::string &a_fragmentShaderCode);

		bool hasShader(const std::string &a_id);
		Shader* getShader(const std::string &a_id);

		Shader* defaultShader() const;
		Shader* defaultShader(GLuint a_newId);
		Shader* defaultShader(const std::string &a_id);

		void registerShader(std::shared_ptr<Scene::Node> a_node);

		void checkGlError(std::string a_location = "[not supplied location]"){
			GLenum error = glGetError();
			if(error != GL_NO_ERROR) {
				std::cerr << "GL Error: (" << error << ")\nencountered in " << a_location << ".\n" << std::endl;
			}
		}
	private:
		void validateShaderStatus(GLuint a_id, bool a_isShader);
		void loadPartOfShader(GLuint a_id, const std::string &a_code);

		bool setupSDL();
		void setupOpengl();

		Color clearBackgroundColor;
		bool initialized;
		SDL_Renderer *sdlRenderer;
		Window sdlWindow;
		RenderWorld mvWorld;

		MatrixStack contextProjectionMatrix;
		MatrixStack contextModelviewMatrix;

		std::map<std::string, Shader> shaders;
		Shader* defaultShaderPtr = nullptr;

		static bool firstInitializationSDL;
		static bool firstInitializationOpenGL;

		std::vector<std::shared_ptr<Scene::Node>> needShaderRegistration;
	};
	
	void checkSDLError(int line = -1);
}
#endif
