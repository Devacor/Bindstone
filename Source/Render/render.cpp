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
	GLint GLAPI gluProject(GLdouble objx, GLdouble objy, GLdouble objz, const GLdouble modelMatrix[16], const GLdouble projMatrix[16], const GLint viewport[4], GLdouble *winx, GLdouble *winy, GLdouble *winz) {
			double in[4];
			double out[4];

			in[0]=objx;
			in[1]=objy;
			in[2]=objz;
			in[3]=1.0;
			gluMultMatrixVecd(modelMatrix, in, out);
			gluMultMatrixVecd(projMatrix, out, in);
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
			glBlendFuncSeparate(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#endif
		}else{
			glBlendFunc(a_sfactorRGB, a_dfactorRGB);
		}
	}

	void glExtensionBlendMode::loadExtensionBlendMode( char *a_extensionsList ){
		if(strstr(a_extensionsList, "GL_EXT_blend_func_separate ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_EXT_blend_func_separate IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		}else{
			initialized = true;
#ifdef WIN32
			pglBlendFuncSeparateEXT = ( void (APIENTRY*) (GLenum, GLenum, GLenum, GLenum)) SDL_GL_GetProcAddress("glBlendFuncSeparateEXT");
			pglBlendFuncSeparateEXT(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
#else
			glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
#endif
		}
	}

	glExtensionFramebufferObject::glExtensionFramebufferObject(int &a_windowWidth, int &a_windowHeight) 
		:windowWidth(a_windowWidth), windowHeight(a_windowHeight),
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
		glGenFramebuffers(1, &a_framebuffer.framebuffer);
		glGenRenderbuffers(1, &a_framebuffer.renderbuffer);
#endif
	}

	void glExtensionFramebufferObject::startUsingFramebuffer( const Framebuffer &a_framebuffer ){
		require(initialized, ResourceException("StartUsingFramebuffer failed because the extension could not be loaded"));
		
		glViewport( 0, 0, a_framebuffer.width, a_framebuffer.height );
		projectionMatrix().push().makeOrtho(0, a_framebuffer.width, 0, a_framebuffer.height, -128.0f, 128.0f);
#ifdef WIN32
		pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, a_framebuffer.framebuffer);
		pglFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, *a_framebuffer.texture, 0);
		pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, a_framebuffer.renderbuffer);
		pglRenderbufferStorageEXT( GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, roundUpPowerOfTwo(a_framebuffer.width), roundUpPowerOfTwo(a_framebuffer.height));
#else
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, a_framebuffer.framebuffer);
		glFramebufferTexture2D(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, *a_framebuffer.texture, 0);
		glBindRenderbuffer(GL_RENDERBUFFER_EXT, a_framebuffer.renderbuffer);
		glRenderbufferStorage( GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, roundUpPowerOfTwo(a_framebuffer.width), roundUpPowerOfTwo(a_framebuffer.height));
#endif
	}

	void glExtensionFramebufferObject::stopUsingFramebuffer(){
		require(initialized, ResourceException("StopUsingFramebuffer failed because the extension could not be loaded"));
#ifdef WIN32
		pglBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0); 
		pglBindRenderbufferEXT(GL_RENDERBUFFER_EXT, 0);
#else
		glBindFramebuffer(GL_FRAMEBUFFER_EXT, 0);
		glBindRenderbuffer(GL_RENDERBUFFER_EXT, 0);
