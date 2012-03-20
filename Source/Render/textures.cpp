#include "textures.h"
#include <iostream>
#include <algorithm>
#include "Utility/generalUtility.h"

#include <boost/property_tree/json_parser.hpp>

namespace MV {
   boost::property_tree::ptree MainTexture::save() const{
      boost::property_tree::ptree outputStructure;
      outputStructure.put("Type", ((dynamicTexture)?"Dynamic":"File"));
      outputStructure.put("File", file);
      outputStructure.put("Repeat", repeat);
      boost::property_tree::ptree subTextureOutput;
      std::for_each(subTextures.begin(), subTextures.end(), [&](std::pair<const std::string, SubTexture> current){
         subTextureOutput.add_child(current.first, current.second.save());
      });
      outputStructure.add_child("SubTextures", subTextureOutput);
      return outputStructure;
   }

   SubTexture * MainTexture::getSubTexture( const std::string &a_subName ){
      if(subTextures.find(a_subName) != subTextures.end()){
         return &(subTextures[a_subName]);
      }
      return nullptr;
   }

   SubTexture * MainTexture::getSubTexture( int a_index ){
      auto node = subTextures.begin();
      for(;a_index > 0 && node != subTextures.end();a_index--){
         node++;
      }
      if(node!=subTextures.end()){
         return &(node->second);
      }
      return nullptr;
   }

   SubTexture& SubTexture::operator=( MainTexture& a_other ){
      percentWidth = double(width)/double(a_other.width);
      percentHeight = double(height)/double(a_other.height);
      percentX = double(x)/double(a_other.width);
      percentY = double(y)/double(a_other.height);
      mainHeight = a_other.height;
      mainWidth = a_other.width;
      parentTexture = &a_other.texture;

      return *this;
   }

   SubTexture::SubTexture( const std::string &a_subName, boost::property_tree::ptree &inputStructure ) :name(a_subName){
      x = inputStructure.get<int>("X", 0);
      y = inputStructure.get<int>("Y", 0);
      width = inputStructure.get<int>("Width", 0);
      height = inputStructure.get<int>("Height", 0);
   }

   boost::property_tree::ptree SubTexture::save() const{
      boost::property_tree::ptree outputStructure;
      outputStructure.put("X", x);
      outputStructure.put("Y", y);
      outputStructure.put("Width", width);
      outputStructure.put("Height", height);
      return outputStructure;
   }

   TextureManager::TextureManager():invalidTexture("M2_INVALID_TEXTURE", 16, 16, ""), invalidSubTexture("M2_INVALID_SUB_TEXTURE", 0, 0, 16, 16){
      initializeInvalidTexture();
   }

   TextureManager::~TextureManager(){
   }

   MainTexture* TextureManager::createEmptyTexture(const std::string &name, unsigned int width, unsigned int height, std::function<void(MainTexture&)> reloadCallback){
      unsigned int originalWidth = width, originalHeight = height;
      width = roundUpPowerOfTwo(width);
      height = roundUpPowerOfTwo(height);
      
      GLuint txtnumber;						// Texture ID
      unsigned int* data;						// Stored Data
      unsigned int imageSize = (width * height)* 4 * sizeof(unsigned int);
      // Create Storage Space For Texture Data (128x128x4)
      data = (unsigned int*)new GLuint[(imageSize)];
      memset(data, 0,(imageSize));
      glGenTextures(1, &txtnumber);					// Create 1 Texture
      glBindTexture(GL_TEXTURE_2D, txtnumber);			// Bind The Texture
      // Build Texture Using Information In data
      glTexImage2D(GL_TEXTURE_2D, 0, 4, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);

      delete [] data;							// Release data
      MainTexture tmp;
      tmp.dynamicTexture = true;
      tmp.reloadCallback = reloadCallback;
      tmp.file = "";
      tmp.height = height;
      tmp.width = width;
      tmp.name = name;
      tmp.texture = txtnumber;
      tmp.subTextures["FullSize"] = SubTexture("FullSize", 0, 0, originalWidth, originalHeight);
      mainTextures[name] = tmp;
      mainTextures[name].subTextures["FullSize"] = mainTextures[name];
      return &mainTextures[name];
   }


   bool TextureManager::deleteSubTexture(const std::string &mainName, const std::string &subName){
      if(MainTexture *tmpMain = getMainTexture(mainName)){
         if(tmpMain && (tmpMain->subTextures.find(subName) != tmpMain->subTextures.end())){
            tmpMain->subTextures.erase(subName);
            return true;
         }
      }
      return false;
   }

   bool TextureManager::loadTexture(const std::string &mainName, const std::string &file, bool repeat){
      MainTexture txtToAdd;
      txtToAdd.repeat = repeat;
      txtToAdd.name = mainName;
      txtToAdd.file = file;
      txtToAdd.dynamicTexture = 0;
      //Create the texture
      bool success = loadTextureFromFile(file, txtToAdd.texture, txtToAdd.width, txtToAdd.height, repeat);

      if(success){
         mainTextures[mainName] = txtToAdd;
         return true;
      }
      return false;
   }

