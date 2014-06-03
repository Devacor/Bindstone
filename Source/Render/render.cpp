#include "render.h"
#include "Scene/node.h"
#include "Utility/generalUtility.h"


namespace MESA {
	void gluMultMatrixVecf(const GLfloat matrix[16], const GLfloat in[4], GLfloat out[4]) {
		for (int i=0; i<4; i++) {
			out[i] = 
				in[0] * matrix[0*4+i] +
				in[1] * matrix[1*4+i] +
				in[2] * matrix[2*4+i] +
				in[3] * matrix[3*4+i];
		}
	}

	int gluInvertMatrixf(const GLfloat m[16], GLfloat invOut[16]) {
		GLfloat inv[16], det;

		inv[0] =   m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15]
				 + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
		inv[4] =  -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15]
				 - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
		inv[8] =   m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15]
				 + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
		inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14]
				 - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
		inv[1] =  -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15]
				 - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
		inv[5] =   m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15]
				 + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
		inv[9] =  -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15]
				 - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
		inv[13] =  m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14]
				 + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
		inv[2] =   m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15]
				 + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
		inv[6] =  -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15]
				 - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
		inv[10] =  m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15]
				 + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
		inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14]
				 - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
		inv[3] =  -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11]
				 - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
		inv[7] =   m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11]
				 + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
		inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11]
				 - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
		inv[15] =  m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10]
				 + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

		det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
		if (det == 0){
			return GL_FALSE;
		}
		det = 1.0f / det;

		for (int i = 0; i < 16; i++){
			invOut[i] = inv[i] * det;
		}
		return GL_TRUE;
	}

	void gluMultMatricesf(const GLfloat a[16], const GLfloat b[16], GLfloat r[16]){
		for (int i = 0; i < 4; i++) {
			for (int j = 0; j < 4; j++) {
				r[i*4+j] = 
				a[i*4+0]*b[0*4+j] +
				a[i*4+1]*b[1*4+j] +
				a[i*4+2]*b[2*4+j] +
				a[i*4+3]*b[3*4+j];
			}
		}
	}

	GLint gluProject(GLfloat objx, GLfloat objy, GLfloat objz,
		const GLfloat modelMatrix[16], const GLfloat projMatrix[16], const GLint viewport[4],
		GLfloat *winx, GLfloat *winy, GLfloat *winz){

		GLfloat in[4];
		GLfloat out[4];

		in[0]=objx;
		in[1]=objy;
		in[2]=objz;
		in[3]=1.0f;
		gluMultMatrixVecf(modelMatrix, in, out);
		gluMultMatrixVecf(projMatrix, out, in);
		if (MV::equals(in[3], 0.0f)){
			return(GL_FALSE);
		}
		in[0] /= in[3];
		in[1] /= in[3];
		in[2] /= in[3];
		/* Map x, y and z to range 0-1 */
		in[0] = in[0] * 0.5f + 0.5f;
		in[1] = in[1] * 0.5f + 0.5f;
		in[2] = in[2] * 0.5f + 0.5f;

		/* Map x,y to viewport */
		in[0] = in[0] * viewport[2] + viewport[0];
		in[1] = in[1] * viewport[3] + viewport[1];

		*winx=in[0];
		*winy=in[1];
		*winz=in[2];
		return(GL_TRUE);
	}

	GLint gluUnProject(GLfloat winx, GLfloat winy, GLfloat winz,
		const GLfloat modelMatrix[16], const GLfloat projMatrix[16], const GLint viewport[4],
		GLfloat *objx, GLfloat *objy, GLfloat *objz) {

		GLfloat finalMatrix[16];
		GLfloat in[4];
		GLfloat out[4];

		gluMultMatricesf(modelMatrix, projMatrix, finalMatrix);
		if (!gluInvertMatrixf(finalMatrix, finalMatrix)){
			return(GL_FALSE);
		}

		in[0]=winx;
		in[1]=winy;
		in[2]=winz;
		in[3]=1.0f;

		/* Map x and y from window coordinates */
		in[0] = (in[0] - viewport[0]) / viewport[2];
		in[1] = (in[1] - viewport[1]) / viewport[3];

		/* Map to range -1 to 1 */
		in[0] = in[0] * 2.0f - 1.0f;
		in[1] = in[1] * 2.0f - 1.0f;
		in[2] = in[2] * 2.0f - 1.0f;

		gluMultMatrixVecf(finalMatrix, in, out);
		if (MV::equals(out[3], 0.0f)){
			return(GL_FALSE);
		}
		out[0] /= out[3];
		out[1] /= out[3];
		out[2] /= out[3];
		*objx = out[0];
		*objy = out[1];
		*objz = out[2];
		return(GL_TRUE);
	}
}

