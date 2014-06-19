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

		PointPrecision height() const{
			return lineHeight;
		}
		PointPrecision base() const{
			return baseLine;
		}
		TextLibrary* library() const{
			return textLibrary;
		}
	private:
		std::string file;
		TTF_Font* font;
		int size;
		PointPrecision lineHeight;
		PointPrecision baseLine;
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
			lineHeight = static_cast<PointPrecision>(TTF_FontLineSkip(font));
			baseLine = static_cast<PointPrecision>(TTF_FontAscent(font));
		}

		FontDefinition(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font):
			file(a_file),
			size(a_size),
			font(a_font),
			textLibrary(a_library),
			lineHeight(static_cast<PointPrecision>(TTF_FontLineSkip(a_font))),
			baseLine(static_cast<PointPrecision>(TTF_FontAscent(a_font))){
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

	enum class FontStyle : int{
		NORMAL = TTF_STYLE_NORMAL,
		BOLD = TTF_STYLE_BOLD,
		ITALIC = TTF_STYLE_ITALIC,
		UNDERLINE = TTF_STYLE_UNDERLINE
	};

	inline FontStyle operator | (FontStyle lhs, FontStyle rhs){
		return static_cast<FontStyle>((int)lhs | (int)rhs);
	}

	inline FontStyle& operator |= (FontStyle& a_lhs, FontStyle a_rhs){
		a_lhs = static_cast<FontStyle>(static_cast<int>(a_lhs) | static_cast<int>(a_rhs));
		return a_lhs;
	}

	class TextLibrary{
	public:
		TextLibrary(Draw2D *a_rendering);
		~TextLibrary(){}

		bool loadFont(const std::string &a_identifier, std::string a_fontFileLocation, int a_pointSize, FontStyle a_styleFlags = FontStyle::NORMAL);

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
		FormattedState(PointPrecision a_minimumLineHeight, const std::shared_ptr<FormattedState> &a_currentState);

		Color color;
		std::shared_ptr<FontDefinition> font;
		PointPrecision minimumLineHeight;
	};

	class FormattedLine;
	struct FormattedCharacter{
		static std::shared_ptr<FormattedCharacter> make(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state){
			return std::shared_ptr<FormattedCharacter>(new FormattedCharacter(parent, a_character, a_state));
		}

		Size<> characterSize() const{
			if(isPartOfFormat){
				return{0.0, static_cast<PointPrecision>(character->characterSize().height)};
			}else{
				return castSize<PointPrecision>(character->characterSize());
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

		void offsetForLineHeight(PointPrecision a_lineHeight) const{
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

		Point<> offset(PointPrecision a_lineHeight, PointPrecision a_baseLine){
			PointPrecision height = character->font()->height();
			PointPrecision base = character->font()->base();
			offset({offsetPosition.x, a_baseLine - base});
			shape->position(basePosition + offsetPosition);
			return offsetPosition;
		}

		Point<> offset(PointPrecision a_x, PointPrecision a_lineHeight, PointPrecision a_baseLine){
			PointPrecision height = character->font()->height();
			PointPrecision base = character->font()->base();
			offset({a_x, a_baseLine - base});
			shape->position(basePosition + offsetPosition);
			return offsetPosition;
		}

		void applyState(const std::shared_ptr<FormattedState> &a_state){
			state = a_state;
			character = state->font->getCharacter(textCharacter);
			shape->setSize(castSize<PointPrecision>(character->characterSize()));
			shape->texture(character->texture());
			shape->color(state->color);
		}

		bool partOfFormat(bool a_isPartOfFormat){
			isPartOfFormat = a_isPartOfFormat;
			if(isPartOfFormat){
				shape->hide();
			} else{
				shape->show();
			}
			return isPartOfFormat;
		}

		bool partOfFormat() const{
			return isPartOfFormat;
		}
		bool isSoftBreakCharacter() const{
			return character->isSoftBreakCharacter();
		}

		UtfChar textCharacter;
		std::shared_ptr<TextCharacter> character;
		std::shared_ptr<Scene::Rectangle> shape;
		std::shared_ptr<FormattedLine> line;
		std::shared_ptr<FormattedState> state;

	private:
		bool isPartOfFormat = false;
		Point<> basePosition;
		Point<> offsetPosition;
		FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state):
			textCharacter(a_character),
			state(a_state),
			character(a_state->font->getCharacter(a_character)){

			shape = parent->make<Scene::Rectangle>(castSize<PointPrecision>(character->characterSize()));
			shape->setSize(castSize<PointPrecision>(character->characterSize()));
			shape->texture(character->texture());
			shape->color(state->color);
		}
	};

	class FormattedText;
	struct FormattedCharacter;
	class FormattedLine : public std::enable_shared_from_this<FormattedLine> {
	public:
		static std::shared_ptr<FormattedLine> make(FormattedText &a_text, size_t a_lineIndex);

		std::vector<std::shared_ptr<FormattedCharacter>> removeCharacters(size_t a_characterIndex, size_t a_totalToRemove);
		void addCharacters(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		std::vector<std::shared_ptr<FormattedCharacter>> removeLeadingCharactersForWidth(float a_width);

		std::shared_ptr<FormattedCharacter>& operator[](size_t a_index);
		size_t size() const{
			return characters.size();
		}
		bool empty() const{
			return characters.empty();
		}
		PointPrecision height() const{
			return lineHeight;
		}


		size_t index(size_t a_newIndex){
			lineIndex = a_newIndex;
			return lineIndex;
		}
		size_t index() const{
			return lineIndex;
		}

		UtfString string() const{
			UtfString result;
			for(auto& character : characters){
				result+=character->character->character();
			}
			return result;
		}

		void minimumLineHeightChanged(){
			fixVisuals();
		}

		float lineWidth() const{
			if(characters.empty()){
				return 0.0f;
			} else{
				return characters.back()->position().x + characters.back()->characterSize().width;
			}
		}

		void applyAlignment();
	private:
		std::string makeCharacterGUID(size_t a_index){
			return guid(wideToChar(characters[a_index]->character->character()));
		}

		FormattedLine(FormattedText &a_lines, size_t a_lineIndex);

		void updateFormatAfterAdd(size_t a_startIndex, size_t a_endIndex);
		void updateLineHeight();

		std::shared_ptr<FormattedState> getFontState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getColorState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getHeightState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);

		std::shared_ptr<FormattedState> getNewState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_current);

		void fixVisuals();

		PointPrecision lineHeight;
		PointPrecision baseLine;

		size_t lineIndex;

		PointPrecision linePosition;
		std::vector<std::shared_ptr<FormattedCharacter>> characters;
		FormattedText& text;
	};

	std::shared_ptr<FormattedState> getTextState(const UtfString &a_text, size_t a_i, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState, std::pair<size_t, size_t> &o_range);

	class FormattedText{
	public:
		FormattedText(TextLibrary &a_library, PointPrecision a_width, const std::string &a_defaultStateIdentifier, TextWrapMethod a_wrapping = SOFT, TextJustification a_justification = LEFT);

		PointPrecision width(PointPrecision a_width);
		PointPrecision width() const{
			return textWidth;
		}

		bool exceedsWidth(PointPrecision a_xPosition) const{
			return a_xPosition > textWidth;
		}

		PointPrecision positionForLine(size_t a_index);

		std::shared_ptr<FormattedState> defaultState(const std::string &a_defaultStateIdentifier);
		std::shared_ptr<FormattedState> defaultState() const{
			return defaultTextState;
		}

		std::shared_ptr<FormattedLine>& operator[](size_t a_index);

		size_t size() const{
			return lines.size();
		}
		bool empty() const{
			return lines.empty();
		}

		size_t length() const;

		UtfString string() const;

		std::shared_ptr<FormattedCharacter> characterRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex){
			auto index = absoluteIndex(a_lineIndex, a_characterIndex);
			if(a_relativeCharacterIndex + static_cast<int64_t>(index) > 0){
				return characterForIndex(index + a_relativeCharacterIndex);
			}else{
				return nullptr;
			}
		}

		void applyState(const std::shared_ptr<FormattedState> &a_newState, size_t a_newFormatStart, size_t a_newFormatEnd){
			std::shared_ptr<FormattedState> originalState;
			size_t i = a_newFormatStart;
			for(auto character = characterForIndex(i); character; ++i, character = characterForIndex(i)){
				if(i == a_newFormatStart){
					originalState = character->state;
				}else if(character->state != originalState){
					break;
				}
				character->partOfFormat(i <= a_newFormatEnd);
				character->applyState(a_newState);
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
			if(characterInLineIndex >= line->size()){
				return nullptr;
			}else{
				return (*line)[characterInLineIndex];
			}
		}
		
		std::tuple<std::shared_ptr<FormattedLine>, size_t> lineForCharacterIndex(size_t a_characterIndex){
			std::shared_ptr<FormattedLine> found;
			size_t foundIndex = 0;
			int64_t characterIndex = a_characterIndex;
			for(foundIndex = 0; foundIndex < lines.size() && (characterIndex - static_cast<int64_t>(lines[foundIndex]->size())) > 0; ++foundIndex){
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

		void removeCharacters(size_t a_startIndex, size_t a_count){
			if(a_count == 0){
				return;
			}
			std::shared_ptr<FormattedLine> lineStart;
			size_t lineStartCharacter;
			std::tie(lineStart, lineStartCharacter) = lineForCharacterIndex(a_startIndex);

			std::shared_ptr<FormattedLine> lineEnd;
			size_t lineEndCharacter;
			std::tie(lineEnd, lineEndCharacter) = lineForCharacterIndex(a_startIndex + a_count);
			
			if(lineStart->index() == lineEnd->index()){
				lineStart->removeCharacters(lineStartCharacter, lineEndCharacter - lineStartCharacter);

			} else{
				if(lineEnd->index() - lineStart->index() > 1){
					lines.erase(lines.begin() + lineStart->index() + 1, lines.begin() + lineEnd->index());
					for(size_t i = lineStart->index() + 1; i < lines.size(); ++i){
						lines[i]->index(i);
					}
				}
				lineEnd->removeCharacters(0, lineEndCharacter);
				lineStart->removeCharacters(lineStartCharacter, lineStart->size() - lineStartCharacter);
			}
		}

		void addCharacters(size_t a_startIndex, const UtfString &a_characters){
			if(a_characters.empty()){
				return;
			}
			std::shared_ptr<FormattedLine> line;
			size_t characterInLineIndex;
			std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);

			std::shared_ptr<FormattedState> foundState = defaultTextState;
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
			std::shared_ptr<FormattedState> foundState = defaultTextState;
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
				rawString.push_back(character);
			}

			std::shared_ptr<FormattedLine> line = lines.back();
			line->addCharacters(line->size(), formattedCharacters);
		}

		PointPrecision minimumLineHeight() const{
			return minimumTextLineHeight;
		}

		PointPrecision minimumLineHeight(PointPrecision a_minimumLineHeight){
			minimumTextLineHeight = a_minimumLineHeight;
			for(auto line : lines){
				line->minimumLineHeightChanged();
			}
			return minimumTextLineHeight;
		}

		void justification(TextJustification a_newJustification){
			if(textJustification != a_newJustification){
				textJustification = a_newJustification;
				for(auto &line : lines){
					line->applyAlignment();
				}
			}
		}
		TextJustification justification() const{
			return textJustification;
		}

		TextLibrary &library;
		TextWrapMethod wrapping;
		std::vector<std::shared_ptr<FormattedLine>> lines;
		std::shared_ptr<FormattedState> defaultTextState;
		std::shared_ptr<Scene::Node> scene;
		UtfString rawString;
		PointPrecision textWidth;
		PointPrecision minimumTextLineHeight;

	private:
		TextJustification textJustification = TextJustification::LEFT;
	};

	class TextBox{
		friend cereal::access;
	public:
		TextBox(TextLibrary *a_textLibrary, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, const Size<> &a_size);

		void justification(TextJustification a_newJustification){
			formattedText.justification(a_newJustification);
		}
		TextJustification justification() const{
			return formattedText.justification();
		}

		void wrapping(TextWrapMethod a_newWrapMethod){
			if(a_newWrapMethod != formattedText.wrapping){
				formattedText.wrapping = a_newWrapMethod; //do this better later.
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
		void appendText(Uint16 a_char){
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
		PointPrecision getMinimumLineHeight() const{
			return minimumLineHeight;
		}
		void setMinimumLineHeight(PointPrecision a_newLineHeight){
			minimumLineHeight = a_newLineHeight;
			formattedText.minimumLineHeight(a_newLineHeight);
		}

		void makeSingleLine(){
			isSingleLine = true;
		}
		void makeManyLine(){
			isSingleLine = false;
		}

		std::shared_ptr<Scene::Node> scene();

		FormattedText formattedText;
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
		PointPrecision minimumLineHeight;
		bool isSingleLine;
	};
}

#endif