#endif
		glViewport( 0, 0, windowWidth, windowHeight );
		projectionMatrix().pop();
	}

	void glExtensionFramebufferObject::loadExtensionFramebufferObject( char* a_extensionsList ){
		if(strstr(a_extensionsList, "GL_EXT_framebuffer_object ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_EXT_framebuffer_object IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
		}else{
			initialized = true;
#ifdef WIN32
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
#endif
		}
	}


	void glExtensionVertexBufferObject::loadExtensionVertexBufferObject( char* a_extensionsList ){
		if(strstr(a_extensionsList, "GL_ARB_vertex_buffer_object ")==nullptr){
			initialized = false;
			std::cerr << "\nError: The OpenGL extension GL_ARB_vertex_buffer_object IS NOT SUPPORTED ON THIS SYSTEM\n"  << std::endl;
		}else{
			initialized = true;
#ifdef WIN32
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
#endif
		}
	}

	/*************************\
	| --------Draw2D--------- |
	\*************************/

	Draw2D::Draw2D() : glExtensions(windowWidth, windowHeight), backgroundColor(0.0, 0.0, 0.0, 0.0), windowTitle("M2tM Application"), initialized(0), windowIsFullScreen(0){
		SDLflags=SDL_OPENGL|SDL_ANYFORMAT|SDL_SRCALPHA;
	}

	void Draw2D::allowWindowResize( bool a_allowResize ){
		if(a_allowResize){
			SDLflags = SDLflags | SDL_RESIZABLE;
		}else{
			SDLflags = SDLflags & ~ SDL_RESIZABLE;
		}
	}

	void Draw2D::useFullScreen( bool a_isFullScreen ){
		if(a_isFullScreen){
			SDLflags = SDLflags | SDL_FULLSCREEN;
		}else{
			SDLflags = SDLflags & ~ SDL_FULLSCREEN;
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
			return true;
		}
		return false;
	}

	bool Draw2D::setupSDL(){
		if( SDL_Init(SDL_INIT_VIDEO) < 0 ){
			// Failed, exit
			std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
			return false;
		}
		windowIsFullScreen = 0;
		if(SDLflags&SDL_FULLSCREEN){
			windowIsFullScreen = 1;
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		}
		SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_ALPHA_SIZE, 8 );

		SDL_GL_SetAttribute( SDL_GL_ACCUM_RED_SIZE, 16 );
		SDL_GL_SetAttribute( SDL_GL_ACCUM_GREEN_SIZE, 16 );
		SDL_GL_SetAttribute( SDL_GL_ACCUM_BLUE_SIZE, 16 );
		SDL_GL_SetAttribute( SDL_GL_ACCUM_ALPHA_SIZE, 16 );

		SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 16 );
		SDL_GL_SetAttribute( SDL_GL_SWAP_CONTROL, 0 ); //disable or enable vsync
		SDL_WM_SetCaption(windowTitle.c_str(), windowTitle.c_str());
		if( SDL_SetVideoMode( windowWidth, windowHeight, 32, SDLflags) == NULL ){
			std::cerr << "Video mode set failed: " << SDL_GetError() << std::endl;
			atexit(SDL_Quit); // Quit SDL at exit.
			return false;
		}
		SDL_EnableUNICODE(1);
		initialized = true;
		return true;
	}

	void Draw2D::setupOpengl(){
		glViewport( 0, 0, windowWidth, windowHeight );

		projectionMatrix().clear(); //ensure nothing else has trampled on us.
		projectionMatrix().top().makeOrtho(0, worldWidth, worldHeight, 0, -128.0, 128.0);

		glClearColor(backgroundColor.R,backgroundColor.G,backgroundColor.B,0.0f);
		glClearDepth(15.0f);

		glEnable (GL_BLEND);
		glDisable (GL_ALPHA_TEST);

		glDisable(GL_DEPTH_TEST);
		glShadeModel(GL_SMOOTH);
		glDisable(GL_CULL_FACE);
		glHint(GL_GENERATE_MIPMAP_HINT, GL_NICEST);
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		glHint(GL_SMOOTH, GL_NICEST);
		setBlendMode(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);

		glDepthMask(0);

		glEnable(GL_TEXTURE_2D);
		glDisable(GL_LIGHTING);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	bool Draw2D::resizeWindow( int a_x, int a_y ){
		if(a_x > 0 && a_y > 0){
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
		return fullScreenResult != 0;
	}

	void Draw2D::resizeWorldSpace( double a_worldsWidth, double a_worldsHeight ){
		worldWidth = a_worldsWidth; worldHeight = a_worldsHeight;
		setupOpengl();
	}

	int Draw2D::fullScreenTransition(){
		if(!windowIsFullScreen && (SDLflags & SDL_FULLSCREEN)){
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
		}
		return -1;
	}

	bool Draw2D::isWindowFullScreen(){
		return windowIsFullScreen;
	}

	void Draw2D::clearScreen(){
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	}

	void Draw2D::updateScreen(){
		SDL_GL_SwapBuffers( );
	}

	void Draw2D::setWindowTitle( std::string a_title ){
		windowTitle = a_title;
		if(initialized){
			SDL_WM_SetCaption(windowTitle.c_str(), windowTitle.c_str());
		}
	}

	Point Draw2D::getObjectToWorldPoint( double a_worldX, double a_worldY ){
		GLint viewport[4];
		Point tmpPoint;
		glGetIntegerv(GL_VIEWPORT, viewport);

		MESA::gluProject(a_worldX, a_worldY, 0.0, &(*modelviewMatrix().top().getMatrixArray())[0], &(*projectionMatrix().top().getMatrixArray())[0], viewport, &tmpPoint.x, &tmpPoint.y, &tmpPoint.z);
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
		Point tmpPoint;
		glGetIntegerv(GL_VIEWPORT, viewport);

		MESA::gluProject(a_windowX, a_windowY, 0.0, &(*modelviewMatrix().top().getMatrixArray())[0], &(*projectionMatrix().top().getMatrixArray())[0], viewport, &tmpPoint.x, &tmpPoint.y, &tmpPoint.z);
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
		glClearColor(backgroundColor.R,backgroundColor.G,backgroundColor.B,0.0f);
	}

}
