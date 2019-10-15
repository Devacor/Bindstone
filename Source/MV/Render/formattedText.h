#ifndef _MV_FORMATTEDTEXT_H_
#define _MV_FORMATTEDTEXT_H_

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <memory>
#include "MV/Utility/generalUtility.h"
#include "SDL_ttf.h"
#include "MV/Render/points.h"

namespace MV {
	namespace Scene{
		class Node;
		class Sprite;
	}
	extern const UtfString COLOR_IDENTIFIER;
	extern const UtfString FONT_IDENTIFIER;
	extern const UtfString HEIGHT_IDENTIFIER;

	Color parseColorString(const std::string &a_colorString);

	enum class TextWrapMethod {
		NONE,
		SCALE,
		HARD,
		SOFT
	};

	enum class TextJustification {
		LEFT,
		CENTER,
		RIGHT
	};

	enum class FontStyle : int {
		NORMAL = TTF_STYLE_NORMAL,
		BOLD = TTF_STYLE_BOLD,
		ITALIC = TTF_STYLE_ITALIC,
		UNDERLINE = TTF_STYLE_UNDERLINE
	};

	inline FontStyle operator | (FontStyle lhs, FontStyle rhs) {
		return static_cast<FontStyle>((int)lhs | (int)rhs);
	}

	inline FontStyle& operator |= (FontStyle& a_lhs, FontStyle a_rhs) {
		a_lhs = static_cast<FontStyle>(static_cast<int>(a_lhs) | static_cast<int>(a_rhs));
		return a_lhs;
	}

	class FontDefinition;
	std::ostream& operator<<(std::ostream& os, const FontDefinition& a_font);

	class CharacterDefinition;
	class TextLibrary;
	///////////////////////////////////
	class FontDefinition : public std::enable_shared_from_this<FontDefinition>{
		friend cereal::access;
		friend TextLibrary;
		friend std::ostream& operator<<(std::ostream&, const FontDefinition&);
	public:
		static std::shared_ptr<FontDefinition> make(TextLibrary &a_library, const std::string &a_identifier, const std::string &a_file, int a_size, FontStyle a_style = FontStyle::NORMAL);
		~FontDefinition(){
			if(font != nullptr){
				TTF_CloseFont(font);
			}
		}

		std::shared_ptr<CharacterDefinition> characterDefinition(const std::string &renderChar);

		PointPrecision height() const{
			return lineHeight;
		}
		PointPrecision base() const{
			return baseLine;
		}
		TextLibrary* library() const{
			return textLibrary;
		}
		std::string id() const {
			return identifier;
		}

		bool equivalent(const std::string &a_identifier, const std::string &a_file, int a_size, FontStyle a_style) const {
			return identifier == a_identifier && file == a_file && size == a_size && style == a_style;
		}
	private:
		std::string file;
		TTF_Font* font = nullptr;
		std::string identifier;
		int size = 0;
		FontStyle style = FontStyle::NORMAL;
		PointPrecision lineHeight = 0;
		PointPrecision baseLine = 0;
		TextLibrary *textLibrary = nullptr;

		typedef std::map<std::string, std::shared_ptr<CharacterDefinition>> CachedGlyphs;
		CachedGlyphs cachedGlyphs;

		template <class Archive>
		void serialize(Archive & archive) const {
			archive(
				CEREAL_NVP(identifier),
				CEREAL_NVP(file),
				CEREAL_NVP(size),
				CEREAL_NVP(style)
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<FontDefinition> &construct, std::uint32_t const){
			construct();
			archive(
				cereal::make_nvp("identifier", construct->identifier),
				cereal::make_nvp("file", construct->file),
				cereal::make_nvp("size", construct->size),
				cereal::make_nvp("style", construct->style)
			);
			construct->initializeFromLoad();
		}

		void initializeFromLoad() {
			TTF_Font* newFont = TTF_OpenFont(file.c_str(), size);
			if (newFont) {
				TTF_SetFontHinting(newFont, TTF_HINTING_NORMAL);
				TTF_SetFontStyle(newFont, static_cast<int>(style));
			} else {
				require<ResourceException>(false, "Error loading font [", identifier, "]: ", TTF_GetError());
			}
		}

		FontDefinition(TextLibrary *a_library, const std::string &a_file, int a_size, TTF_Font* a_font, FontStyle a_style, const std::string &a_identifier):
			file(a_file),
			font(a_font),
            size(a_size),
			identifier(a_identifier),
            style(a_style),
			lineHeight(static_cast<PointPrecision>(TTF_FontLineSkip(a_font))),
			baseLine(static_cast<PointPrecision>(TTF_FontAscent(a_font))),
            textLibrary(a_library){
		}
		FontDefinition(){}

		FontDefinition(const FontDefinition &a_other) = delete;
		FontDefinition& operator=(const FontDefinition &a_other) = delete;
	};

