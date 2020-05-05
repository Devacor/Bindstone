#include "render.h"
#include "Scene/node.h"
#include "textures.h"
#include "sharedTextures.h"
#include "MV/Utility/generalUtility.h"
#include "MV/Utility/stringUtility.h"
#include "MV/Utility/stopwatch.h"

namespace MV {
	const std::string DEFAULT_ID = "default";
	const std::string PREMULTIPLY_ID = "premultiply";
	const std::string COLOR_PICKER_ID = "colorPicker";
	const std::string ALPHA_FILTER_ID = "alphaFilter";

	bool RUNNING_IN_HEADLESS = false;

	bool Draw2D::firstInitializationSDL = true;
	bool Draw2D::firstInitializationOpenGL = true;

	static GLint viewport[4];

	GLint glExtensionFramebufferObject::originalFramebufferId = 0;
	GLint glExtensionFramebufferObject::originalRenderbufferId = 0;

	Point<> ProjectionDetails::projectScreenRaw(const Point<> &a_point, int32_t a_cameraId, const TransformMatrix &a_modelview, const MV::Point<>& a_viewOffset, const MV::Size<>& a_viewSize) {
		auto v4Result = renderer.cameraProjectionMatrix(a_cameraId) * fullMatrixPointMultiply(a_modelview, a_point);
		if (MV::equals(v4Result[3], 0.0f)) {
			MV::error("projectScreen projection failure, point is exactly at the clip pane.");
			return {};
		}
		Point<> result(v4Result[0] / v4Result[3], v4Result[1] / v4Result[3], v4Result[2] / v4Result[3]);

		// Map x, y and z to range 0-1 (was -1 to +1)
		result.x = result.x * .5f + .5f;
		result.y = result.y * .5f + .5f;
		result.z = result.z * .5f + .5f;

		// Map x,y to viewport
		result.x = (result.x * a_viewSize.width) + a_viewOffset.x;
		result.y = a_viewSize.height - ((result.y * a_viewSize.height) + a_viewOffset.y); //invert y

		// Return z to original (for 2D points, remove for 3D)!
		result.z = a_point.z;

		return result;
	}

	Point<int> ProjectionDetails::projectScreen(const Point<> &a_point, int32_t a_cameraId, const TransformMatrix &a_modelview){
		return round<int>(projectScreenRaw(a_point, a_cameraId, a_modelview, 
			{ static_cast<PointPrecision>(viewport[0]), static_cast<PointPrecision>(viewport[1]) }, 
			{ static_cast<PointPrecision>(viewport[2]), static_cast<PointPrecision>(viewport[3]) }));
	}

	Point<> ProjectionDetails::projectWorld(const Point<> &a_point, int32_t a_cameraId, const TransformMatrix &a_modelview){
		return projectScreenRaw(a_point, a_cameraId, a_modelview, 
			{ 0.0f, 0.0f }, { renderer.world().size().width, renderer.world().size().height });
	}

	Point<> ProjectionDetails::unProjectScreenRaw(const Point<>& a_point, int32_t a_cameraId, const TransformMatrix& a_modelview, const MV::Point<>& a_viewOffset, const MV::Size<>& a_viewSize) {
		auto result = a_point;

		float det;
		auto conversionMatrix = inverse(renderer.cameraProjectionMatrix(a_cameraId) * a_modelview, det);
		if (MV::equals(det, 0.0f)) {
			MV::error("unProjectScreen failure, point is exactly at the clip pane.");
			result.clear();
		} else {
			// Map x and y from window coordinates 
			result.x = (result.x - a_viewOffset.x) / a_viewSize.width;
			result.y = ((a_viewSize.height - result.y) - a_viewOffset.y) / a_viewSize.height;

			// Map to range -1 to 1 
			result.x = result.x * 2.0f - 1.0f;
			result.y = result.y * 2.0f - 1.0f;
			result.z = result.z * 2.0f - 1.0f;

			PointPrecision w;
			result = fullMatrixPointMultiply(conversionMatrix, result, w);
			if (MV::equals(w, 0.0f)) {
				MV::error("unProjectScreen	failure, point is exactly at the clip pane.");
				result.clear();
			} else {
				result /= w;
				result.z = a_point.z; //restore original z since we're just 2d.
			}
		}
		return result;
	}

	Point<> ProjectionDetails::unProjectScreen(const Point<int> &a_point, int32_t a_cameraId, const TransformMatrix &a_modelview) {
		return unProjectScreenRaw(cast<PointPrecision>(a_point), a_cameraId, a_modelview, 
			{ static_cast<PointPrecision>(viewport[0]), static_cast<PointPrecision>(viewport[1]) }, 
			{ static_cast<PointPrecision>(viewport[2]), static_cast<PointPrecision>(viewport[3]) });
	}

	Point<> ProjectionDetails::unProjectWorld(const Point<> &a_point, int32_t a_cameraId, const TransformMatrix &a_modelview){
		return unProjectScreenRaw(a_point, a_cameraId, a_modelview, 
			{ 0.0f, 0.0f }, { renderer.world().size().width, renderer.world().size().height });
	}

	void checkSDLError(int line)
	{
		const char *error = SDL_GetError();
		while (*error != '\0')
		{
			std::cerr << "SDL Error: " << error;
			if (line != -1){
				std::cerr << " Line: " << line;
			}
			std::cerr << std::endl;
			SDL_ClearError();
			error = SDL_GetError();
		}
	}

