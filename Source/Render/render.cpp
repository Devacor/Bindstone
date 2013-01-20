#include "render.h"
#include "Utility/generalUtility.h"


namespace MESA {
	//these two methods taken from the MESA library.
	static void gluMultMatrixVecd(const GLdouble matrix[16], const GLdouble in[4], GLdouble out[4]) {
		int i;

		for (i=0; i<4; i++) {
			out[i] = 
				in[0] * matrix[0*4+i] +
				in[1] * matrix[1*4+i] +
				in[2] * matrix[2*4+i] +
				in[3] * matrix[3*4+i];
		}
	}
	GLint extern gluProject(GLdouble objx, GLdouble objy, GLdouble objz, const GLdouble modelMatrix[16], const GLdouble projMatrix[16], const GLint viewport[4], GLdouble *winx, GLdouble *winy, GLdouble *winz) {
		GLdouble in[4];
		GLdouble out[4];

		in[0]=objx;
		in[1]=objy;
		in[2]=objz;
		in[3]=1.0;
		MESA::gluMultMatrixVecd(modelMatrix, in, out);
		MESA::gluMultMatrixVecd(projMatrix, out, in);
		if (in[3] == 0.0) return(GL_FALSE);
		in[0] /= in[3];
		in[1] /= in[3];
		in[2] /= in[3];
		/* Map x, y and z to range 0-1 */
		in[0] = in[0] * 0.5 + 0.5;
		in[1] = in[1] * 0.5 + 0.5;
		in[2] = in[2] * 0.5 + 0.5;

		/* Map x,y to viewport */
		in[0] = in[0] * viewport[2] + viewport[0];
		in[1] = in[1] * viewport[3] + viewport[1];

		*winx=in[0];
		*winy=in[1];
		*winz=in[2];
		return(GL_TRUE);
	}
}

namespace MV {

	void checkSDLError(int line)
	{
		const char *error = SDL_GetError();
		if (*error != '\0')
		{
			std::cerr << "SDL Error: " << error << std::endl;
			if (line != -1){
				std::cerr << "Line: " << error << std::endl;
			}
			SDL_ClearError();
		}
	}

	MatrixStack& projectionMatrix(){
		static MatrixStack projectionMatrix;
		return projectionMatrix;
	}

	MatrixStack& modelviewMatrix(){
		static MatrixStack modelviewMatrix;
		return modelviewMatrix;
	}

	/*************************\
	| ------Extensions------- |
	\*************************/

	glExtensionBlendMode::glExtensionBlendMode() :initialized(false)
#ifdef WIN32
		,pglBlendFuncSeparateEXT(nullptr)
#endif
	{
	}