   bool TextureManager::reloadTexture(MainTexture &ourTexture){
      if(ourTexture.dynamicTexture){
         SubTexture *fullSize = ourTexture.getSubTexture("FullSize");
         createEmptyTexture(ourTexture.name, fullSize->width, fullSize->height, ourTexture.reloadCallback);
         if(ourTexture.reloadCallback != nullptr){
            ourTexture.reloadCallback(ourTexture);
         }
      }else{
         //Create the texture
         bool success = loadTextureFromFile(ourTexture.file, ourTexture.texture, ourTexture.width, ourTexture.height, ourTexture.repeat);

         if(success){
            return true;
         }
      }
      return false;
   }

   //Taken from an example in the SDL documentation
   Uint32 TextureManager::getPixel(SDL_Surface *surface, int x, int y)
   {
      int bpp = surface->format->BytesPerPixel;
      /* Here p is the address to the pixel we want to retrieve */
      Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

      switch(bpp) {
      case 1:
         return *p;

      case 2:
         return *(Uint16 *)p;

      case 3:
         if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
         else
            return p[0] | p[1] << 8 | p[2] << 16;

      case 4:
         return *(Uint32 *)p;
      }

      return 0;
   }

   void TextureManager::setPixel(SDL_Surface *screen, int x, int y, Uint8 r, Uint8 g, Uint8 b, Uint8 a)
   {
      Uint8 *ubuff8;
      Uint16 *ubuff16;
      Uint32 *ubuff32;
      Uint32 color;
      char c1, c2, c3;

      /* Lock the screen, if needed */
      if(SDL_MUSTLOCK(screen)) {
         if(SDL_LockSurface(screen) < 0) 
            return;
      }

      /* Get the color */
      color = SDL_MapRGBA( screen->format, r, g, b, a );

      /* How we draw the pixel depends on the bitdepth */
      switch(screen->format->BytesPerPixel) {
      case 1: 
         ubuff8 = (Uint8*) screen->pixels;
         ubuff8 += (y * screen->pitch) + x; 
         *ubuff8 = (Uint8) color;
         break;

      case 2:
         ubuff8 = (Uint8*) screen->pixels;
         ubuff8 += (y * screen->pitch) + (x*2);
         ubuff16 = (Uint16*) ubuff8;
         *ubuff16 = (Uint16) color; 
         break;  

      case 3:
         ubuff8 = (Uint8*) screen->pixels;
         ubuff8 += (y * screen->pitch) + (x*3);


         if(SDL_BYTEORDER == SDL_LIL_ENDIAN) {
            c1 = (color & 0xFF0000) >> 16;
            c2 = (color & 0x00FF00) >> 8;
            c3 = (color & 0x0000FF);
         } else {
            c3 = (color & 0xFF0000) >> 16;
            c2 = (color & 0x00FF00) >> 8;
            c1 = (color & 0x0000FF);	
         }

         ubuff8[0] = c3;
         ubuff8[1] = c2;
         ubuff8[2] = c1;
         break;

      case 4:
         ubuff8 = (Uint8*) screen->pixels;
         ubuff8 += (y*screen->pitch) + (x*4);
         ubuff32 = (Uint32*)ubuff8;
         *ubuff32 = color;
         break;

      default:
         std::cerr << "Error: Unknown bit-depth!\n";
      }

      /* Unlock the screen if needed */
      if(SDL_MUSTLOCK(screen)) {
         SDL_UnlockSurface(screen);
      }
   }

   void TextureManager::initializeInvalidTexture(){
      SDL_Surface *img = SDL_CreateRGBSurface(SDL_SWSURFACE, 16, 16, 24, 0, 255, 0, 255);
      if(img != nullptr){
         SDL_Rect fillLocation;
         fillLocation.x = 0; fillLocation.y = 0;
         fillLocation.w = 8; fillLocation.h = 8;
         SDL_FillRect(img, &fillLocation, SDL_MapRGB(img->format, 0, 0, 255));
         fillLocation.x = 8; fillLocation.y = 8;
         SDL_FillRect(img, &fillLocation, SDL_MapRGB(img->format, 0, 0, 255));

         bool success = loadTextureFromSurface(img, invalidTexture.texture, invalidTexture.width, invalidTexture.height, true);

         SDL_FreeSurface( img );
      }
   }

   bool TextureManager::loadTextureFromFile(const std::string &file, GLuint &imageLoaded, int &w, int &h, bool repeat) {
      SDL_Surface *img = nullptr;
      img = IMG_Load(file.c_str());
      if(img == nullptr){
         return false;
      }

      bool result = loadTextureFromSurface(img, imageLoaded, w, h, repeat);

      SDL_FreeSurface( img );
      return result;
   }

