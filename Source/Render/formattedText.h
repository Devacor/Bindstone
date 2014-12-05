#ifndef _MV_FORMATTEDTEXT_H_
#define _MV_FORMATTEDTEXT_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
#include "Utility/package.h"
#include "SDL_ttf.h"
#include "Render/points.h"

namespace MV {
	namespace Scene{
		class Node;
		class Sprite;
	}
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
	class SurfaceTextureDefinition;
	class TextureHandle;
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
	class Draw2D;
	class TextLibrary{
	public:
		TextLibrary(Draw2D& a_rendering);
		~TextLibrary(){}

		bool loadFont(const std::string &a_identifier, std::string a_fontFileLocation, int a_pointSize, FontStyle a_styleFlags = FontStyle::NORMAL);

		Draw2D& renderer(){return render;}

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
			construct(*renderer);
			archive(
				cereal::make_nvp("loadedFonts", construct()->loadedFonts)
			);
		}

		std::map<std::string, std::shared_ptr<FontDefinition>> loadedFonts;
		SDL_Color white;
		Draw2D& render;
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
		~FormattedCharacter();
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
		std::shared_ptr<Scene::Node> shape;
		std::shared_ptr<FormattedLine> line;
		std::shared_ptr<FormattedState> state;

	private:
		bool isPartOfFormat = false;
		Point<> basePosition;
		Point<> offsetPosition;
		FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state);
	};

	class FormattedText;
	///////////////////////////////////
	class FormattedLine : public std::enable_shared_from_this<FormattedLine> {
	public:
		static std::shared_ptr<FormattedLine> make(FormattedText &a_text, size_t a_lineIndex);

		std::vector<std::shared_ptr<FormattedCharacter>> erase(size_t a_characterIndex, size_t a_totalToRemove);
		void insert(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

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


		void erase(size_t a_startIndex, size_t a_count);
		void removeLines(size_t a_startIndex, size_t a_count);
		size_t popCharacters(size_t a_count); //return new size

		void insert(size_t a_startIndex, const UtfString &a_characters);
		void insert(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

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
}

#endif