	void glExtensionBlendMode::setBlendMode( GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha ){
		if(initialized){
#ifdef WIN32
			pglBlendFuncSeparateEXT(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#else
			glBlendFuncSeparateOES(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#endif
		}else{
			glBlendFunc(a_sfactorRGB, a_dfactorRGB);
		}
	}

	void glExtensionBlendMode::loadExtensionBlendMode( char *a_extensionsList ){
#ifdef WIN32
		if(strstr(a_extensionsList, "GL_EXT_blend_func_separate ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_EXT_blend_func_separate IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}else{
			initialized = true;
			pglBlendFuncSeparateEXT = ( void (APIENTRY*) (GLenum, GLenum, GLenum, GLenum)) SDL_GL_GetProcAddress("glBlendFuncSeparateEXT");
			pglBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
		}
#else
		initialized = true;
		glBlendFuncSeparateOES(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
#endif
	}

	glExtensionFramebufferObject::glExtensionFramebufferObject(Draw2D *a_renderer)
		:renderer(a_renderer),
		initialized(false)
#ifdef WIN32
		,pglIsRenderbufferEXT(nullptr),pglBindRenderbufferEXT(nullptr),pglDeleteRenderbuffersEXT(nullptr),
		pglGenRenderbuffersEXT(nullptr),pglRenderbufferStorageEXT(nullptr),pglGetRenderbufferParameterivEXT(nullptr),
		pglIsFramebufferEXT(nullptr),pglBindFramebufferEXT(nullptr),pglDeleteFramebuffersEXT(nullptr),
		pglGenFramebuffersEXT(nullptr),pglCheckFramebufferStatusEXT(nullptr),pglFramebufferTexture1DEXT(nullptr),
		pglFramebufferTexture2DEXT(nullptr),pglFramebufferTexture3DEXT(nullptr),pglFramebufferRenderbufferEXT(nullptr),
		pglGetFramebufferAttachmentParameterivEXT(nullptr),pglGenerateMipmapEXT(nullptr)
#endif
		{
	}

	void glExtensionFramebufferObject::createFramebuffer( Framebuffer &a_framebuffer ){
		require(initialized, ResourceException("CreateFramebuffer failed because the extension could not be loaded"));
#ifdef WIN32
		pglGenFramebuffersEXT(1, &a_framebuffer.framebuffer);
		pglGenRenderbuffersEXT(1, &a_framebuffer.renderbuffer);
#else
		glGenFramebuffersOES(1, &a_framebuffer.framebuffer);
		glGenRenderbuffersOES(1, &a_framebuffer.renderbuffer);
		glGenRenderbuffersOES(1, &a_framebuffer.depthbuffer);
#endif
	}

	void glExtensionFramebufferObject::startUsingFramebuffer( const Framebuffer &a_framebuffer ){
		require(initialized, ResourceException("StartUsingFramebuffer failed because the extension could not be loaded"));
		
#ifdef WIN32
		pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, a_framebuffer.framebuffer);
		pglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, *a_framebuffer.texture, 0);
		pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, a_framebuffer.renderbuffer);
		pglRenderbufferStorageEXT( GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, roundUpPowerOfTwo(a_framebuffer.width), roundUpPowerOfTwo(a_framebuffer.height));
#else
		originalFramebufferIds.push_back(0);
		originalRenderbufferIds.push_back(0);
		glGetIntegerv(GL_FRAMEBUFFER_BINDING_OES, &originalFramebufferIds[0]);
		glGetIntegerv(GL_RENDERBUFFER_BINDING_OES, &originalRenderbufferIds[0]);
		
		int width = roundUpPowerOfTwo(a_framebuffer.width);
		int height = roundUpPowerOfTwo(a_framebuffer.height);
		
		glBindFramebufferOES(GL_FRAMEBUFFER_OES, a_framebuffer.framebuffer);
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, a_framebuffer.renderbuffer);
		
		glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_RGBA, width, height);
		glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_RENDERBUFFER_OES, a_framebuffer.renderbuffer);
		
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, a_framebuffer.depthbuffer);
		glRenderbufferStorageOES(GL_RENDERBUFFER_OES, GL_DEPTH_COMPONENT16_OES, width, height);
		glFramebufferRenderbufferOES(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, a_framebuffer.depthbuffer);
		
		glFramebufferTexture2DOES(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, *a_framebuffer.texture, 0);

		if(glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES){
			std::cout << "Start Using Framebuffer failure: " << glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) << std::endl;
		}
#endif
		glViewport( 0, 0, a_framebuffer.width, a_framebuffer.height );
		projectionMatrix().push().makeOrtho(0, a_framebuffer.width, 0, a_framebuffer.height, -128.0f, 128.0f);
		renderer->clearScreen();
	}

	void glExtensionFramebufferObject::stopUsingFramebuffer(){
		require(initialized, ResourceException("StopUsingFramebuffer failed because the extension could not be loaded"));
#ifdef WIN32
		pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); 
		pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
#else
		glBindFramebufferOES(GL_FRAMEBUFFER_OES, originalFramebufferIds.back());
		glBindRenderbufferOES(GL_RENDERBUFFER_OES, originalRenderbufferIds.back());
		originalFramebufferIds.pop_back();
		originalRenderbufferIds.pop_back();
		
		if(glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES){
			std::cout << "Stop Using Framebuffer failure: " << glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) << std::endl;
		}
