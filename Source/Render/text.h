#ifndef _MV_TEXT_H_
#define _MV_TEXT_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
#include "Utility/package.h"
#include "Render/render.h"
#include "Render/Scene/package.h"
#include "SDL_ttf.h"

namespace MV {
	//WARNING:
	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size (not larger) unless that changes.
	struct TextState{
		TextState(const UtfString &a_text, const Color &a_color, const std::string &a_fontIdentifier, int a_lineHeight = -1)
			:text(a_text),color(a_color),fontIdentifier(a_fontIdentifier), lineHeight(a_lineHeight){}
		UtfString text;
		Color color;
		std::string fontIdentifier;
		int lineHeight;
	};

	extern const UtfString COLOR_IDENTIFIER;
	extern const UtfString FONT_IDENTIFIER;
	extern const UtfString HEIGHT_IDENTIFIER;

	Color parseColorString(const UtfString &a_colorString);

	std::vector<TextState> parseTextStateList(std::string a_defaultFontIdentifier, UtfString a_text);

	class FontDefinition;
	class TextCharacter {
	public:
		static std::shared_ptr<TextCharacter> make(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);

		UtfChar character() const;
		std::shared_ptr<TextureHandle> texture() const;
		Size<int> characterSize() const;
		Size<int> textureSize() const;

		bool isSoftBreakCharacter();

		std::shared_ptr<FontDefinition> font() const;
	private:
		TextCharacter(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);

		UtfChar glyphCharacter;
		std::shared_ptr<SurfaceTextureDefinition> glyphTexture;
		std::shared_ptr<TextureHandle> glyphHandle;
		std::shared_ptr<FontDefinition> fontDefinition;
	};

	class TextLibrary;
	class FontDefinition : public std::enable_shared_from_this<FontDefinition>{
	public:
		static std::shared_ptr<FontDefinition> make(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font){
			return std::shared_ptr<FontDefinition>(new FontDefinition(a_library, a_file, a_size, a_font));
		}
		~FontDefinition(){
			if(font != nullptr){
				TTF_CloseFont(font);
			}
		}

		TTF_Font* font;
		std::string file;
		int size;

		TextLibrary *library;

		std::shared_ptr<TextCharacter> getCharacter(UtfChar renderChar);

	private:
		typedef std::map<UtfChar, std::shared_ptr<TextCharacter>> CachedGlyphs;
		CachedGlyphs cachedGlyphs;

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

