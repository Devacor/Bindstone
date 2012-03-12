#include "text.h"
#include <boost/algorithm/string.hpp>

namespace M2Rend {

   M2Rend::Color M2Rend::parseColorString(std::string a_colorString){
      if(a_colorString == ""){return M2Rend::Color();}
      std::vector<std::string> colorStrings;
      boost::split(colorStrings, a_colorString, boost::is_any_of(":"));
      M2Rend::Color result;
      if(colorStrings.empty()){

      }else{
         if(colorStrings.size() >= 1){
            try{result.R = boost::lexical_cast<float>(colorStrings[0]);}catch(boost::bad_lexical_cast){result.R = 1.0;}
         }
         if(colorStrings.size() >= 2){
            try{result.G = boost::lexical_cast<float>(colorStrings[1]);}catch(boost::bad_lexical_cast){result.G = 1.0;}
         }
         if(colorStrings.size() >= 3){
            try{result.B = boost::lexical_cast<float>(colorStrings[2]);}catch(boost::bad_lexical_cast){result.B = 1.0;}
         }
         if(colorStrings.size() >= 4){
            try{result.A = boost::lexical_cast<float>(colorStrings[3]);}catch(boost::bad_lexical_cast){result.A = 1.0;}
         }
      }
      return result;
   }

   void parseExpression(const M2Util::UtfString &a_expression, M2Rend::Color &a_currentColor, std::string &a_currentFontIdentifier, const std::string &a_defaultFontIdentifier){
      if(a_expression[0] == 'f'){
         a_currentFontIdentifier = M2Util::wideToString(a_expression.substr(2));
         if(a_currentFontIdentifier == ""){a_currentFontIdentifier = a_defaultFontIdentifier;}
      }else if(a_expression[0] == 'c'){
         a_currentColor = parseColorString(M2Util::wideToString(a_expression.substr(2)));
      }
   }

   std::vector<TextState> M2Rend::parseTextStateList(std::string a_defaultFontIdentifier, M2Util::UtfString a_text){
      M2Util::UtfString commit;
      M2Rend::Color currentColor;
      std::string currentFontIdentifier = a_defaultFontIdentifier;
      std::vector<TextState> textList;
      std::size_t found = 0, end;
      while(found != std::string::npos){
         found = std::min(a_text.find(UTF_CHAR_STR("[[f|")), a_text.find(UTF_CHAR_STR("[[c|")));
         commit = (a_text.substr(0, found));
         if(!commit.empty()){
            textList.push_back(TextState(commit, currentColor, currentFontIdentifier));
         }

         if(found != std::string::npos){
            end = a_text.find(UTF_CHAR_STR("]]"), found);
            if(end == std::string::npos){
               commit = a_text.substr(found);
               if(!commit.empty()){
                  textList.push_back(TextState(a_text.substr(found), currentColor, currentFontIdentifier));
               }
               break;
            }

            parseExpression(a_text.substr(found+2, end-found-2), currentColor, currentFontIdentifier, a_defaultFontIdentifier);
            a_text = a_text.substr(end+2);
         }
      }

      return textList;
   }

   /*************************\
   | -----TextCharacter----- |
   \*************************/

   void TextCharacter::assignSDLSurface(SDL_Surface *a_newSurface, M2Util::UtfChar a_glyphChar){
      M2Util::require(a_newSurface != nullptr, M2Util::PointerException("TextCharacter::assignSDLSurface was passed a null SDL_Surface pointer."));
      set = true;
      character = a_glyphChar;

      int widthPowerOfTwo = M2Util::roundUpPowerOfTwo(a_newSurface->w);
      int heightPowerOfTwo = M2Util::roundUpPowerOfTwo(a_newSurface->h);

      size = Point(a_newSurface->w, a_newSurface->h);
      textureSize = Point(widthPowerOfTwo, heightPowerOfTwo);

      assignSurfaceToOpenGL(a_newSurface, widthPowerOfTwo, heightPowerOfTwo);
   }