#endif
		glViewport( 0, 0, renderer->getWindowSize().x, renderer->getWindowSize().y);
		projectionMatrix().pop();
	}
	
	void glExtensionFramebufferObject::deleteFramebuffer( Framebuffer &a_framebuffer ){
		glDeleteFramebuffersOES(1, &a_framebuffer.framebuffer);
		glDeleteRenderbuffersOES(1, &a_framebuffer.renderbuffer);
		a_framebuffer.framebuffer = 0;
		a_framebuffer.renderbuffer = 0;
	}

	void glExtensionFramebufferObject::loadExtensionFramebufferObject( char* a_extensionsList ){
#ifdef WIN32
		if(strstr(a_extensionsList, "GL_EXT_framebuffer_object ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_EXT_framebuffer_object IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
		}else{
			initialized = true;
			pglGenFramebuffersEXT						= (PFNGLGENFRAMEBUFFERSEXTPROC)wglGetProcAddress("glGenFramebuffersEXT");
			pglDeleteFramebuffersEXT					= (PFNGLDELETEFRAMEBUFFERSEXTPROC)wglGetProcAddress("glDeleteFramebuffersEXT");
			pglBindFramebufferEXT						= (PFNGLBINDFRAMEBUFFEREXTPROC)wglGetProcAddress("glBindFramebufferEXT");
			pglCheckFramebufferStatusEXT				= (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)wglGetProcAddress("glCheckFramebufferStatusEXT");
			pglGetFramebufferAttachmentParameterivEXT	= (PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC)wglGetProcAddress("glGetFramebufferAttachmentParameterivEXT");
			pglGenerateMipmapEXT						= (PFNGLGENERATEMIPMAPEXTPROC)wglGetProcAddress("glGenerateMipmapEXT");
			pglFramebufferTexture2DEXT					= (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)wglGetProcAddress("glFramebufferTexture2DEXT");
			pglFramebufferRenderbufferEXT				= (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)wglGetProcAddress("glFramebufferRenderbufferEXT");
			pglGenRenderbuffersEXT						= (PFNGLGENRENDERBUFFERSEXTPROC)wglGetProcAddress("glGenRenderbuffersEXT");
			pglDeleteRenderbuffersEXT					= (PFNGLDELETERENDERBUFFERSEXTPROC)wglGetProcAddress("glDeleteRenderbuffersEXT");
			pglBindRenderbufferEXT						= (PFNGLBINDRENDERBUFFEREXTPROC)wglGetProcAddress("glBindRenderbufferEXT");
			pglRenderbufferStorageEXT					= (PFNGLRENDERBUFFERSTORAGEEXTPROC)wglGetProcAddress("glRenderbufferStorageEXT");
			pglGetRenderbufferParameterivEXT			= (PFNGLGETRENDERBUFFERPARAMETERIVEXTPROC)wglGetProcAddress("glGetRenderbufferParameterivEXT");
			pglIsRenderbufferEXT						= (PFNGLISRENDERBUFFEREXTPROC)wglGetProcAddress("glIsRenderbufferEXT");
		}
#else
		initialized = true;
#endif
	}


	void glExtensionVertexBufferObject::loadExtensionVertexBufferObject( char* a_extensionsList ){
#ifdef WIN32
		if(strstr(a_extensionsList, "GL_ARB_vertex_buffer_object ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_ARB_vertex_buffer_object IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
		}else{
			initialized = true;
			pglBindBufferARB			= (PFNGLBINDBUFFERARBPROC)wglGetProcAddress("glBindBufferARB");
			pglDeleteBuffersARB			= (PFNGLDELETEFRAMEBUFFERSEXTPROC)wglGetProcAddress("glDeleteBuffersARB");
			pglGenBuffersARB			= (PFNGLGENBUFFERSARBPROC)wglGetProcAddress("glGenBuffersARB");
			pglIsBufferARB				= (PFNGLISBUFFERARBPROC)wglGetProcAddress("glIsBufferARB");
			pglBufferDataARB			= (PFNGLBUFFERDATAARBPROC)wglGetProcAddress("glBufferDataARB");
			pglBufferSubDataARB			= (PFNGLBUFFERSUBDATAARBPROC)wglGetProcAddress("glBufferSubDataARB");
			pglGetBufferSubDataARB		= (PFNGLGETBUFFERSUBDATAARBPROC)wglGetProcAddress("glGetBufferSubDataARB");
			pglMapBufferARB				= (PFNGLMAPBUFFERARBPROC)wglGetProcAddress("glMapBufferARB");
			pglUnmapBufferARB			= (PFNGLUNMAPBUFFERARBPROC)wglGetProcAddress("glUnmapBufferARB");
			pglGetBufferParameterivARB	= (PFNGLGETBUFFERPARAMETERIVARBPROC)wglGetProcAddress("glGetBufferParameterivARB");
			pglGetBufferPointervARB		= (PFNGLGETBUFFERPOINTERVARBPROC)wglGetProcAddress("glGetBufferPointervARB");
		}
#else
		initialized = true;
#endif
	}

	/*************************\
	| --------Draw2D--------- |
	\*************************/

	Draw2D::Draw2D() :
		glExtensions(this),
		backgroundColor(0.0, 0.0, 0.0, 0.0),
		windowTitle("M2tM Application"),
		initialized(0),
		windowIsFullScreen(0),
		glcontext(0){
		SDLflags=SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN;
	}
	
	Draw2D::~Draw2D(){
		SDL_GL_DeleteContext(glcontext);
		SDL_DestroyWindow(window);
	}

	void Draw2D::allowWindowResize( bool a_allowResize ){
		if(a_allowResize){
			SDLflags = SDLflags | SDL_WINDOW_RESIZABLE;
		}else{
			SDLflags = SDLflags & ~ SDL_WINDOW_RESIZABLE;
		}
	}

	void Draw2D::useFullScreen( bool a_isFullScreen ){
		if(a_isFullScreen){
			SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN;
		}else{
			SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		}
	}
	
	void Draw2D::useBorderlessWindow( bool a_isBorderless ){
		if(a_isBorderless){
			SDLflags = SDLflags | SDL_WINDOW_BORDERLESS;
		}else{
			SDLflags = SDLflags & ~ SDL_WINDOW_BORDERLESS;
		}
	}

	bool Draw2D::initialize(int a_windowsWidth, int a_windowsHeight, double a_drawingWidth, double a_drawingHeight, bool a_requireExtensions){
		windowWidth = a_windowsWidth; windowHeight = a_windowsHeight;
		if(a_drawingWidth == -1 || a_drawingHeight == -1){
			worldWidth = windowWidth; worldHeight = windowHeight;
		}else{
			worldWidth = a_drawingWidth; worldHeight = a_drawingHeight;
		}
		if(setupSDL()){
			setupOpengl();
			initializeExtensions();
			summarizeDisplayMode();
			return true;
		}
		return false;
	}
	
	void Draw2D::summarizeDisplayMode() const{
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(0, &mode);
		int width, height;
		
		SDL_GetWindowSize(window, &width, &height);

		std::cout << "\\/==================================================\\/" << std::endl;
		std::cout << "Window	  : (" << width << " x " << height << ")" << std::endl;
		std::cout << "Driver	  : " << SDL_GetCurrentVideoDriver() << std::endl;
		std::cout << "Screen bpp : " << SDL_BITSPERPIXEL(mode.format) << std::endl;
		std::cout << "Vendor	  : " << glGetString(GL_VENDOR) << std::endl;
		std::cout << "Renderer	: " << glGetString(GL_RENDERER) << std::endl;
		std::cout << "Version	 : " << glGetString(GL_VERSION) << std::endl;
		std::cout << "Extensions : " << glGetString(GL_EXTENSIONS) << std::endl;
		std::cout << "/\\==================================================/\\" << std::endl;
	}

	bool Draw2D::setupSDL(){
		if( SDL_GetNumVideoDrivers() < 1 || SDL_VideoInit(0) < 0 ){
			// Failed, exit
			std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
			return false;
		}
		windowIsFullScreen = 0;
		if(SDLflags&SDL_WINDOW_FULLSCREEN){
			windowIsFullScreen = 1;
		}
		
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
		
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 0);
		
		SDL_GL_SetAttribute(SDL_GL_RETAINED_BACKING, 1);
		
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		
		window = SDL_CreateWindow(windowTitle.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowWidth, windowHeight, SDLflags);
		if(!window){
			std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
			atexit(SDL_Quit); // Quit SDL at exit.
			return false;
		}
		SDL_GetWindowSize(window, &windowWidth, &windowHeight);
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
		return true;
	}

	void Draw2D::setupOpengl(){
		if(!glcontext){
			glcontext = SDL_GL_CreateContext(window);
			if(!glcontext){
				std::cerr << "OpenGL context failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			
			if (SDL_GL_MakeCurrent(window, glcontext)) {
				std::cerr << "SDL_GL_MakeCurrent(): " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			SDL_GL_SetSwapInterval(0);
		}
		checkSDLError(__LINE__);
		std::cout << "2ErrCH: " << glGetError() << std::endl;
		
		SDL_GL_SetSwapInterval(0);
		
		glViewport( 0, 0, windowWidth, windowHeight );

		projectionMatrix().clear(); //ensure nothing else has trampled on us.
		projectionMatrix().top().makeOrtho(0, worldWidth, worldHeight, 0, -128.0, 128.0);

		glClearColor(backgroundColor.R,backgroundColor.G,backgroundColor.B,backgroundColor.A);
		
        glShadeModel(GL_SMOOTH);
		
		glEnable (GL_BLEND);
		
		glDisable(GL_CULL_FACE);
		
		glDisable (GL_ALPHA_TEST);
		glShadeModel(GL_SMOOTH);
		
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
		glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
		
		setBlendMode(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
		
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
		
#ifdef HAVE_OPENGLES
		glClearDepthf(16.0f);
#else
		glClearDepth(16.0f);
#endif
		
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	bool Draw2D::resizeWindow( int a_x, int a_y ){
		/*if(a_x > 0 && a_y > 0){
			windowWidth = a_x; windowHeight = a_y;
		}
		int fullScreenResult = fullScreenTransition();
		if(fullScreenResult==-1){
			if(SDL_SetVideoMode(a_x, a_y, 32, SDLflags)==NULL){
				return false;
			}else{
				setupOpengl();
				return true;
			}
		}
		return fullScreenResult != 0;*/
		return false;
	}

	void Draw2D::resizeWorldSpace( double a_worldsWidth, double a_worldsHeight ){
		worldWidth = a_worldsWidth; worldHeight = a_worldsHeight;
		setupOpengl();
	}

	int Draw2D::fullScreenTransition(){
		/*if(!windowIsFullScreen && (SDLflags & SDL_FULLSCREEN)){
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			if(setupSDL()){
				setupOpengl();
				return 1;
			}
			return 0;
		}else if(windowIsFullScreen && !(SDLflags & SDL_FULLSCREEN)){
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
			if(setupSDL()){
				setupOpengl();
				return 1;
			}
			return 0;
		}*/
		return -1;
	}

	bool Draw2D::isWindowFullScreen(){
		return windowIsFullScreen;
	}

	void Draw2D::clearScreen(){
		refreshContext();
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void Draw2D::refreshContext(){
		if(SDL_GL_MakeCurrent(window, glcontext)){
			std::cerr << "Problem with refreshContext()." << SDL_GetError() << std::endl;
		}
	}

	void Draw2D::updateScreen(){
		SDL_GL_SwapWindow( window );
	}

	void Draw2D::setWindowTitle( std::string a_title ){
		windowTitle = a_title;
		if(initialized){
			//SDL_WM_SetCaption(windowTitle.c_str(), windowTitle.c_str());
		}
	}

	Point Draw2D::getObjectToWorldPoint( double a_worldX, double a_worldY ){
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);

		GLdouble x, y, z;
		MESA::gluProject(a_worldX, a_worldY, 0.0, &(*modelviewMatrix().top().getMatrixArray())[0], &(*projectionMatrix().top().getMatrixArray())[0], viewport, &x, &y, &z);
		Point tmpPoint(x, y, z);
		
		//Re-orient for our actual window position based on 0, 0 as top left instead of bottom left.
		tmpPoint.y-=windowHeight;
		tmpPoint.y*=-1.0;
		//Finally, retrieve the actual location of the object in the world by taking into account the window dimensions
		//in relation to the screen dimensions.
		double scaleDifferenceY = worldHeight/(double)windowHeight, scaleDifferenceX = worldWidth/(double)windowWidth;

		tmpPoint.x*=scaleDifferenceX;
		tmpPoint.y*=scaleDifferenceY;

		return tmpPoint;
	}

	Point Draw2D::getObjectToWorldPoint( Point a_worldPoint ){
		return getObjectToWorldPoint(a_worldPoint.x, a_worldPoint.y);
	}

	Point Draw2D::getWindowToWorldPoint( double a_windowX, double a_windowY ){
		GLint viewport[4];
		glGetIntegerv(GL_VIEWPORT, viewport);

		GLdouble x, y, z;
		MESA::gluProject(a_windowX, a_windowY, 0.0, &(*modelviewMatrix().top().getMatrixArray())[0], &(*projectionMatrix().top().getMatrixArray())[0], viewport, &x, &y, &z);
		Point tmpPoint(x, y, z);
		
		//Re-orient for our actual window position based on 0, 0 as top left instead of bottom left.
		tmpPoint.y-=windowHeight;
		tmpPoint.y*=-1.0;
		//Finally, retrieve the actual location of the object in the world by taking into account the window dimensions
		//in relation to the screen dimensions.
		double scaleDifferenceY = worldHeight/(double)windowHeight, scaleDifferenceX = worldWidth/(double)windowWidth;

		tmpPoint.x*=scaleDifferenceX;
		tmpPoint.y*=scaleDifferenceY;

		return tmpPoint;
	}

	Point Draw2D::getWindowToWorldPoint( Point a_windowPoint ){
		return getWindowToWorldPoint(a_windowPoint.x, a_windowPoint.y);
	}

	Point Draw2D::getWorldSize(){
		Point tmpPoint;
		tmpPoint.x = worldWidth;
		tmpPoint.y = worldHeight;
		return tmpPoint;
	}

	Point Draw2D::getWindowSize(){
		Point tmpPoint;
		tmpPoint.x = windowWidth;
		tmpPoint.y = windowHeight;
		return tmpPoint;
	}

	void Draw2D::setBackgroundColor( Color a_newColor ){
		backgroundColor = a_newColor;
		glClearColor(backgroundColor.R,backgroundColor.G,backgroundColor.B,backgroundColor.A);
	}

}
