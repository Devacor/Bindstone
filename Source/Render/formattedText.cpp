#include "formattedText.h"
#include "Render/Scene/sprite.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>
#include "Utility/log.h"
#include "Utility/tinyutf8.h"

namespace MV {

	std::shared_ptr<MV::FontDefinition> FontDefinition::make(TextLibrary& a_library, const std::string &a_identifier, const std::string &a_file, int a_size, FontStyle a_style) {
		auto exists = a_library.get(a_identifier);
		if (exists){
			require<ResourceException>(exists->equivalent(a_identifier, a_file, a_size, a_style), "Found an existing definition that doesn't match: ", exists);
			return exists;
		} else {
			TTF_Font* newFont = TTF_OpenFont(a_file.c_str(), a_size);
			if (newFont) {
				TTF_SetFontHinting(newFont, TTF_HINTING_NORMAL);
				TTF_SetFontStyle(newFont, static_cast<int>(a_style));
				auto fontDefinitionToAdd = std::shared_ptr<FontDefinition>(new FontDefinition(&a_library, a_file, a_size, newFont, a_style, a_identifier));
				a_library.add(fontDefinitionToAdd);
				return fontDefinitionToAdd;
			} else {
				require<ResourceException>(false, "Error loading font [", a_identifier, "]: ", TTF_GetError());
			}
		}
		return nullptr;
	}

	/*************************\
	| -----FontDefinition---- |
	\*************************/
	
	std::shared_ptr<CharacterDefinition> FontDefinition::characterDefinition(const std::string &renderChar) {
		std::shared_ptr<CharacterDefinition> &character = cachedGlyphs[renderChar];
		if(!character){
			character = CharacterDefinition::make(
				SurfaceTextureDefinition::make(renderChar, [=]() -> SDL_Surface* {
					if (textLibrary->renderer().headless()) { return nullptr; }
					
					return TTF_RenderUTF8_Blended(font, renderChar.c_str(), {255, 255, 255, 255});
				}),
				renderChar,
				shared_from_this()
			);
		}
		return character;
	}





	/*************************\
	| --CharacterDefinition-- |
	\*************************/

	std::shared_ptr<CharacterDefinition> CharacterDefinition::make(std::shared_ptr<SurfaceTextureDefinition> a_texture, const std::string &a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition) {
		return std::shared_ptr<CharacterDefinition>(new CharacterDefinition(a_texture, a_glyphCharacter, a_fontDefinition));
	}
	
	CharacterDefinition::CharacterDefinition(std::shared_ptr<SurfaceTextureDefinition> a_texture, const std::string &a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition):
		glyphTexture(a_texture),
		glyphHandle(a_texture->makeHandle()),
		glyphCharacter(a_glyphCharacter),
		fontDefinition(a_fontDefinition){

		glyphHandle->bounds(glyphTexture->surfaceSize());
	}

	std::string CharacterDefinition::character() const{
		return glyphCharacter;
	}

	std::shared_ptr<TextureHandle> CharacterDefinition::texture() const{
		return glyphHandle;
	}

	Size<int> CharacterDefinition::characterSize() const{
		return (glyphTexture) ? glyphTexture->surfaceSize() : Size<int>(0, 0);
	}

	Size<int> CharacterDefinition::textureSize() const{
		return (glyphTexture) ? glyphTexture->size() : Size<int>(0, 0);
	}

	bool CharacterDefinition::isSoftBreakCharacter() {
		return glyphCharacter == " " || glyphCharacter == "-";
	}

	std::shared_ptr<FontDefinition> CharacterDefinition::font() const {
		return fontDefinition;
	}




	/*************************\
	| ------TextLibrary------ |
	\*************************/

	TextLibrary::TextLibrary(Draw2D &a_rendering):
		render(a_rendering){
		SDL_StartTextInput();
		white.r = 255; white.g = 255; white.b = 255; white.a = 0;
		if(!TTF_WasInit()){
			TTF_Init();
		}
	}

	void TextLibrary::add(const std::shared_ptr<FontDefinition> &a_definition){
		loadedFonts[a_definition->id()] = a_definition;
	}

	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.