   void TextCharacter::assignSurfaceToOpenGL( SDL_Surface *a_newSurface, int a_widthPowerOfTwo, int a_heightPowerOfTwo ){
      M2Util::require(a_newSurface != nullptr, M2Util::PointerException("TextCharacter::assignSurfaceToOpenGL was passed a null SDL_Surface pointer."));
      SDL_Surface *surface = SDL_CreateRGBSurface(0, a_widthPowerOfTwo, a_heightPowerOfTwo, 32, 
         M2Rend::SDL_RMASK, M2Rend::SDL_GMASK, M2Rend::SDL_BMASK, M2Rend::SDL_AMASK);

      SDL_SetAlpha(a_newSurface, 0, 0);
      SDL_BlitSurface(a_newSurface, 0, surface, 0);

      SDL_FreeSurface(a_newSurface);
      generateTexture(surface);
   }

   void TextCharacter::generateTexture( SDL_Surface *a_surface ){
      M2Util::require(a_surface != nullptr, M2Util::PointerException("TextCharacter::generateTexture was passed a null SDL_Surface pointer."));

      glGenTextures(1, &textureId);
      glBindTexture(GL_TEXTURE_2D, textureId);
      glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
      glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
      glTexImage2D(GL_TEXTURE_2D, 0, 4, a_surface->w, a_surface->h, 0, GL_BGRA, GL_UNSIGNED_BYTE, a_surface->pixels);
      SDL_FreeSurface(a_surface);
   }

   bool TextCharacter::isSet(){
      return set;
   }

   M2Util::UtfChar TextCharacter::getChar(){
      return character;
   }

   GLuint* TextCharacter::getTextureId(){
      return &textureId;
   }

   Point TextCharacter::getCharacterSize(){
      return size;
   }

   Point TextCharacter::getTextureSize(){
      return textureSize;
   }

   /*************************\
   | ----------Text--------- |
   \*************************/

   TextLibrary::TextLibrary( M2Rend::Draw2D *a_rendering ){
      M2Util::require(a_rendering != nullptr, M2Util::PointerException("TextLibrary::TextLibrary was passed a null Draw2D pointer."));
      white.r = 255; white.g = 255; white.b = 255; white.unused = 0;
      render = a_rendering;
      if(!TTF_WasInit()){
         TTF_Init();
      }
   }

   bool TextLibrary::loadFont( std::string a_identifier, int a_pointSize, std::string a_fontFileLocation ){
      if(loadedFonts.find(a_identifier) == loadedFonts.end()){
         TTF_Font* newFont;
         if(newFont = TTF_OpenFont(a_fontFileLocation.c_str(), a_pointSize)) {
            loadedFonts[a_identifier].font = newFont;
            return true;
         }else{
            std::cerr << "Error loading font: " << TTF_GetError() << std::endl;
         }
      }else{
         std::cerr << "Error, font identifier already used, cannot assign another font to the same id: " << a_identifier << std::endl;
      }
      return false;
   }

