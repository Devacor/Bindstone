#include "text.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>

namespace MV {

	/*************************\
	| -----TextCharacter----- |
	\*************************/

	UtfChar TextCharacter::character() const{
		return glyphCharacter;
	}

	std::shared_ptr<TextureHandle> TextCharacter::texture() const{
		return glyphHandle;
	}

	Size<int> TextCharacter::characterSize() const{
		return (glyphTexture) ? glyphTexture->surfaceSize() : Size<int>(0, 0);
	}

	Size<int> TextCharacter::textureSize() const{
		return (glyphTexture) ? glyphTexture->size() : Size<int>(0, 0);
	}

	TextCharacter::TextCharacter(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition):
		glyphTexture(a_texture),
		glyphHandle(a_texture->makeHandle()),
		glyphCharacter(a_glyphCharacter),
		fontDefinition(a_fontDefinition){

		glyphHandle->setBounds(Point<int>(), glyphTexture->surfaceSize());
	}

	bool TextCharacter::isSoftBreakCharacter() {
		return glyphCharacter == ' ' || glyphCharacter == '-';
	}

	std::shared_ptr<FontDefinition> TextCharacter::font() const {
		return fontDefinition;
	}

	std::shared_ptr<TextCharacter> TextCharacter::make(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter, std::shared_ptr<FontDefinition> a_fontDefinition) {
		return std::shared_ptr<TextCharacter>(new TextCharacter(a_texture, a_glyphCharacter, a_fontDefinition));
	}