	std::shared_ptr<FontDefinition> TextLibrary::get(const std::string &a_identifier) const {
		auto found = loadedFonts.find(a_identifier);
		if(found != loadedFonts.end()){
			return found->second;
		}
		return nullptr;
	}




	/*************************\
	| -----FormattedState---- |
	\*************************/

	FormattedState::FormattedState():
		font(),
		minimumLineHeight(-1) {
	}

	FormattedState::FormattedState(const std::shared_ptr<FontDefinition> &a_font, const std::shared_ptr<FormattedState> &a_currentState) :
		font(a_font),
		color((a_currentState) ? a_currentState->color : Color(1.0f, 1.0f, 1.0f)),
		minimumLineHeight((a_currentState) ? a_currentState->minimumLineHeight : -1) {
	}

	FormattedState::FormattedState(const Color &a_color, const std::shared_ptr<FormattedState> &a_currentState):
		font(a_currentState->font),
		color(a_color),
		minimumLineHeight(a_currentState->minimumLineHeight) {
	}

	FormattedState::FormattedState(PointPrecision a_minimumLineHeight, const std::shared_ptr<FormattedState> &a_currentState):
		font(a_currentState->font),
		color(a_currentState->color),
		minimumLineHeight(a_minimumLineHeight) {
	}





	/*************************\
	| ---FormattedCharacter-- |
	\*************************/

	FormattedCharacter::~FormattedCharacter() {
		if(shape){
			shape->removeFromParent();
		}
	}

	std::shared_ptr<FormattedCharacter> FormattedCharacter::make(const std::shared_ptr<Scene::Node> &parent, const std::string &a_character, const std::shared_ptr<FormattedState> &a_state, bool a_isPassword) {
		return std::shared_ptr<FormattedCharacter>(new FormattedCharacter(parent, a_character, a_state, a_isPassword));
	}

	Size<> FormattedCharacter::characterSize() const {
		if(isPartOfFormat){
			return{0.0, static_cast<PointPrecision>(character->characterSize().height)};
		} else{
			return cast<PointPrecision>(character->characterSize());
		}
	}

	Point<> FormattedCharacter::position() const {
		return basePosition;
	}

	Point<> FormattedCharacter::position(const Point<> &a_newPosition) {
		basePosition = a_newPosition;
		shape->silence()->position((basePosition + offsetPosition) * characterScale);
		return basePosition;
	}