   std::shared_ptr<Scene> TextLibrary::composeScene(std::vector<TextState> a_textStateList, double a_maxWidth, TextWrapMethod a_wrapMethod){
      auto textScene = std::make_shared<M2Rend::Scene>(render);
      if(a_textStateList.empty()){return textScene;}

      Color currentColor;
      int lineHeight, baseLine;
      int offset = 0;
      double characterLocationX = 0, nextCharacterLocationX = characterLocationX;
      double characterLocationY = 0;
      size_t characterCount = 0;
      size_t previousLine = 0;
      std::vector<M2Util::UtfChar> currentLineContent;
      std::vector<double> currentLineCharacterSizes;
      for(auto current = a_textStateList.begin();current != a_textStateList.end();++current){
         auto specifiedFont = loadedFonts.find(current->fontIdentifier);
         currentColor = current->color;
         if(specifiedFont != loadedFonts.end()){
            CachedGlyphs *characterList = initGlyphs(current->fontIdentifier, current->text);
            if(characterCount == 0){
               lineHeight = TTF_FontLineSkip(specifiedFont->second.font);
               baseLine = TTF_FontAscent(specifiedFont->second.font);
            }else{
               int newLineHeight = TTF_FontLineSkip(specifiedFont->second.font);
               int newBaseLine = TTF_FontAscent(specifiedFont->second.font);
               if(newBaseLine > baseLine){
                  for(size_t i = previousLine;i < characterCount;++i){
                     textScene->get(boost::lexical_cast<std::string>(i))->translate(Point(0, (newBaseLine-baseLine)));
                  }
                  offset = 0;
                  lineHeight = newLineHeight;
                  baseLine = newBaseLine;
               }else{
                  offset = baseLine-newBaseLine;
               }
            }

            for(auto renderChar = current->text.begin();renderChar != current->text.end();++renderChar){
               nextCharacterLocationX += (*characterList)[*renderChar].getCharacterSize().x;
               bool lineWidthExceeded = (a_maxWidth!=0 && nextCharacterLocationX > a_maxWidth);
               if(*renderChar == '\n' || lineWidthExceeded){
                  characterLocationX = 0;
                  nextCharacterLocationX = (!lineWidthExceeded)?0:(*characterList)[*renderChar].getCharacterSize().x;
                  characterLocationY+=lineHeight;
                  offset = 0;
                  lineHeight = TTF_FontLineSkip(specifiedFont->second.font);
                  baseLine = TTF_FontAscent(specifiedFont->second.font);
                  if(lineWidthExceeded && a_wrapMethod == SOFT){
                     auto lastSpace = std::find(currentLineContent.rbegin(), currentLineContent.rend(), UTF_CHAR_STR(' '));
                     size_t distance = std::distance(currentLineContent.rbegin(), lastSpace);
                     if(distance != currentLineContent.size()){
                        previousLine = characterCount - distance;
                        for(;distance > 0;--distance){
                           auto renderedCharacter = textScene->get(boost::lexical_cast<std::string>(characterCount-distance));
                           renderedCharacter->placeAt(Point(characterLocationX, renderedCharacter->getLocation().y+lineHeight));
                           characterLocationX+=currentLineCharacterSizes[currentLineContent.size()-distance];
                        }
                        nextCharacterLocationX+=characterLocationX;
                     }else{
                        previousLine = characterCount;
                     }
                  }else{
                     previousLine = characterCount;
                  }
                  
                  currentLineContent.clear();
                  currentLineCharacterSizes.clear();
               }
               if(*renderChar != '\n'){
                  auto character = std::make_shared<M2Rend::DrawRectangle>(render);
                  character->setTwoCorners(M2Rend::Point(), M2Rend::Point((*characterList)[*renderChar].getTextureSize().x, (*characterList)[*renderChar].getTextureSize().y, 0));
                  character->translate(M2Rend::Point(characterLocationX, characterLocationY + offset));
                  AssignTextureToRectangle(*character, (*characterList)[*renderChar].getTextureId());
                  character->setColor(currentColor);
                  textScene->add(character, boost::lexical_cast<std::string>(characterCount));
                  characterLocationX = nextCharacterLocationX;
                  currentLineContent.push_back(*renderChar);
                  currentLineCharacterSizes.push_back((*characterList)[*renderChar].getCharacterSize().x);
                  characterCount++;
               }
            }
         }else{
            std::cerr << "A text scene was requested for a font identifier which is not loaded! (" << current->fontIdentifier << ")" << std::endl;
         }
      }
      return textScene;
   }

   //SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.
   void TextLibrary::reloadTextures(){
      std::for_each(cachedGlyphs.begin(), cachedGlyphs.end(), [&](std::pair<const std::string, CachedGlyphs> &glyphList){
         TTF_Font *currentFont = loadedFonts[glyphList.first].font;
         std::for_each(glyphList.second.begin(), glyphList.second.end(), [&](std::pair<const M2Util::UtfChar, TextCharacter> &glyph){
            if(glyph.second.isSet()){
               Uint16 text[] = {glyph.first, '\0'};
               glyph.second.assignSDLSurface(TTF_RenderUNICODE_Blended(currentFont, text, white), glyph.first);
            }
         });
      });
   }

   TextLibrary::CachedGlyphs* TextLibrary::initGlyphs( const std::string &a_identifier, const M2Util::UtfString &a_text ){
      CachedGlyphs *characterList = &(cachedGlyphs[a_identifier]);
      loadIndividualGlyphs(a_identifier, a_text, *characterList);
      return characterList;
   }

   //SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.
   void TextLibrary::loadIndividualGlyphs( const std::string &a_identifier, const M2Util::UtfString &a_text, CachedGlyphs &a_characterList ){
      TTF_Font* fontFace = loadedFonts[a_identifier].font;
      std::for_each(a_text.begin(), a_text.end(), [&](M2Util::UtfChar renderChar){
         if(!a_characterList[renderChar].isSet()){
            Uint16 text[] = {renderChar, '\0'};
            a_characterList[renderChar].assignSDLSurface(TTF_RenderUNICODE_Blended(fontFace, text, white), renderChar);
         }
      });
   }

