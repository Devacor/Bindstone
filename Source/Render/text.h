#ifndef _MV_TEXT_H_
#define _MV_TEXT_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "Utility/package.h"
#include "Render/render.h"
#include "Render/Scene/package.h"
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
		std::string file;
		int size;

		void set(const std::string &a_file, int a_size, TTF_Font* a_font){
			file = a_file;
			size = a_size;
			font = a_font;
		}

		FontDefinition(FontDefinition &&a_other){
			font = a_other.font;
			a_other.font = nullptr;
		}

		FontDefinition& operator=(FontDefinition &&a_other){
			font = a_other.font;
			a_other.font = nullptr;
			return *this;
		}

	private:
		template <class Archive>
		void save(Archive & archive){
			archive(
				CEREAL_NVP(file),
				CEREAL_NVP(size)
			);
		}

		template <class Archive>
		void load(Archive & archive){
			archive(
				CEREAL_NVP(file),
				CEREAL_NVP(size)
			);
			font = TTF_OpenFont(file.c_str(), size);
		}

		FontDefinition(const FontDefinition &a_other) = delete;
		FontDefinition& operator=(const FontDefinition &a_other) = delete;
	};

	enum TextWrapMethod{
		NONE,
		HARD,
		SOFT
	};

	enum TextJustification{
		LEFT,
		CENTER,
		RIGHT
	};

	class TextLibrary{
	public:
		TextLibrary(Draw2D *a_rendering);
		~TextLibrary(){}

		bool loadFont(const std::string &a_identifier, int a_pointSize, std::string a_fontFileLocation);

		std::shared_ptr<Scene::Node> composeScene(const std::vector<TextState> &a_textStateList, double a_maxWidth = 0, TextWrapMethod a_wrapMethod = SOFT, TextJustification a_justify = LEFT);

		//Call this when the OpenGL context is destroyed to re-load the textures.
		void reloadTextures();

		Draw2D *getRenderer(){return render;}
	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(
				CEREAL_NVP(loadedFonts)
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextLibrary> &construct){
			TextLibrary *renderer = nullptr;
			archive.extract(
				cereal::make_nvp("renderer", renderer)
			);
			construct(renderer);
			archive(
				cereal::make_nvp("loadedFonts", construct()->loadedFonts)
			);
		}

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
		friend cereal::access;
	public:
		TextBox(TextLibrary *a_textLibrary, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, const Size<> &a_size);

		void justification(TextJustification a_newJustification){
			if(a_newJustification != textJustification){
				textJustification = a_newJustification;
				refreshTextBoxContents();
			}
		}
		TextJustification justification() const{
			return textJustification;
		}

		void wrapping(TextWrapMethod a_newWrapMethod){
			if(a_newWrapMethod != wrapMethod){
				wrapMethod = a_newWrapMethod;
				refreshTextBoxContents();
			}
		}
		TextWrapMethod wrapping() const{
			return wrapMethod;
		}

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

		Size<> getContentSize();

		std::shared_ptr<Scene::Node> scene();

		void draw();
	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(
				CEREAL_NVP(text),
				CEREAL_NVP(fontIdentifier),
				CEREAL_NVP(boxSize),
				CEREAL_NVP(contentScrollPosition),
				CEREAL_NVP(wrapMethod),
				CEREAL_NVP(justification)
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextBox> &construct){
			TextLibrary *textLibrary = nullptr;
			Size<> boxSize;
			archive(
				cereal::make_nvp("boxSize", boxSize)
			).extract(
				cereal::make_nvp("textLibrary", textLibrary)
			);
			construct(textLibrary, boxSize);
			archive(
				cereal::make_nvp("text", construct()->text),
				cereal::make_nvp("fontIdentifier", construct()->fontIdentifier),
				cereal::make_nvp("contentScrollPosition", construct()->contentScrollPosition),
				cereal::make_nvp("wrapMethod", construct()->wrapMethod),
				cereal::make_nvp("textJustification", construct()->textJustification)
			);
		}

		void initialize(const UtfString &a_text);
		void refreshTextBoxContents();

		Size<> boxSize;
		Point<> contentScrollPosition;
		bool firstRun;
		std::shared_ptr<Scene::Node> textScene;
		std::shared_ptr<Scene::Clipped> textboxScene;
		TextLibrary *textLibrary;
		Draw2D *render;
		
		TextJustification textJustification = LEFT;
		TextWrapMethod wrapMethod = SOFT;

		UtfString text;
		std::string fontIdentifier;
	};
}

#endif