	Point<> FormattedCharacter::offset() const {
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(const Point<> &a_newPosition) {
		offsetPosition = a_newPosition;
		shape->silence()->position((basePosition + offsetPosition) * characterScale);
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(PointPrecision a_lineHeight, PointPrecision a_baseLine) {
		PointPrecision height = character->font()->height();
		PointPrecision base = character->font()->base();
		offset({offsetPosition.x, a_baseLine - base});
		shape->silence()->position((basePosition + offsetPosition) * characterScale);
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(PointPrecision a_x, PointPrecision a_lineHeight, PointPrecision a_baseLine) {
		PointPrecision height = character->font()->height();
		PointPrecision base = character->font()->base();
		offset({ a_x, a_baseLine - base });
		shape->silence()->position((basePosition + offsetPosition) * characterScale);
		return offsetPosition;
	}

	Scale FormattedCharacter::scale() const{
		return characterScale;
	}
	void FormattedCharacter::scale(const Scale& a_scale){
		characterScale = a_scale;
		shape->silence()->position((basePosition + offsetPosition) * characterScale)->component<Scene::Sprite>()->size(cast<PointPrecision>(character->characterSize()) * characterScale);
	}

	void FormattedCharacter::applyState(const std::shared_ptr<FormattedState> &a_state, bool a_isPassword) {
		state = a_state;
		character = a_isPassword ? a_state->font->characterDefinition(u8"●") : state->font->characterDefinition(textCharacter);
		auto silencedShape = shape->silence();
		auto sprite = shape->component<Scene::Sprite>();
		sprite->size(cast<PointPrecision>(character->characterSize()) * characterScale);
		sprite->texture(character->texture());
		sprite->color(state->color);
	}

	bool FormattedCharacter::partOfFormat(bool a_isPartOfFormat) {
		isPartOfFormat = a_isPartOfFormat;
		if(isPartOfFormat){
			shape->silence()->hide();
		} else{
			shape->silence()->show();
		}
		return isPartOfFormat;
	}

	bool FormattedCharacter::partOfFormat() const {
		return isPartOfFormat;
	}

	bool FormattedCharacter::isSoftBreakCharacter() const {
		return character->isSoftBreakCharacter();
	}

	void FormattedCharacter::removeFromParent() {
		if(shape){
			shape->removeFromParent();
		}
	}

	FormattedCharacter::FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, const std::string &a_character, const std::shared_ptr<FormattedState> &a_state, bool a_isPassword):
		textCharacter(a_character),
		state(a_state),
		character(a_isPassword ? a_state->font->characterDefinition(u8"●") : a_state->font->characterDefinition(a_character)) {

		shape = parent->silence()->make(guid(character->character()))->
			attach<Scene::Sprite>()->
			size(cast<PointPrecision>(character->characterSize()))->
			texture(character->texture())->
			color(state->color)->
			shader(MV::PREMULTIPLY_ID)->
			owner();
	}




	/*************************\
	| -----FormattedLine----- |
	\*************************/

	std::shared_ptr<FormattedLine> FormattedLine::make(FormattedText &a_text, size_t a_lineIndex) {
		auto line = std::shared_ptr<FormattedLine>(new FormattedLine(a_text, a_lineIndex));
		return line;
	}

	FormattedLine::FormattedLine(FormattedText &a_text, size_t a_lineIndex):
		text(a_text),
		lineIndex(a_lineIndex),
		lineHeight(0),
		linePosition(a_text.positionForLine(a_lineIndex)){
	}

	Color parseColorString(const std::string &a_colorString){
		if(a_colorString.empty()){ return Color(); }
		std::vector<std::string> colorStrings;
		boost::split(colorStrings, a_colorString, boost::is_any_of(":"));
		Color result;
		if(!colorStrings.empty()){
			if(colorStrings.size() >= 1){
				try{ result.R = std::stof(colorStrings[0]); } catch(std::exception&){ result.R = 1.0; }
			}
			if(colorStrings.size() >= 2){
				try{ result.G = std::stof(colorStrings[1]); } catch(std::exception&){ result.G = 1.0; }
			}
			if(colorStrings.size() >= 3){
				try{ result.B = std::stof(colorStrings[2]); } catch(std::exception&){ result.B = 1.0; }
			}
			if(colorStrings.size() >= 4){
				try{ result.A = std::stof(colorStrings[3]); } catch(std::exception&){ result.A = 1.0; }
			}
		}
		return result;
	}

	std::ostream& operator<<(std::ostream& os, const FontDefinition& a_font) {
		os << "Identifier [" << a_font.identifier << "] - File [" << a_font.file << "] - Size [" << a_font.size << "] - Style [" << static_cast<int>(a_font.style) << "]\n";
		return os;
	}

	std::shared_ptr<FormattedState> FormattedLine::getHeightState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string heightString = a_text.substr(2);
		if(heightString.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->minimumLineHeight, a_current);
		} else{
			PointPrecision newMinimumHeight = 0;
			try{ newMinimumHeight = std::stof(heightString); } catch(std::exception&) { newMinimumHeight = 0.0; }
			return std::make_shared<FormattedState>(newMinimumHeight, a_current);
		}
	}

