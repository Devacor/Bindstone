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
#include <unordered_map>
#include <fstream>
#include "MV/Render/points.h"
#include "MV/Render/matrix.hpp"
#include "MV/Render/device.h"   // RHI seam; Shader/Draw2D hold Render::Device + Bound* handles.
#include "MV/Utility/generalUtility.h"
#include <jaiscript/signals/signal_decl.hpp>

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
	//#include <SDL_opengl.h>
#include <SDL_opengles2.h>
//#include <OpenGLES/ES3/gl.h>
//#include <OpenGLES/ES3/glext.h>
//typedef GLfloat GLdouble; //opengles has no GLdouble

inline int gl3wInit() {
	return 0;
}
#else
#include <gl3w/include/GL/gl3w.h>
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
	
	enum BlendMode { 
		BLEND_DEFAULT, 
		BLEND_MULTIPLY, 
		BLEND_ADD, 
		BLEND_SCREEN 
	};
	namespace Scene {
		class Node;
	}
	class Draw2D;
	namespace Render { class Device; }

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
		glExtensionBlendMode(Draw2D* a_renderer);
		bool blendModeExtensionEnabled() { return initialized; }
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha);
		void setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB);
		void setBlendEquation(GLenum a_rgbBlendFunc, GLenum a_alphaBlendFunc);
	protected:
		void loadExtensionBlendMode(char* a_extensionsList);
	private:
		bool initialized;
		Draw2D* renderer;
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

		void setSize(const Size<int>& a_size) {
			frameSize = a_size;
		}
		Size<int> size() const {
			return frameSize;
		}
		void setPosition(const Point<int>& a_position) {
			framePosition = a_position;
		}
		Point<int> position() const {
			return framePosition;
		}
	private:
		friend glExtensionFramebufferObject;
		Framebuffer(Draw2D* a_renderer, GLuint a_framebuffer, GLuint a_renderbuffer, GLuint a_depthbuffer, GLuint a_texture, const Size<int>& a_size, const Point<int>& a_position, const Color& a_backgroundColor);

		GLuint framebuffer;
		GLuint renderbuffer;
		GLuint depthbuffer;
		GLuint texture;
		Size<int> frameSize;
		Point<int> framePosition;

		Color background;

		bool started;

		Draw2D* renderer;
	};

	class glExtensionFramebufferObject {
		friend Framebuffer;
	public:
		glExtensionFramebufferObject(Draw2D* a_renderer);

		bool framebufferObjectExtensionEnabled() { return initialized; }
		std::shared_ptr<Framebuffer> makeFramebuffer(const Point<int>& a_position, const Size<int>& a_size, GLuint a_texture, const Color& a_backgroundColor = Color(0.0f, 0.0f, 0.0f, 0.0f));

		void startUsingFramebuffer(std::weak_ptr<Framebuffer> a_framebuffer, bool a_push = true);
		void stopUsingFramebuffer();

		inline static GLint defaultFramebufferId() {
			return originalFramebufferId;
		}

		inline static GLint defaultRenderbufferId() {
			return originalFramebufferId;
		}
	protected:
		static GLint originalFramebufferId;
		static GLint originalRenderbufferId;
		std::vector< std::weak_ptr<Framebuffer> > activeFramebuffers;

		void loadExtensionFramebufferObject(char* a_extensionsList);
		void initializeOriginalBufferIds();
	private:
		void deleteFramebuffer(Framebuffer& a_framebuffer);
		Draw2D* renderer;
		bool initialized;

		Color savedClearColor;
	};

	class glExtensions :
		public glExtensionBlendMode,
		public glExtensionFramebufferObject
	{
	public:
		glExtensions(Draw2D* a_renderer);
	protected:
		void initializeExtensions();
	private:
		Draw2D* renderer;
	};

	class Draw2D;
	struct ProjectionDetails {
		ProjectionDetails(const Draw2D& a_renderer) :
			renderer(a_renderer) {
		}

		Point<int> projectScreen(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview);
		Point<> projectWorld(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview);
		Point<> unProjectScreen(const Point<int>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview);
		Point<> unProjectWorld(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview);

		const Draw2D& renderer;
	private:
		inline Point<> projectScreenRaw(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview, const MV::Point<>& a_viewOffset, const MV::Size<>& a_viewSize);
		inline Point<> unProjectScreenRaw(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview, const MV::Point<>& a_viewOffset, const MV::Size<>& a_viewSize);
	};

	class RenderWorld {
		friend Draw2D;
	public:
		void resize(const Size<>& a_size);

		const Size<>& size() const;
	private:
		RenderWorld(Draw2D& a_renderer);
		Size<> worldSize;

		Draw2D& renderer;
	};

	class Window {
		friend Draw2D;
	public:
		~Window();
		void setTitle(const std::string& a_title);

		MV::Size<int> resize(const Size<int>& a_size);
		Window& allowUserResize(bool a_maintainProportions = true, const Size<int>& a_minSize = Size<int>(1, 1), const Size<int>& a_maxSize = Size<int>(16384, 16384));
		Window& lockUserResize();

		bool resizeWorldWithWindow() const;
		Window& resizeWorldWithWindow(bool a_sizeWorldWithWindow);

		Window& windowedMode();
		Window& fullScreenMode();
		Window& fullScreenWindowedMode();

		Window& highResolution();
		Window& normalResolution();

		Window& borderless();
		Window& bordered();

		// Custom-chrome hit testing (borderless windows): the tester maps a window-space point to
		// SDL_HITTEST_DRAGGABLE / RESIZE_* / NORMAL so native drag, edge resize, snap and
		// double-click-maximize keep working. Empty function removes the hook.
		using HitTester = std::function<SDL_HitTestResult(const Point<int>& a_windowPoint, const Size<int>& a_windowSize)>;
		Window& hitTest(HitTester a_test);

		Window& minimize();
		Window& maximize();
		Window& restore();
		bool maximized() const;

		const Size<int>& drawableSize() const;
		const Size<int>& windowSize() const;

		bool handleEvent(const SDL_Event& event, RenderWorld& a_world);

		float windowDpi() const;
		float uiScale() const;
		constexpr float systemDefaultDpi() const;
	private:
		Window(Draw2D& a_renderer);
		bool initialize();
		void ensureValidGLContext();
		void refreshContext();
		void updateScreen();
		void updateWindowResizeLimits();
		void conformToAspectRatio(int& a_width, int& a_height) const;

		SDL_GLContext glcontext = 0;

		void updateAspectRatio();
		bool maintainProportions = true;
		bool sizeWorldWithWindow = false;
		Size<int> ourWindowSize;
		Size<int> ourDrawableSize;
		PointPrecision aspectRatio;
		uint32_t SDLflags;
		std::string title;

		bool vsync = false;
		bool userCanResize = false;
		Size<int> minSize;
		Size<int> maxSize;
		HitTester hitTester;

		SDL_Window* window = nullptr;
		Draw2D& renderer;
	};

	class TextureDefinition;
	class TextureHandle;
	
	namespace Scene {
		class Drawable;
	}
	
	class Shader {
		friend Draw2D;
	public:
		Shader(const std::string& a_stringId, GLuint a_id, const std::string& a_vertexFile = "", const std::string& a_fragmentFile = "") :
			stringId(a_stringId),
			programId(a_id),
			vertexShaderFile(a_vertexFile),
			fragmentShaderFile(a_fragmentFile) {
		}

		inline std::string id() const {
			return stringId;
		}

		bool set(const std::string& a_variableName, GLuint a_texture, GLuint a_textureBindIndex = 0, bool a_errorIfNotPresent = true);
		bool set(const std::string& a_variableName, const std::shared_ptr<TextureDefinition>& a_texture, GLuint a_textureBindIndex, bool a_errorIfNotPresent = true);
		// Raw-pointer overload for unmanaged definitions (spine atlas pages); the shared_ptr form delegates here.
		bool set(const std::string& a_variableName, TextureDefinition* a_texture, GLuint a_textureBindIndex, bool a_errorIfNotPresent = true);
		bool set(const std::string& a_variableName, const std::shared_ptr<TextureHandle>& a_value, GLuint a_textureBindIndex, bool a_errorIfNotPresent = true);

		inline bool set(std::string a_variableName, PointPrecision a_value, bool a_errorIfNotPresent = true) {
			if (recording()) {
				renderDevice->setUniform(recordingUniformSet, a_variableName, &a_value, 1);
				return true;
			}
			return false;
		}

		inline bool setVec2(const std::string& a_variableName, const Point<PointPrecision>& a_point, bool a_errorIfNotPresent = true) {
			if (recording()) {
				renderDevice->setUniform(recordingUniformSet, a_variableName, &a_point.x, 2);
				return true;
			}
			return false;
		}
		inline bool setVec3(const std::string& a_variableName, const Point<PointPrecision>& a_point, bool a_errorIfNotPresent = true) {
			if (recording()) {
				renderDevice->setUniform(recordingUniformSet, a_variableName, &a_point.x, 3);
				return true;
			}
			return false;
		}

		inline bool set(const std::string& a_variableName, const TransformMatrix& a_matrix, bool a_errorIfNotPresent = true) {
			if (recording()) {
				renderDevice->setUniformMatrix(recordingUniformSet, a_variableName, &((a_matrix.getMatrixArray())[0]));
				return true;
			}
			return false;
		}

		// Creates the backend shader modules from GLSL; after this, draw routes through pipelineFor + a recorded set.
		void initializeDeviceModules(Render::Device* a_device, const std::string& a_vertexSource, const std::string& a_fragmentSource) {
			renderDevice = a_device;
			if (!renderDevice) { return; }
			Render::ShaderModuleDesc vs; vs.stage = Render::ShaderStage::Vertex;   vs.glslSource = a_vertexSource;
			Render::ShaderModuleDesc fs; fs.stage = Render::ShaderStage::Fragment; fs.glslSource = a_fragmentSource;
			vertexModule   = renderDevice->createShaderModule(vs);
			fragmentModule = renderDevice->createShaderModule(fs);
		}
		// While a uniform set is open, set(...) records into it instead of issuing GL. Re-entrant:
		// returns the previous target so a nested draw (e.g. mid-draw atlas FBO refill on this same
		// shared Shader) can restore it and not clobber the outer draw's set.
		Render::BoundUniformSet beginRecording(Render::BoundUniformSet a_set) {
			Render::BoundUniformSet prev = recordingUniformSet;
			recordingUniformSet = a_set;
			return prev;
		}
		void endRecording(Render::BoundUniformSet a_restore = Render::BoundUniformSet{}) { recordingUniformSet = a_restore; }
		bool recording() const { return recordingUniformSet.valid(); }

		Render::Device* device() const { return renderDevice; }
		Render::BoundShaderModule vertexShaderModule() const { return vertexModule; }
		Render::BoundShaderModule fragmentShaderModule() const { return fragmentModule; }
		GLuint glProgramId() const { return programId; }   // pipeline-cache identity key.

	private:
		Render::BoundTexture getDefaultBoundTexture() const;
		Render::BoundSampler getDefaultBoundSampler() const;

		std::string stringId;
		std::string vertexShaderFile;
		std::string fragmentShaderFile;
		GLuint programId;

		// recordingUniformSet valid => set() records into it. Device-only: no legacy GL path.
		Render::Device*           renderDevice = nullptr;
		Render::BoundShaderModule vertexModule;
		Render::BoundShaderModule fragmentModule;
		Render::BoundUniformSet   recordingUniformSet;
	};

	struct MaterialContext {
		Draw2D& renderer;
		float time;
		const TransformMatrix& worldTransform;
		float alpha;
		int32_t cameraId;
		// Add other context data as needed
	};

	class Material {
	public:
		using ApplyFunction = std::function<void(Shader&, const MaterialContext&)>;
		
		Material(const std::string& id, const std::string& shaderProgramId)
			: ourId(id), ourShaderProgramId(shaderProgramId) {}
		
		Material& setApply(ApplyFunction applyFn) {
			applyFunction = applyFn;
			return *this;
		}
		
		Material& texture(size_t slot, std::shared_ptr<TextureHandle> tex) {
			defaultTextures[slot] = tex;
			return *this;
		}
		
		Material& blend(BlendMode mode) {
			blendMode = mode;
			return *this;
		}
		
		BlendMode blend() const { 
			return blendMode; 
		}
		
		void apply(Shader& shader, const MaterialContext& context) const;
		
		const std::string& id() const { return ourId; }
		const std::string& shaderProgramId() const { return ourShaderProgramId; }
		const std::map<size_t, std::shared_ptr<TextureHandle>>& textures() const { return defaultTextures; }
		
	private:
		std::string ourId;
		std::string ourShaderProgramId;
		ApplyFunction applyFunction;
		std::map<size_t, std::shared_ptr<TextureHandle>> defaultTextures;
		BlendMode blendMode = BLEND_DEFAULT;
	};

	//If attempting to make multiple instances of Draw2D bear in mind it modifies global state in the
	//projection matrices, OpenGL, and SDL.
	// Owned by Draw2D; the whole stencil-clip nesting state is one int of depth (each level's GPU
	// config is a pure function of depth, so nothing is stored per level). The Stencil component
	// brackets its children with push()/pop(): draw the mask shape incrementing the stencil, test
	// children == depth, then decrement and restore the enclosing level. reset() each frame so a
	// throw mid-clip can't poison later frames.
	class StencilStack {
	public:
		StencilStack(Draw2D& a_renderer) : renderer(a_renderer) {}

		void push(const std::function<void()>& a_drawMask);
		void pop(const std::function<void()>& a_drawMask);

		int current() const { return depth; }

	private:
		Render::StencilState writeState(Render::StencilOp a_op) const; // Always + a_op, ref=depth, writeMask 0xFF
		Render::StencilState testState() const;                        // Equal, ref=depth, writeMask 0x00

		Draw2D& renderer;
		int depth = 0;
	};

	class Draw2D : public glExtensions {
		friend Window;
		friend RenderWorld;

		jai::signal_emitter<void(int32_t)> onCameraUpdatedSignal;
	public:
		Draw2D();
		~Draw2D();

		jai::signal<void(int32_t)> onCameraUpdated;

		typedef jai::signal<void(int32_t)>::shared_receiver_type CameraRecieveType;

		Window& window();
		const Window& window() const;

		RenderWorld& world();
		const RenderWorld& world() const;

		MatrixStack& projectionMatrix() {
			return contextProjectionMatrix;
		}

		const MatrixStack& projectionMatrix() const {
			return contextProjectionMatrix;
		}

		TransformMatrix& camera(int32_t a_index) {
			cameraAccessedThisFrame[a_index] = true;
			return ourCameras[a_index];
		}

		const TransformMatrix& cameraProjectionMatrix(int32_t a_index) const {
			auto found = ourCameraProjectionMatrices.find(a_index);
			if (found != ourCameraProjectionMatrices.end()) {
				return found->second;
			}
			else {
				return contextProjectionMatrix.top();
			}
		}

		void clearCameraProjectionMatrices() {
			ourCameraProjectionMatrices.clear();
		}

		void updateCameraProjectionMatrices() {
			ourCameraProjectionMatrices.clear();
			for (auto&& kv : ourCameras) {
				ourCameraProjectionMatrices.emplace(kv.first, projectionMatrix().top() * kv.second);
			}
			for (auto&& kv : cameraAccessedThisFrame) {
				if (kv.second) {
					onCameraUpdatedSignal(kv.first);
					kv.second = false;
				}
			}
		}

		Draw2D& makeHeadless() {
			require<ResourceException>(!initialized, "Renderer: Failed to make headless because we're already initialized!");
			isHeadless = true;
			RUNNING_IN_HEADLESS = true;
			// A headless renderer still has a device — the inert null backend — so the scene draw path
			// (which requires a device) works without a GL context. Headless == "a device that does nothing".
			backend = Render::Device::makeHeadless();
			backend->initialize(Render::SwapchainDesc{});
			return *this;
		}

		//call for every event to handle window actions correctly
		bool handleEvent(const SDL_Event& event);

		bool initialize(Size<int> a_window, Size<> a_world = Size<>(-1, -1), bool a_requireExtensions = false, bool a_summarize = false);

		Color backgroundColor(Color a_newColor);
		Color backgroundColor() const;

		void clearScreen();
		void updateScreen();

		Point<> worldFromLocal(const Point<>& a_localPoint, int32_t a_cameraId, const TransformMatrix& a_modelview) const;
		Point<int> screenFromLocal(const Point<>& a_localPoint, int32_t a_cameraId, const TransformMatrix& a_modelview) const;

		Point<> localFromWorld(const Point<>& a_worldPoint, int32_t a_cameraId, const TransformMatrix& a_modelview) const;
		Point<> localFromScreen(const Point<int>& a_screenPoint, int32_t a_cameraId, const TransformMatrix& a_modelview) const;

		Point<> screenFromWorldRaw(const Point<>& a_worldPoint) const;
		Point<int> screenFromWorld(const Point<>& a_worldPoint) const;

		Point<> worldFromScreen(const Point<int>& a_screenPoint) const;
		Point<> worldFromScreenRaw(const Point<>& a_screenPoint) const;

		void summarizeDisplayMode() const;

		void defaultBlendFunction();

		void loadDefaultShaders();
		std::shared_ptr<Shader> loadShader(const std::string& a_id, const std::string& a_vertexShaderFilename, const std::string& a_fragmentShaderFilename);
		std::shared_ptr<Shader> loadShaderCode(const std::string& a_id, const std::string& a_vertexShaderCode, const std::string& a_fragmentShaderCode);

		// Shared BoundPipeline keyed by (shader program, blend, topology) so variants dedupe instead of
		// per-drawable allocating. Borrowed by Drawables; owned + destroyed here.
		Render::BoundPipeline pipelineFor(const std::shared_ptr<Shader>& a_shader, BlendMode a_blend, GLenum a_drawType, uint8_t a_colorWriteMask = 0xF);
		void clearPipelineCache();

		void reloadShaders();

		bool hasShader(const std::string& a_id);
		std::shared_ptr<Shader> getShader(const std::string& a_id);

		std::shared_ptr<Shader> defaultShader() const;
		std::shared_ptr<Shader> defaultShader(const std::string& a_id);

		bool headless() const {
			return isHeadless;
		}

		// The active RHI backend; owned here, borrowed by the texture system and scene draw path. Null until initialize().
		Render::Device* device() const {
			return backend.get();
		}

		// Clip-stencil nesting tracker; the Stencil component drives it via push/pop. See StencilStack.
		StencilStack& stencil() { return stencilStack; }

		// Material management
		std::shared_ptr<Material> loadMaterial(const std::string& a_id, const std::string& a_shaderProgramId);
		bool hasMaterial(const std::string& a_id) const;
		std::shared_ptr<Material> getMaterial(const std::string& a_id);
		std::shared_ptr<const Material> getMaterial(const std::string& a_id) const;
		
		std::shared_ptr<Material> defaultMaterial() const;
		std::shared_ptr<Material> defaultMaterial(const std::string& a_id);

		void checkGlError(std::string a_location = "[not supplied location]") {
			GLenum error = glGetError();
			if (error != GL_NO_ERROR) {
				std::cerr << "GL Error: (" << error << ")\nencountered in " << a_location << ".\n" << std::endl;
			}
		}

		void resetViewport();
		MV::Size<int> monitorSize();
	private:
		GLuint loadShaderGetProgramId(std::string a_vertexShaderCode, std::string a_fragmentShaderCode);

		void validateShaderStatus(GLuint a_id, bool a_isShader);
		void loadPartOfShader(GLuint a_id, const std::string& a_code);

		void setInitialSDLAttributes();
		void setupSDL();
		void setupOpengl();

		void refreshWorldAndWindowSize();

		Color clearBackgroundColor;
		bool initialized;
		SDL_Renderer* sdlRenderer;
		Window sdlWindow;
		RenderWorld mvWorld;

		MatrixStack contextProjectionMatrix;

		std::unordered_map<int32_t, bool> cameraAccessedThisFrame;
		std::unordered_map<int32_t, TransformMatrix> ourCameras;
		std::unordered_map<int32_t, TransformMatrix> ourCameraProjectionMatrices;

		std::unordered_map<std::string, std::shared_ptr<Shader>> shaders;
		std::shared_ptr<Shader> defaultShaderPtr = nullptr;

		// (program, blend, topology, colorWriteMask) -> shared pipeline. colorWriteMask is in the key so
		// the Stencil's no-color mask pipeline is distinct from the normal one. See pipelineFor.
		struct PipelineKey {
			GLuint program; BlendMode blend; GLenum topology; uint8_t colorWriteMask;
			bool operator==(const PipelineKey& o) const { return program == o.program && blend == o.blend && topology == o.topology && colorWriteMask == o.colorWriteMask; }
		};
		struct PipelineKeyHash {
			size_t operator()(const PipelineKey& k) const {
				return ((std::hash<GLuint>()(k.program) * 31u + static_cast<size_t>(k.blend)) * 31u + static_cast<size_t>(k.topology)) * 31u + k.colorWriteMask;
			}
		};
		std::unordered_map<PipelineKey, Render::BoundPipeline, PipelineKeyHash> pipelineCache;
		
		std::unordered_map<std::string, std::shared_ptr<Material>> materials;
		std::shared_ptr<Material> defaultMaterialPtr = nullptr;

		bool isHeadless = false;
		std::unique_ptr<Render::Device> backend;
		StencilStack stencilStack{ *this };

		static bool firstInitializationSDL;
		static bool firstInitializationOpenGL;
	};

	void checkSDLError(int line = -1);
}
#endif
