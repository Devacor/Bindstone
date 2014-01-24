#ifndef __TEXT_H__
#define __TEXT_H__

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Utility/package.h"
#include "Render/render.h"
#include "Render/scene.h"
#include "Render/compositeScene.h"
#include "SDL_ttf.h"

namespace MV {
	//WARNING:
	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size (not larger) unless that changes.

	struct TextState{
		TextState(const UtfString &a_text, const Color &a_color, const std::string &a_fontIdentifier)
			:text(a_text),color(a_color),fontIdentifier(a_fontIdentifier){}
		UtfString text;
		Color color;
		std::string fontIdentifier;
	};

	Color parseColorString(std::string a_colorString);

	std::vector<TextState> parseTextStateList(std::string a_defaultFontIdentifier, UtfString a_text);

	class TextCharacter {
	public:
		TextCharacter():glyphCharacter(' '){}
		~TextCharacter(){}

		void setCharacter(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter);

		bool isSet() const;

		UtfChar character() const;
		std::shared_ptr<TextureHandle> texture() const;
		Size<int> characterSize() const;
		Size<int> textureSize() const;
	private:
		UtfChar glyphCharacter;
		std::shared_ptr<SurfaceTextureDefinition> glyphTexture;
		std::shared_ptr<TextureHandle> glyphHandle;
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
		FontDefinition(FontDefinition &&a_other){
			font = a_other.font;
			a_other.font = nullptr;
		}

		FontDefinition& operator=(FontDefinition &&a_other){
			font = a_other.font;
			a_other.font = nullptr;
			return *this;
		}

		FontDefinition(const FontDefinition &a_other) = delete;
		FontDefinition& operator=(const FontDefinition &a_other) = delete;
	};

	enum TextWrapMethod{
		NONE,
		HARD,
		SOFT
	};

	class TextLibrary{
	public:
		TextLibrary(Draw2D *a_rendering);
		~TextLibrary(){}

		bool loadFont(const std::string &a_identifier, int a_pointSize, std::string a_fontFileLocation);

		std::shared_ptr<Scene::Node> composeScene(const std::vector<TextState> &a_textStateList, double a_maxWidth = 0, TextWrapMethod a_wrapMethod = SOFT);

		//Call this when the OpenGL context is destroyed to re-load the textures.
		void reloadTextures();

		Draw2D *getRenderer(){return render;}
	private:
		typedef std::map<UtfChar, TextCharacter> CachedGlyphs;
		typedef std::map<std::string, CachedGlyphs> CachedGlyphList;
		CachedGlyphs* initGlyphs(const std::string &a_identifier, const UtfString &a_text);
		void loadIndividualGlyphs(const std::string &a_identifier, const UtfString &a_text, CachedGlyphs &a_characterList);

		std::map<std::string, FontDefinition> loadedFonts;
		CachedGlyphList cachedGlyphs;
		SDL_Color white;
		Draw2D *render;
	};

	class TextBox{
	public:
		TextBox(TextLibrary *a_textLibrary, Size<> a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, Size<> a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, Size<> a_size);

		void setText(const UtfString &a_text, const std::string &a_fontIdentifier = "");
		void setText(UtfChar a_char, const std::string &a_fontIdentifier = ""){
			setText(UtfString()+a_char, a_fontIdentifier);
		}
		void setText(Uint16 a_char, const std::string &a_fontIdentifier = ""){
			UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
			setText(*character, a_fontIdentifier);
		}
		//any further development to input handling should probably be broken out elsewhere.
		bool setText(SDL_Event &event);

		void appendText(const UtfString &a_text){
			setText(text + a_text);
		}
		void appendText(const UtfChar a_char){
			setText(text+a_char);
		}
		void appendTextUint(Uint16 a_char){
			UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
			appendText(*character);
		}

		void prependText(const UtfString &a_text){
			setText(a_text + text);
		}
		void prependText(UtfChar a_char){
			setText(a_char+text);
		}
		void prependText(Uint16 a_char){
			UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
			prependText(*character);
		}

		void backspace(){
			setText(text.substr(0, text.size()-1));
		}

		UtfString getText(){return text;}

		void setTextBoxSize(Size<> a_size);

		void setScrollPosition(Point<> a_position, bool a_overScroll = false);
		void translateScrollPosition(Point<> a_position, bool a_overScroll = false){
			setScrollPosition((contentScrollPosition + a_position), a_overScroll);
		}

		Point<> getScrollPosition() const{
			return contentScrollPosition;
		}

		Size<> getContentSize(){
			return textScene->getLocalAABB().getSize();
		}

		std::shared_ptr<Scene::Node> scene(){
			return textboxScene;
		}

		void draw(){
			textboxScene->draw();
		}
	private:
		void initialize(const UtfString &a_text);
		void refreshTextBoxContents();

		Size<> boxSize;
		Point<> contentScrollPosition;
		bool firstRun;
		std::shared_ptr<Scene::Node> textScene;
		std::shared_ptr<Scene::Clipped> textboxScene;
		TextLibrary *textLibrary;
		Draw2D *render;
		
		UtfString text;
		std::string fontIdentifier;
	};
}

#endif
