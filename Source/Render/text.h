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
	extern const UtfString COLOR_IDENTIFIER;
	extern const UtfString FONT_IDENTIFIER;
	extern const UtfString HEIGHT_IDENTIFIER;

	Color parseColorString(const std::string &a_colorString);


	class CharacterDefinition;
	class TextLibrary;
	///////////////////////////////////
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

		std::shared_ptr<CharacterDefinition> characterDefinition(UtfChar renderChar);

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

		typedef std::map<UtfChar, std::shared_ptr<CharacterDefinition>> CachedGlyphs;
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

	///////////////////////////////////
	class CharacterDefinition {
	public:
		static std::shared_ptr<CharacterDefinition> make(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);

		UtfChar character() const;
		std::shared_ptr<TextureHandle> texture() const;
		Size<int> characterSize() const;
		Size<int> textureSize() const;

		bool isSoftBreakCharacter();

		std::shared_ptr<FontDefinition> font() const;
	private:
		CharacterDefinition(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);

		UtfChar glyphCharacter;
		std::shared_ptr<SurfaceTextureDefinition> glyphTexture;
		std::shared_ptr<TextureHandle> glyphHandle;
		std::shared_ptr<FontDefinition> fontDefinition;
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

	///////////////////////////////////
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

	///////////////////////////////////
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
	///////////////////////////////////
	class FormattedCharacter{
	public:
		static std::shared_ptr<FormattedCharacter> make(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state);

		Size<> characterSize() const;

		Point<> position() const;
		Point<> position(const Point<> &a_newPosition);

		Point<> offset() const;
		Point<> offset(const Point<> &a_newPosition);
		Point<> offset(PointPrecision a_lineHeight, PointPrecision a_baseLine);
		Point<> offset(PointPrecision a_x, PointPrecision a_lineHeight, PointPrecision a_baseLine);

		void applyState(const std::shared_ptr<FormattedState> &a_state);
		bool partOfFormat(bool a_isPartOfFormat);
		bool partOfFormat() const;
		bool isSoftBreakCharacter() const;

		void removeFromParent();

		UtfChar textCharacter;
		std::shared_ptr<CharacterDefinition> character;
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
			character(a_state->font->characterDefinition(a_character)){

			shape = parent->make<Scene::Rectangle>(castSize<PointPrecision>(character->characterSize()));

			shape->texture(character->texture())->
				color(state->color);
		}
	};

	class FormattedText;
	///////////////////////////////////
	class FormattedLine : public std::enable_shared_from_this<FormattedLine> {
	public:
		~FormattedLine();
		static std::shared_ptr<FormattedLine> make(FormattedText &a_text, size_t a_lineIndex);

		std::vector<std::shared_ptr<FormattedCharacter>> removeCharacters(size_t a_characterIndex, size_t a_totalToRemove);
		void addCharacters(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		std::vector<std::shared_ptr<FormattedCharacter>> removeLeadingCharactersForWidth(float a_width);

		std::shared_ptr<FormattedCharacter>& operator[](size_t a_index);
		size_t size() const;
		bool empty() const;
		PointPrecision height() const;


		size_t index(size_t a_newIndex);
		size_t index() const;

		UtfString string() const;
		void minimumLineHeightChanged();

		float lineWidth() const;

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

	///////////////////////////////////
	class FormattedText{
		friend FormattedLine;
	public:
		FormattedText(TextLibrary &a_library, PointPrecision a_width, const std::string &a_defaultStateIdentifier, TextWrapMethod a_wrapping = SOFT, TextJustification a_justification = LEFT);

		PointPrecision width(PointPrecision a_width);
		PointPrecision width() const;

		bool exceedsWidth(PointPrecision a_xPosition) const;

		PointPrecision positionForLine(size_t a_index);

		std::shared_ptr<FormattedState> defaultState(const std::string &a_defaultStateIdentifier);
		std::shared_ptr<FormattedState> defaultState() const;

		std::shared_ptr<FormattedLine>& operator[](size_t a_index);

		size_t size() const;
		bool empty() const;
		void clear();

		size_t length() const;

		UtfString string() const;

		std::shared_ptr<FormattedCharacter> characterRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex);

		void applyState(const std::shared_ptr<FormattedState> &a_newState, size_t a_newFormatStart, size_t a_newFormatEnd);

		size_t absoluteIndexFromRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex);

		size_t absoluteIndex(size_t a_lineIndex, size_t a_characterIndex) const;

		std::shared_ptr<FormattedCharacter> characterForIndex(size_t a_characterIndex);
		
		std::tuple<std::shared_ptr<FormattedLine>, size_t> lineForCharacterIndex(size_t a_characterIndex);


		void removeCharacters(size_t a_startIndex, size_t a_count);
		void removeLines(size_t a_startIndex, size_t a_count);
		void popCharacters(size_t a_count);

		void addCharacters(size_t a_startIndex, const UtfString &a_characters);
		void addCharacters(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		void append(const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);
		void append(const UtfString &a_characters);

		PointPrecision minimumLineHeight() const;
		PointPrecision minimumLineHeight(PointPrecision a_minimumLineHeight);

		void justification(TextJustification a_newJustification);
		TextJustification justification() const;

		void wrapping(TextWrapMethod a_newWrapping);
		TextWrapMethod wrapping() const;

		std::shared_ptr<Scene::Node> scene() const;
	private:
		TextLibrary &library;
		TextWrapMethod textWrapping;
		std::vector<std::shared_ptr<FormattedLine>> lines;
		std::shared_ptr<FormattedState> defaultTextState;
		std::shared_ptr<Scene::Node> textScene;
		PointPrecision textWidth;
		PointPrecision minimumTextLineHeight;

		TextJustification textJustification = TextJustification::LEFT;
	};

	///////////////////////////////////
	class TextBox{
		friend cereal::access;
	public:
		TextBox(TextLibrary *a_textLibrary, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const Size<> &a_size);
		TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, const Size<> &a_size);

		UtfString string() const{
			return formattedText.string();
		}
		double number() const{
			return stod(string());
		}

		void justification(TextJustification a_newJustification){
			formattedText.justification(a_newJustification);
		}
		TextJustification justification() const{
			return formattedText.justification();
		}

		void wrapping(TextWrapMethod a_newWrapMethod){
			formattedText.wrapping(a_newWrapMethod);
		}
		TextWrapMethod wrapping() const{
			return formattedText.wrapping();
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
			formattedText.append(a_text);
		}
		void appendText(const UtfChar a_char){
			formattedText.append(UtfString(1, a_char));
		}
		void appendText(Uint16 a_char){
			UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
			appendText(*character);
		}

		void prependText(Uint16 a_char){
			UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
			prependText(*character);
		}

		void backspace(){
			formattedText.popCharacters(1);
		}

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
		PointPrecision minimumLineHeight;
		bool isSingleLine;
	};
}

#endif