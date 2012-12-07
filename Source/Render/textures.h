#ifndef __TEXTURES_H__
#define __TEXTURES_H__

#if defined(WIN32) && !defined(GL_BGR)
#define GL_BGR GL_BGR_EXT
#endif

#if !defined(WIN32) && !defined(GLNULL)
#define GLNULL GLuint(NULL)
#endif

#ifndef GLNULL
#define GLNULL 0
#endif

#include <fstream>
#include <map>
#include <vector>
#include <string>
#include <functional>

#include <SDL/SDL.h>
#include <SDL/SDL_opengl.h>
#include <SDL/SDL_image.h>

#include <boost/property_tree/ptree.hpp>

namespace MV {
   struct MainTexture;
   /*---------------------------------------*\
   | SubTexture:                             |
   | Acts to store information under a main  |
   | texture.  Holds a pointer to the GLuint |
   | of the parent and pixel coordinates of  |
   | the location on the main texture that   |
   | make up the sub-texture.                |
   |                                         |
   | percent** holds the percent values of   |
   | the main texture height that the sub    |
   | texture holds (useful in application)   |
   \*---------------------------------------*/
   struct SubTexture{
   public:
      SubTexture():parentTexture(nullptr){}
      SubTexture(const std::string &a_subName, int a_x, int a_y, int a_width, int a_height)
         :name(a_subName), x(a_x), y(a_y), width(a_width), height(a_height), parentTexture(nullptr){
      }
      SubTexture(const std::string &a_subName, boost::property_tree::ptree &inputStructure);

      SubTexture& operator=(MainTexture& a_other);

      boost::property_tree::ptree save() const;

      std::string name;
      GLuint *parentTexture;
      int x, y, width, height;
      double percentX, percentY, percentWidth, percentHeight;
      int mainHeight, mainWidth;
   };

   /*---------------------------------------*\
   | MainTexture:                            |
   | Stores a single texture and keeps track |
   | of several sub-textures.  Maintains the |
   | GLuint identifier, file name, and name  |
   | handle which the programmer uses to     |
   | call this texture info in the future.   |
   \*---------------------------------------*/

   struct MainTexture{
   public:
      MainTexture():dynamicTexture(false){}
      MainTexture(const std::string &a_name, int a_width, int a_height, const std::string &a_file)
         :name(a_name), width(a_width), height(a_height), file(a_file), dynamicTexture(false),
         reloadCallback(nullptr){
      }

      SubTexture *getSubTexture(const std::string &a_subName);
      SubTexture *getSubTexture(int a_index);
      void clearSubTextures(){
         subTextures.clear();
      }

      boost::property_tree::ptree save() const;

      std::string name;
      GLuint texture;
      int width, height;
      std::string file;
      std::map<std::string, SubTexture> subTextures;
      bool dynamicTexture;
      std::function<void(MainTexture&)> reloadCallback;
      bool repeat;
   };

   /*---------------------------------------*\
   | TextureManager:                         |
   | Maintains several MainTextures and lets |
   | users save the current texture info or  |
   | load a previous state.  Also allows     |
   | simple adding of textures and recalling |
   | stored textures to apply to polygons.   |
   \*---------------------------------------*/
   class TextureManager{
   public:
      TextureManager();
      ~TextureManager();

      MainTexture* createEmptyTexture(const std::string &name, unsigned int width, unsigned int height, std::function<void(MainTexture&)> reloadCallback = nullptr);

      bool loadTexture(const std::string &name, const std::string &file, bool repeat = 0);
      bool addSubTexture(const std::string &mainName, const SubTexture &subTextureToAdd);

      int getNumMainTextures(){return (int)mainTextures.size();}
      MainTexture *getMainTexture(const std::string &mainName);
      MainTexture *getMainTexture(int num);

      GLuint *getMainTextureGLuint(const std::string &mainName);

      SubTexture *getSubTexture(const std::string &mainName, const std::string &subName);

      bool deleteMainTexture(const std::string &mainName);
      bool deleteSubTexture(const std::string &mainName, const std::string &subName);

      void clear(){mainTextures.clear();}
      bool clearSubTextures(const std::string &mainName);

      void save(const std::string &file);
      void load(const std::string &file);

      void reloadAllTextures(){
         std::map<std::string, MainTexture>::iterator cell;
         for(cell = mainTextures.begin();cell != mainTextures.end();cell++){
            reloadTexture(cell->second);
         }
         initializeInvalidTexture();
      }
   private:
      bool reloadTexture(MainTexture &ourTexture);
      Uint32 getPixel(SDL_Surface *surface, int x, int y);
      void setPixel(SDL_Surface *screen, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a = 255);
      void initializeInvalidTexture();
      bool loadTextureFromFile(const std::string &file, GLuint &imageLoaded, int &w, int &h, bool repeat);
      bool loadTextureFromSurface(SDL_Surface *img, GLuint &imageLoaded, int &w, int &h, bool repeat);
      std::map<std::string, MainTexture> mainTextures;
      MainTexture invalidTexture;
      SubTexture invalidSubTexture;
   };
}
#endif
