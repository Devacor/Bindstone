#ifndef __TEXT_H__
#define __TEXT_H__

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Utility/package.h"
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_opengl.h>
#include "Render/drawShapes.h"

namespace M2Rend {
   //WARNING:
   //SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size (not larger) unless that changes.

   struct TextState{
      TextState(const M2Util::UtfString &a_text, const M2Rend::Color &a_color, const std::string &a_fontIdentifier)
         :text(a_text),color(a_color),fontIdentifier(a_fontIdentifier){}
      M2Util::UtfString text;
      M2Rend::Color color;
      std::string fontIdentifier;
   };

   M2Rend::Color parseColorString(std::string a_colorString);

   std::vector<TextState> parseTextStateList(std::string a_defaultFontIdentifier, M2Util::UtfString a_text);

   class TextCharacter {
   public:
      TextCharacter():set(false),textureId(0),character(' '){}
      ~TextCharacter(){}

      void assignSDLSurface(SDL_Surface *a_newSurface, M2Util::UtfChar a_glyphChar);

      bool isSet();

      M2Util::UtfChar getChar();
      GLuint* getTextureId();
      Point getCharacterSize();
      Point getTextureSize();
   private:
      void assignSurfaceToOpenGL(SDL_Surface *a_newSurface, int a_widthPowerOfTwo, int a_heightPowerOfTwo);
      void generateTexture(SDL_Surface *a_surface);

      bool set;
      M2Util::UtfChar character;
      Point size, textureSize;
      GLuint textureId;
   };

   struct FontDefinition{
   public:
      FontDefinition():font(nullptr){}
      ~FontDefinition(){
         if(font != nullptr){
            TTF_CloseFont(font);
         }
      }

      TTF_Font* font;
      FontDefinition(FontDefinition &a_other){
         font = a_other.font;
         a_other.font = nullptr;
      }

      FontDefinition& operator=(FontDefinition &a_other){
         font = a_other.font;
         a_other.font = nullptr;
      }
   };

   enum TextWrapMethod{
      HARD,
      SOFT
   };

   class TextLibrary{
   public:
      TextLibrary(M2Rend::Draw2D *a_rendering);
      ~TextLibrary(){}

      bool loadFont(std::string a_identifier, int a_pointSize, std::string a_fontFileLocation);

      std::shared_ptr<Scene> composeScene(std::vector<TextState> a_textStateList, double a_maxWidth = 0, TextWrapMethod a_wrapMethod = SOFT);

      //Call this when the OpenGL context is destroyed to re-load the textures.
      void reloadTextures();

      M2Rend::Draw2D *getRenderer(){return render;}
   private:
      typedef std::map<M2Util::UtfChar, TextCharacter> CachedGlyphs;
      typedef std::map<std::string, CachedGlyphs> CachedGlyphList;
      CachedGlyphs* initGlyphs(const std::string &a_identifier, const M2Util::UtfString &a_text);
      void loadIndividualGlyphs(const std::string &a_identifier, const M2Util::UtfString &a_text, CachedGlyphs &a_characterList);

      std::map<std::string, FontDefinition> loadedFonts;
      CachedGlyphList cachedGlyphs;
      SDL_Color white;
      M2Rend::Draw2D *render;
   };

   class TextBox{
   public:
      TextBox(TextureManager *a_textures, TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, int a_width, int a_height);
      TextBox(TextureManager *a_textures, TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const M2Util::UtfString &a_text, int a_width, int a_height);

      void initialize();

      void setText(const M2Util::UtfString &a_text, const std::string &a_fontIdentifier = "");
      void setText(M2Util::UtfChar a_char, const std::string &a_fontIdentifier = ""){
         setText(M2Util::UtfString()+a_char, a_fontIdentifier);
      }
      void setText(Uint16 a_char, const std::string &a_fontIdentifier = ""){
         M2Util::UtfChar *character = reinterpret_cast<M2Util::UtfChar*>(&a_char);
         setText(*character, a_fontIdentifier);
      }
      //any further development to input handling should probably be broken out elsewhere.
      bool setText(SDL_Event &event);

      void appendText(const M2Util::UtfString &a_text){
         setText(text + a_text);
      }
      void appendText(const M2Util::UtfChar a_char){
         setText(text+a_char);
      }
      void appendText(Uint16 a_char){
         M2Util::UtfChar *character = reinterpret_cast<M2Util::UtfChar*>(&a_char);
         appendText(*character);
      }

      void prependText(const M2Util::UtfString &a_text){
         setText(a_text + text);
      }
      void prependText(M2Util::UtfChar a_char){
         setText(a_char+text);
      }
      void prependText(Uint16 a_char){
         M2Util::UtfChar *character = reinterpret_cast<M2Util::UtfChar*>(&a_char);
         prependText(*character);
      }

      void backspace(){
         setText(text.substr(0, text.size()-1));
      }

      M2Util::UtfString getText(){return text;}

      void setTextBoxSize(int a_width, int a_height);

      void setScrollPosition(const Point &a_position){
         M2Util::require(initialized, M2Util::DefaultException("TextBox::setScrollPosition called before initialization."));
         textTextureScene.placeAt(a_position);
         renderTextWindowTexture();
      }
      void translateScrollPosition(const Point &a_position){
         M2Util::require(initialized, M2Util::DefaultException("TextBox::translateScrollPosition called before initialization."));
         textTextureScene.translate(a_position);
         renderTextWindowTexture();
      }

      std::shared_ptr<Scene> scene(){
         return textBoxFullScene;
      }

      void draw(){
         M2Util::require(initialized, M2Util::DefaultException("TextBox::draw called before initialization."));
         textBoxFullScene->draw();
      }
   private:
      void initializeTextWindowTexture();

      void renderTextWindowTexture();

      //it is important to reload TextLibrary textures before reloading the atlas textures as rendering
      //here relies on the textures being valid in the TextLibrary.
      void reloadTexture(M2Rend::MainTexture &textureUpdating){
         render->createFramebuffer(textWindowFramebuffer);
         renderTextWindowTexture();
      }

      void initializeTextBoxFullScene();

      bool initialized;
      int width, height;
      std::shared_ptr<Scene> textBoxFullScene;
      Scene textTextureScene;
      TextLibrary *textLibrary;
      TextureManager *textures;
      MainTexture *textWindowTexture;
      Draw2D *render;
      Framebuffer textWindowFramebuffer;
      M2Util::UtfString text;
      std::string fontIdentifier;
      std::string textureHandle;
   };
}

#endif
