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

	Color parseColorString(const std::string &a_colorString);

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
		friend cereal::access;
	public:
		static std::shared_ptr<FontDefinition> make(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font){
			return std::shared_ptr<FontDefinition>(new FontDefinition(a_library, a_file, a_size, a_font));
		}
		~FontDefinition(){
			if(font != nullptr){
				TTF_CloseFont(font);
			}
		}

		std::shared_ptr<TextCharacter> getCharacter(UtfChar renderChar);

		double height() const{
			return lineHeight;
		}
		double base() const{
			return baseLine;
		}
		TextLibrary* library() const{
			return textLibrary;
		}
	private:
		std::string file;
		TTF_Font* font;
		int size;
		double lineHeight;
		double baseLine;
		TextLibrary *textLibrary;

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
			//TODO! Get textLibrary.
			font = TTF_OpenFont(file.c_str(), size);
			lineHeight = TTF_FontLineSkip(font);
			baseLine = TTF_FontAscent(font);
		}

		FontDefinition(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font):
			file(a_file),
			size(a_size),
			font(a_font),
			textLibrary(a_library),
			lineHeight(TTF_FontLineSkip(a_font)),
			baseLine(TTF_FontAscent(a_font)){
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
		FormattedState(const std::shared_ptr<FontDefinition> &a_font, const std::shared_ptr<FormattedState> &a_currentState = nullptr);
		FormattedState(const Color &a_color, const std::shared_ptr<FormattedState> &a_currentState);
		FormattedState(double a_minimumLineHeight, const std::shared_ptr<FormattedState> &a_currentState);

		Color color;
		std::shared_ptr<FontDefinition> font;
		double minimumLineHeight;
	};

	class FormattedLine;
	struct FormattedCharacter{
		static std::shared_ptr<FormattedCharacter> make(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state){
			return std::shared_ptr<FormattedCharacter>(new FormattedCharacter(parent, a_character, a_state));
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

		void offsetForLineHeight(double a_lineHeight) const{
			//TODO!
		}

		Point<> offset() const{
			return offsetPosition;
		}

		Point<> offset(const Point<> &a_newPosition){
			offsetPosition = a_newPosition;
			shape->position(basePosition + offsetPosition);
			return offsetPosition;
		}

		Point<> offset(double lineHeight, double baseLine){
			double height = character->font()->height();
			double base = character->font()->base();
			offset({offsetPosition.x, (baseLine - base) + ((lineHeight - height) / 2.0)});
			shape->position(basePosition + offsetPosition);
			return offsetPosition;
		}

		void applyState(const std::shared_ptr<FormattedState> &a_state){
			state = a_state;
			character = state->font->getCharacter(textCharacter);
			shape->setSize(character->characterSize());
			shape->texture(character->texture());
			shape->color(state->color);
		}

		bool partOfFormat = false;
		UtfChar textCharacter;
		std::shared_ptr<TextCharacter> character;
		std::shared_ptr<Scene::Rectangle> shape;
		std::shared_ptr<FormattedLine> line;
		std::shared_ptr<FormattedState> state;

	private:
		Point<> basePosition;
		Point<> offsetPosition;
		FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state):
			textCharacter(a_character),
			state(a_state),
			character(a_state->font->getCharacter(a_character)){

			shape = parent->make<Scene::Rectangle>(character->characterSize());
			shape->setSize(character->characterSize());
			shape->texture(character->texture());
			shape->color(state->color);
		}
	};

	class FormattedText;
	struct FormattedCharacter;
	class FormattedLine : public std::enable_shared_from_this<FormattedLine> {
	public:
		static std::shared_ptr<FormattedLine> make(FormattedText &a_text, size_t a_lineIndex);

		void removeCharacters(size_t a_characterIndex, size_t a_totalToRemove);
		void addCharacters(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		std::shared_ptr<FormattedCharacter>& operator[](size_t a_index);
		size_t size() const{
			return characters.size();
		}
		bool empty() const{
			return characters.empty();
		}
		double height() const{
			return lineHeight;
		}

		size_t index() const{
			return lineIndex;
		}
	private:
		std::string makeCharacterGUID(size_t a_index){
			return guid(wideToChar(characters[a_index]->character->character()));
		}

		FormattedLine(FormattedText &a_lines, size_t a_lineIndex);

		void updateLineHeight();
		void ripplePositionUpdate();
		void updateFormatAfterAdd(size_t a_startIndex, size_t a_endIndex);
		
		std::shared_ptr<FormattedState> getFontState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getColorState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getHeightState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);

		std::shared_ptr<FormattedState> getNewState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_current);

		void fixVisualsFromIndex(size_t a_characterIndex);

		double lineHeight;
		double baseLine;

		size_t lineIndex;

		double linePosition;
		std::vector<std::shared_ptr<FormattedCharacter>> characters;
		FormattedText& text;
	};

	std::shared_ptr<FormattedState> getTextState(const UtfString &a_text, size_t a_i, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState, std::pair<size_t, size_t> &o_range);

	class FormattedText{
	public:
		FormattedText(TextLibrary &a_library, double a_width, const std::string &a_defaultStateIdentifier, TextWrapMethod a_wrapping = SOFT):
			library(a_library),
			textWidth(a_width),
			defaultState(std::make_shared<FormattedState>(a_library.fontDefinition(a_defaultStateIdentifier))),
			wrapping(a_wrapping){

			scene = Scene::Node::make(library.getRenderer());
		}

		double width(double a_width){
			textWidth = a_width;
			return textWidth;
		}

		double width() const{
			return textWidth;
		}

		bool exceedsWidth(double a_xPosition) const{
			return a_xPosition > textWidth;
		}

		double positionForLine(size_t a_index){
			return std::accumulate(lines.begin(), lines.end(), 0.0, [](double a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
				return a_line->height() + a_accumulated;
			});
		}

		std::shared_ptr<FormattedState> getDefaultState() const{
			return defaultState;
		}

		std::shared_ptr<FormattedLine>& operator[](size_t a_index);

		size_t size() const{
			return lines.size();
		}
		bool empty() const{
			return lines.empty();
		}

		size_t textLength() const{
			return std::accumulate(lines.begin(), lines.end(), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
				return a_line->size() + a_accumulated;
			});
		}

		std::shared_ptr<FormattedCharacter> characterRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex){
			auto index = absoluteIndex(a_lineIndex, a_characterIndex);
			if(a_relativeCharacterIndex + static_cast<int64_t>(index) > 0){
				return characterForIndex(index + a_relativeCharacterIndex);
			}else{
				return nullptr;
			}
		}

		void applyState(const std::shared_ptr<FormattedState> &a_newState, size_t a_newFormatStart, size_t a_newFormatEnd){
			std::shared_ptr<FormattedCharacter> character = characterForIndex(a_newFormatStart);
			auto originalState = character->state;
			for(size_t i = a_newFormatStart + 1;character && character->state == originalState;++i){
				if(i <= a_newFormatEnd){
					character->partOfFormat = true;
				}
				character->state = a_newState;
				character = characterForIndex(i);
			}
		}

		size_t absoluteIndexFromRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex){
			auto index = absoluteIndex(a_lineIndex, a_characterIndex);
			if(a_relativeCharacterIndex + static_cast<int64_t>(index) > 0){
				return index + a_relativeCharacterIndex;
			}else{
				return 0;
			}
		}

		size_t absoluteIndex(size_t a_lineIndex, size_t a_characterIndex) const{
			auto accumulatedIndex = std::accumulate(lines.begin(), lines.begin() + std::max<int64_t>(0, static_cast<int64_t>(a_lineIndex)-1), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
				return a_line->size() + a_accumulated;
			});
			return accumulatedIndex + a_characterIndex;
		}

		std::shared_ptr<FormattedCharacter> characterForIndex(size_t a_characterIndex){
			if(lines.empty()){
				return nullptr;
			}

			std::shared_ptr<FormattedLine> line;
			size_t characterInLineIndex;
			std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_characterIndex);
			if(line->empty()){
				return nullptr;
			}else{
				return (*line)[characterInLineIndex-1];
			}
		}
		
		std::tuple<std::shared_ptr<FormattedLine>, size_t> lineForCharacterIndex(size_t a_characterIndex){
			std::shared_ptr<FormattedLine> found;
			size_t foundIndex = 0;
			int64_t characterIndex = a_characterIndex;
			for(size_t foundIndex = 0; foundIndex < lines.size() && characterIndex - lines[foundIndex]->size() > 0; ++foundIndex){
				characterIndex -= lines[foundIndex]->size();
			}
			if(foundIndex < lines.size()){
				return std::make_tuple(lines[foundIndex], std::min<size_t>(characterIndex, lines[foundIndex]->size()));
			}else{
				auto newLine = FormattedLine::make(*this, lines.size());
				lines.push_back(newLine);
				return std::make_tuple(newLine, 0);
			}
		}

		void addCharacters(size_t a_startIndex, const UtfString &a_characters){
			if(a_characters.empty()){
				return;
			}
			std::shared_ptr<FormattedLine> line;
			size_t characterInLineIndex;
			std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);

			std::shared_ptr<FormattedState> foundState = defaultState;
			if(a_startIndex > 0){
				if(characterInLineIndex > 0){
					foundState = (*line)[characterInLineIndex - 1]->state;
				} else if(line->index() > 0 && !lines[line->index() -1]->empty()){
					auto previousLine = lines[line->index() - 1];
					foundState = (*previousLine)[previousLine->size() - 1]->state;
				}
			}

			std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
			for(const UtfChar &character : a_characters){
				formattedCharacters.push_back(FormattedCharacter::make(scene, character, foundState));
			}

			line->addCharacters(characterInLineIndex, formattedCharacters);
		}

		void addCharacters(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters){
			if(a_characters.empty()){
				return;
			}
			std::shared_ptr<FormattedLine> line;
			size_t characterInLineIndex;
			std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);
			line->addCharacters(a_startIndex, a_characters);
		}

		void append(const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters){
			if(a_characters.empty()){
				return;
			}
			if(lines.empty()){
				lines.push_back(FormattedLine::make(*this, lines.size()));
			}
			std::shared_ptr<FormattedLine> line = lines.back();
			line->addCharacters(line->size(), a_characters);
		}

		void append(const UtfString &a_characters){
			if(a_characters.empty()){
				return;
			}
			std::shared_ptr<FormattedState> foundState = defaultState;
			if(lines.empty()){
				lines.push_back(FormattedLine::make(*this, lines.size()));
			} else{
				if(!lines.back()->empty()){
					foundState = (*lines.back())[lines.back()->size() - 1]->state;
				} else if(lines.size() > 1 && !lines[lines.size() - 1]->empty()){
					auto previousLine = lines[lines.size() - 1];
					foundState = (*previousLine)[previousLine->size()-1]->state;
				}
			}

			std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
			for(const UtfChar &character : a_characters){
				formattedCharacters.push_back(FormattedCharacter::make(scene, character, foundState));
			}

			std::shared_ptr<FormattedLine> line = lines.back();
			line->addCharacters(line->size(), formattedCharacters);
		}

		double minimumLineHeight() const{
			return minimumTextLineHeight;
		}

		double minimumLineHeight(double a_minimumLineHeight){
			minimumTextLineHeight = a_minimumLineHeight;
			return minimumTextLineHeight;
		}

		TextLibrary &library;
		TextWrapMethod wrapping;
		TextJustification justification;
		std::vector<std::shared_ptr<FormattedLine>> lines;
		std::shared_ptr<FormattedState> defaultState;
		std::shared_ptr<Scene::Node> scene;
		UtfString raw;
		double textWidth;
		double minimumTextLineHeight;
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

		FormattedText formattedText;
		UtfString text;
		size_t cursor;
		UtfString editText;
		std::string fontIdentifier;
		int minimumLineHeight;
		bool isSingleLine;
	};
}

#endif