   TextBox::TextBox(TextureManager *a_textures, TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, int a_width, int a_height)
      :textLibrary(a_textLibrary),render(a_textLibrary->getRenderer()), textures(a_textures),fontIdentifier(a_fontIdentifier),
      textTextureScene(a_textLibrary->getRenderer()), textBoxFullScene(std::make_shared<Scene>(a_textLibrary->getRenderer())),
      width(a_width), height(a_height), initialized(false)
   {}

   TextBox::TextBox( TextureManager *a_textures, TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const M2Util::UtfString &a_text, int a_width, int a_height ) 
      :textLibrary(a_textLibrary),render(a_textLibrary->getRenderer()), textures(a_textures),fontIdentifier(a_fontIdentifier),
      textTextureScene(a_textLibrary->getRenderer()), textBoxFullScene(std::make_shared<Scene>(a_textLibrary->getRenderer())),
      width(a_width), height(a_height), initialized(false)
   {
      initialize();
   }

   void TextBox::initialize(){
      if(!initialized){
         initialized = true;
         textureHandle = M2Util::getNewStringId();

         initializeTextWindowTexture();
         initializeTextBoxFullScene();

         render->createFramebuffer(textWindowFramebuffer);
         setText(UTF_CHAR_STR(""), fontIdentifier);
      }
   }

   void TextBox::setText( const M2Util::UtfString &a_text, const std::string &a_fontIdentifier /*= ""*/ ){
      M2Util::require(initialized, M2Util::DefaultException("TextBox::setText called before initialization."));
      if(a_fontIdentifier != ""){fontIdentifier = a_fontIdentifier;}
      text = a_text;
      textTextureScene.add(textLibrary->composeScene(parseTextStateList(fontIdentifier, text), width), "Text");
      renderTextWindowTexture();
   }


   bool TextBox::setText( SDL_Event &event ){
      M2Util::require(initialized, M2Util::DefaultException("TextBox::setText called before initialization."));
      if(event.type == SDL_KEYDOWN){
         switch(event.key.keysym.sym){
         case SDLK_TAB:
            break;
         case SDLK_RETURN:
            return true;
            break;
         case SDLK_BACKSPACE:
            backspace();
            break;
         default:
            if(event.key.keysym.unicode != 0){
               appendText(event.key.keysym.unicode);
            }
            break;
         }
      }
      return false;
   }

   void TextBox::setTextBoxSize( int a_width, int a_height ){
      M2Util::require(initialized, M2Util::DefaultException("TextBox::setTextBoxSize called before initialization."));
      width = a_width;
      height = a_height;

      auto textBoxWindow = textBoxFullScene->get<DrawRectangle>("TextBoxWindow");
      textBoxWindow->setSizeAndLocation(textBoxWindow->getLocation(), width, height);

      textures->deleteMainTexture(textureHandle);
      initializeTextWindowTexture();
      textTextureScene.add(textLibrary->composeScene(parseTextStateList(fontIdentifier, text), width), "Text");
      renderTextWindowTexture();
   }

   void TextBox::initializeTextWindowTexture(){
      textWindowTexture = textures->createEmptyTexture(textureHandle, width, height, MEMBER_FUNCTION_POINTER(TextBox::reloadTexture, this));
      textWindowFramebuffer.texture = &textWindowTexture->texture;
      textWindowFramebuffer.width = width;
      textWindowFramebuffer.height = height;
   }

   void TextBox::renderTextWindowTexture(){
      render->startUsingFramebuffer(textWindowFramebuffer);
      render->clearScreen();
      textTextureScene.draw();
      render->stopUsingFramebuffer();
   }

   void TextBox::initializeTextBoxFullScene(){
      auto textBoxWindow = std::make_shared<M2Rend::DrawRectangle>();
      textBoxWindow->setSizeAndLocation(M2Rend::Point(), width, height);
      M2Rend::AssignTextureToRectangle(*textBoxWindow, textWindowTexture->getSubTexture("FullSize"));
      textBoxFullScene->add(textBoxWindow, "TextBoxWindow");
   }
}