	/*************************\
	| ------Extensions------- |
	\*************************/

	glExtensionBlendMode::glExtensionBlendMode(Draw2D *a_renderer) :renderer(a_renderer), initialized(false)
	{
	}

	void glExtensionBlendMode::setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha){
		if (!renderer->headless()) {
			if (initialized) {
				glBlendFuncSeparate(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
			} else {
				glBlendFunc(a_sfactorRGB, a_dfactorRGB);
			}
		}
	}

	void glExtensionBlendMode::setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB){
		if (!renderer->headless()) {
			glBlendFunc(a_sfactorRGB, a_dfactorRGB);
		}
	}

	void glExtensionBlendMode::setBlendEquation(GLenum a_rgbBlendFunc, GLenum a_alphaBlendFunc){
		if(initialized){
			glBlendEquationSeparate(a_rgbBlendFunc, a_alphaBlendFunc);
		}
	}

	void glExtensionBlendMode::loadExtensionBlendMode( char *a_extensionsList ){
		initialized = true;
	}

	glExtensionFramebufferObject::glExtensionFramebufferObject(Draw2D *a_renderer)
		:renderer(a_renderer),
		initialized(false)
		{
	}

	void glExtensionFramebufferObject::initializeOriginalBufferIds(){
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &originalFramebufferId);
		glGetIntegerv(GL_RENDERBUFFER_BINDING, &originalRenderbufferId);
	}

	std::shared_ptr<Framebuffer> glExtensionFramebufferObject::makeFramebuffer(const Point<int> &a_position, const Size<int> &a_size, GLuint a_texture, const Color &a_backgroundColor){
		require<ResourceException>(renderer->headless() || initialized, "CreateFramebuffer failed because the extension could not be loaded");
		GLuint framebufferId = 0, renderbufferId = 0, depthbufferId = 0;
		if (!renderer->headless()) {
			glGenFramebuffers(1, &framebufferId);
			glGenRenderbuffers(1, &renderbufferId);
			//glGenRenderbuffers(1, &depthbufferId); //not used right now
		}
		return std::shared_ptr<Framebuffer>(new Framebuffer(renderer, framebufferId, renderbufferId, depthbufferId, a_texture, a_size, a_position, a_backgroundColor));
	}

	void glExtensionFramebufferObject::startUsingFramebuffer(std::weak_ptr<Framebuffer> a_framebuffer, bool a_push){
		require<ResourceException>(renderer->headless() || (initialized && !a_framebuffer.expired()), "StartUsingFramebuffer failed because the extension could not be loaded");

		savedClearColor = renderer->backgroundColor();
		auto sharedFramebuffer = a_framebuffer.lock();
		renderer->backgroundColor(sharedFramebuffer->background);

		if(a_push){
			activeFramebuffers.push_back(a_framebuffer);
		}
		if (!renderer->headless()) {
//#ifdef WIN32
        
			glBindFramebuffer(GL_FRAMEBUFFER, sharedFramebuffer->framebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sharedFramebuffer->texture, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, sharedFramebuffer->renderbuffer);

			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT_DEFAULT, roundUpPowerOfTwo(sharedFramebuffer->frameSize.width), roundUpPowerOfTwo(sharedFramebuffer->frameSize.height));

/*#else
			int width = roundUpPowerOfTwo(a_framebuffer.frameSize.width);
			int height = roundUpPowerOfTwo(a_framebuffer.frameSize.height);

			glBindFramebufferOES(GL_FRAMEBUFFER_OES, a_framebuffer.framebuffer);
			glBindRenderbufferOES(GL_RENDERBUFFER_OES, a_framebuffer.renderbuffer);

			glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_RGBA, width, height);
			glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_RENDERBUFFER_OES, a_framebuffer.renderbuffer);

			glBindRenderbufferOES(GL_RENDERBUFFER_OES, a_framebuffer.depthbuffer);
			glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, width, height);
			glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, a_framebuffer.depthbuffer);

			glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, a_framebuffer.texture, 0);

			if (glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES) {
				std::cout << "Start Using Framebuffer failure: " << glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) << std::endl;
			}
#endif*/
			glViewport(sharedFramebuffer->framePosition.x, sharedFramebuffer->framePosition.y, sharedFramebuffer->frameSize.width, sharedFramebuffer->frameSize.height);
			glGetIntegerv(GL_VIEWPORT, viewport);
		}
		//.makeOrtho(0, mvWorld.width(), mvWorld.height(), 0, -1.0, 1.0);
		renderer->projectionMatrix().push().makeOrtho(static_cast<PointPrecision>(sharedFramebuffer->framePosition.x), static_cast<PointPrecision>(sharedFramebuffer->framePosition.x + sharedFramebuffer->frameSize.width), static_cast<PointPrecision>(sharedFramebuffer->framePosition.y), static_cast<PointPrecision>(sharedFramebuffer->framePosition.y + sharedFramebuffer->frameSize.height), -128.0f, 128.0f);

		GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
		//pglDrawBuffersEXT(1, buffers);
        
		renderer->clearScreen();
		renderer->clearCameraProjectionMatrices(); //important after clearScreen;
	}

	void glExtensionFramebufferObject::stopUsingFramebuffer(){
		require<ResourceException>(renderer->headless() || initialized, "StopUsingFramebuffer failed because the extension could not be loaded");
		activeFramebuffers.pop_back();
		if(!activeFramebuffers.empty()){
			startUsingFramebuffer(activeFramebuffers.back(), false);
		} else {
			if (!renderer->headless()) {
				glBindFramebuffer(GL_FRAMEBUFFER, originalFramebufferId);
				glBindRenderbuffer(GL_RENDERBUFFER, originalRenderbufferId);

				renderer->resetViewport();
			}
			renderer->projectionMatrix().pop();
			renderer->updateCameraProjectionMatrices();
			renderer->backgroundColor(savedClearColor);
		}
	}
	
	void glExtensionFramebufferObject::deleteFramebuffer( Framebuffer &a_framebuffer ){
		if (!renderer->headless()) {
			glDeleteFramebuffers(1, &a_framebuffer.framebuffer);
			glDeleteRenderbuffers(1, &a_framebuffer.renderbuffer);
			//glDeleteRenderbuffers(1, &a_framebuffer.depthbuffer); //not used currently
		}
		a_framebuffer.framebuffer = 0;
		a_framebuffer.renderbuffer = 0;
		a_framebuffer.depthbuffer = 0;
	}

	void glExtensionFramebufferObject::loadExtensionFramebufferObject( char* a_extensionsList ){
		initialized = true;
		initializeOriginalBufferIds();
	}

	/*************************\
	| --------Window--------- |
	\*************************/

	Window::Window(Draw2D &a_renderer):
        SDLflags(SDL_WINDOW_OPENGL),
        renderer(a_renderer){
		
		updateAspectRatio();
	}

	Window::~Window(){
		if (!renderer.headless()) {
			SDL_GL_DeleteContext(glcontext);
			SDL_DestroyWindow(window);
		}
	}

	void Window::setTitle(const std::string &a_title){
		title = a_title;
		//seems to mess up Android applications and possibly not good on iOS so let's ifdef this out for anything but Windows for now.
#ifdef WIN32
		if (window) {
			SDL_SetWindowTitle(window, title.c_str());
		}
#endif
	}

	MV::Size<int> Window::resize(const Size<int> &a_size){
		auto oldDrawableSize = ourDrawableSize;
		ourWindowSize = a_size;
		ourDrawableSize = ourWindowSize;
		if(window){
			if (!renderer.headless()) {
				SDL_SetWindowSize(window, ourWindowSize.width, ourWindowSize.height);
				SDL_GL_GetDrawableSize(window, &ourDrawableSize.width, &ourDrawableSize.height);
				SDL_GetWindowSize(window, &ourWindowSize.width, &ourWindowSize.height);
			}
			updateAspectRatio();
			renderer.setupOpengl();
			if(!userCanResize){
				lockUserResize();
			}
		} else {
			updateAspectRatio();
		}
		return oldDrawableSize - ourDrawableSize;
	}

	Window& Window::allowUserResize(bool a_maintainProportions, const Size<int> &a_minSize, const Size<int> &a_maxSize){
		maintainProportions = a_maintainProportions;
		minSize = a_minSize;
		maxSize = a_maxSize;
		userCanResize = true;
		updateWindowResizeLimits();
		return *this;
	}

	Window& Window::lockUserResize(){
		userCanResize = false;
		if(window){
			minSize = Size<int>(ourWindowSize.width, ourWindowSize.height);
			maxSize = minSize;
		}else{
			minSize = Size<int>(ourWindowSize.width, ourWindowSize.height);
			maxSize = minSize;
		}
		updateWindowResizeLimits();
		return *this;
	}

	void Window::updateWindowResizeLimits(){
		if(window && !renderer.headless()){
			SDL_SetWindowMinimumSize(window, std::max(minSize.width, 1), std::max(minSize.height, 1));
			SDL_SetWindowMaximumSize(window, std::max(maxSize.width, 1), std::max(maxSize.height, 1));
			SDL_GL_GetDrawableSize(window, &ourDrawableSize.width, &ourDrawableSize.height);
			SDL_GetWindowSize(window, &ourWindowSize.width, &ourWindowSize.height);
			updateAspectRatio();
			checkSDLError(__LINE__);
		}
	}

	Window& Window::windowedMode(){
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(window && !renderer.headless()){
			SDL_SetWindowFullscreen(window, 0);
		}
		return *this;
	}
    
    Window& Window::highResolution(){
        SDLflags = SDLflags | SDL_WINDOW_ALLOW_HIGHDPI;
        return *this;
    }
    
    Window& Window::normalResolution(){
        SDLflags = SDLflags & ~ SDL_WINDOW_ALLOW_HIGHDPI;
        return *this;
    }

	Window& Window::fullScreenMode(){
		SDLflags = SDLflags & ~SDL_WINDOW_FULLSCREEN_DESKTOP;
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN;
		if(window && !renderer.headless()){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
		}
        return *this;
	}

	Window& Window::fullScreenWindowedMode(){
		SDLflags = SDLflags & ~SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(window && !renderer.headless()){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
        return *this;
	}
	
	Window& Window::borderless(){
		SDLflags = SDLflags | SDL_WINDOW_BORDERLESS;
		if(window && !renderer.headless()){
			SDL_SetWindowBordered(window, SDL_FALSE);
		}
		return *this;
	}

	Window& Window::bordered(){
		SDLflags = SDLflags & ~ SDL_WINDOW_BORDERLESS;
		if(window && !renderer.headless()){
			SDL_SetWindowBordered(window, SDL_TRUE);
		}
		return *this;
	}

	const Size<int>& Window::drawableSize() const{
		return ourDrawableSize;
	}

	const Size<int>& Window::windowSize() const {
		return ourWindowSize;
	}

	void Window::updateAspectRatio(){
		aspectRatio = (ourDrawableSize.height != 0) ? static_cast<PointPrecision>(ourDrawableSize.width) / static_cast<PointPrecision>(ourDrawableSize.height) : 0.0f;
	}

	void Window::conformToAspectRatio(int &a_width, int &a_height) const{
		if(a_width < a_height){
			a_width = static_cast<int>(a_height * aspectRatio);
		}else{
			a_height = static_cast<int>(a_width / aspectRatio);
		}
	}

	bool Window::initialize(){
		if (!window && !renderer.headless()) {
			window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, ourWindowSize.width, ourWindowSize.height, SDLflags);
			if (!window) {
				std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit); // Quit SDL at exit.
				return false;
			}
			if (!title.empty()) {
				SDL_SetWindowTitle(window, title.c_str());
			}
			checkSDLError(__LINE__);

			SDL_GetWindowSize(window, &ourWindowSize.width, &ourWindowSize.height);
			SDL_GL_GetDrawableSize(window, &ourDrawableSize.width, &ourDrawableSize.height);
			updateAspectRatio();
			updateWindowResizeLimits();
			checkSDLError(__LINE__);

			SDL_DisplayMode displayMode;
			SDL_zero(displayMode);
			displayMode.format = SDL_PIXELFORMAT_RGB888;

			if (SDL_SetWindowDisplayMode(window, &displayMode) < 0) {
				std::cerr << "Window display mode failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
				return false;
			}
			checkSDLError(__LINE__);

			SDL_ShowWindow(window);
			checkSDLError(__LINE__);
		}

		if(!userCanResize){
			lockUserResize();
		}

		return true;
	}

	void Window::ensureValidGLContext(){
		if(!glcontext && !renderer.headless()){
			glcontext = SDL_GL_CreateContext(window);
			if(!glcontext){
				std::cerr << "OpenGL context failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			if(renderer.firstInitializationOpenGL){
				MV::require<MV::PointerException>(gl3wInit() == 0, "gl3wInit failed!");
				renderer.firstInitializationOpenGL = false;
			}

			if (SDL_GL_MakeCurrent(window, glcontext)) {
				std::cerr << "SDL_GL_MakeCurrent(): " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}

#ifndef HAVE_OPENGLES
			if (vsync && SDL_GL_SetSwapInterval(-1) == -1) { //set adaptave vsync (-1), but if it fails it returns -1
				SDL_GL_SetSwapInterval(1); //on
			} else {
				SDL_GL_SetSwapInterval(0); //off
			}
#endif
		}
	}

	constexpr float Window::systemDefaultDpi() const {
#if defined(__APPLE__)
#	if TARGET_OS_MAC
			return 72.0f;
#	else
			return 132.0f
#	endif
#elif defined(__ANDROID__)
			return 160.0f;
#elif defined(_WIN32)
			return 96.0f;
#endif
		return 0;
	}

	float Window::windowDpi() const {
		float dpiResult;

		if (SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), NULL, &dpiResult, NULL) != 0) {
			dpiResult = systemDefaultDpi();
		}

		return dpiResult;
	}

	float Window::uiScale() const {
		return windowDpi() / systemDefaultDpi();
	}

	bool Window::handleEvent(const SDL_Event &a_event, RenderWorld &a_world){
		if(a_event.type == SDL_WINDOWEVENT){
			if (a_event.window.event == SDL_WINDOWEVENT_RESIZED) {
				MV::info("Window Resized!");
				auto worldScreenDelta = cast<PointPrecision>(ourDrawableSize) / a_world.size();
				Size<int> newSize(std::min(std::max(a_event.window.data1, minSize.width), maxSize.width), std::min(std::max(a_event.window.data2, minSize.height), maxSize.height));
				MV::PointPrecision oldRatio = aspectRatio;
				if (maintainProportions) {
					conformToAspectRatio(newSize.width, newSize.height);
				}
				auto screenSizeDelta = resize(newSize);
				if (sizeWorldWithWindow) {
					auto worldSizeDelta = cast<PointPrecision>(screenSizeDelta) / worldScreenDelta;
					MV::info("Drawable Screen Resize: ", screenSizeDelta, " World Resize: ", worldSizeDelta);
					a_world.resize(a_world.size() - worldSizeDelta);
				}
				if (maintainProportions) {
					aspectRatio = oldRatio; //just prevent resize from mucking with this.
				}
			}
			return true;
		}
		return false;
	}

	void Window::refreshContext(){
		ensureValidGLContext();
		if(!renderer.headless() && SDL_GL_MakeCurrent(window, glcontext)){
			MV::error("Problem with refreshContext(): ", SDL_GetError());
		}
	}

	void Window::updateScreen(){
		if (!renderer.headless()) {
			SDL_GL_SwapWindow(window);
		}
	}

	bool Window::resizeWorldWithWindow() const {
		return sizeWorldWithWindow;
	}

	Window& Window::resizeWorldWithWindow(bool a_sizeWorldWithWindow) {
		sizeWorldWithWindow = a_sizeWorldWithWindow;
		return *this;
	}

	/************************\
	| --------World--------- |
	\************************/

	RenderWorld::RenderWorld(Draw2D &a_renderer):
		renderer(a_renderer){
	}

	void RenderWorld::resize( const Size<> &a_size ){
		worldSize = a_size;
	}

	const Size<>& RenderWorld::size() const{
		return worldSize;
	}

	/*************************\
	| --------Draw2D--------- |
	\*************************/

	Draw2D::Draw2D() :
		onCameraUpdated(onCameraUpdatedSignal),
		glExtensions(this),
		clearBackgroundColor(0x3d3d3d),
		initialized(0),
		sdlRenderer(nullptr),
		sdlWindow(*this),
		mvWorld(*this){

		RUNNING_IN_HEADLESS = false;
	}

	Draw2D::~Draw2D(){
	}

	bool Draw2D::initialize(Size<int> a_window, Size<> a_world, bool a_requireExtensions, bool a_summarize){
		setupSDL();

		sdlWindow.resize(a_window);
		if(a_world.width < 0 || a_world.height < 0){
			a_world = cast<PointPrecision>(a_window);
		}
		mvWorld.resize(a_world);

		if (!sdlWindow.initialize()) {
			MV::error("Window initialization failed: ", SDL_GetError());
			return false;
		}

		setupOpengl();
		initializeExtensions();
		summarizeDisplayMode();
		if (a_summarize) {
			summarizeDisplayMode();
		}
		loadDefaultShaders();
		return true;
	}

	MV::Size<int> Draw2D::monitorSize() {
		if (headless()) {
			return MV::size<int>(1920, 1080);
		}
		setupSDL();
		SDL_DisplayMode DM;
		SDL_GetCurrentDisplayMode(0, &DM);
		MV::info("Monitor Size: ", DM.w, "x", DM.h);
		return MV::Size<int>(DM.w, DM.h);
	}
	
	void Draw2D::summarizeDisplayMode() const{
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(0, &mode);

		std::stringstream summary;
		summary << "\\/==================================================\\/" << std::endl;
		summary << "WindowSize : (" << sdlWindow.windowSize() << ")" << std::endl;
		summary << "DrawableSize : (" << sdlWindow.drawableSize() << ")" << std::endl;
		if (!headless()) {
			summary << "Driver	  : " << SDL_GetCurrentVideoDriver() << std::endl;
			summary << "Screen bpp : " << SDL_BITSPERPIXEL(mode.format) << std::endl;
			summary << "Vendor	  : " << glGetString(GL_VENDOR) << std::endl;
			summary << "Renderer	: " << glGetString(GL_RENDERER) << std::endl;
			summary << "Version	 : " << glGetString(GL_VERSION) << std::endl;
			summary << "Extensions : " << glGetString(GL_EXTENSIONS) << std::endl;
		} else {
			summary << "HEADLESS" << std::endl;
		}
		summary << "/\\==================================================/\\" << std::endl;
		MV::info(summary.str());
	}

	void Draw2D::setupSDL(){
		if(firstInitializationSDL){
			firstInitializationSDL = false;
			if (!headless()) {
				if (SDL_Init(SDL_INIT_EVERYTHING) == -1) {
					MV::error("SDL_Init: ", SDL_GetError());
				}
                
				/*if (SDL_GetNumVideoDrivers() < 1 || SDL_VideoInit(0) < 0) {
					// Failed, exit
					std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
					return false;
				}*/
                gl3wInit();
			} else {
				if (SDL_Init(SDL_INIT_EVENTS) == -1) {
					MV::error("SDL_Init [HEADLESS]: ", SDL_GetError());
				}
			}
			setInitialSDLAttributes();
		}
	}

	void Draw2D::setInitialSDLAttributes() {
#ifdef HAVE_OPENGLES
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) != 0) {
			MV::error("Failed to set GL Context to ES");
		}
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0) {
			MV::error("Failed to set GL Major Context to 3!");
		}
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0) != 0) {
			MV::error("Failed to set GL Minor Context to 0!");
		}