namespace MV {

	bool Draw2D::firstInitializationSDL = true;
	bool Draw2D::firstInitializationOpenGL = true;

	Point<int> ProjectionDetails::projectScreen(const Point<> &a_point){
		Point<> result;
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		if(MESA::gluProject(a_point.x, a_point.y, a_point.z, &(*renderer.modelviewMatrix().top().getMatrixArray())[0], &(*renderer.projectionMatrix().top().getMatrixArray())[0], viewport, &result.x, &result.y, &result.z) == GL_FALSE){
			std::cerr << "gluProject failure!" << std::endl;
		}
		result.y=renderer.window().height()-result.y;
		result.z = a_point.z;//restore original z since we're just 2d.
		return castPoint<int>(result);
	}

	Point<> ProjectionDetails::projectWorld(const Point<> &a_point){
		Point<> result;
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		if(MESA::gluProject(a_point.x, a_point.y, a_point.z, &(*renderer.modelviewMatrix().top().getMatrixArray())[0], &(*renderer.projectionMatrix().top().getMatrixArray())[0], viewport, &result.x, &result.y, &result.z) == GL_FALSE){
			std::cerr << "gluProject failure!" << std::endl;
		}
		result.y/=renderer.window().height()/renderer.world().height();
		result.x/=renderer.window().width()/renderer.world().width();
		result.y=renderer.world().height()-result.y;
		result.z = a_point.z;//restore original z since we're just 2d.
		return result;
	}

