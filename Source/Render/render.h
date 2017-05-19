/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _RENDER_H_
#define _RENDER_H_
#ifndef NOMINMAX
	#define NOMINMAX 1
#endif
// #if defined(WIN32) && !defined(GL_BGR)
// #define GL_BGR GL_BGR_EXT
// #endif

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

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES

#ifdef HAVE_OPENGLES
	#include <SDL_opengl.h>
	#include <SDL_opengles.h>
	typedef GLfloat GLdouble; //opengles has no GLdouble
#else
	#include "gl3w/include/GL/gl3w.h"
	#include <SDL_opengl.h>
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

	extern bool RUNNING_IN_HEADLESS;

	//Built In Shaders
	extern const std::string DEFAULT_ID;
	extern const std::string PREMULTIPLY_ID;
	extern const std::string COLOR_PICKER_ID;
	extern const std::string ALPHA_FILTER_ID;

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

	class glExtensionBlendMode {
	public:
		glExtensionBlendMode(Draw2D *a_renderer);
		bool blendModeExtensionEnabled() { return initialized; }
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha);
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB);
		void setBlendEquation(GLenum a_rgbBlendFunc, GLenum a_alphaBlendFunc);
	protected:
		void loadExtensionBlendMode(char *a_extensionsList);
	private:
		bool initialized;
		Draw2D *renderer;
	};

	class glExtensionFramebufferObject;
	class Framebuffer : public std::enable_shared_from_this<Framebuffer> {
	public:
		~Framebuffer();

		std::shared_ptr<Framebuffer> start();
		void stop();

		void setTextureId(GLuint a_texture) {
			texture = a_texture;
		}
		GLuint textureId() const {
			return texture;
		}

		void setSize(const Size<int> &a_size) {
			frameSize = a_size;
		}
		Size<int> size() const {
			return frameSize;
		}
		void setPosition(const Point<int> &a_position) {
			framePosition = a_position;
		}
		Point<int> position() const {
			return framePosition;
		}
	private:
		friend glExtensionFramebufferObject;
		Framebuffer(Draw2D *a_renderer, GLuint a_framebuffer, GLuint a_renderbuffer, GLuint a_depthbuffer, GLuint a_texture, const Size<int> &a_size, const Point<int> &a_position, const Color &a_backgroundColor);

		GLuint framebuffer;
		GLuint renderbuffer;
		GLuint depthbuffer;
		GLuint texture;
		Size<int> frameSize;
		Point<int> framePosition;

		Color background;

		bool started;

		Draw2D *renderer;
	};

	class glExtensionFramebufferObject {
		friend Framebuffer;
	public:
		glExtensionFramebufferObject(Draw2D *a_renderer);

		bool framebufferObjectExtensionEnabled() { return initialized; }
		std::shared_ptr<Framebuffer> makeFramebuffer(const Point<int> &a_position, const Size<int> &a_size, GLuint a_texture, const Color &a_backgroundColor = Color(0.0f, 0.0f, 0.0f, 0.0f));

		void startUsingFramebuffer(std::weak_ptr<Framebuffer> a_framebuffer, bool a_push = true);
		void stopUsingFramebuffer();
	protected:
		GLint originalFramebufferId;
		GLint originalRenderbufferId;
		std::vector< std::weak_ptr<Framebuffer> > activeFramebuffers;

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
		glExtensions(Draw2D *a_renderer);
	protected:
		void initializeExtensions();
	private:
		Draw2D* renderer;
	};

	class Draw2D;
	struct ProjectionDetails {
		ProjectionDetails(const Draw2D &a_renderer):
			renderer(a_renderer){
		}
			
		Point<int> projectScreen(const Point<> &a_point, const TransformMatrix &a_modelview);
		Point<> projectWorld(const Point<> &a_point, const TransformMatrix &a_modelview);
		Point<> unProjectScreen(const Point<int> &a_point, const TransformMatrix &a_modelview);
		Point<> unProjectWorld(const Point<> &a_point, const TransformMatrix &a_modelview);

		const Draw2D &renderer;
	private:
		Point<> projectScreenRaw(const Point<> &a_point, const TransformMatrix &a_modelview);
		Point<> unProjectScreenRaw(const Point<> &a_point, const TransformMatrix &a_modelview);
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

	class Window {
		friend Draw2D;
	public:
		~Window();
		void setTitle(const std::string &a_title);

		MV::Size<int> resize(const Size<int> &a_size);
		Window& allowUserResize(bool a_maintainProportions = true, const Size<int> &a_minSize = Size<int>(1, 1), const Size<int> &a_maxSize = Size<int>(1000000, 1000000));
		Window& lockUserResize();

		bool resizeWorldWithWindow() const;
		Window& resizeWorldWithWindow(bool a_sizeWorldWithWindow);

		Window& windowedMode();
		void fullScreenMode();
		void fullScreenWindowedMode();

		Window& borderless();
		Window& bordered();

		int height() const;
		int width() const;
		Size<int> size() const;

		bool handleEvent(const SDL_Event &event, RenderWorld &a_world);
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
		bool sizeWorldWithWindow;
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

	class TextureDefinition;
	class TextureHandle;
	class Shader {
	public:
		Shader(const std::string &a_stringId, GLuint a_id, bool a_headless):
			stringId(a_stringId),
			programId(a_id),
			headless(a_headless){

			if (!headless) {
				if (!glIsProgram(a_id)) {
					std::cerr << "GL Program Id IS NOT A PROGRAM: " << a_id << std::endl;
				} else {
					int total = -1;
					glGetProgramiv(programId, GL_ACTIVE_UNIFORMS, &total);
					std::cout << "Shader Id: " << programId << std::endl;
					for (int i = 0; i < total; ++i) {
						int name_len = -1, num = -1;
						GLenum type = GL_ZERO;
						char name[256];
						glGetActiveUniform(programId, GLuint(i), sizeof(name) - 1, &name_len, &num, &type, name);
						name[name_len] = 0;
						GLuint location = glGetUniformLocation(programId, name);
						std::cout << "Shader Uniform: [" << name << "] = " << location << std::endl;
						variables[name] = location;
					}
					std::cout << "_" << std::endl;
				}
			}
		}

		std::string id() const{
			return stringId;
		}

		void use(){
			if (!headless) {
				glUseProgram(programId);
			}
		}

		void set(std::string a_variableName, const std::shared_ptr<TextureHandle> &a_texture, GLuint a_textureBindIndex = 0, bool a_errorIfNotPresent = true);
		void set(std::string a_variableName, const std::shared_ptr<TextureDefinition> &a_texture, GLuint a_textureBindIndex = 0, bool a_errorIfNotPresent = true);
		void set(std::string a_variableName, GLuint a_texture, GLuint a_textureBindIndex = 0, bool a_errorIfNotPresent = true);

		void set(std::string a_variableName, PointPrecision a_value, bool a_errorIfNotPresent = true);

		void set(std::string a_variableName, const TransformMatrix &a_matrix, bool a_errorIfNotPresent = true);

		bool has(std::string a_variableName) {
			return variableOffset(a_variableName) >= 0;
		}
	private:
		GLint variableOffset(const std::string &a_variableName){
			auto found = variables.find(a_variableName);
			if(found != variables.end()){
				return found->second;
			} else {
				auto foundLocation = glGetUniformLocation(programId, a_variableName.c_str());
				//might be -1 if missing, this is fine, avoid calling glGetUniformLocation next time.
				variables[a_variableName] = foundLocation;
				return foundLocation;
			}
		}
		std::string stringId;
		GLuint programId;
		std::map<std::string, GLuint> variables;
		bool headless;
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

		const MatrixStack& projectionMatrix() const{
			return contextProjectionMatrix;
		}

		Draw2D& makeHeadless() {
			require<ResourceException>(!initialized, "Renderer: Failed to make headless because we're already initialized!");
			isHeadless = true;
			RUNNING_IN_HEADLESS = true;
			return *this;
		}

		//call for every event to handle window actions correctly
		bool handleEvent(const SDL_Event &event);

		bool initialize(Size<int> a_window, Size<> a_world = Size<>(-1, -1), bool a_requireExtensions = false, bool a_summarize = false);

		Color backgroundColor(Color a_newColor);
		Color backgroundColor() const;
		
		void clearScreen();
		void updateScreen();

		Point<> worldFromLocal(const Point<> &a_localPoint, const TransformMatrix &a_modelview) const;
		Point<int> screenFromLocal(const Point<> &a_localPoint, const TransformMatrix &a_modelview) const;

		Point<> localFromWorld(const Point<> &a_worldPoint, const TransformMatrix &a_modelview) const;
		Point<> localFromScreen(const Point<int> &a_screenPoint, const TransformMatrix &a_modelview) const;

		Point<> screenFromWorldRaw(const Point<> &a_worldPoint) const;
		Point<int> screenFromWorld(const Point<> &a_worldPoint) const;

		Point<> worldFromScreen(const Point<int> &a_screenPoint) const;
		Point<> worldFromScreenRaw(const Point<> &a_screenPoint) const;
		
		void summarizeDisplayMode() const;

		void defaultBlendFunction();

		void loadDefaultShaders();
		Shader* loadShader(const std::string &a_id, const std::string &a_vertexShaderFilename, const std::string &a_fragmentShaderFilename);
		Shader* loadShaderCode(const std::string &a_id, const std::string &a_vertexShaderCode, const std::string &a_fragmentShaderCode);

		bool hasShader(const std::string &a_id);
		Shader* getShader(const std::string &a_id);

		Shader* defaultShader() const;
		Shader* defaultShader(const std::string &a_id);

		bool headless() const {
			return isHeadless;
		}

		//void registerShader(std::shared_ptr<Scene::Node> a_node);

		void checkGlError(std::string a_location = "[not supplied location]"){
			GLenum error = glGetError();
			if(error != GL_NO_ERROR) {
				std::cerr << "GL Error: (" << error << ")\nencountered in " << a_location << ".\n" << std::endl;
			}
		}

		void draw(GLenum drawType, std::shared_ptr<Scene::Node> a_node);
	private:
		void validateShaderStatus(GLuint a_id, bool a_isShader);
		void loadPartOfShader(GLuint a_id, const std::string &a_code);

		bool setupSDL();
		void setupOpengl();

		void refreshWorldAndWindowSize();

		Color clearBackgroundColor;
		bool initialized;
		SDL_Renderer *sdlRenderer;
		Window sdlWindow;
		RenderWorld mvWorld;

		MatrixStack contextProjectionMatrix;

		std::map<std::string, Shader> shaders;
		Shader* defaultShaderPtr = nullptr;

		bool isHeadless = false;

		static bool firstInitializationSDL;
		static bool firstInitializationOpenGL;
	};
	
	void checkSDLError(int line = -1);
}
#endif