   //Load an opengl texture
   bool TextureManager::loadTextureFromSurface(SDL_Surface *img, GLuint &imageLoaded, int &w, int &h, bool repeat) {
      bool alpha = 1;
      GLenum texture_format;
      bool mipmaps = 1;
      int nOfColors;

      // Build the texture from the surface
      w = img->w;
      h = img->h; 

      //Check that the image's width is valid and then check that the image's width is a power of 2
      if(!isPowerOfTwo(w) || !isPowerOfTwo(h)){
         return false;
      }

      nOfColors = img->format->BytesPerPixel;
      if (nOfColors == 4){     // contains an alpha channel
         if (img->format->Rmask == 0x000000ff){
            texture_format = GL_RGBA;
         }else{
            texture_format = GL_BGRA;
         }
      } else if (nOfColors == 3){     // no alpha channel
         if (img->format->Rmask == 0x000000ff){
            texture_format = GL_RGB;
         }else{
            texture_format = GL_BGR;
         }
      } else {
         return false;
      }

      int type = (alpha) ? GL_RGBA : GL_RGB;
      glGenTextures(1, &imageLoaded);		// Generate texture ID
      glBindTexture(GL_TEXTURE_2D, imageLoaded);

      glTexImage2D(GL_TEXTURE_2D, 0, nOfColors, img->w, img->h, 0, texture_format, GL_UNSIGNED_BYTE, img->pixels);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (repeat) ? GL_REPEAT : GL_CLAMP_TO_EDGE);

      if(mipmaps){
         gluBuild2DMipmaps(GL_TEXTURE_2D, nOfColors, img->w, img->h, texture_format, GL_UNSIGNED_BYTE, img->pixels);
      }
      return true;
   }

   MainTexture *TextureManager::getMainTexture(int mainNum){
      std::map<std::string, MainTexture>::iterator node = mainTextures.begin();
      for(;mainNum > 0 && node != mainTextures.end();mainNum--){
         node++;
      }
      if(node!=mainTextures.end()){
         return &(node->second);
      }
      return &invalidTexture;
   }

   MainTexture *TextureManager::getMainTexture(const std::string &mainName){
      if((mainTextures.find(mainName) != mainTextures.end())){
         return &mainTextures[mainName];
      }else{
         return &invalidTexture;
      }
   }

   GLuint *TextureManager::getMainTextureGLuint(const std::string &mainName){
      MainTexture *tmp = getMainTexture(mainName);
      if(tmp){
         return &tmp->texture;
      }else{
         return &(invalidTexture.texture);
      }
   }

   bool TextureManager::addSubTexture(const std::string &mainName, SubTexture &subTextureToAdd){
      MainTexture *tmp = getMainTexture(mainName);
      if(tmp->name == "M2_INVALID_TEXTURE"){
         return false;
      }
      subTextureToAdd = *tmp;
      tmp->subTextures[subTextureToAdd.name]=subTextureToAdd;
      return true;
   }

   SubTexture *TextureManager::getSubTexture(const std::string &mainName, const std::string &subName){
      MainTexture *tmp = getMainTexture(mainName);
      if(tmp->subTextures.find(subName) != tmp->subTextures.end()){
         return &(tmp->subTextures[subName]);
      }
      invalidSubTexture.width = tmp->width;
      invalidSubTexture.height = tmp->height;
      invalidSubTexture = (*tmp);
      return &invalidSubTexture;
   }

   void TextureManager::save(const std::string &file){
      using boost::property_tree::ptree;
      ptree textureList;
      std::for_each(mainTextures.begin(), mainTextures.end(), [&](const std::pair<const std::string, MainTexture>& current){
         textureList.add_child(current.first, current.second.save());
      });
      ptree outputStructure;
      outputStructure.add_child("Textures", textureList);
      write_json(file, outputStructure);
   }

   //load a file holding the state of a texture manager.  Loads all images and such
   //into opengl as well.
   void TextureManager::load(const std::string &file){
      using boost::property_tree::ptree;
      ptree inputStructure;
      read_json(file, inputStructure);

      try{
         ptree textureList = inputStructure.get_child("Textures");
         std::for_each(textureList.begin(), textureList.end(), [&](boost::property_tree::ptree::value_type currentTexture){
            std::string type = currentTexture.second.get<std::string>("Type", "");
            if(type == "File"){
               std::string textureName = currentTexture.first;
               std::string fileName = currentTexture.second.get<std::string>("File", "");
               bool repeating = currentTexture.second.get<bool>("Repeat", false);

               loadTexture(textureName, fileName, repeating);

               ptree subTextureList = currentTexture.second.get_child("SubTextures");
               std::for_each(subTextureList.begin(), subTextureList.end(), [&](boost::property_tree::ptree::value_type currentSubTexture){
                  addSubTexture(textureName, SubTexture(currentSubTexture.first, currentSubTexture.second));
               });
            }
         });
      }catch(...){
         std::cerr << "Failed to load texture definition (" << file << ") is it correctly formed?" << std::endl;
      }
   }

   bool TextureManager::deleteMainTexture(const std::string &mainName){
      if(mainTextures.find(mainName) != mainTextures.end()){
         mainTextures.erase(mainName);
         return true;
      }else{
         return false;
      }
   }

   bool TextureManager::clearSubTextures( const std::string &mainName ){
      MainTexture *tmpMain = getMainTexture(mainName);
      if(tmpMain){
         tmpMain->clearSubTextures();
         return true;
      }
      return false;
   }
}