	///////////////////////////////////
	class SurfaceTextureDefinition;
	class TextureHandle;
	class CharacterDefinition {
	public:
		static std::shared_ptr<CharacterDefinition> make(std::shared_ptr<SurfaceTextureDefinition> a_texture, const std::string &a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);
		
		std::string character() const;
		std::shared_ptr<TextureHandle> texture() const;
		Size<int> characterSize() const;
		Size<int> textureSize() const;

		bool isSoftBreakCharacter();

		std::shared_ptr<FontDefinition> font() const;
	private:
		CharacterDefinition(std::shared_ptr<SurfaceTextureDefinition> a_texture, const std::string &a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition);

		std::string glyphCharacter;
		std::shared_ptr<SurfaceTextureDefinition> glyphTexture;
		std::shared_ptr<TextureHandle> glyphHandle;
		std::shared_ptr<FontDefinition> fontDefinition;
	};

	///////////////////////////////////
	class Draw2D;
	class TextLibrary{
		friend FontDefinition;
	public:
		TextLibrary(Draw2D& a_rendering);
		~TextLibrary(){}

		void add(const std::shared_ptr<FontDefinition> &a_definition);
		std::shared_ptr<FontDefinition> get(const std::string &a_identifier) const;

		Draw2D& renderer(){return render;}
	private:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/){
			archive(
				CEREAL_NVP(loadedFonts)
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<TextLibrary> &construct, std::uint32_t const version){
			auto& services = cereal::get_user_data<MV::Services>(archive);
			auto* renderer = services.get<MV::Draw2D>();

			construct(*renderer);
			archive.add(cereal::make_nvp("textLibrary", construct.ptr()));
			std::map<std::string, std::shared_ptr<FontDefinition>> fontLoadScratchPad;
			construct->serialize(archive, version);
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
		static std::shared_ptr<FormattedCharacter> make(const std::shared_ptr<Scene::Node> &parent, const std::string &a_character, const std::shared_ptr<FormattedState> &a_state, bool a_isPassword);

		Size<> characterSize() const;

		Point<> position() const;
		Point<> position(const Point<> &a_newPosition);

		Point<> offset() const;
		Point<> offset(const Point<> &a_newPosition);
		Point<> offset(PointPrecision a_lineHeight, PointPrecision a_baseLine);
		Point<> offset(PointPrecision a_x, PointPrecision a_lineHeight, PointPrecision a_baseLine);

		Scale scale() const;
		void scale(const Scale& a_scale);

		void applyState(const std::shared_ptr<FormattedState> &a_state, bool a_isPassword = false);
		bool partOfFormat(bool a_isPartOfFormat);
		bool partOfFormat() const;
		bool isSoftBreakCharacter() const;

		void removeFromParent();

		std::string textCharacter;
		std::shared_ptr<CharacterDefinition> character;
		std::shared_ptr<Scene::Node> shape;
		std::weak_ptr<FormattedLine> line;
		std::shared_ptr<FormattedState> state;

	private:
		bool isPartOfFormat = false;
		Point<> basePosition;
		Point<> offsetPosition;
		Scale characterScale;
		FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, const std::string &a_character, const std::shared_ptr<FormattedState> &a_state, bool a_isPassword);
	};

	class FormattedText;
	///////////////////////////////////
	class FormattedLine : public std::enable_shared_from_this<FormattedLine> {
		friend FormattedText;
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

		void applyAlignmentAndScale();
	private:
		FormattedLine(FormattedText &a_lines, size_t a_lineIndex);

		void updateFormatAfterAdd(size_t a_startIndex, size_t a_endIndex);
		void updateLineHeight();

		std::shared_ptr<FormattedState> getFontState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getColorState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);
		std::shared_ptr<FormattedState> getHeightState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current);