#else
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) != 0) {
			MV::error("Failed to set GL Major Context to 3!");
		}
		if (SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1) != 0) {
			MV::error("Failed to set GL Minor Context to 1!");
		}
#endif

		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 0);

		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 1);

		SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
		SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);

		SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
		SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);

		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

		SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 1);

		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
		checkSDLError(__LINE__);
	}

	void Draw2D::setupOpengl(){
		sdlWindow.ensureValidGLContext();
		if (!headless()) {
			checkSDLError(__LINE__);
#ifndef HAVE_OPENGLES
			SDL_GL_SetSwapInterval(0);
#endif
		}
		refreshWorldAndWindowSize();

		if (!headless()) {
			glClearColor(clearBackgroundColor.R, clearBackgroundColor.G, clearBackgroundColor.B, clearBackgroundColor.A);

			glEnable(GL_BLEND);

			glDisable(GL_CULL_FACE);

			defaultBlendFunction();
			setBlendEquation(GL_FUNC_ADD, GL_FUNC_ADD);


			//No depth testing
			glDepthMask(GL_FALSE);
			glDisable(GL_DEPTH_TEST);
			glDepthFunc(GL_LEQUAL);


#ifdef HAVE_OPENGLES
			glClearDepthf(1.0f);
#else
			glClearDepth(1.0f);
            
            glDisable(GL_POLYGON_SMOOTH);
            glDisable(GL_LINE_SMOOTH);
            
            glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
            glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
            glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
            glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);
            
			//GL_DEPTH_CLAMP: Just avoid doing this, no support on GLES anyway, just set z to 0 in shaders or scale z within 0-1.
			//In 2D we don't really want to clip anything and partially transparent sprites don't mix well with depth testing
			//so setting Z to 0 and rendering in order works.
            //glEnable(GL_DEPTH_CLAMP);
#endif
			glClearStencil(0);

			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
	}

	bool Draw2D::handleEvent(const SDL_Event &event){
		if(sdlWindow.handleEvent(event, mvWorld)){
			refreshWorldAndWindowSize();
			return true;
		}
		return false;
	}

	void Draw2D::clearScreen(){
		updateCameraProjectionMatrices();
		sdlWindow.refreshContext();
		if (!headless()) {
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
	}

	void Draw2D::updateScreen(){
		sdlWindow.updateScreen();
	}

	Point<> Draw2D::worldFromLocal(const Point<> &a_localPoint, int32_t a_cameraId, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectWorld(a_localPoint, a_cameraId, a_modelview);
	}

	Point<int> Draw2D::screenFromLocal(const Point<> &a_localPoint, int32_t a_cameraId, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectScreen(a_localPoint, a_cameraId, a_modelview);
	}

	Point<> Draw2D::localFromWorld(const Point<> &a_worldPoint, int32_t a_cameraId, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectWorld(a_worldPoint, a_cameraId, a_modelview);
	}

	Point<> Draw2D::localFromScreen(const Point<int> &a_worldPoint, int32_t a_cameraId, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectScreen(a_worldPoint, a_cameraId, a_modelview);
	}

	Point<> Draw2D::worldFromScreenRaw(const Point<> &a_screenPoint) const {
		PointPrecision widthRatio = window().drawableSize().width / world().size().width;
		PointPrecision heightRatio = window().drawableSize().height / world().size().height;
		return Point<>(a_screenPoint.x / widthRatio, a_screenPoint.y / heightRatio, a_screenPoint.z);
	}

	Point<> Draw2D::worldFromScreen(const Point<int> &a_screenPoint) const {
		return worldFromScreenRaw(cast<PointPrecision>(a_screenPoint));
	}

	Point<> Draw2D::screenFromWorldRaw(const Point<> &a_worldPoint) const {
		PointPrecision widthRatio = window().drawableSize().width / world().size().width;
		PointPrecision heightRatio = window().drawableSize().height / world().size().height;
		return Point<>(a_worldPoint.x * widthRatio, a_worldPoint.y * heightRatio, a_worldPoint.z);
	}

	Point<int> Draw2D::screenFromWorld(const Point<> &a_worldPoint) const{
		return round<int>(screenFromWorldRaw(a_worldPoint));
	}

	Color Draw2D::backgroundColor( Color a_newColor ){
		clearBackgroundColor = a_newColor;
		if (!headless()) {
			glClearColor(clearBackgroundColor.R, clearBackgroundColor.G, clearBackgroundColor.B, clearBackgroundColor.A);
		}
		return a_newColor;
	}

	Color Draw2D::backgroundColor() const{
		return clearBackgroundColor;
	}

	Window& Draw2D::window(){
		return sdlWindow;
	}

	const Window& Draw2D::window() const{
		return sdlWindow;
	}

	RenderWorld& Draw2D::world(){
		return mvWorld;
	}

	const RenderWorld& Draw2D::world() const{
		return mvWorld;
	}

	void Draw2D::defaultBlendFunction() {
		setBlendFunction(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE); //needs premultiply
	}

	void Draw2D::validateShaderStatus(GLuint a_id, bool a_isShader) {
		if (!headless()) {
			GLint loadResult = GL_FALSE;
			if (a_isShader) {
				glGetShaderiv(a_id, GL_COMPILE_STATUS, &loadResult);
			} else {
				glGetProgramiv(a_id, GL_LINK_STATUS, &loadResult);
			}

			if (loadResult != GL_TRUE) {
				int infoLogLength;
				glGetShaderiv(a_id, GL_INFO_LOG_LENGTH, &infoLogLength);
				std::vector<char> vertexShaderErrorMessage(std::max(infoLogLength, int(1)));
				glGetShaderInfoLog(a_id, infoLogLength, 0, &vertexShaderErrorMessage[0]);
				MV::require<ResourceException>(false, "Shader Error: ", std::string(vertexShaderErrorMessage.begin(), vertexShaderErrorMessage.end()));
			}
		}
	}

	void Draw2D::loadPartOfShader(GLuint a_id, const std::string &a_code) {
		if (!headless()) {
			char const * sourcePointer = a_code.c_str();
			glShaderSource(a_id, 1, &sourcePointer, 0);
			glCompileShader(a_id);

			validateShaderStatus(a_id, true);
		}
	}

	Shader* Draw2D::loadShaderCode(const std::string &a_id, const std::string &a_vertexShaderCode, const std::string &a_fragmentShaderCode) {
		bool makeDefault = shaders.empty();
		auto programId = loadShaderGetProgramId(a_vertexShaderCode, a_fragmentShaderCode);

		auto emplaceResult = shaders.emplace(std::make_pair(a_id, Shader(a_id, programId, headless())));
		MV::require<ResourceException>(emplaceResult.second, "Failed to insert shader to map: ", a_id);
		Shader* shaderPtr = &emplaceResult.first->second;

		if (makeDefault) {
			defaultShaderPtr = shaderPtr;
		}

		return shaderPtr;
	}

	void Draw2D::reloadShaders() {
		if (!headless()) {
			for (auto&& shader : shaders) {
				try {
					if (shader.second.vertexShaderFile.empty() || shader.second.fragmentShaderFile.empty()) { continue; }

					std::string vertexShaderCode = fileContents(shader.second.vertexShaderFile);
					if (vertexShaderCode.empty()) { continue; }

					std::string fragmentShaderCode = fileContents(shader.second.fragmentShaderFile);
					if (fragmentShaderCode.empty()) { continue; }
                    
					auto newId = loadShaderGetProgramId(vertexShaderCode, fragmentShaderCode);
					glDeleteProgram(shader.second.programId);
					shader.second.programId = newId;
					shader.second.initialize();
				} catch (ResourceException &e) {
					std::cerr << "Failed to reload shader: " << e.what() << std::endl;
				}
			}
		}
	}

	GLuint Draw2D::loadShaderGetProgramId(std::string a_vertexShaderCode, std::string a_fragmentShaderCode) {
		GLuint programId = -1;
		if (!headless()) {
			auto vsId = glCreateShader(GL_VERTEX_SHADER);
			auto fsId = glCreateShader(GL_FRAGMENT_SHADER);
            
#ifdef HAVE_OPENGLES
            replaceFirst(a_vertexShaderCode, "#version 330 core", "#version 300 es\nprecision highp float;\n");
            replaceFirst(a_fragmentShaderCode, "#version 330 core", "#version 300 es\nprecision highp float;\n");
#endif
            
			loadPartOfShader(vsId, a_vertexShaderCode);
			loadPartOfShader(fsId, a_fragmentShaderCode);

			programId = glCreateProgram();

			glAttachShader(programId, vsId);
			glAttachShader(programId, fsId);

			glLinkProgram(programId);

			glDeleteShader(vsId);
			glDeleteShader(fsId);

			validateShaderStatus(programId, false);
		}
		return programId;
	}

	void Draw2D::loadDefaultShaders() {
		loadShader(MV::DEFAULT_ID, "Shaders/default.vert", "Shaders/default.frag");
		loadShader(MV::PREMULTIPLY_ID, "Shaders/default.vert", "Shaders/premultiply.frag");
		loadShader(MV::ALPHA_FILTER_ID, "Shaders/default.vert", "Shaders/alphaFilter.frag");
		loadShader(MV::COLOR_PICKER_ID, "Shaders/default.vert", "Shaders/colorPicker.frag");
	}

	Shader* Draw2D::loadShader(const std::string &a_id, const std::string &a_vertexShaderFilename, const std::string &a_fragmentShaderFilename) {
		auto found = shaders.find(a_id);
		if(found == shaders.end()){
			std::string vertexShaderCode = fileContents(a_vertexShaderFilename);
			MV::require<ResourceException>(!vertexShaderCode.empty(), "Failed to load vertex shader: ", a_vertexShaderFilename);

			std::string fragmentShaderCode = fileContents(a_fragmentShaderFilename);
			MV::require<ResourceException>(!fragmentShaderCode.empty(), "Failed to load fragment shader: ", a_fragmentShaderFilename);

			bool makeDefault = shaders.empty();
            GLuint programId;
            try {
                programId = loadShaderGetProgramId(vertexShaderCode, fragmentShaderCode);
            }catch(ResourceException &e){
                e.append("ShaderID: " + a_id);
                throw;
            }
            auto emplaceResult = shaders.emplace(std::make_pair(a_id, Shader(a_id, programId, headless(), a_vertexShaderFilename, a_fragmentShaderFilename)));
            MV::require<ResourceException>(emplaceResult.second, "Failed to insert shader to map: ", a_id);
            Shader* shaderPtr = &emplaceResult.first->second;
                
            if (makeDefault) {
                defaultShaderPtr = shaderPtr;
            }
            return shaderPtr;
		} else {
			return &found->second;
		}
	}


	bool Draw2D::hasShader(const std::string &a_id) {
		auto found = shaders.find(a_id);
		return found != shaders.end();
	}

	Shader* Draw2D::getShader(const std::string &a_id) {
		auto found = shaders.find(a_id);
		MV::require<RangeException>(found != shaders.end(), "Shader not loaded: ", a_id);
		return &found->second;
	}

	Shader* Draw2D::defaultShader(const std::string &a_id) {
		defaultShaderPtr = getShader(a_id);
		MV::require<MV::PointerException>(defaultShaderPtr != nullptr, "No default shader.");
		return defaultShaderPtr;
	}

	Shader* Draw2D::defaultShader() const {
		MV::require<PointerException>(defaultShaderPtr != nullptr, "No default shader.");
		return defaultShaderPtr;
	}

	void Draw2D::resetViewport() {
		if (!headless()) {
			
			glViewport(0, 0, sdlWindow.drawableSize().width, sdlWindow.drawableSize().height);
			glGetIntegerv(GL_VIEWPORT, viewport);
			MV::info("ResetViewport: Window:(", sdlWindow.drawableSize() ,") | ViewPort:(", viewport[0], ", ", viewport[1], ") -> (", viewport[2], ", ", viewport[3], ")");
		} else {
			viewport[0] = 0;
			viewport[1] = 0;
			viewport[2] = static_cast<GLint>(sdlWindow.drawableSize().width);
			viewport[3] = static_cast<GLint>(sdlWindow.drawableSize().height);
		}
	}

// 	void Draw2D::registerShader(std::shared_ptr<Scene::Node> a_node) {
// 		if (hasShader(a_node->shader())) {
// 			a_node->shader(a_node->shader());
// 		} else {
// 			needShaderRegistration.push_back(a_node);
// 		}
// 	}

	void Draw2D::draw(GLenum drawType, std::shared_ptr<Scene::Node> a_node) {
// 		a_node->shaderProgram->use();
// 
// 		if(a_node->bufferId == 0){
// 			glGenBuffers(1, &a_node->bufferId);
// 		}
// 
// 		glBindBuffer(GL_ARRAY_BUFFER, a_node->bufferId);
// 		auto structSize = static_cast<GLsizei>(sizeof(a_node->points[0]));
// 		glBufferData(GL_ARRAY_BUFFER, a_node->points.size() * structSize, &(a_node->points[0]), GL_STATIC_DRAW);
// 
// 		glEnableVertexAttribArray(0);
// 		glEnableVertexAttribArray(1);
// 		glEnableVertexAttribArray(2);
// 
// 		auto positionOffset = static_cast<GLsizei>(offsetof(DrawPoint, x));
// 		auto textureOffset = static_cast<GLsizei>(offsetof(DrawPoint, textureX));
// 		auto colorOffset = static_cast<GLsizei>(offsetof(DrawPoint, R));
// 		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
// 		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
// 		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color
// 
// 		TransformMatrix transformationMatrix(projectionMatrix().top() * modelviewMatrix().top());
// 
// 		a_node->shaderProgram->set("texture", a_node->ourTexture);
// 		a_node->shaderProgram->set("transformation", transformationMatrix);
// 
// 		if(!a_node->vertexIndices.empty()){
// 			glDrawElements(drawType, static_cast<GLsizei>(a_node->vertexIndices.size()), GL_UNSIGNED_INT, &a_node->vertexIndices[0]);
// 		} else{
// 			glDrawArrays(drawType, 0, static_cast<GLsizei>(a_node->points.size()));
// 		}
// 
// 		glDisableVertexAttribArray(0);
// 		glDisableVertexAttribArray(1);
// 		glDisableVertexAttribArray(2);
// 		glUseProgram(0);
	}

	void Draw2D::refreshWorldAndWindowSize() {
		resetViewport();

		projectionMatrix().clear(); //ensure nothing else has trampled on us.
		projectionMatrix().top().makeOrtho(0, mvWorld.size().width, mvWorld.size().height, 0, -1.0, 1.0);
	}

	Framebuffer::Framebuffer(Draw2D *a_renderer, GLuint a_framebuffer, GLuint a_renderbuffer, GLuint a_depthbuffer, GLuint a_texture, const Size<int> &a_size, const Point<int> &a_position, const Color &a_backgroundColor):
		renderbuffer(a_renderbuffer),
		framebuffer(a_framebuffer),
		depthbuffer(a_depthbuffer),
		texture(a_texture),
		frameSize(a_size),
		framePosition(a_position),
		renderer(a_renderer),
		started(false),
		background(a_backgroundColor){
	}

	Framebuffer::~Framebuffer(){
		if(started){
			stop();
		}
		renderer->deleteFramebuffer(*this);
	}

	std::shared_ptr<Framebuffer> Framebuffer::start() {
		started = true;
		renderer->startUsingFramebuffer(shared_from_this());
		return shared_from_this();
	}

	void Framebuffer::stop() {
		started = false;
		renderer->stopUsingFramebuffer();
	}

	glExtensions::glExtensions(Draw2D *a_renderer) :
		renderer(a_renderer),
		glExtensionBlendMode(a_renderer),
		glExtensionFramebufferObject(a_renderer) {

	}

	void glExtensions::initializeExtensions() {
		if (!renderer->headless()) {
			char* extensionsList = (char*)glGetString(GL_EXTENSIONS);
			if (!extensionsList) {
				std::cerr << "ERROR: Could not load extensions list from glGetString(GL_EXTENSIONS)" << std::endl;
			} else {
				loadExtensionBlendMode(extensionsList);
				loadExtensionFramebufferObject(extensionsList);
			}
		}
	}

	GLuint Shader::getDefaultTextureId() const {
		return SharedTextures::white()->texture()->textureId();
	}

	bool Shader::set(const std::string &a_variableName, GLuint a_texture, GLuint a_textureBindIndex, bool a_errorIfNotPresent /*= true*/) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_texture != 0) ?
					a_texture :
					getDefaultTextureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
				return true;
			} else if (a_errorIfNotPresent) {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
		return false;
	}

	bool Shader::set(const std::string &a_variableName, const std::shared_ptr<TextureDefinition> &a_texture, GLuint a_textureBindIndex, bool a_errorIfNotPresent /*= true*/) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_texture != nullptr) ?
					a_texture->textureId() :
					getDefaultTextureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
				return true;
			} else if (a_errorIfNotPresent) {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
		return false;
	}

	bool Shader::set(const std::string &a_variableName, const std::shared_ptr<TextureHandle> &a_value, GLuint a_textureBindIndex, bool a_errorIfNotPresent /*= true*/) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_value != nullptr && a_value->texture() != nullptr) ?
					a_value->texture()->textureId() :
					getDefaultTextureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
				return true;
			} else if (a_errorIfNotPresent) {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
		return false;
	}

}