	std::shared_ptr<FormattedState> FormattedLine::getColorState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string colorString = a_text.substr(2);
		if(colorString.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->color, a_current);
		} else{
			return std::make_shared<FormattedState>(parseColorString(colorString), a_current);
		}
	}

	std::shared_ptr<FormattedState> FormattedLine::getFontState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string fontIdentifier = a_text.substr(2);
		if(fontIdentifier.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->font, a_current);
		} else{
			return std::make_shared<FormattedState>(text.library->get(fontIdentifier), a_current);
		}
	}

	std::shared_ptr<FormattedState> FormattedLine::getNewState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_current){
		if(a_text.length() >= 2){
			if(a_text[0] == 'f'){
				return getFontState(a_text, a_current);
			} else if(a_text[0] == 'c'){
				return getColorState(a_text, a_current);
			} else if(a_text[0] == 'h'){
				return getHeightState(a_text, a_current);
			}
		}
		return a_current;
	}

	void FormattedLine::updateFormatAfterAdd(size_t a_startIndex, size_t a_endIndex){
		for(size_t i = a_startIndex; i < a_endIndex; ++i){
			if(!characters[i]->partOfFormat()){
				if(characters[i]->textCharacter == "]"){
					auto previous = text.characterRelativeTo(lineIndex, i, -1); 
					if(previous && previous->textCharacter == "]"){
						UtfString content;
						previous = text.characterRelativeTo(lineIndex, i, -2);
						auto previous2 = text.characterRelativeTo(lineIndex, i, -3);
						int64_t startOfNewState = -3;
						for(size_t total = 0; previous && previous2 && total < 32; --startOfNewState, ++total){
							content += previous->textCharacter;
							previous = text.characterRelativeTo(lineIndex, i, startOfNewState);
							previous2 = text.characterRelativeTo(lineIndex, i, startOfNewState-1);
							if(previous->textCharacter == "[" && previous2->textCharacter == "["){
								std::reverse(content.begin(), content.end());
								auto newState = getNewState(content, previous2->state);
								if (newState != previous2->state) {
									text.applyState(newState, text.absoluteIndexFromRelativeTo(lineIndex, i, startOfNewState), text.absoluteIndex(lineIndex, i));
								}
								break;
							}
						}
					}
				}
			}
		}
	}

	void FormattedLine::insert(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters){
		if(!a_characters.empty()){
			auto insertIndex = std::min(a_characterIndex, characters.size());
			characters.insert(characters.begin() + insertIndex, a_characters.begin(), a_characters.end());
			updateFormatAfterAdd(insertIndex, insertIndex + a_characters.size());
			fixVisuals();
		}
	}

	void FormattedLine::applyAlignmentAndScale(){
		float scaleTo = 1.0f;
		if (text.wrapping() == TextWrapMethod::SCALE) {
			auto width = lineWidth();
			if (width > text.width()) {
				scaleTo = text.width() / width;
			}
		}

		float width = lineWidth();
		float offset = 0;
		if (text.wrapping() != TextWrapMethod::SCALE || width < text.width()) {
			if (text.justification() == TextJustification::CENTER) {
				offset = (text.width() - width) * scaleTo / 2.0f;
			}
			else if (text.justification() == TextJustification::RIGHT) {
				offset = text.width() - width * scaleTo;
			}
		}

		for(size_t index = 0; index < characters.size(); ++index){
			characters[index]->offset({offset, characters[index]->offset().y});
			if (characters[index]->scale().x != scaleTo) {
				characters[index]->scale(scaleTo);
			}
		}
	}

	void FormattedLine::reapplyState() {
		for (auto&& character : characters) {
			character->applyState(character->state, text.showAsPassword);
		}
	}

	void FormattedLine::fixVisuals(){
		if(!characters.empty()){
			linePosition = text.positionForLine(lineIndex);
			characters[0]->position({0, linePosition});
			updateLineHeight();
			bool exceeded = false;
			for(size_t index = 0; index < characters.size(); ++index){
				auto characterPosition = (index > 0) ? characters[index - 1]->position().x + characters[index - 1]->characterSize().width : 0;
				characters[index]->position({characterPosition, linePosition});
				characters[index]->offset(0.0f, lineHeight, baseLine);
				
				if(text.wrapping() != TextWrapMethod::NONE && text.wrapping() != TextWrapMethod::SCALE && index > 0 && text.exceedsWidth(characters[index]->position().x)){
					std::vector<std::shared_ptr<FormattedCharacter>> overflow(characters.begin() + index, characters.end());
					characters.erase(characters.begin() + index, characters.end());
					if(text.lines.size() <= lineIndex + 1){
						text.lines.push_back(FormattedLine::make(text, text.lines.size()));
					}
					text.lines[lineIndex + 1]->insert(0, overflow);
					exceeded = true;
					break;
				}
			}

			if(!exceeded && lineIndex+1 < text.lines.size()){
				MV::PointPrecision widthRemaining = text.width() - lineWidth();
				insert(characters.size(), text.lines[lineIndex + 1]->removeLeadingCharactersForWidth(widthRemaining));
			}
			applyAlignmentAndScale();
		}
	}

	void FormattedLine::updateLineHeight() {
		PointPrecision maxCharacterHeight = 0;
		baseLine = 0.0;
		if(!characters.empty()){
			auto maxHeightCharacter = (*std::max_element(characters.begin(), characters.end(), [](const std::shared_ptr<FormattedCharacter> &a_lhs, const std::shared_ptr<FormattedCharacter> &a_rhs){
				return a_lhs->character->font()->height() < a_rhs->character->font()->height();
			}));
			maxCharacterHeight = maxHeightCharacter->character->font()->height();
			baseLine = maxHeightCharacter->character->font()->base();
		}
		if(text.minimumLineHeight() > maxCharacterHeight){
			lineHeight = text.minimumLineHeight();
			baseLine += (text.minimumLineHeight() - maxCharacterHeight) / 2.0f;
		} else{
			lineHeight = maxCharacterHeight;
		}
	}

	std::shared_ptr<FormattedCharacter>& FormattedLine::operator[](size_t a_index) {
		MV::require<RangeException>(a_index < characters.size(), "FormattedLine::operator[] supplied invalid index (", a_index, " > ", characters.size(), ")");
		return characters[a_index];
	}

	std::vector<std::shared_ptr<FormattedCharacter>> FormattedLine::removeLeadingCharactersForWidth(float a_width) {
		std::vector<std::shared_ptr<FormattedCharacter>> result;
		if(text.textWrapping == TextWrapMethod::NONE || text.textWrapping == TextWrapMethod::SCALE){
			return result;
		}

		for(int i = 0; i < characters.size() && a_width - characters[i]->characterSize().width > 0.0f; ++i){
			result.push_back(characters[i]);
			a_width -= characters[i]->characterSize().width;
		}

		if(text.textWrapping == TextWrapMethod::SOFT && !result.empty()){
			size_t i = result.size() - 1;
			auto foundSoftBreak = std::find_if(result.begin(), result.end(), [&](std::shared_ptr<FormattedCharacter> a_element){
				return a_element->isSoftBreakCharacter();
			});
			
			if(foundSoftBreak != result.end()){
				result.erase(foundSoftBreak, result.end());
			} else{
				bool lineCanSoftBreak = std::find_if(characters.begin(), characters.end(), [&](std::shared_ptr<FormattedCharacter> a_element){
					return a_element->isSoftBreakCharacter();
				}) != characters.end();

				if(lineCanSoftBreak){
					result.clear();
				}
			}
		}

		if(!result.empty()){
			characters.erase(characters.begin(), characters.begin() + result.size());
		}
		fixVisuals();
		return result;
	}

	std::vector<std::shared_ptr<FormattedCharacter>> FormattedLine::erase(size_t a_characterIndex, size_t a_totalToRemove) {
		if(a_characterIndex > characters.size() || a_totalToRemove == 0){
			return{};
		} else if(a_totalToRemove + a_characterIndex > characters.size()){
			a_totalToRemove = characters.size() - a_characterIndex;
		}
		std::vector<std::shared_ptr<FormattedCharacter>> result;
		for(size_t i = a_characterIndex; i < characters.size() && i < a_characterIndex + a_totalToRemove; ++i){
			characters[i]->removeFromParent();
			result.push_back(characters[i]);
		}
		characters.erase(characters.begin() + a_characterIndex, characters.begin() + a_characterIndex + a_totalToRemove);
		fixVisuals();
		return result;
	}

	size_t FormattedLine::size() const {
		return characters.size();
	}

	bool FormattedLine::empty() const {
		return characters.empty();
	}

	MV::PointPrecision FormattedLine::height() const {
		return lineHeight;
	}

	size_t FormattedLine::index(size_t a_newIndex) {
		lineIndex = a_newIndex;
		return lineIndex;
	}

	size_t FormattedLine::index() const {
		return lineIndex;
	}

	MV::UtfString FormattedLine::string() const {
		UtfString result;
		for(auto& character : characters){
			result += character->character->character();
		}
		return result;
	}

	void FormattedLine::minimumLineHeightChanged() {
		fixVisuals();
	}

	float FormattedLine::lineWidth() const {
		if(characters.empty()){
			return 0.0f;
		} else{
			return characters.back()->position().x + characters.back()->characterSize().width;
		}
	}



	/*************************\
	| -----FormattedText----- |
	\*************************/

	std::shared_ptr<FormattedLine>& FormattedText::operator[](size_t a_index) {
		MV::require<RangeException>(a_index < lines.size(), "FormattedText::operator[] supplied invalid index (", a_index, " > ", lines.size(), ")");
		return lines[a_index];
	}

	FormattedText::FormattedText(const FormattedText& a_rhs):
		FormattedText(*a_rhs.library, a_rhs.defaultStateIdentifier, a_rhs.textWidth, a_rhs.textWrapping, a_rhs.textJustification){

		append(a_rhs.string());
	}

	FormattedText::FormattedText(TextLibrary &a_library, const std::string &a_defaultStateIdentifier, PointPrecision a_width, TextWrapMethod a_wrapping, TextJustification a_justification):
		library(&a_library),
		textWidth(a_width),
		defaultStateIdentifier(a_defaultStateIdentifier),
		cachedDefaultState(std::make_shared<FormattedState>(a_library.get(a_defaultStateIdentifier))),
		textWrapping(a_wrapping),
		textJustification(a_justification),
		minimumTextLineHeight(-1){

		textScene = Scene::Node::make(library->renderer())->serializable(false);
	}

	PointPrecision FormattedText::width(PointPrecision a_width) {
		textWidth = a_width;
		for (auto&& line : lines) {
			line->fixVisuals();
		}
		return textWidth;
	}

	MV::PointPrecision FormattedText::width() const {
		return textWidth;
	}

	PointPrecision FormattedText::positionForLine(size_t a_index) {
		a_index = std::min(a_index, lines.size());
		return std::accumulate(lines.begin(), lines.begin()+a_index, 0.0f, [&](PointPrecision a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return std::max(a_line->height(), minimumTextLineHeight) + a_accumulated;
		});
	}

	FormattedText& FormattedText::defaultState(const std::string &a_defaultStateIdentifier) {
		cachedDefaultState = std::make_shared<FormattedState>(library->get(a_defaultStateIdentifier));
		return *this;
	}

	std::shared_ptr<FormattedState> FormattedText::defaultState() const {
		return cachedDefaultState;
	}

	size_t FormattedText::length() const {
		return std::accumulate(lines.begin(), lines.end(), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return a_line->size() + a_accumulated;
		});
	}

	MV::UtfString FormattedText::string() const {
		UtfString result;
		for(auto& line : lines){
			result += line->string();
		}
		return result;
	}

	MV::FormattedText& FormattedText::string(const std::string &a_newContent) {
		clear();
		append(a_newContent);
		return *this;
	}

	bool FormattedText::exceedsWidth(PointPrecision a_xPosition) const {
		return textWidth > 0.0f && a_xPosition > textWidth;
	}

	size_t FormattedText::size() const {
		return std::accumulate(lines.begin(), lines.end(), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return a_line->size() + a_accumulated;
		});
	}

	bool FormattedText::empty() const {
		return lines.empty();
	}

	void FormattedText::clear() {
		lines.clear();
	}

	std::shared_ptr<FormattedCharacter> FormattedText::characterRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex) {
		auto index = absoluteIndex(a_lineIndex, a_characterIndex);
		if(a_relativeCharacterIndex + static_cast<int64_t>(index) > 0){
			return characterForIndex(index + a_relativeCharacterIndex);
		} else{
			return nullptr;
		}
	}

	void FormattedText::applyState(const std::shared_ptr<FormattedState> &a_newState, size_t a_newFormatStart, size_t a_newFormatEnd) {
		auto silenced = scene()->silence();
		std::shared_ptr<FormattedState> originalState;
		size_t i = a_newFormatStart;
		for(auto character = characterForIndex(i); character; ++i, character = characterForIndex(i)){
			if(i == a_newFormatStart){
				originalState = character->state;
			} else if(character->state != originalState){
				break;
			}
			character->partOfFormat(i <= a_newFormatEnd);
			character->applyState(a_newState, showAsPassword);
		}
	}

	size_t FormattedText::absoluteIndexFromRelativeTo(size_t a_lineIndex, size_t a_characterIndex, int64_t a_relativeCharacterIndex) {
		auto index = absoluteIndex(a_lineIndex, a_characterIndex);
		if(a_relativeCharacterIndex + static_cast<int64_t>(index) > 0){
			return index + a_relativeCharacterIndex;
		} else{
			return 0;
		}
	}

	size_t FormattedText::absoluteIndex(size_t a_lineIndex, size_t a_characterIndex) const {
		auto accumulatedIndex = std::accumulate(lines.begin(), lines.begin() + std::max<int64_t>(0, static_cast<int64_t>(a_lineIndex)-1), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return a_line->size() + a_accumulated;
		});
		return accumulatedIndex + a_characterIndex;
	}

	std::shared_ptr<FormattedCharacter> FormattedText::characterForIndex(size_t a_characterIndex) {
		if(lines.empty()){
			return nullptr;
		}

		std::shared_ptr<FormattedLine> line;
		size_t characterInLineIndex;
		std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_characterIndex);
		if(characterInLineIndex >= line->size()){
			return nullptr;
		} else{
			return (*line)[characterInLineIndex];
		}
	}

	std::tuple<std::shared_ptr<FormattedLine>, size_t> FormattedText::lineForCharacterIndex(size_t a_characterIndex) {
		std::shared_ptr<FormattedLine> found;
		size_t foundIndex = 0;
		int64_t characterIndex = a_characterIndex;
		for(foundIndex = 0; foundIndex < lines.size() && (characterIndex - static_cast<int64_t>(lines[foundIndex]->size())) > 0; ++foundIndex){
			characterIndex -= lines[foundIndex]->size();
		}
		if(foundIndex < lines.size()){
			return std::make_tuple(lines[foundIndex], std::min<size_t>(characterIndex, lines[foundIndex]->size()));
		} else{
			auto newLine = FormattedLine::make(*this, lines.size());
			lines.push_back(newLine);
			return std::make_tuple(newLine, 0);
		}
	}

	void FormattedText::passwordField(bool a_passwordField) {
		if (a_passwordField != showAsPassword) {
			showAsPassword = a_passwordField;
			for (auto&& line : lines) {
				line->reapplyState();
			}
			for (auto&& line : lines) {
				line->fixVisuals();
			}
		}
	}

	void FormattedText::erase(size_t a_startIndex, size_t a_count) {
		auto silenced = scene()->silence();
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
			lineStart->erase(lineStartCharacter, lineEndCharacter - lineStartCharacter);
			if(lineStart->empty()){
				removeLines(lineStart->index(), 1);
			}
		} else{
			removeLines(lineStart->index() + 1, lineEnd->index() - lineStart->index() - 1);

			lineEnd->erase(0, lineEndCharacter);
			lineStart->erase(lineStartCharacter, lineStart->size() - lineStartCharacter);
			if(lineEnd->empty()){
				removeLines(lineEnd->index(), 1);
			}
			if(lineStart->empty()){
				removeLines(lineStart->index(), 1);
			}
		}
	}

	void FormattedText::removeLines(size_t a_startIndex, size_t a_count) {
		if(a_count > 0){
			auto silenced = scene()->silence();
			lines.erase(lines.begin() + a_startIndex, lines.begin() + a_startIndex + a_count);
			for(size_t i = a_startIndex; i < lines.size(); ++i){
				lines[i]->index(i);
			}
		}
	}

	size_t FormattedText::popCharacters(size_t a_count) {
		auto textSize = size() - a_count;
		erase(textSize, a_count);
		return textSize;
	}

	size_t FormattedText::insert(size_t a_startIndex, const UtfString &a_characters) {
		if(a_characters.empty()){
			return 0;
		}
		auto silenced = scene()->silence();
		std::shared_ptr<FormattedLine> line;
		size_t characterInLineIndex;
		std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);

		std::shared_ptr<FormattedState> foundState = defaultState();
		if(a_startIndex > 0){
			if(characterInLineIndex > 0){
				foundState = (*line)[characterInLineIndex - 1]->state;
			} else if(line->index() > 0 && !lines[line->index() - 1]->empty()){
				auto previousLine = lines[line->index() - 1];
				foundState = (*previousLine)[previousLine->size() - 1]->state;
			}
		}

		size_t inserted = 0;
		std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
		utf8_string utfCharacters(a_characters);
		for (auto it = utfCharacters.begin(); it != utfCharacters.end();++it) {
			formattedCharacters.push_back(FormattedCharacter::make(textScene, it.str(), foundState, showAsPassword));
			++inserted;
		}

		line->insert(characterInLineIndex, formattedCharacters);
		return inserted;
	}

	void FormattedText::insert(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters) {
		if(a_characters.empty()){
			return;
		}
		auto silenced = scene()->silence();
		std::shared_ptr<FormattedLine> line;
		size_t characterInLineIndex;
		std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);
		line->insert(a_startIndex, a_characters);
	}

	void FormattedText::append(const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters) {
		if(a_characters.empty()){
			return;
		}
		auto silenced = scene()->silence();
		if(lines.empty()){
			lines.push_back(FormattedLine::make(*this, lines.size()));
		}
		std::shared_ptr<FormattedLine> line = lines.back();
		line->insert(line->size(), a_characters);
	}

	size_t FormattedText::append(const UtfString &a_characters) {
		if(a_characters.empty()){
			return 0;
		}
		auto silenced = scene()->silence();
		std::shared_ptr<FormattedState> foundState = defaultState();
		if(lines.empty()){
			lines.push_back(FormattedLine::make(*this, lines.size()));
		} else{
			if(!lines.back()->empty()){
				foundState = (*lines.back())[lines.back()->size() - 1]->state;
			} else if(lines.size() > 1 && !lines[lines.size() - 1]->empty()){
				auto previousLine = lines[lines.size() - 1];
				foundState = (*previousLine)[previousLine->size() - 1]->state;
			}
		}
		size_t inserted = 0;
		std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
		utf8_string utfCharacters(a_characters);
		for (auto it = utfCharacters.begin(); it != utfCharacters.end(); ++it) {
			formattedCharacters.push_back(FormattedCharacter::make(textScene, it.str(), foundState, showAsPassword));
			++inserted;
		}

		std::shared_ptr<FormattedLine> line = lines.back();
		line->insert(line->size(), formattedCharacters);
		return inserted;
	}

	MV::PointPrecision FormattedText::minimumLineHeight() const {
		return minimumTextLineHeight;
	}

	MV::PointPrecision FormattedText::minimumLineHeight(PointPrecision a_minimumLineHeight) {
		if (!equals(minimumTextLineHeight, a_minimumLineHeight)) {
			minimumTextLineHeight = a_minimumLineHeight;
			auto silenced = scene()->silence();
			for (auto line : lines) {
				line->minimumLineHeightChanged();
			}
		}
		return minimumTextLineHeight;
	}

	void FormattedText::justification(TextJustification a_newJustification) {
		if(textJustification != a_newJustification){
			auto silenced = scene()->silence();
			textJustification = a_newJustification;
			for(auto &line : lines){
				line->applyAlignmentAndScale();
			}
		}
	}

	MV::TextJustification FormattedText::justification() const {
		return textJustification;
	}

	void FormattedText::wrapping(TextWrapMethod a_newWrapping, PointPrecision a_newWidth) {
		if (textWrapping != a_newWrapping || !equals(textWidth, a_newWidth)) {
			auto silenced = scene()->silence();
			textWidth = a_newWidth;
			textWrapping = a_newWrapping;
			for (auto&& line : lines) {
				line->fixVisuals();
			}
		}
	}

	void FormattedText::wrapping(TextWrapMethod a_newWrapping) {
		if (textWrapping != a_newWrapping) {
			auto silenced = scene()->silence();
			textWrapping = a_newWrapping;
			for (auto&& line : lines) {
				line->fixVisuals();
			}
		}
	}

	MV::TextWrapMethod FormattedText::wrapping() const {
		return textWrapping;
	}

	std::shared_ptr<Scene::Node> FormattedText::scene() const {
		return textScene;
	}

}