		std::shared_ptr<FormattedState> getNewState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_current);

		void fixVisuals();
		void reapplyState();

		PointPrecision lineHeight;
		PointPrecision baseLine;

		size_t lineIndex;

		PointPrecision linePosition;
		std::vector<std::shared_ptr<FormattedCharacter>> characters;
		FormattedText& text;
	};

	///////////////////////////////////
	class FormattedText{
		friend cereal::access;
		friend FormattedLine;
	public:
		FormattedText(const FormattedText& a_rhs);
		FormattedText(TextLibrary &a_library, const std::string &a_defaultStateIdentifier, PointPrecision a_width, TextWrapMethod a_wrapping = TextWrapMethod::SOFT, TextJustification a_justification = TextJustification::LEFT);
		FormattedText(TextLibrary &a_library, const std::string &a_defaultStateIdentifier, TextJustification a_justification = TextJustification::LEFT) :
			FormattedText(a_library, a_defaultStateIdentifier, 0.0f, TextWrapMethod::NONE, a_justification){
		}
		PointPrecision width(PointPrecision a_width);
		PointPrecision width() const;

		bool exceedsWidth(PointPrecision a_xPosition) const;

		PointPrecision positionForLine(size_t a_index);

		FormattedText& defaultState(const std::string &a_defaultStateIdentifier);
		std::shared_ptr<FormattedState> defaultState() const;
		std::string defaultStateId() const {
			return defaultStateIdentifier;
		}

		std::shared_ptr<FormattedLine>& operator[](size_t a_index);

		size_t size() const;
		bool empty() const;
		void clear();

		size_t length() const;

		UtfString string() const;
		FormattedText& string(const std::string &a_newContent);

		std::shared_ptr<FormattedCharacter> characterRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex);

		void applyState(const std::shared_ptr<FormattedState> &a_newState, size_t a_newFormatStart, size_t a_newFormatEnd);

		size_t absoluteIndexFromRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex);

		size_t absoluteIndex(size_t a_lineIndex, size_t a_characterIndex) const;

		std::shared_ptr<FormattedCharacter> characterForIndex(size_t a_characterIndex);
		
		std::tuple<std::shared_ptr<FormattedLine>, size_t> lineForCharacterIndex(size_t a_characterIndex);

		void passwordField(bool a_passwordField);

		bool passwordField() const {
			return showAsPassword;
		}

		void erase(size_t a_startIndex, size_t a_count);
		void removeLines(size_t a_startIndex, size_t a_count);
		size_t popCharacters(size_t a_count); //return new size

		//returns the total utf8 code points inserted
		size_t insert(size_t a_startIndex, const UtfString &a_characters);
		void insert(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		//returns the total utf8 code points inserted
		size_t append(const UtfString &a_characters);
		void append(const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters);

		PointPrecision minimumLineHeight() const;
		PointPrecision minimumLineHeight(PointPrecision a_minimumLineHeight);

		void justification(TextJustification a_newJustification);
		TextJustification justification() const;

		void wrapping(TextWrapMethod a_newWrapping, PointPrecision a_newWidth);
		void wrapping(TextWrapMethod a_newWrapping);
		TextWrapMethod wrapping() const;

		std::shared_ptr<Scene::Node> scene() const;

		FormattedText& operator=(const FormattedText& a_rhs) {
			defaultState(a_rhs.defaultStateIdentifier);
			textWidth = a_rhs.textWidth;
			textJustification = a_rhs.textJustification;
			minimumTextLineHeight = a_rhs.minimumTextLineHeight;
			showAsPassword = a_rhs.showAsPassword;
			string(a_rhs.string());
			return *this;
		}
	private:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const a_version) {
			if (a_version > 0) {
				archive(
					cereal::make_nvp("minimumTextLineHeight", minimumTextLineHeight),
					cereal::make_nvp("showAsPassword", showAsPassword)
				);
			}
			archive(
				cereal::make_nvp("defaultStateIdentifier", defaultStateIdentifier),
				cereal::make_nvp("textWidth", textWidth),
				cereal::make_nvp("textWrapping", textWrapping),
				cereal::make_nvp("textJustification", textJustification),
				cereal::make_nvp("string", string())
			);
		}

		template <class Archive>
		static void load_and_construct(Archive & archive, cereal::construct<FormattedText> &construct, std::uint32_t const a_version) {
			auto& services = cereal::get_user_data<MV::Services>(archive);
			auto* library = services.get<MV::TextLibrary>();

			std::string defaultStateIdentifier;
			float textWidth;
			PointPrecision minimumTextLineHeight = 0.0f;
			TextJustification textJustification;
			TextWrapMethod textWrapping;
			std::string stringContents;
			bool showAsPassword = false;
			if (a_version > 0) {
				archive(
					cereal::make_nvp("minimumTextLineHeight", minimumTextLineHeight),
					cereal::make_nvp("showAsPassword", showAsPassword)
				);
			}
			archive(
				cereal::make_nvp("defaultStateIdentifier", defaultStateIdentifier),
				cereal::make_nvp("textWidth", textWidth),
				cereal::make_nvp("textWrapping", textWrapping),
				cereal::make_nvp("textJustification", textJustification),
				cereal::make_nvp("string", stringContents)
			);

			construct(*library, defaultStateIdentifier, textWidth, textWrapping, textJustification);
			construct->minimumTextLineHeight = minimumTextLineHeight;
			construct->showAsPassword = showAsPassword;
			construct->append(stringContents);
		}

		mutable TextLibrary *library;
		TextWrapMethod textWrapping;
		std::vector<std::shared_ptr<FormattedLine>> lines;
		std::shared_ptr<Scene::Node> textScene;
		std::string defaultStateIdentifier;
		std::shared_ptr<FormattedState> cachedDefaultState;
		PointPrecision textWidth;
		PointPrecision minimumTextLineHeight;
		bool showAsPassword = false;

		TextJustification textJustification = TextJustification::LEFT;
	};
}

CEREAL_CLASS_VERSION(MV::FormattedText, 1);

#endif