		FontDefinition(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font):
			file(a_file),
			size(a_size),
			font(a_font),
			library(a_library){
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

		Draw2D *getRenderer(){return render;}

		std::shared_ptr<FontDefinition> fontDefinition(const std::string &a_identifier) const;
	private:
		template <class Archive>
		void serialize(Archive & archive){
			archive(
				CEREAL_NVP(loadedFonts)
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextLibrary> &construct){
			Draw2D *renderer = nullptr;
			archive.extract(
				cereal::make_nvp("renderer", renderer)
			);
			construct(renderer);
			archive(
				cereal::make_nvp("loadedFonts", construct()->loadedFonts)
			);
		}

		std::map<std::string, std::shared_ptr<FontDefinition>> loadedFonts;
		SDL_Color white;
		Draw2D *render;
	};

	struct FormattedState {
		FormattedState();
		FormattedState(const std::shared_ptr<FontDefinition> &a_font, const std::shared_ptr<FormattedState> &a_currentState);
		FormattedState(const Color &a_color, const std::shared_ptr<FormattedState> &a_currentState);
		FormattedState(int a_minimumLineHeight, const std::shared_ptr<FormattedState> &a_currentState);

		Color color;
		std::shared_ptr<FontDefinition> font;
		int minimumLineHeight;
	};

	struct FormattedCharacter;
	struct FormattedLine : public std::enable_shared_from_this<FormattedLine> {
		FormattedLine(const std::shared_ptr<FormattedCharacter> &a_firstCharacter, size_t a_characterIndex, size_t a_lineIndex);

		void removeCharacter(size_t a_charcterIndex);
		void addCharacter(size_t a_charIndex, const std::shared_ptr<FormattedCharacter> &a_newCharacter);
		int lineHeight;

		size_t firstCharacterIndex;
		size_t lastCharacterIndex;
		size_t lineIndex;

		std::map<size_t, int> characterHeights;
	};

	struct FormattedCharacter{
		static std::shared_ptr<FormattedCharacter> make(const std::shared_ptr<TextCharacter> &a_character, const std::shared_ptr<FormattedState> &a_state){
			return std::shared_ptr<FormattedCharacter>(new FormattedCharacter(a_character, a_state));
		}

		Size<> characterSize() const{
			if(partOfFormat){
				return{0.0, static_cast<double>(character->characterSize().height)};
			}else{
				return character->characterSize();
			}
		}

		Point<> position() const{
			return basePosition;
		}

		Point<> position(const Point<> &a_newPosition){
			basePosition = a_newPosition;
			shape->position(basePosition + offsetPosition);
			return basePosition;
		}

		Point<> offset() const{
			return offsetPosition;
		}

		Point<> offset(const Point<> &a_newPosition){
			offsetPosition = a_newPosition;
			shape->position(basePosition + offsetPosition);
			return offsetPosition;
		}

		bool partOfFormat = false;
		std::shared_ptr<TextCharacter> character;
		std::shared_ptr<Scene::Rectangle> shape;
		std::shared_ptr<FormattedLine> line;
		std::shared_ptr<FormattedState> state;

	private:
		Point<> basePosition;
		Point<> offsetPosition;
		FormattedCharacter(const std::shared_ptr<TextCharacter> &a_character, const std::shared_ptr<FormattedState> &a_state):
			character(a_character),
			state(a_state){
		}
	};

	struct FormattedText{
		FormattedText(TextLibrary &a_library):
		library(a_library){
		}

		void append(const UtfString &a_text){
			raw += a_text;
			auto fixStart = characters.size();
			std::shared_ptr<FormattedState> initState = !characters.empty() ? characters.back()->state : defaultState;
			for(auto character : a_text){
				characters.push_back(FormattedCharacter::make(initState->font->getCharacter(character), initState));
			}
			auto fixEnd = characters.size();
			fix(fixStart, fixEnd);
		}

		Point<> getPositionForCharacter(size_t a_index) const{
			if(a_index > 0){
				double x = characters[a_index - 1]->position().x + characters[a_index - 1]->characterSize().width;
				double y = characters[a_index - 1]->position().y;
				if(characters[a_index - 1]->line != characters[a_index]->line || characters[a_index]->character->character() == UTF_CHAR_STR('\n')){
					x = 0.0;
					y += characters[a_index - 1]->line->lineHeight;
				}
				return point(x, y);
			} else if(!characters.empty()){
				return point(0.0, (characters[a_index]->character->character() == UTF_CHAR_STR('\n')) ?
					static_cast<double>(characters[a_index]->line->lineHeight) :
					0.0);
			} else{
				return point(0.0, 0.0);
			}
		}

		void positionCharacter(size_t a_index){
			auto position = getPositionForCharacter(a_index);
			if(position.x > width && wrapping != NONE){
				if(characters[a_index]->line->lineIndex == lines.size() - 1){
					lines.push_back(std::make_shared<FormattedLine>(characters[a_index], a_index, lines.size()));
					lines.back()->lineHeight = std::max(defaultState->minimumLineHeight, static_cast<int>(characters[a_index]->characterSize().height));
				}
				bool hardWrap = wrapping == HARD;
				if(wrapping == SOFT){
					int toBreakIndex = a_index;
					for(; toBreakIndex > characters[toBreakIndex]->line->firstCharacterIndex && !characters[toBreakIndex]->character->isSoftBreakCharacter(); ++toBreakIndex){}
					hardWrap = !bumpCharactersToNextLine(toBreakIndex, a_index);
				}
				if(hardWrap && a_index != characters[a_index]->line->firstCharacterIndex){
					position.x = 0.0;
					position.y += characters[a_index]->line->lineHeight;
					characters[a_index]->line = lines[characters[a_index]->line->lineIndex + 1];
				}
			} else{
				characters[a_index]->position(position);
			}
		}

		bool bumpCharactersToNextLine(size_t a_beginIndex, size_t a_endIndex){
			if(a_beginIndex != a_endIndex && a_beginIndex != characters[a_beginIndex]->line->firstCharacterIndex){
				characters[a_beginIndex]->line = lines[characters[a_endIndex]->line->lineIndex + 1];
				characters[a_beginIndex]->position({0.0, characters[a_beginIndex]->position().y + characters[a_index]->line->lineHeight});
				for(int i = a_beginIndex + 1; i <= a_endIndex; ++i){
					characters[i]->position(getPositionForCharacter(i));
				}
				return true;
			}
			return false;
		}

		void fix(size_t start, size_t end){
			for(size_t i = start; i < end; ++i){
				std::pair<size_t, size_t> found(0, 0);
				auto state = getTextState(raw, i, characters[i]->state, defaultState, found);
				auto originalState = characters[i]->state;
				if(found.first != 0 || found.second != 0){
					for(size_t j = found.first; characters[j]->state == originalState; ++j){
						if(j <= found.second){
							characters[j]->partOfFormat = true;
						}
						characters[j]->state = state;
						//double positionX = getXForCharacter(j);
						//TODO!
						//characters[j]->shape->position({, characters[j]->shape->position().y});
					}
				}
			}
		}


		TextLibrary &library;
		TextWrapMethod wrapping;
		TextJustification justification;
		std::vector<std::shared_ptr<FormattedLine>> lines;
		std::vector<std::shared_ptr<FormattedCharacter>> characters;
		std::shared_ptr<FormattedState> defaultState;
		UtfString raw;
		double width;
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

		UtfString currentTextContents(){
			if(!editText.empty()){

			} else{
				return text;
			}
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

		void setTemporaryText(const UtfString &a_text, size_t a_cursorStart, size_t a_cursorEnd){
			editText = a_text;
		}

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
		int getMinimumLineHeight() const{
			return minimumLineHeight;
		}
		void setMinimumLineHeight(int a_newLineHeight){
			minimumLineHeight = a_newLineHeight;
			refreshTextBoxContents();
		}

		void makeSingleLine(){
			isSingleLine = true;
		}
		void makeManyLine(){
			isSingleLine = false;
		}

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
		size_t cursor;
		UtfString editText;
		std::string fontIdentifier;
		int minimumLineHeight;
		bool isSingleLine;
	};
}

#endif