	Point<> ProjectionDetails::unProjectScreen(const Point<int> &a_point){
		Point<> result;
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);
		if(MESA::gluUnProject(static_cast<PointPrecision>(a_point.x), static_cast<PointPrecision>(renderer.window().height() - a_point.y), 0, &(*renderer.modelviewMatrix().top().getMatrixArray())[0], &(*renderer.projectionMatrix().top().getMatrixArray())[0], viewport, &result.x, &result.y, &result.z) == GL_FALSE){
			std::cerr << "gluUnProject failure!" << std::endl;
		}
		result.z = static_cast<PointPrecision>(a_point.z);//restore original z since we're just 2d.
		return result;
	}

	Point<> ProjectionDetails::unProjectWorld(const Point<> &a_point){
		return unProjectScreen(renderer.screenFromWorld(a_point));
	}

	void checkSDLError(int line)
	{
		const char *error = SDL_GetError();
		while (*error != '\0')
		{
			std::cerr << "SDL Error: " << error << std::endl;
			if (line != -1){
				std::cerr << "Line: " << line << std::endl;
			}
			SDL_ClearError();
			error = SDL_GetError();
		}
	}

	/*************************\
	| ------Extensions------- |
	\*************************/

	glExtensionBlendMode::glExtensionBlendMode() :initialized(false)
	{
	}

	void glExtensionBlendMode::setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha){
		if(initialized){
#ifdef WIN32
			glBlendFuncSeparate(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#else
			glBlendFuncSeparateOES(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#endif
		}else{
			glBlendFunc(a_sfactorRGB, a_dfactorRGB);
		}
	}

	void glExtensionBlendMode::setBlendEquation(GLenum a_rgbBlendFunc, GLenum a_alphaBlendFunc){
		if(initialized){
#ifdef WIN32
			glBlendEquationSeparate(a_rgbBlendFunc, a_alphaBlendFunc);
#else
			glBlendEquationSeparate(a_rgbBlendFunc, a_alphaBlendFunc);
#endif
		}
	}

	void glExtensionBlendMode::loadExtensionBlendMode( char *a_extensionsList ){
#ifdef WIN32
		initialized = true;
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
#else
		initialized = true;
		glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
#endif
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

	std::shared_ptr<Framebuffer> glExtensionFramebufferObject::makeFramebuffer(const Point<int> &a_position, const Size<int> &a_size, GLuint a_texture){
		require(initialized, ResourceException("CreateFramebuffer failed because the extension could not be loaded"));
		GLuint framebufferId = 0, renderbufferId = 0, depthbufferId = 0;
#ifdef WIN32
		glGenFramebuffers(1, &framebufferId);
		glGenRenderbuffers(1, &renderbufferId);
		//glGenRenderbuffers(1, &depthbufferId); //not used right now
#else
		glGenFramebuffersOES(1, &framebufferId);
		glGenRenderbuffersOES(1, &renderbufferId);
		glGenRenderbuffersOES(1, &depthbufferId);
#endif
		return std::shared_ptr<Framebuffer>(new Framebuffer(renderer, framebufferId, renderbufferId, depthbufferId, a_texture, a_size, a_position));
	}

	void glExtensionFramebufferObject::startUsingFramebuffer(std::shared_ptr<Framebuffer> a_framebuffer, bool a_push){
		savedClearColor = renderer->backgroundColor();
		renderer->backgroundColor({1.0, 1.0, 1.0, 0.0});

		require(initialized, ResourceException("StartUsingFramebuffer failed because the extension could not be loaded"));
		if(a_push){
			activeFramebuffers.push_back(a_framebuffer);
		}
#ifdef WIN32
		glBindFramebuffer(GL_FRAMEBUFFER, a_framebuffer->framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a_framebuffer->texture, 0);
		glBindRenderbuffer(GL_RENDERBUFFER, a_framebuffer->renderbuffer);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, roundUpPowerOfTwo(a_framebuffer->frameSize.width), roundUpPowerOfTwo(a_framebuffer->frameSize.height));
#else
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

		if(glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES){
			std::cout << "Start Using Framebuffer failure: " << glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) << std::endl;
		}
#endif
		glViewport(a_framebuffer->framePosition.x, a_framebuffer->framePosition.y, a_framebuffer->frameSize.width, a_framebuffer->frameSize.height);
		renderer->projectionMatrix().push().makeOrtho(0, static_cast<MatrixValue>(a_framebuffer->frameSize.width), 0, static_cast<MatrixValue>(a_framebuffer->frameSize.height), -128.0f, 128.0f);

#ifdef WIN32
		GLenum buffers[] = {GL_COLOR_ATTACHMENT0};
		//pglDrawBuffersEXT(1, buffers);
#else
		GLenum buffers[] = {GL_COLOR_ATTACHMENT0_OES};
		//glDrawBuffersOES(1, buffers)
#endif

		renderer->clearScreen();
	}

	void glExtensionFramebufferObject::stopUsingFramebuffer(){
		require(initialized, ResourceException("StopUsingFramebuffer failed because the extension could not be loaded"));
		activeFramebuffers.pop_back();
		if(!activeFramebuffers.empty()){
			startUsingFramebuffer(activeFramebuffers.back(), false);
		} else {
#ifdef WIN32
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, 0);
#else
			glBindFramebufferOES(GL_FRAMEBUFFER_OES, originalFramebufferId);
			glBindRenderbufferOES(GL_RENDERBUFFER_OES, originalRenderbufferId);
#endif
			glViewport(0, 0, renderer->window().width(), renderer->window().height());
			renderer->projectionMatrix().pop();
			renderer->backgroundColor(savedClearColor);
		}
	}
	
	void glExtensionFramebufferObject::deleteFramebuffer( Framebuffer &a_framebuffer ){
#ifdef WIN32
		glDeleteFramebuffers(1, &a_framebuffer.framebuffer);
		glDeleteRenderbuffers(1, &a_framebuffer.renderbuffer);
		//glDeleteRenderbuffers(1, &a_framebuffer.depthbuffer); //not used currently
#else
		glDeleteFramebuffersOES(1, &a_framebuffer.framebuffer);
		glDeleteRenderbuffersOES(1, &a_framebuffer.renderbuffer);
		glDeleteRenderbuffersOES(1, &a_framebuffer.depthbuffer);
#endif
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
		renderer(a_renderer),
		maintainProportions(true),
		SDLflags(SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE),
		initialized(false),
		vsync(false),
		userCanResize(false),
		glcontext(0){
		
		updateAspectRatio();
	}

	Window::~Window(){
		SDL_GL_DeleteContext(glcontext);
		SDL_DestroyWindow(window);
	}

	void Window::setTitle(const std::string &a_title){
		title = a_title;
	}

	void Window::resize(const Size<int> &a_size){
		windowSize = a_size;
		updateAspectRatio();
		if(initialized){
			SDL_SetWindowSize(window, windowSize.width, windowSize.height);
			renderer.setupOpengl();
			if(!userCanResize){
				lockUserResize();
			}
		}
	}

	Window& Window::allowUserResize(bool a_maintainProportions, const Size<int> &a_minSize, const Size<int> &a_maxSize){
		maintainProportions = a_maintainProportions;
		minSize = a_minSize;
		maxSize = a_maxSize;
		userCanResize = true;
		if(initialized){
			updateWindowResizeLimits();
		}
		return *this;
	}

	Window& Window::lockUserResize(){
		userCanResize = false;
		if(initialized){
			SDL_GetWindowSize(window, &windowSize.width, &windowSize.height);
			updateAspectRatio();
			minSize = Size<int>(windowSize.width, windowSize.height);
			maxSize = minSize;
			updateWindowResizeLimits();
		}else{
			minSize = Size<int>(windowSize.width, windowSize.height);
			maxSize = minSize;
		}
		return *this;
	}

	void Window::updateWindowResizeLimits(){
		if(initialized){
			SDL_SetWindowMinimumSize(window, std::max(minSize.width, 1), std::max(minSize.height, 1));
			SDL_SetWindowMaximumSize(window, std::max(maxSize.width, 1), std::max(maxSize.height, 1));
			checkSDLError(__LINE__);
		}
	}

	Window& Window::windowedMode(){
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(initialized){
			SDL_SetWindowFullscreen(window, 0);
		}
		return *this;
	}

	void Window::fullScreenMode(){
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(initialized){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
		}
	}

	void Window::fullScreenWindowedMode(){
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN_DESKTOP;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		if(initialized){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
	}
	
	Window& Window::borderless(){
		SDLflags = SDLflags | SDL_WINDOW_BORDERLESS;
		if(initialized){
			SDL_SetWindowBordered(window, SDL_FALSE);
		}
		return *this;
	}

	Window& Window::bordered(){
		SDLflags = SDLflags & ~ SDL_WINDOW_BORDERLESS;
		if(initialized){
			SDL_SetWindowBordered(window, SDL_TRUE);
		}
		return *this;
	}

	int Window::height() const{
		return windowSize.height;
	}

	int Window::width() const{
		return windowSize.width;
	}

	Size<int> Window::size() const{
		return windowSize;
	}

	void Window::updateAspectRatio(){
		aspectRatio = (windowSize.height != 0) ? static_cast<PointPrecision>(windowSize.width) / static_cast<PointPrecision>(windowSize.height) : 0.0f;
	}

	void Window::conformToAspectRatio(int &a_width, int &a_height) const{
		if(a_width < a_height){
			a_width = static_cast<int>(a_height * aspectRatio);
		}else{
			a_height = static_cast<int>(a_width / aspectRatio);
		}
	}

	bool Window::initialize(){
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 0);
		
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
		
        SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);
		
        SDL_GL_SetAttribute(SDL_GL_STEREO, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
		
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
		
		SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 1);
		
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		
		window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowSize.width, windowSize.height, SDLflags);
		if(!window){
			std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
			atexit(SDL_Quit); // Quit SDL at exit.
			return false;
		}
		SDL_GetWindowSize(window, &windowSize.width, &windowSize.height);
		updateAspectRatio();
		checkSDLError(__LINE__);
		
		SDL_DisplayMode displayMode;
		SDL_zero(displayMode);
		displayMode.format = SDL_PIXELFORMAT_RGB888;
		
		if(SDL_SetWindowDisplayMode(window, &displayMode) < 0){
			std::cerr << "Window display mode failed: " << SDL_GetError() << std::endl;
			atexit(SDL_Quit);
			return false;
		}
		checkSDLError(__LINE__);
		
		SDL_ShowWindow(window);
		initialized = true;

		//must happen after we flag initialized as true.
		if(!userCanResize){
			lockUserResize();
		}

		return true;
	}

	void Window::ensureValidGLContext(){
		if(!glcontext){
			glcontext = SDL_GL_CreateContext(window);
			if(!glcontext){
				std::cerr << "OpenGL context failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			if(renderer.firstInitializationOpenGL){
				MV::require(gl3wInit() == 0, MV::PointerException("gl3wInit failed!"));
				renderer.firstInitializationOpenGL = false;
			}

			if (SDL_GL_MakeCurrent(window, glcontext)) {
				std::cerr << "SDL_GL_MakeCurrent(): " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			SDL_GL_SetSwapInterval(vsync);
		}
	}

	bool Window::handleEvent(const SDL_Event &event){
		switch(event.type){
			case SDL_WINDOWEVENT:
				switch(event.window.event){
					case SDL_WINDOWEVENT_RESIZED:
						Size<int> newSize(event.window.data1, event.window.data2);
						MV::PointPrecision oldRatio = aspectRatio;
						if(maintainProportions){
							conformToAspectRatio(newSize.width, newSize.height);
						}
						resize(newSize);
						if(maintainProportions){
							aspectRatio = oldRatio; //just prevent resize from mucking with this.
						}
						return true;
					break;
				}
			break;
		}
		return false;
	}

	void Window::refreshContext(){
		ensureValidGLContext();
		if(SDL_GL_MakeCurrent(window, glcontext)){
			std::cerr << "Problem with refreshContext()." << SDL_GetError() << std::endl;
		}
	}

	void Window::updateScreen(){
		SDL_GL_SwapWindow( window );
	}

	/************************\
	| --------World--------- |
	\************************/

	RenderWorld::RenderWorld(Draw2D &a_renderer):
		renderer(a_renderer){
	}

	void RenderWorld::resize( const Size<> &a_size ){
		worldSize = a_size;
		if(renderer.initialized){
			renderer.setupOpengl();
		}
	}

	PointPrecision RenderWorld::height() const{
		return worldSize.height;
	}

	PointPrecision RenderWorld::width() const{
		return worldSize.width;
	}

	Size<> RenderWorld::size() const{
		return worldSize;
	}

	/*************************\
	| --------Draw2D--------- |
	\*************************/

	Draw2D::Draw2D() :
		glExtensions(this),
		clearBackgroundColor(0x3d3d3d),
		initialized(0),
		sdlWindow(*this),
		mvWorld(*this){
	}

	Draw2D::~Draw2D(){
	}

	bool Draw2D::initialize(Size<int> a_window, Size<> a_world, bool a_requireExtensions, bool a_summarize){
		sdlWindow.resize(a_window);
		if(a_world.width < 0 || a_world.height < 0){
			a_world = castSize<PointPrecision>(a_window);
		}
		mvWorld.resize(a_world);

		if(setupSDL()){
			setupOpengl();
			initializeExtensions();
			if(a_summarize){
				summarizeDisplayMode();
			}
			return true;
		}
		return false;
	}
	
	void Draw2D::summarizeDisplayMode() const{
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(0, &mode);

		std::cout << "\\/==================================================\\/" << std::endl;
		std::cout << "Window	  : (" << sdlWindow.width() << " x " << sdlWindow.height() << ")" << std::endl;
		std::cout << "Driver	  : " << SDL_GetCurrentVideoDriver() << std::endl;
		std::cout << "Screen bpp : " << SDL_BITSPERPIXEL(mode.format) << std::endl;
		std::cout << "Vendor	  : " << glGetString(GL_VENDOR) << std::endl;
		std::cout << "Renderer	: " << glGetString(GL_RENDERER) << std::endl;
		std::cout << "Version	 : " << glGetString(GL_VERSION) << std::endl;
		std::cout << "Extensions : " << glGetString(GL_EXTENSIONS) << std::endl;
		std::cout << "/\\==================================================/\\" << std::endl;
	}

	bool Draw2D::setupSDL(){
		if(firstInitializationSDL){
			firstInitializationSDL = false;
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

			if(SDL_Init(SDL_INIT_EVERYTHING) == -1){
				std::cerr << "SDL_Init: " << SDL_GetError();
			}

			if( SDL_GetNumVideoDrivers() < 1 || SDL_VideoInit(0) < 0 ){
				// Failed, exit
				std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
				return false;
			}
		}
		if(!sdlWindow.initialize()){
			std::cerr << "Window initialization failed!" << std::endl;
			return false;
		}
		
		initialized = true;
		return true;
	}

	void Draw2D::setupOpengl(){
		sdlWindow.ensureValidGLContext();
		checkSDLError(__LINE__);
		
		SDL_GL_SetSwapInterval(0);
		
		glViewport( 0, 0, sdlWindow.width(), sdlWindow.height() );

		projectionMatrix().clear(); //ensure nothing else has trampled on us.
		projectionMatrix().top().makeOrtho(0, mvWorld.width(), mvWorld.height(), 0, -128.0, 128.0);

		glClearColor(clearBackgroundColor.R,clearBackgroundColor.G,clearBackgroundColor.B,clearBackgroundColor.A);
		
        //glShadeModel(GL_SMOOTH);
		
		glEnable (GL_BLEND);
		
		glDisable(GL_CULL_FACE);
		
		//glDisable (GL_ALPHA_TEST);

		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
		glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
		
		defaultBlendFunction();
		setBlendEquation(GL_FUNC_ADD, GL_FUNC_ADD);

		glDepthMask(GL_FALSE);
		glDisable(GL_DEPTH_TEST);
		glEnable(GL_DEPTH_CLAMP);
        glDepthFunc(GL_LEQUAL);


#ifdef HAVE_OPENGLES
		glClearDepthf(16.0f);
#else
		glClearDepth(16.0f);
#endif
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	bool Draw2D::handleEvent(const SDL_Event &event){
		if(sdlWindow.handleEvent(event)){
			return true;
		}
		return false;
	}

	void Draw2D::clearScreen(){
		sdlWindow.refreshContext();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	void Draw2D::updateScreen(){
		sdlWindow.updateScreen();
	}

	Point<> Draw2D::worldFromLocal(const Point<> &a_localPoint ) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectWorld(a_localPoint);
	}

	Point<int> Draw2D::screenFromLocal(const Point<> &a_localPoint) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectScreen(a_localPoint);
	}

	Point<> Draw2D::localFromWorld(const Point<> &a_worldPoint ) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectWorld(a_worldPoint);
	}

	Point<> Draw2D::localFromScreen(const Point<int> &a_worldPoint ) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectScreen(a_worldPoint);
	}

	Point<> Draw2D::worldFromScreen(const Point<int> &a_screenPoint) const{
		PointPrecision widthRatio = window().width() / world().width();
		PointPrecision heightRatio = window().height() / world().height();
		return Point<>(static_cast<PointPrecision>(a_screenPoint.x) / widthRatio, static_cast<PointPrecision>(a_screenPoint.y) / heightRatio, static_cast<PointPrecision>(a_screenPoint.z));
	}

	Point<int> Draw2D::screenFromWorld(const Point<> &a_worldPoint) const{
		PointPrecision widthRatio = window().width() / world().width();
		PointPrecision heightRatio = window().height() / world().height();
		return Point<int>(static_cast<int>(a_worldPoint.x*widthRatio), static_cast<int>(a_worldPoint.y*heightRatio), static_cast<int>(a_worldPoint.z));
	}

	Color Draw2D::backgroundColor( Color a_newColor ){
		clearBackgroundColor = a_newColor;
		glClearColor(clearBackgroundColor.R,clearBackgroundColor.G,clearBackgroundColor.B,clearBackgroundColor.A);
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
		setBlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE);
	}

	void Draw2D::validateShaderStatus(GLuint a_id, bool a_isShader) {
		GLint loadResult = GL_FALSE;
		if(a_isShader){
			glGetShaderiv(a_id, GL_COMPILE_STATUS, &loadResult);
		} else{
			glGetProgramiv(a_id, GL_LINK_STATUS, &loadResult);
		}

		if(loadResult != GL_TRUE){
			int infoLogLength;
			glGetShaderiv(a_id, GL_INFO_LOG_LENGTH, &infoLogLength);
			std::vector<char> vertexShaderErrorMessage(std::max(infoLogLength, int(1)));
			glGetShaderInfoLog(a_id, infoLogLength, 0, &vertexShaderErrorMessage[0]);
			MV::require(0, MV::ResourceException(std::string("Shader Error: ") + std::string(vertexShaderErrorMessage.begin(), vertexShaderErrorMessage.end())));
		}
	}

	void Draw2D::loadPartOfShader(GLuint a_id, const std::string &a_code) {
		char const * sourcePointer = a_code.c_str();
		glShaderSource(a_id, 1, &sourcePointer, 0);
		glCompileShader(a_id);

		validateShaderStatus(a_id, true);
	}

	Shader* Draw2D::loadShaderCode(const std::string &a_id, const std::string &a_vertexShaderCode, const std::string &a_fragmentShaderCode) {
		auto vsId = glCreateShader(GL_VERTEX_SHADER);
		auto fsId = glCreateShader(GL_FRAGMENT_SHADER);

		loadPartOfShader(vsId, a_vertexShaderCode);
		loadPartOfShader(fsId, a_fragmentShaderCode);

		GLuint programId = glCreateProgram();
		glAttachShader(programId, vsId);
		glAttachShader(programId, fsId);
		glLinkProgram(programId);

		validateShaderStatus(programId, false);

		bool makeDefault = shaders.empty();

		shaders.emplace(std::make_pair(a_id, Shader(a_id, programId)));
		if(makeDefault){
			defaultShaderPtr = &(shaders.find(a_id)->second);
			std::for_each(needShaderRegistration.begin(), needShaderRegistration.end(), [&](std::shared_ptr<Scene::Node> a_node){
				a_node->shader(a_id);
			});
			return defaultShaderPtr;
		}else{
			return &shaders.find(a_id)->second;
		}
	}

	Shader* Draw2D::loadShader(const std::string &a_id, const std::string &a_vertexShaderFilename, const std::string &a_fragmentShaderFilename) {
		std::ifstream vertexShaderFile(a_vertexShaderFilename);
		MV::require(vertexShaderFile.is_open(), MV::ResourceException("Failed to load vertex shader: " + a_vertexShaderFilename));
		std::string vertexShaderCode((std::istreambuf_iterator<char>(vertexShaderFile)), std::istreambuf_iterator<char>());

		std::ifstream fragmentShaderFile(a_fragmentShaderFilename);
		MV::require(vertexShaderFile.is_open(), MV::ResourceException("Failed to load fragment shader: " + a_fragmentShaderFilename));
		std::string fragmentShaderCode((std::istreambuf_iterator<char>(fragmentShaderFile)), std::istreambuf_iterator<char>());

		return loadShaderCode(a_id, vertexShaderCode, fragmentShaderCode);
	}

	Shader* Draw2D::getShader(const std::string &a_id) {
		auto found = shaders.find(a_id);
		MV::require(found != shaders.end(), MV::RangeException("Shader not loaded: " + a_id));
		return &found->second;
	}

	Shader* Draw2D::defaultShader(const std::string &a_id) {
		defaultShaderPtr = getShader(a_id);
		MV::require(defaultShaderPtr != nullptr, MV::PointerException("No default shader."));
		return defaultShaderPtr;
	}

	Shader* Draw2D::defaultShader() const {
		MV::require(defaultShaderPtr != nullptr, MV::PointerException("No default shader."));
		return defaultShaderPtr;
	}

	void Draw2D::registerDefaultShader(std::shared_ptr<Scene::Node> a_node) {
		if(defaultShaderPtr != nullptr){
			a_node->shader(defaultShaderPtr->id());
		} else{
			needShaderRegistration.push_back(a_node);
		}
	}




	Framebuffer::Framebuffer(Draw2D *a_renderer, GLuint a_framebuffer, GLuint a_renderbuffer, GLuint a_depthbuffer, GLuint a_texture, const Size<int> &a_size, const Point<int> &a_position) :
		renderbuffer(a_renderbuffer),
		framebuffer(a_framebuffer),
		depthbuffer(a_depthbuffer),
		texture(a_texture),
		frameSize(a_size),
		framePosition(a_position),
		renderer(a_renderer){
	}

	Framebuffer::~Framebuffer(){
		renderer->deleteFramebuffer(*this);
	}

	void Framebuffer::start() {
		renderer->startUsingFramebuffer(shared_from_this());
	}

	void Framebuffer::stop() {
		renderer->stopUsingFramebuffer();
	}

}