	/*************************\
	| ----------Text--------- |
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


	TextBox::TextBox(TextLibrary *a_textLibrary, const Size<> &a_size):
		TextBox(a_textLibrary, "default", a_size){
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const Size<> &a_size) :
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		//textboxScene(Scene::Node::make(a_textLibrary->getRenderer())),
		textScene(nullptr),
		boxSize(a_size),
		isSingleLine(false),
		formattedText(*a_textLibrary, a_size.width, a_fontIdentifier){
		firstRun = true;
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, const Size<> &a_size):
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		//textboxScene(Scene::Node::make(a_textLibrary->getRenderer())),
		textScene(nullptr),
		boxSize(a_size),
		isSingleLine(false),
		formattedText(*a_textLibrary, a_size.width, a_fontIdentifier){
		firstRun = true;
		setText(a_text, a_fontIdentifier);
	}

	void TextBox::setText(const UtfString &a_text, const std::string &a_fontIdentifier){
		if(a_fontIdentifier != ""){ fontIdentifier = a_fontIdentifier; }
		text = a_text;

		refreshTextBoxContents();
	}

	bool TextBox::setText(SDL_Event &event){
		if(event.type == SDL_TEXTINPUT){
			appendText(stringToWide(event.text.text));
		} else if(event.type == SDL_TEXTEDITING) {
			setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
		} else if(event.type == SDL_KEYDOWN){
			if(event.key.keysym.sym == SDLK_BACKSPACE && text.length() > 0){
				text.pop_back();
				refreshTextBoxContents();
			} else if(event.key.keysym.sym == SDLK_v && SDL_GetModState() & KMOD_CTRL){
				appendText(stringToWide(SDL_GetClipboardText()));
			}
		}
		return false;
	}

	void TextBox::setTextBoxSize(Size<> a_size){
		boxSize = a_size;
		textboxScene->setSize(a_size);
		formattedText.textWidth = a_size.width;
		refreshTextBoxContents();
	}

	void TextBox::refreshTextBoxContents(){
		if(fontIdentifier != ""){
			formattedText.append(text);
			textScene = textboxScene->add("Text", formattedText.scene);
			//TODO
			//textScene = textboxScene->add("Text", textLibrary->composeScene(parseTextStateList(fontIdentifier, currentTextContents()), boxSize.width, wrapMethod, textJustification, minimumLineHeight, isSingleLine));
			//textScene->position(contentScrollPosition);
		} else{
			std::cerr << "Warning: refreshTextBoxContents called, but no fontIdentifier has been set yet!" << std::endl;
		}
	}

	void TextBox::setScrollPosition(Point<> a_position, bool a_overScroll /*= false*/) {
		if(!a_overScroll){
			auto contentSize = getContentSize();
			if(contentSize.height < boxSize.height){
				a_position.y = 0;
			} else if(a_position.y < -(contentSize.height - boxSize.height)){
				a_position.y = -(contentSize.height - boxSize.height);
			} else if(a_position.y > 0){
				a_position.y = 0;
			}

			a_position.x = 0;
		}
		contentScrollPosition = a_position;
		textScene->position(contentScrollPosition);
	}

	Size<> TextBox::getContentSize() {
		return textScene->localAABB().size();
	}

	std::shared_ptr<Scene::Node> TextBox::scene() {
		return textboxScene;
	}

	std::shared_ptr<TextCharacter> FontDefinition::getCharacter(UtfChar renderChar) {
		std::shared_ptr<TextCharacter> &character = cachedGlyphs[renderChar];
		if(!character){
			character = TextCharacter::make(
				SurfaceTextureDefinition::make("", [=](){
					Uint16 text[] = {static_cast<Uint16>(renderChar), '\0'};
					return TTF_RenderUNICODE_Blended(font, text, {255, 255, 255, 255});
				}),
				renderChar,
				shared_from_this()
			);
		}
		return character;
	}


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

	void FormattedLine::addCharacters(size_t a_characterIndex, const std::vector<std::shared_ptr<FormattedCharacter>> &a_characters){
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
					text.lines[lineIndex + 1]->addCharacters(0, overflow);
					exceeded = true;
					break;
				}
			}

			if(!exceeded && lineIndex+1 < text.lines.size()){
				MV::PointPrecision widthRemaining = text.textWidth - lineWidth();
				addCharacters(characters.size(), text.lines[lineIndex + 1]->removeLeadingCharactersForWidth(widthRemaining));
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
		if(text.wrapping == NONE){
			return result;
		}

		for(int i = 0; i < characters.size() && a_width - characters[i]->characterSize().width > 0.0f; ++i){
			result.push_back(characters[i]);
			a_width -= characters[i]->characterSize().width;
		}

		if(text.wrapping == SOFT && !result.empty()){
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

	std::vector<std::shared_ptr<FormattedCharacter>> FormattedLine::removeCharacters(size_t a_characterIndex, size_t a_totalToRemove) {
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




	std::shared_ptr<FormattedLine>& FormattedText::operator[](size_t a_index) {
		MV::require(a_index < lines.size(), MV::RangeException("FormattedText::operator[] supplied invalid index (" + std::to_string(a_index) + " > " + std::to_string(lines.size()) + ")"));
		return lines[a_index];
	}

	FormattedText::FormattedText(TextLibrary &a_library, PointPrecision a_width, const std::string &a_defaultStateIdentifier, TextWrapMethod a_wrapping, TextJustification a_justification):
		library(a_library),
		textWidth(a_width),
		defaultTextState(std::make_shared<FormattedState>(a_library.fontDefinition(a_defaultStateIdentifier))),
		wrapping(a_wrapping),
		textJustification(a_justification),
		minimumTextLineHeight(-1){

		scene = Scene::Node::make(library.getRenderer());
	}

	PointPrecision FormattedText::width(PointPrecision a_width) {
		textWidth = a_width;
		return textWidth;
	}

	PointPrecision FormattedText::positionForLine(size_t a_index) {
		a_index = std::min(a_index, lines.size());
		return std::accumulate(lines.begin(), lines.begin()+a_index, 0.0f, [](PointPrecision a_accumulated, const std::shared_ptr<FormattedLine> &a_line){
			return a_line->height() + a_accumulated;
		});
	}

	std::shared_ptr<FormattedState> FormattedText::defaultState(const std::string &a_defaultStateIdentifier) {
		defaultTextState = std::make_shared<FormattedState>(library.fontDefinition(a_defaultStateIdentifier));
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

}
