#include "formattedText.h"
#include "Render/Scene/package.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>

namespace MV {

	/*************************\
	| -----FontDefinition---- |
	\*************************/
	
	std::shared_ptr<CharacterDefinition> FontDefinition::characterDefinition(UtfChar renderChar) {
		std::shared_ptr<CharacterDefinition> &character = cachedGlyphs[renderChar];
		if(!character){
			character = CharacterDefinition::make(
				SurfaceTextureDefinition::make(wideToString(renderChar), [=](){
					Uint16 text[] = {static_cast<Uint16>(renderChar), '\0'};
					return TTF_RenderUNICODE_Blended(font, text, {255, 255, 255, 255});
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

	std::shared_ptr<CharacterDefinition> CharacterDefinition::make(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition) {
		return std::shared_ptr<CharacterDefinition>(new CharacterDefinition(a_texture, a_glyphCharacter, a_fontDefinition));
	}
	
	CharacterDefinition::CharacterDefinition(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition):
		glyphTexture(a_texture),
		glyphHandle(a_texture->makeHandle()),
		glyphCharacter(a_glyphCharacter),
		fontDefinition(a_fontDefinition){

		glyphHandle->setBounds(Point<int>(), glyphTexture->surfaceSize());
	}

	UtfChar CharacterDefinition::character() const{
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
		return glyphCharacter == ' ' || glyphCharacter == '-';
	}

	std::shared_ptr<FontDefinition> CharacterDefinition::font() const {
		return fontDefinition;
	}




	/*************************\
	| ------TextLibrary------ |
	\*************************/

	TextLibrary::TextLibrary(Draw2D *a_rendering){
		require(a_rendering != nullptr, PointerException("TextLibrary::TextLibrary was passed a null Draw2D pointer."));
		SDL_StartTextInput();
		white.r = 255; white.g = 255; white.b = 255; white.a = 0;
		render = a_rendering;
		if(!TTF_WasInit()){
			TTF_Init();
		}
	}

	bool TextLibrary::loadFont(const std::string &a_identifier, std::string a_fontFileLocation, int a_pointSize, FontStyle a_styleFlags){
		auto found = loadedFonts.find(a_identifier);
		if(found == loadedFonts.end()){
			TTF_Font* newFont = TTF_OpenFont(a_fontFileLocation.c_str(), a_pointSize);
			TTF_SetFontHinting(newFont, TTF_HINTING_NORMAL);
			TTF_SetFontStyle(newFont, static_cast<int>(a_styleFlags));
			if(newFont) {
				loadedFonts.insert({a_identifier, FontDefinition::make(this, a_fontFileLocation, a_pointSize, newFont)});
				return true;
			} else{
				std::cerr << "Error loading font: " << TTF_GetError() << std::endl;
			}
		} else{
			std::cerr << "Error, font identifier already used, cannot assign another font to the same id: " << a_identifier << std::endl;
		}
		return false;
	}

	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.

	std::shared_ptr<FontDefinition> TextLibrary::fontDefinition(const std::string &a_identifier) const {
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
		color((a_currentState) ? a_currentState->color : Color(1, 1, 1)),
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

	std::shared_ptr<FormattedCharacter> FormattedCharacter::make(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state) {
		return std::shared_ptr<FormattedCharacter>(new FormattedCharacter(parent, a_character, a_state));
	}

	Size<> FormattedCharacter::characterSize() const {
		if(isPartOfFormat){
			return{0.0, static_cast<PointPrecision>(character->characterSize().height)};
		} else{
			return castSize<PointPrecision>(character->characterSize());
		}
	}

	Point<> FormattedCharacter::position() const {
		return basePosition;
	}

	Point<> FormattedCharacter::position(const Point<> &a_newPosition) {
		basePosition = a_newPosition;
		shape->position(basePosition + offsetPosition);
		return basePosition;
	}

	Point<> FormattedCharacter::offset() const {
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(const Point<> &a_newPosition) {
		offsetPosition = a_newPosition;
		shape->position(basePosition + offsetPosition);
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(PointPrecision a_lineHeight, PointPrecision a_baseLine) {
		PointPrecision height = character->font()->height();
		PointPrecision base = character->font()->base();
		offset({offsetPosition.x, a_baseLine - base});
		shape->position(basePosition + offsetPosition);
		return offsetPosition;
	}

	Point<> FormattedCharacter::offset(PointPrecision a_x, PointPrecision a_lineHeight, PointPrecision a_baseLine) {
		PointPrecision height = character->font()->height();
		PointPrecision base = character->font()->base();
		offset({a_x, a_baseLine - base});
		shape->position(basePosition + offsetPosition);
		return offsetPosition;
	}

	void FormattedCharacter::applyState(const std::shared_ptr<FormattedState> &a_state) {
		state = a_state;
		character = state->font->characterDefinition(textCharacter);
		shape->size(castSize<PointPrecision>(character->characterSize()));
		shape->texture(character->texture());
		shape->color(state->color);
	}

	bool FormattedCharacter::partOfFormat(bool a_isPartOfFormat) {
		isPartOfFormat = a_isPartOfFormat;
		if(isPartOfFormat){
			shape->hide();
		} else{
			shape->show();
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

	FormattedCharacter::FormattedCharacter(const std::shared_ptr<Scene::Node> &parent, UtfChar a_character, const std::shared_ptr<FormattedState> &a_state):
		textCharacter(a_character),
		state(a_state),
		character(a_state->font->characterDefinition(a_character)) {

		shape = parent->make<Scene::Rectangle>(guid(wideToString(character->character())), castSize<PointPrecision>(character->characterSize()))->
			texture(character->texture())->
			color(state->color);
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
				try{ result.R = boost::lexical_cast<float>(colorStrings[0]); } catch(boost::bad_lexical_cast){ result.R = 1.0; }
			}
			if(colorStrings.size() >= 2){
				try{ result.G = boost::lexical_cast<float>(colorStrings[1]); } catch(boost::bad_lexical_cast){ result.G = 1.0; }
			}
			if(colorStrings.size() >= 3){
				try{ result.B = boost::lexical_cast<float>(colorStrings[2]); } catch(boost::bad_lexical_cast){ result.B = 1.0; }
			}
			if(colorStrings.size() >= 4){
				try{ result.A = boost::lexical_cast<float>(colorStrings[3]); } catch(boost::bad_lexical_cast){ result.A = 1.0; }
			}
		}
		return result;
	}

	std::shared_ptr<FormattedState> FormattedLine::getHeightState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string heightString = wideToString(a_text.substr(2));
		if(heightString.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->minimumLineHeight, a_current);
		} else{
			PointPrecision newMinimumHeight = 0;
			try{ newMinimumHeight = boost::lexical_cast<PointPrecision>(heightString); } catch(boost::bad_lexical_cast){ newMinimumHeight = 0.0; }
			return std::make_shared<FormattedState>(newMinimumHeight, a_current);
		}
	}

	std::shared_ptr<FormattedState> FormattedLine::getColorState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string colorString = wideToString(a_text.substr(2));
		if(colorString.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->color, a_current);
		} else{
			return std::make_shared<FormattedState>(parseColorString(colorString), a_current);
		}
	}

	std::shared_ptr<FormattedState> FormattedLine::getFontState(const UtfString &a_text, const std::shared_ptr<FormattedState> & a_current) {
		std::string fontIdentifier = wideToString(a_text.substr(2));
		if(fontIdentifier.empty()){
			return std::make_shared<FormattedState>(text.defaultState()->font, a_current);
		} else{
			return std::make_shared<FormattedState>(text.library.fontDefinition(fontIdentifier), a_current);
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
				if(characters[i]->textCharacter == UTF_CHAR_STR(']')){
					auto previous = text.characterRelativeTo(lineIndex, i, -1); 
					if(previous && previous->textCharacter == UTF_CHAR_STR(']')){
						UtfString content;
						previous = text.characterRelativeTo(lineIndex, i, -2);
						auto previous2 = text.characterRelativeTo(lineIndex, i, -3);
						int64_t startOfNewState = -3;
						for(size_t total = 0; previous && previous2 && (previous->textCharacter != UTF_CHAR_STR('[') && previous->textCharacter != UTF_CHAR_STR('[')) && total < 32; --startOfNewState, ++total){
							content += previous->textCharacter;
							previous = text.characterRelativeTo(lineIndex, i, startOfNewState);
							previous2 = text.characterRelativeTo(lineIndex, i, startOfNewState-1);
						}
						if(!content.empty() && previous->textCharacter == UTF_CHAR_STR('[') && previous2->textCharacter == UTF_CHAR_STR('[')){
							std::reverse(content.begin(), content.end());
							auto newState = getNewState(content, previous2->state);
							if(newState != previous2->state){
								text.applyState(newState, text.absoluteIndexFromRelativeTo(lineIndex, i, startOfNewState), text.absoluteIndex(lineIndex, i));
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

	void FormattedLine::applyAlignment(){
		float width = lineWidth();
		float offset = 0;
		if(text.justification() == CENTER){
			offset = (text.width() - width) / 2.0f;
		}else if(text.justification() == RIGHT){
			offset = text.width() - width;
		}
		for(size_t index = 0; index < characters.size(); ++index){
			characters[index]->offset({offset, characters[index]->offset().y});
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
				
				if(index > 0 && text.exceedsWidth(characters[index]->position().x)){
					std::vector<std::shared_ptr<FormattedCharacter>> overflow(characters.begin() + index-1, characters.end());
					characters.erase(characters.begin() + index-1, characters.end());
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
			}else if(text.justification() != LEFT){
				applyAlignment();
			}
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
		MV::require(a_index < characters.size(), MV::RangeException("FormattedLine::operator[] supplied invalid index (" + std::to_string(a_index) + " > " + std::to_string(characters.size()) + ")"));
		return characters[a_index];
	}

	std::vector<std::shared_ptr<FormattedCharacter>> FormattedLine::removeLeadingCharactersForWidth(float a_width) {
		std::vector<std::shared_ptr<FormattedCharacter>> result;
		if(text.textWrapping == NONE){
			return result;
		}

		for(int i = 0; i < characters.size() && a_width - characters[i]->characterSize().width > 0.0f; ++i){
			result.push_back(characters[i]);
			a_width -= characters[i]->characterSize().width;
		}

		if(text.textWrapping == SOFT && !result.empty()){
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
		MV::require(a_index < lines.size(), MV::RangeException("FormattedText::operator[] supplied invalid index (" + std::to_string(a_index) + " > " + std::to_string(lines.size()) + ")"));
		return lines[a_index];
	}

	FormattedText::FormattedText(TextLibrary &a_library, PointPrecision a_width, const std::string &a_defaultStateIdentifier, TextWrapMethod a_wrapping, TextJustification a_justification):
		library(a_library),
		textWidth(a_width),
		defaultTextState(std::make_shared<FormattedState>(a_library.fontDefinition(a_defaultStateIdentifier))),
		textWrapping(a_wrapping),
		textJustification(a_justification),
		minimumTextLineHeight(-1){

		textScene = Scene::Node::make(library.getRenderer())->blockSerialize();
	}

	PointPrecision FormattedText::width(PointPrecision a_width) {
		textWidth = a_width;
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

	std::shared_ptr<FormattedState> FormattedText::defaultState(const std::string &a_defaultStateIdentifier) {
		defaultTextState = std::make_shared<FormattedState>(library.fontDefinition(a_defaultStateIdentifier));
		return defaultTextState;
	}

	std::shared_ptr<FormattedState> FormattedText::defaultState() const {
		return defaultTextState;
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

	bool FormattedText::exceedsWidth(PointPrecision a_xPosition) const {
		return a_xPosition > textWidth;
	}

	size_t FormattedText::size() const {
		return std::accumulate(lines.begin(), lines.end(), static_cast<size_t>(0), [](size_t a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return a_line->size() + a_accumulated;
		});;
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
		std::shared_ptr<FormattedState> originalState;
		size_t i = a_newFormatStart;
		for(auto character = characterForIndex(i); character; ++i, character = characterForIndex(i)){
			if(i == a_newFormatStart){
				originalState = character->state;
			} else if(character->state != originalState){
				break;
			}
			character->partOfFormat(i <= a_newFormatEnd);
			character->applyState(a_newState);
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

	void FormattedText::erase(size_t a_startIndex, size_t a_count) {
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

	void FormattedText::insert(size_t a_startIndex, const UtfString &a_characters) {
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
			} else if(line->index() > 0 && !lines[line->index() - 1]->empty()){
				auto previousLine = lines[line->index() - 1];
				foundState = (*previousLine)[previousLine->size() - 1]->state;
			}
		}

		std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
		for(const UtfChar &character : a_characters){
			formattedCharacters.push_back(FormattedCharacter::make(textScene, character, foundState));
		}

		line->insert(characterInLineIndex, formattedCharacters);
	}

	void FormattedText::insert(size_t a_startIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters) {
		if(a_characters.empty()){
			return;
		}
		std::shared_ptr<FormattedLine> line;
		size_t characterInLineIndex;
		std::tie(line, characterInLineIndex) = lineForCharacterIndex(a_startIndex);
		line->insert(a_startIndex, a_characters);
	}

	void FormattedText::append(const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters) {
		if(a_characters.empty()){
			return;
		}
		if(lines.empty()){
			lines.push_back(FormattedLine::make(*this, lines.size()));
		}
		std::shared_ptr<FormattedLine> line = lines.back();
		line->insert(line->size(), a_characters);
	}

	void FormattedText::append(const UtfString &a_characters) {
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
				foundState = (*previousLine)[previousLine->size() - 1]->state;
			}
		}

		std::vector<std::shared_ptr<FormattedCharacter>> formattedCharacters;
		for(const UtfChar &character : a_characters){
			formattedCharacters.push_back(FormattedCharacter::make(textScene, character, foundState));
		}

		std::shared_ptr<FormattedLine> line = lines.back();
		line->insert(line->size(), formattedCharacters);
	}

	MV::PointPrecision FormattedText::minimumLineHeight() const {
		return minimumTextLineHeight;
	}

	MV::PointPrecision FormattedText::minimumLineHeight(PointPrecision a_minimumLineHeight) {
		minimumTextLineHeight = a_minimumLineHeight;
		for(auto line : lines){
			line->minimumLineHeightChanged();
		}
		return minimumTextLineHeight;
	}

	void FormattedText::justification(TextJustification a_newJustification) {
		if(textJustification != a_newJustification){
			textJustification = a_newJustification;
			for(auto &line : lines){
				line->applyAlignment();
			}
		}
	}

	MV::TextJustification FormattedText::justification() const {
		return textJustification;
	}

	void FormattedText::wrapping(TextWrapMethod a_newWrapping) {
		textWrapping = a_newWrapping;
		//TODO: Need to update wrapping.
	}

	MV::TextWrapMethod FormattedText::wrapping() const {
		return textWrapping;
	}

	std::shared_ptr<Scene::Node> FormattedText::scene() const {
		return textScene;
	}

}