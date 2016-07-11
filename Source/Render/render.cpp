#include "render.h"
#include "Scene/node.h"
#include "textures.h"
#include "sharedTextures.h"
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
	const std::string DEFAULT_ID = "default";
	const std::string PREMULTIPLY_ID = "premultiply";
	const std::string COLOR_PICKER_ID = "colorPicker";
	const std::string ALPHA_FILTER_ID = "alphaFilter";

	bool RUNNING_IN_HEADLESS = false;

	bool Draw2D::firstInitializationSDL = true;
	bool Draw2D::firstInitializationOpenGL = true;

	static GLint viewport[4];

	Point<> ProjectionDetails::projectScreenRaw(const Point<> &a_point, const TransformMatrix &a_modelview) {
		Point<> result;

		if (MESA::gluProject(a_point.x, a_point.y, a_point.z, &(*a_modelview.getMatrixArray())[0], &(*renderer.projectionMatrix().top().getMatrixArray())[0], viewport, &result.x, &result.y, &result.z) == GL_FALSE) {
			std::cerr << "gluProject failure!" << std::endl;
		}
		result.y = renderer.window().height() - result.y;
		result.z = a_point.z;//restore original z since we're just 2d.
		//std::cout << "project: " << a_point << result << "\n" << a_modelview << "_____________________" << std::endl;
		return result;
	}

	Point<int> ProjectionDetails::projectScreen(const Point<> &a_point, const TransformMatrix &a_modelview){
		return round<int>(projectScreenRaw(a_point, a_modelview));
	}

	Point<> ProjectionDetails::projectWorld(const Point<> &a_point, const TransformMatrix &a_modelview){
		return renderer.worldFromScreenRaw(projectScreenRaw(a_point, a_modelview));
	}

	Point<> ProjectionDetails::unProjectScreenRaw(const Point<> &a_point, const TransformMatrix &a_modelview){
		Point<> result;

		auto inputPoint = a_point;
		inputPoint.y = renderer.window().height() - inputPoint.y;
		if(MESA::gluUnProject(inputPoint.x, inputPoint.y, .5, &(*a_modelview.getMatrixArray())[0], &(*renderer.projectionMatrix().top().getMatrixArray())[0], viewport, &result.x, &result.y, &result.z) == GL_FALSE){
			std::cerr << "gluUnProject failure!" << std::endl;
		}
		result.z = static_cast<PointPrecision>(a_point.z);//restore original z since we're just 2d.
		//std::cout << "unproj: " << inputPoint << result << "\n" << a_modelview << "_____________________" << std::endl;
		return result;
	}

	Point<> ProjectionDetails::unProjectScreen(const Point<int> &a_point, const TransformMatrix &a_modelview) {
		return unProjectScreenRaw(cast<PointPrecision>(a_point), a_modelview);
	}

	Point<> ProjectionDetails::unProjectWorld(const Point<> &a_point, const TransformMatrix &a_modelview){
		return renderer.screenFromWorldRaw(unProjectScreenRaw(a_point, a_modelview));
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

	glExtensionBlendMode::glExtensionBlendMode(Draw2D *a_renderer) :renderer(a_renderer), initialized(false)
	{
	}

	void glExtensionBlendMode::setBlendFunction(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha){
		if (!renderer->headless()) {
			if (initialized) {
#ifdef WIN32
				glBlendFuncSeparate(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#else
				glBlendFuncSeparateOES(a_sfactorRGB, a_dfactorRGB, a_sfactorAlpha, a_dfactorAlpha);
#endif
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
#ifdef WIN32
			glBlendEquationSeparate(a_rgbBlendFunc, a_alphaBlendFunc);
#else
			glBlendEquationSeparate(a_rgbBlendFunc, a_alphaBlendFunc);
#endif
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
#ifdef WIN32
			glGenFramebuffers(1, &framebufferId);
			glGenRenderbuffers(1, &renderbufferId);
			//glGenRenderbuffers(1, &depthbufferId); //not used right now
#else
			glGenFramebuffersOES(1, &framebufferId);
			glGenRenderbuffersOES(1, &renderbufferId);
			glGenRenderbuffersOES(1, &depthbufferId);
#endif
		}
		return std::shared_ptr<Framebuffer>(new Framebuffer(renderer, framebufferId, renderbufferId, depthbufferId, a_texture, a_size, a_position, a_backgroundColor));
	}

	void glExtensionFramebufferObject::startUsingFramebuffer(std::weak_ptr<Framebuffer> a_framebuffer, bool a_push){
		require<ResourceException>(renderer->headless() || (initialized && !a_framebuffer.expired()), "StartUsingFramebuffer failed because the extension could not be loaded");

		savedClearColor = renderer->backgroundColor();
		renderer->backgroundColor(a_framebuffer.lock()->background);

		if(a_push){
			activeFramebuffers.push_back(a_framebuffer);
		}
		if (!renderer->headless()) {
#ifdef WIN32
			glBindFramebuffer(GL_FRAMEBUFFER, a_framebuffer.lock()->framebuffer);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a_framebuffer.lock()->texture, 0);
			glBindRenderbuffer(GL_RENDERBUFFER, a_framebuffer.lock()->renderbuffer);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, roundUpPowerOfTwo(a_framebuffer.lock()->frameSize.width), roundUpPowerOfTwo(a_framebuffer.lock()->frameSize.height));
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

			if (glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) != GL_FRAMEBUFFER_COMPLETE_OES) {
				std::cout << "Start Using Framebuffer failure: " << glCheckFramebufferStatusOES(GL_FRAMEBUFFER_OES) << std::endl;
			}
#endif
			glViewport(a_framebuffer.lock()->framePosition.x, a_framebuffer.lock()->framePosition.y, a_framebuffer.lock()->frameSize.width, a_framebuffer.lock()->frameSize.height);
			glGetIntegerv(GL_VIEWPORT, viewport);
		}
		renderer->projectionMatrix().push().makeOrtho(0, static_cast<MatrixValue>(a_framebuffer.lock()->frameSize.width), 0, static_cast<MatrixValue>(a_framebuffer.lock()->frameSize.height), -128.0f, 128.0f);

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
		require<ResourceException>(renderer->headless() || initialized, "StopUsingFramebuffer failed because the extension could not be loaded");
		activeFramebuffers.pop_back();
		if(!activeFramebuffers.empty()){
			startUsingFramebuffer(activeFramebuffers.back(), false);
		} else {
			if (!renderer->headless()) {
#ifdef WIN32
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glBindRenderbuffer(GL_RENDERBUFFER, 0);
#else
				glBindFramebufferOES(GL_FRAMEBUFFER_OES, originalFramebufferId);
				glBindRenderbufferOES(GL_RENDERBUFFER_OES, originalRenderbufferId);
#endif
				glViewport(0, 0, renderer->window().width(), renderer->window().height());
				glGetIntegerv(GL_VIEWPORT, viewport);
			}
			renderer->projectionMatrix().pop();
			renderer->backgroundColor(savedClearColor);
		}
	}
	
	void glExtensionFramebufferObject::deleteFramebuffer( Framebuffer &a_framebuffer ){
		if (!renderer->headless()) {
#ifdef WIN32
			glDeleteFramebuffers(1, &a_framebuffer.framebuffer);
			glDeleteRenderbuffers(1, &a_framebuffer.renderbuffer);
			//glDeleteRenderbuffers(1, &a_framebuffer.depthbuffer); //not used currently
#else
			glDeleteFramebuffersOES(1, &a_framebuffer.framebuffer);
			glDeleteRenderbuffersOES(1, &a_framebuffer.renderbuffer);
			glDeleteRenderbuffersOES(1, &a_framebuffer.depthbuffer);
#endif
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
		renderer(a_renderer),
		maintainProportions(true),
		sizeWorldWithWindow(false),
		SDLflags(SDL_WINDOW_OPENGL|SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE),
		initialized(false),
		vsync(false),
		userCanResize(false),
		glcontext(0){
		
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
	}

	MV::Size<int> Window::resize(const Size<int> &a_size){
		auto sizeDelta = windowSize - a_size;
		windowSize = a_size;
		updateAspectRatio();
		if(initialized){
			if (!renderer.headless()) {
				SDL_SetWindowSize(window, windowSize.width, windowSize.height);
			}
			renderer.setupOpengl();
			if(!userCanResize){
				lockUserResize();
			}
		}
		return sizeDelta;
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
			if (!renderer.headless()) {
				SDL_GetWindowSize(window, &windowSize.width, &windowSize.height);
			}
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
		if(initialized && !renderer.headless()){
			SDL_SetWindowMinimumSize(window, std::max(minSize.width, 1), std::max(minSize.height, 1));
			SDL_SetWindowMaximumSize(window, std::max(maxSize.width, 1), std::max(maxSize.height, 1));
			checkSDLError(__LINE__);
		}
	}

	Window& Window::windowedMode(){
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(initialized && !renderer.headless()){
			SDL_SetWindowFullscreen(window, 0);
		}
		return *this;
	}

	void Window::fullScreenMode(){
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN_DESKTOP;
		if(initialized && !renderer.headless()){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
		}
	}

	void Window::fullScreenWindowedMode(){
		SDLflags = SDLflags | SDL_WINDOW_FULLSCREEN_DESKTOP;
		SDLflags = SDLflags & ~ SDL_WINDOW_FULLSCREEN;
		if(initialized && !renderer.headless()){
			SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
		}
	}
	
	Window& Window::borderless(){
		SDLflags = SDLflags | SDL_WINDOW_BORDERLESS;
		if(initialized && !renderer.headless()){
			SDL_SetWindowBordered(window, SDL_FALSE);
		}
		return *this;
	}

	Window& Window::bordered(){
		SDLflags = SDLflags & ~ SDL_WINDOW_BORDERLESS;
		if(initialized && !renderer.headless()){
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
		if (!renderer.headless()) {
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

			window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, windowSize.width, windowSize.height, SDLflags);
			if (!window) {
				std::cerr << "Window creation failed: " << SDL_GetError() << std::endl;
				atexit(SDL_Quit); // Quit SDL at exit.
				return false;
			}
			SDL_GetWindowSize(window, &windowSize.width, &windowSize.height);
			
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
		}
		updateAspectRatio();
		initialized = true;

		//must happen after we flag initialized as true.
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
				MV::require<PointerException>(gl3wInit() == 0, "gl3wInit failed!");
				renderer.firstInitializationOpenGL = false;
			}

			if (SDL_GL_MakeCurrent(window, glcontext)) {
				std::cerr << "SDL_GL_MakeCurrent(): " << SDL_GetError() << std::endl;
				atexit(SDL_Quit);
			}
			SDL_GL_SetSwapInterval(vsync);
		}
	}

	bool Window::handleEvent(const SDL_Event &a_event, RenderWorld &a_world){
		if(a_event.type == SDL_WINDOWEVENT && a_event.window.event == SDL_WINDOWEVENT_RESIZED){
			std::cout << "Window Resized!" << std::endl;
			auto worldScreenDelta = cast<PointPrecision>(windowSize) / a_world.size();
			Size<int> newSize(a_event.window.data1, a_event.window.data2);
			MV::PointPrecision oldRatio = aspectRatio;
			if(maintainProportions){
				conformToAspectRatio(newSize.width, newSize.height);
			}
			auto screenSizeDelta = resize(newSize);
			if (sizeWorldWithWindow) {
				auto worldSizeDelta = cast<PointPrecision>(screenSizeDelta) / worldScreenDelta;
				std::cout << "Screen Resize: " << screenSizeDelta << " World Resize: " << worldSizeDelta << std::endl;
				a_world.resize(a_world.size() - worldSizeDelta);
			}
			if(maintainProportions){
				aspectRatio = oldRatio; //just prevent resize from mucking with this.
			}
			return true;
		}
		return false;
	}

	void Window::refreshContext(){
		ensureValidGLContext();
		if(!renderer.headless() && SDL_GL_MakeCurrent(window, glcontext)){
			std::cerr << "Problem with refreshContext()." << SDL_GetError() << std::endl;
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

		RUNNING_IN_HEADLESS = false;
	}

	Draw2D::~Draw2D(){
	}

	bool Draw2D::initialize(Size<int> a_window, Size<> a_world, bool a_requireExtensions, bool a_summarize){
		sdlWindow.resize(a_window);
		if(a_world.width < 0 || a_world.height < 0){
			a_world = cast<PointPrecision>(a_window);
		}
		mvWorld.resize(a_world);

		if(setupSDL()){
			setupOpengl();
			initializeExtensions();
			if(a_summarize){
				summarizeDisplayMode();
			}
			loadDefaultShaders();
			return true;
		}
		return false;
	}
	
	void Draw2D::summarizeDisplayMode() const{
		SDL_DisplayMode mode;
		SDL_GetCurrentDisplayMode(0, &mode);

		std::cout << "\\/==================================================\\/" << std::endl;
		std::cout << "Window	  : (" << sdlWindow.width() << " x " << sdlWindow.height() << ")" << std::endl;
		if (!headless()) {
			std::cout << "Driver	  : " << SDL_GetCurrentVideoDriver() << std::endl;
			std::cout << "Screen bpp : " << SDL_BITSPERPIXEL(mode.format) << std::endl;
			std::cout << "Vendor	  : " << glGetString(GL_VENDOR) << std::endl;
			std::cout << "Renderer	: " << glGetString(GL_RENDERER) << std::endl;
			std::cout << "Version	 : " << glGetString(GL_VERSION) << std::endl;
			std::cout << "Extensions : " << glGetString(GL_EXTENSIONS) << std::endl;
		} else {
			std::cout << "HEADLESS" << std::endl;
		}
		std::cout << "/\\==================================================/\\" << std::endl;
	}

	bool Draw2D::setupSDL(){
		if(firstInitializationSDL){
			firstInitializationSDL = false;
			if (!headless()) {
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
				SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

				if (SDL_Init(SDL_INIT_EVERYTHING) == -1) {
					std::cerr << "SDL_Init: " << SDL_GetError();
				}

				if (SDL_GetNumVideoDrivers() < 1 || SDL_VideoInit(0) < 0) {
					// Failed, exit
					std::cerr << "Video initialization failed: " << SDL_GetError() << std::endl;
					return false;
				}
				gl3wInit();
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
		if (!headless()) {
			checkSDLError(__LINE__);

			SDL_GL_SetSwapInterval(0);
		}
		refreshWorldAndWindowSize();

		if (!headless()) {
			glClearColor(clearBackgroundColor.R, clearBackgroundColor.G, clearBackgroundColor.B, clearBackgroundColor.A);

			//glShadeModel(GL_SMOOTH);


			glEnable(GL_BLEND);

			glDisable(GL_CULL_FACE);

			glDisable(GL_POLYGON_SMOOTH);
			glDisable(GL_LINE_SMOOTH);

			//glDisable (GL_ALPHA_TEST);

			glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
			glHint(GL_FRAGMENT_SHADER_DERIVATIVE_HINT, GL_NICEST);
			glHint(GL_TEXTURE_COMPRESSION_HINT, GL_NICEST);
			glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

			defaultBlendFunction();
			setBlendEquation(GL_FUNC_ADD, GL_FUNC_ADD);

			glDepthMask(GL_FALSE);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_DEPTH_CLAMP);
			glDepthFunc(GL_LEQUAL);


#ifdef HAVE_OPENGLES
			glClearDepthf(1.0f);
#else
			glClearDepth(1.0f);
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
		sdlWindow.refreshContext();
		if (!headless()) {
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
	}

	void Draw2D::updateScreen(){
		sdlWindow.updateScreen();
	}

	Point<> Draw2D::worldFromLocal(const Point<> &a_localPoint, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectWorld(a_localPoint, a_modelview);
	}

	Point<int> Draw2D::screenFromLocal(const Point<> &a_localPoint, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.projectScreen(a_localPoint, a_modelview);
	}

	Point<> Draw2D::localFromWorld(const Point<> &a_worldPoint, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectWorld(a_worldPoint, a_modelview);
	}

	Point<> Draw2D::localFromScreen(const Point<int> &a_worldPoint, const TransformMatrix &a_modelview) const{
		MV::ProjectionDetails projectIt(*this);
		return projectIt.unProjectScreen(a_worldPoint, a_modelview);
	}

	Point<> Draw2D::worldFromScreenRaw(const Point<> &a_screenPoint) const {
		PointPrecision widthRatio = window().width() / world().width();
		PointPrecision heightRatio = window().height() / world().height();
		return Point<>(a_screenPoint.x / widthRatio, a_screenPoint.y / heightRatio, a_screenPoint.z);
	}

	Point<> Draw2D::worldFromScreen(const Point<int> &a_screenPoint) const {
		return worldFromScreenRaw(cast<PointPrecision>(a_screenPoint));
	}

	Point<> Draw2D::screenFromWorldRaw(const Point<> &a_worldPoint) const {
		PointPrecision widthRatio = window().width() / world().width();
		PointPrecision heightRatio = window().height() / world().height();
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
		GLuint programId = -1;
		if (!headless()) {
			auto vsId = glCreateShader(GL_VERTEX_SHADER);
			auto fsId = glCreateShader(GL_FRAGMENT_SHADER);

			loadPartOfShader(vsId, a_vertexShaderCode);
			loadPartOfShader(fsId, a_fragmentShaderCode);

			programId = glCreateProgram();
			glAttachShader(programId, vsId);
			glAttachShader(programId, fsId);
			glLinkProgram(programId);

			validateShaderStatus(programId, false);
		}

		auto emplaceResult = shaders.emplace(std::make_pair(a_id, Shader(a_id, programId, headless())));
		MV::require<ResourceException>(emplaceResult.second, "Failed to insert shader to map: ", a_id);
		Shader* shaderPtr = &emplaceResult.first->second;

		if (makeDefault) {
			defaultShaderPtr = shaderPtr;
		}

		return shaderPtr;
	}

	void Draw2D::loadDefaultShaders() {
		loadShader(MV::DEFAULT_ID, "Assets/Shaders/default.vert", "Assets/Shaders/default.frag");
		loadShader(MV::PREMULTIPLY_ID, "Assets/Shaders/default.vert", "Assets/Shaders/premultiply.frag");
		loadShader(MV::ALPHA_FILTER_ID, "Assets/Shaders/default.vert", "Assets/Shaders/alphaFilter.frag");
		loadShader(MV::COLOR_PICKER_ID, "Assets/Shaders/default.vert", "Assets/Shaders/colorPicker.frag");
	}

	Shader* Draw2D::loadShader(const std::string &a_id, const std::string &a_vertexShaderFilename, const std::string &a_fragmentShaderFilename) {
		auto found = shaders.find(a_id);
		if(found == shaders.end()){
			std::ifstream vertexShaderFile(a_vertexShaderFilename);
			MV::require<ResourceException>(vertexShaderFile.is_open(), "Failed to load vertex shader: ", a_vertexShaderFilename);
			std::string vertexShaderCode((std::istreambuf_iterator<char>(vertexShaderFile)), std::istreambuf_iterator<char>());

			std::ifstream fragmentShaderFile(a_fragmentShaderFilename);
			MV::require<ResourceException>(vertexShaderFile.is_open(), "Failed to load fragment shader: ", a_fragmentShaderFilename);
			std::string fragmentShaderCode((std::istreambuf_iterator<char>(fragmentShaderFile)), std::istreambuf_iterator<char>());

			return loadShaderCode(a_id, vertexShaderCode, fragmentShaderCode);
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
		MV::require<PointerException>(defaultShaderPtr != nullptr, "No default shader.");
		return defaultShaderPtr;
	}

	Shader* Draw2D::defaultShader() const {
		MV::require<PointerException>(defaultShaderPtr != nullptr, "No default shader.");
		return defaultShaderPtr;
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
		if (!headless()) {
			glViewport(0, 0, sdlWindow.width(), sdlWindow.height());
			glGetIntegerv(GL_VIEWPORT, viewport);
		} else {
			viewport[0] = 0;
			viewport[1] = 0;
			viewport[2] = static_cast<GLint>(sdlWindow.width());
			viewport[3] = static_cast<GLint>(sdlWindow.height());
		}

		projectionMatrix().clear(); //ensure nothing else has trampled on us.
		projectionMatrix().top().makeOrtho(0, mvWorld.width(), mvWorld.height(), 0, -1.0, 1.0);
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

	void Shader::set(std::string a_variableName, GLuint a_texture, GLuint a_textureBindIndex) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_texture != 0) ?
					a_texture :
					SharedTextures::white()->texture()->textureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
			} else {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
	}

	void Shader::set(std::string a_variableName, const std::shared_ptr<TextureDefinition> &a_texture, GLuint a_textureBindIndex) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_texture != nullptr) ?
					a_texture->textureId() :
					SharedTextures::white()->texture()->textureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
			} else {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
	}

	void Shader::set(std::string a_variableName, const std::shared_ptr<TextureHandle> &a_value, GLuint a_textureBindIndex) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				auto textureId = (a_value != nullptr && a_value->texture() != nullptr) ?
					a_value->texture()->textureId() :
					SharedTextures::white()->texture()->textureId();

				glActiveTexture(GL_TEXTURE0 + a_textureBindIndex);
				glUniform1i(offset, a_textureBindIndex);
				glBindTexture(GL_TEXTURE_2D, textureId);
			} else {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
	}

	void Shader::set(std::string a_variableName, PointPrecision a_value) {
		if (!headless) {
			GLuint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				glUniform1fv(offset, 1, &a_value);
			} else {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
	}

	void Shader::set(std::string a_variableName, const TransformMatrix &a_matrix) {
		if (!headless) {
			GLint offset = variableOffset(a_variableName);
			if (offset >= 0) {
				GLfloat *mat = &((*a_matrix.getMatrixArray())[0]);
				glUniformMatrix4fv(offset, 1, GL_FALSE, mat);
			} else {
				std::cerr << "Warning: Shader has no variable: " << a_variableName << std::endl;
			}
		}
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

}
