/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

#ifndef _RENDER_H_
#define _RENDER_H_

#include <algorithm>
#include <functional>
#include "Render/points.h"
#include "Render/matrix.h"
#include "SDL/SDL.h"
#include "SDL/SDL_opengl.h"
#include <string>
#include <iostream>

namespace M2Rend {

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

   #ifndef GLNULL
      #define GLNULL 0
   #endif
   #if defined(WIN32) && !defined(GL_BGR)
      #define GL_BGR GL_BGR_EXT
   #endif

   MatrixStack& projectionMatrix();
   MatrixStack& modelviewMatrix();

   class glExtensionBlendMode{
   public:
      glExtensionBlendMode();
      bool blendModeExtensionEnabled(){return initialized;}
      void setBlendMode(GLenum a_sfactorRGB, GLenum a_dfactorRGB, GLenum a_sfactorAlpha, GLenum a_dfactorAlpha);
   protected:
      PFNGLBLENDFUNCSEPARATEEXTPROC pglBlendFuncSeparateEXT;

      void loadExtensionBlendMode(char *a_extensionsList);
   private:
      bool initialized;
   };

   struct Framebuffer{
   public:
      Framebuffer(){}
      Framebuffer(GLuint *a_texture, int a_width, int a_height)
         :texture(a_texture),width(a_width),height(a_height){}
      GLuint framebuffer;
      GLuint renderbuffer;
      GLuint *texture;
      int width, height;
   };

   class glExtensionFramebufferObject{
   public:
      glExtensionFramebufferObject(int &a_windowWidth, int &a_windowHeight);

      bool framebufferObjectExtensionEnabled(){return initialized;}
      void createFramebuffer(Framebuffer &a_framebuffer);

      void startUsingFramebuffer(const Framebuffer &a_framebuffer);
      void stopUsingFramebuffer();
   protected:
      PFNGLISRENDERBUFFEREXTPROC pglIsRenderbufferEXT;
      PFNGLBINDRENDERBUFFEREXTPROC pglBindRenderbufferEXT;
      PFNGLDELETERENDERBUFFERSEXTPROC pglDeleteRenderbuffersEXT;
      PFNGLGENRENDERBUFFERSEXTPROC pglGenRenderbuffersEXT;
      PFNGLRENDERBUFFERSTORAGEEXTPROC pglRenderbufferStorageEXT;
      PFNGLGETRENDERBUFFERPARAMETERIVEXTPROC pglGetRenderbufferParameterivEXT;
      PFNGLISFRAMEBUFFEREXTPROC pglIsFramebufferEXT;
      PFNGLBINDFRAMEBUFFEREXTPROC pglBindFramebufferEXT;
      PFNGLDELETEFRAMEBUFFERSEXTPROC pglDeleteFramebuffersEXT;
      PFNGLGENFRAMEBUFFERSEXTPROC pglGenFramebuffersEXT;
      PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC pglCheckFramebufferStatusEXT;
      PFNGLFRAMEBUFFERTEXTURE1DEXTPROC pglFramebufferTexture1DEXT;
      PFNGLFRAMEBUFFERTEXTURE2DEXTPROC pglFramebufferTexture2DEXT;
      PFNGLFRAMEBUFFERTEXTURE3DEXTPROC pglFramebufferTexture3DEXT;
      PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC pglFramebufferRenderbufferEXT;
      PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC pglGetFramebufferAttachmentParameterivEXT;
      PFNGLGENERATEMIPMAPEXTPROC pglGenerateMipmapEXT;

      void loadExtensionFramebufferObject(char* a_extensionsList);
   private:
      int &windowWidth, &windowHeight;
      bool initialized;
   };

   class glExtensionVertexBufferObject {
   public:
      glExtensionVertexBufferObject():initialized(false){}

      void loadExtensionVertexBufferObject(char* a_extensionsList);
   protected:
      PFNGLBINDBUFFERARBPROC pglBindBufferARB;
      PFNGLDELETEBUFFERSARBPROC pglDeleteBuffersARB;
      PFNGLGENBUFFERSARBPROC pglGenBuffersARB;
      PFNGLISBUFFERARBPROC pglIsBufferARB;
      PFNGLBUFFERDATAARBPROC pglBufferDataARB;
      PFNGLBUFFERSUBDATAARBPROC pglBufferSubDataARB;
      PFNGLGETBUFFERSUBDATAARBPROC pglGetBufferSubDataARB;
      PFNGLMAPBUFFERARBPROC pglMapBufferARB;
      PFNGLUNMAPBUFFERARBPROC pglUnmapBufferARB;
      PFNGLGETBUFFERPARAMETERIVARBPROC pglGetBufferParameterivARB;
      PFNGLGETBUFFERPOINTERVARBPROC pglGetBufferPointervARB;
   private:
      bool initialized;
   };

   class glExtensions : 
      public glExtensionFramebufferObject, 
      public glExtensionBlendMode,
      public glExtensionVertexBufferObject
   {
   public:
      glExtensions(int &a_windowWidth, int &a_windowHeight):glExtensionFramebufferObject(a_windowWidth, a_windowHeight){}
   protected:
      void initializeExtensions(){
         char* extensionsList = (char*) glGetString(GL_EXTENSIONS);
         loadExtensionBlendMode(extensionsList);
         loadExtensionFramebufferObject(extensionsList);
         loadExtensionVertexBufferObject(extensionsList);
      }
   };

   //If attempting to make multiple instances of Draw2D bear in mind it modifies global state in the
   //projection matrices, OpenGL, and SDL.
   class Draw2D : public glExtensions {
   public:
      Draw2D();
      ~Draw2D(){}

      void setWindowTitle(std::string a_title);

      //Warning: Any time the window is resized the OpenGL context is trashed and all textures must be re-loaded.
      void allowWindowResize(bool a_allowResize);
      void useFullScreen(bool a_isFullScreen);
      bool initialize(int a_windowsWidth, int a_windowsHeight, double a_worldsWidth = -1, double a_worldsHeight = -1, bool a_requireExtensions = false);

      void setBackgroundColor(Color a_newColor);
      bool resizeWindow(int a_x = -1, int a_y = -1);
      void resizeWorldSpace(double a_drawingWidth, double a_drawingHeight);
      void clearScreen();
      void updateScreen();

      Point getWindowSize();
      Point getWorldSize();

      //Given a point x, y return the position in world space where that will be rendered
      //within the current projection/modelview.  Gets the final world point after transformations
      //from an object's local points.
      Point getObjectToWorldPoint(double a_objectX, double a_objectY);
      Point getObjectToWorldPoint(Point a_worldPoint);

      Point getWindowToWorldPoint(double a_windowX, double a_windowY);
      Point getWindowToWorldPoint(Point a_windowPoint);

      bool isWindowFullScreen();
   private:
      int fullScreenTransition();
      bool setupSDL();
      void setupOpengl();

      Color backgroundColor;
      std::string windowTitle;
      int SDLflags;
      int windowWidth, windowHeight;
      double worldWidth, worldHeight;
      bool windowIsFullScreen, initialized;
   };
}
#endif
