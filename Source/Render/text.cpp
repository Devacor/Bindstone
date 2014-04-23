#include "text.h"
#include <algorithm>
#include <boost/algorithm/string.hpp>

namespace MV {

	const UtfString COLOR_IDENTIFIER = UTF_CHAR_STR("[[c|");
	const UtfString FONT_IDENTIFIER = UTF_CHAR_STR("[[f|");
	const UtfString HEIGHT_IDENTIFIER = UTF_CHAR_STR("[[h|");

	Color parseColorString(const UtfString &a_colorString){
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

	void parseExpression(const UtfString &a_expression, Color &a_currentColor, std::string &a_currentFontIdentifier, const std::string &a_defaultFontIdentifier, int &a_currentLineHeight){
		if(a_expression[0] == 'f'){
			a_currentFontIdentifier = wideToString(a_expression.substr(2));
			if(a_currentFontIdentifier == ""){ a_currentFontIdentifier = a_defaultFontIdentifier; }
		} else if(a_expression[0] == 'c'){
			a_currentColor = parseColorString(a_expression.substr(2));
		} else if(a_expression[0] == 'h'){
			a_currentLineHeight = boost::lexical_cast<int>(a_expression.substr(2));
		}
	}

	std::vector<TextState> parseTextStateList(std::string a_defaultFontIdentifier, UtfString a_text){
		UtfString commit;
		Color currentColor;
		std::string currentFontIdentifier = a_defaultFontIdentifier;
		std::vector<TextState> textList;
		std::size_t found = 0, end;
		int currentLineHeight = -1;
		while(found != std::string::npos){
			found = std::min(std::min(a_text.find(UTF_CHAR_STR("[[f|")), a_text.find(UTF_CHAR_STR("[[c|"))), a_text.find(UTF_CHAR_STR("[[h|")));
			commit = (a_text.substr(0, found));
			if(!commit.empty()){
				textList.push_back(TextState(commit, currentColor, currentFontIdentifier, currentLineHeight));
			}

			if(found != std::string::npos){
				end = a_text.find(UTF_CHAR_STR("]]"), found);
				if(end == std::string::npos){
					commit = a_text.substr(found);
					if(!commit.empty()){
						textList.push_back(TextState(a_text.substr(found), currentColor, currentFontIdentifier, currentLineHeight));
					}
					break;
				}

				parseExpression(a_text.substr(found + 2, end - found - 2), currentColor, currentFontIdentifier, a_defaultFontIdentifier, currentLineHeight);
				a_text = a_text.substr(end + 2);
			}
		}

		return textList;
	}

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
		return glyphTexture->surfaceSize();
	}

	Size<int> TextCharacter::textureSize() const{
		return glyphTexture->size();
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

	bool TextLibrary::loadFont(const std::string &a_identifier, int a_pointSize, std::string a_fontFileLocation){
		auto found = loadedFonts.find(a_identifier);
		if(found == loadedFonts.end()){
			TTF_Font* newFont = TTF_OpenFont(a_fontFileLocation.c_str(), a_pointSize);
			if(newFont) {
				loadedFonts.insert({a_identifier, FontDefinition::make(this, a_fontFileLocation, a_pointSize, newFont)});
				return true;
			}else{
				std::cerr << "Error loading font: " << TTF_GetError() << std::endl;
			}
		}else{
			std::cerr << "Error, font identifier already used, cannot assign another font to the same id: " << a_identifier << std::endl;
		}
		return false;
	}
	
	std::shared_ptr<FormattedState> getColorTextState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState){
		if(a_text.empty()){
			return std::make_shared<FormattedState>(a_defaultTextState->color, a_currentTextState);
		}else{
			auto color = parseColorString(a_text);
			return std::make_shared<FormattedState>(color, a_currentTextState);
		}
	}

	std::shared_ptr<FormattedState> getFontTextState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState){
		if(a_text.empty()){
			return std::make_shared<FormattedState>(a_defaultTextState->font, a_currentTextState);
		}else{
			auto font = a_defaultTextState->font->library->fontDefinition(wideToString(a_text));
			if(font){
				return std::make_shared<FormattedState>(font, a_currentTextState);
			}else{
				return a_currentTextState;
			}
		}
	}

	std::shared_ptr<FormattedState> getHeightTextState(const UtfString &a_text, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState){
		if(a_text.empty()){
			return std::make_shared<FormattedState>(a_defaultTextState->minimumLineHeight, a_currentTextState);
		}else{
			int newHeight = -1;
			try{ newHeight = boost::lexical_cast<int>(a_text); } catch(boost::bad_lexical_cast){}
			return std::make_shared<FormattedState>(newHeight, a_currentTextState);
		}
	}

	std::shared_ptr<FormattedState> backwardGetTextState(const UtfString &a_text, size_t a_i, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState, std::pair<size_t, size_t> &o_range){
		o_range.first = 0; o_range.second = 0;
		if(a_i - 1 > 0 && a_text[a_i] == UTF_CHAR_STR(']') && a_text[a_i - 1] == UTF_CHAR_STR(']')){
			auto begin = a_text.rfind(COLOR_IDENTIFIER, a_i);
			if(begin == std::string::npos){
				begin = a_text.rfind(FONT_IDENTIFIER, a_i);
			} else{
				begin = a_text.rfind(HEIGHT_IDENTIFIER, a_i);
			}
			if(begin != std::string::npos){
				o_range.first = begin + 4;
				o_range.second = (begin + 4) - (a_i - 2);
				auto contents = a_text.substr(o_range.first, o_range.second);
				if(a_text[begin + 2] == UTF_CHAR_STR('c')){
					return getColorTextState(contents, a_currentTextState, a_defaultTextState);
				} else if(a_text[begin + 2] == UTF_CHAR_STR('f')){
					return getFontTextState(contents, a_currentTextState, a_defaultTextState);
				} else if(a_text[begin + 2] == UTF_CHAR_STR('h')){
					return getHeightTextState(contents, a_currentTextState, a_defaultTextState);
				}
			}
		}
		return nullptr;
	}

	std::shared_ptr<FormattedState> forwardGetTextState(const UtfString &a_text, size_t a_i, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState, std::pair<size_t, size_t> &o_range){
		o_range.first = 0; o_range.second = 0;
		auto symbol = a_text.substr(a_i, 4);
		if(symbol == COLOR_IDENTIFIER || symbol == FONT_IDENTIFIER || symbol == HEIGHT_IDENTIFIER){
			auto end = a_text.find(UTF_CHAR_STR("]]"), a_i + 4);
			if(end != std::string::npos){
				o_range.first = a_i + 4;
				o_range.second = end - (a_i + 4);
				auto contents = a_text.substr(o_range.first, o_range.second);
				if(symbol == COLOR_IDENTIFIER){
					return getColorTextState(contents, a_currentTextState, a_defaultTextState);
				} else if(symbol == FONT_IDENTIFIER){
					return getFontTextState(contents, a_currentTextState, a_defaultTextState);
				} else if(symbol == HEIGHT_IDENTIFIER){
					return getHeightTextState(contents, a_currentTextState, a_defaultTextState);
				}
			}
		}
		return nullptr;
	}
	
	std::shared_ptr<FormattedState> getTextState(const UtfString &a_text, size_t a_i, const std::shared_ptr<FormattedState> &a_currentTextState, const std::shared_ptr<FormattedState> &a_defaultTextState, std::pair<size_t, size_t> &o_range){
		std::shared_ptr<FormattedState> state;
		if(state = backwardGetTextState(a_text, a_i, a_currentTextState, a_defaultTextState, o_range)){
			return state;
		} else if(state = forwardGetTextState(a_text, a_i, a_currentTextState, a_defaultTextState, o_range)){
			return state;
		}
		return a_currentTextState;
	}

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

		Point<> getPositionForCharacter(size_t a_index){
			if(a_index > 0){
				double x = characters[a_index - 1]->shape->position().x + characters[a_index - 1]->characterSize().width;
				double y = characters[a_index - 1]->shape->position().y;
				if(characters[a_index - 1]->line != characters[a_index]->line || characters[a_index]->character->character() == UTF_CHAR_STR('\n')){
					x = 0.0;
					y += characters[a_index - 1]->line->lineHeight;
				}
				return point(x, y);
			} else if(!characters.empty()){
				return point(0.0, (characters[a_index]->character->character() == UTF_CHAR_STR('\n'))?
					static_cast<double>(characters[a_index]->line->lineHeight):
					0.0);
			} else{
				return point(0.0, 0.0);
			}
		}

		void positionCharacter(size_t a_index){
			auto position = getPositionForCharacter(a_index);
			if(position.x > width && wrapping != NONE){
				if(characters[a_index]->line->lineIndex == lines.size() - 1){
					lines.push_back(FormattedLine(characters[a_index], a_index, lines.size()));
					lines.back().lineHeight = std::max(defaultState->minimumLineHeight, static_cast<int>(characters[a_index]->characterSize().height));
				}
				if(wrapping == HARD){
					position.x = 0.0;
					position.y += characters[a_index]->line->lineHeight;
				}else if(wrapping == SOFT){
					//TODO!
				}
			}
			characters[a_index]->shape->position(position);
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
		std::vector<FormattedLine> lines;
		std::vector<std::shared_ptr<FormattedCharacter>> characters;
		std::shared_ptr<FormattedState> defaultState;
		UtfString raw;
		double width;
	};

	/*std::shared_ptr<Scene::Node> TextLibrary::composeScene(const std::vector<TextState> &a_textStateList, double a_maxWidth, TextWrapMethod a_wrapMethod, TextJustification a_justify, int a_minimumLineHeight, bool a_isSingleLine){
		UtfString text;
		std::vector<FormattedLine> lines;
		for(size_t i = 0; i < text.size(); ++i){
			getNewTextState(text, 
		}
	}*/

	void composeText(TextState &state){

	}

	//TODO: organize this method better.  This method is a beast.
	//Also, may want to allow editing to avoid having to do full textbox reconstruction!
	/*std::shared_ptr<Scene::Node> TextLibrary::composeScene(const std::vector<TextState> &a_textStateList, double a_maxWidth, TextWrapMethod a_wrapMethod, TextJustification a_justify, int a_minimumLineHeight, bool a_isSingleLine){
		auto textScene = Scene::Node::make(render);
		if(a_textStateList.empty()){return textScene;}

		Color currentColor;
		int lineHeight, baseLine;
		int offset = 0;
		double characterLocationX = 0, nextCharacterLocationX = 0;
		double characterLocationY = 0, characterLocationYOffset = 0;
		size_t characterCount = 0;
		size_t previousLine = 0;
		size_t oldPreviousLine = 0;
		std::vector<UtfChar> currentLineContent;
		std::vector<double> currentLineCharacterSizes;
		bool endOfAllLines = false;

		for(auto current = a_textStateList.begin(); current != a_textStateList.end() && !endOfAllLines; ++current){
			auto specifiedFont = loadedFonts.find(current->fontIdentifier);
			currentColor = current->color;
			if(specifiedFont != loadedFonts.end()){
				CachedGlyphs *characterList = initGlyphs(current->fontIdentifier, current->text);
				if(characterCount != 0){
					auto fontLineSkip = TTF_FontLineSkip(specifiedFont->second.font);
					lineHeight = std::max(
						(current->lineHeight == -1 ? TTF_FontLineSkip(specifiedFont->second.font) : current->lineHeight),
						a_minimumLineHeight
					);
					characterLocationYOffset = (lineHeight - fontLineSkip) / 2.0;
					baseLine = TTF_FontAscent(specifiedFont->second.font);
				}else{
					auto fontLineSkip = TTF_FontLineSkip(specifiedFont->second.font);
					int newLineHeight = std::max(
						(current->lineHeight == -1 ? TTF_FontLineSkip(specifiedFont->second.font) : current->lineHeight),
						a_minimumLineHeight
					);
					characterLocationYOffset = (newLineHeight - fontLineSkip) / 2.0;
					int newBaseLine = TTF_FontAscent(specifiedFont->second.font);
					if(newBaseLine > baseLine){
						for(size_t i = previousLine;i < characterCount;++i){
							textScene->get(boost::lexical_cast<std::string>(i))->translate(Point<>(0, (newBaseLine - baseLine + characterLocationYOffset)));
						}
						offset = 0;
						lineHeight = newLineHeight;
						baseLine = newBaseLine;
					}else{
						offset = baseLine-newBaseLine;
					}
				}

				for(auto renderChar = current->text.begin();renderChar != current->text.end();++renderChar){
					bool needJustify = false;
					nextCharacterLocationX += (*characterList)[*renderChar].characterSize().width;
					bool lineWidthExceeded = (!equals(a_maxWidth, 0.0) && nextCharacterLocationX > a_maxWidth);
					if(*renderChar == '\n' || (lineWidthExceeded && a_wrapMethod != NONE)){
						if(a_isSingleLine){
							endOfAllLines = true;
							break;
						}
						characterLocationX = 0;
						nextCharacterLocationX = (!lineWidthExceeded)?0:(*characterList)[*renderChar].characterSize().width;
						characterLocationY+=lineHeight;
						offset = 0;
						lineHeight = TTF_FontLineSkip(specifiedFont->second.font);
						baseLine = TTF_FontAscent(specifiedFont->second.font);
						size_t oldPreviousLine = previousLine;
						if(a_wrapMethod == SOFT && lineWidthExceeded && *renderChar != '\n'){
							auto lastSpace = std::find(currentLineContent.rbegin(), currentLineContent.rend(), UTF_CHAR_STR(' '));
							size_t distance = std::distance(currentLineContent.rbegin(), lastSpace);
							if(distance != currentLineContent.size()){
								previousLine = characterCount - distance;
								for(;distance > 0;--distance){
									auto renderedCharacter = textScene->get(boost::lexical_cast<std::string>(characterCount-distance));
									renderedCharacter->position(Point<>(characterLocationX, renderedCharacter->position().y + lineHeight + characterLocationYOffset));
									characterLocationX+=currentLineCharacterSizes[currentLineContent.size()-distance];
								}
								nextCharacterLocationX+=characterLocationX;
							}else{
								previousLine = characterCount;
							}
						}else{
							previousLine = characterCount;
						}
						needJustify = true;

						currentLineContent.clear();
						currentLineCharacterSizes.clear();
					}

					auto tmpRenderChar = renderChar;
					auto tmpCurrent = current;
					endOfAllLines = endOfAllLines || (++tmpCurrent == a_textStateList.end() && ++tmpRenderChar == current->text.end());

					if(*renderChar != '\n'){
						auto character = textScene->make<Scene::Rectangle>(std::to_string(characterCount), Point<>(), castSize<double>((*characterList)[*renderChar].characterSize()), false);
						character->position(Point<>(characterLocationX, characterLocationY + offset));
						character->texture((*characterList)[*renderChar].texture());
						character->color(currentColor);
						characterLocationX = nextCharacterLocationX;
						currentLineContent.push_back(*renderChar);
						currentLineCharacterSizes.push_back((*characterList)[*renderChar].characterSize().width);
						if(!endOfAllLines){
							characterCount++;
						}
					}

					if(endOfAllLines && !needJustify){
						needJustify = true;
						previousLine = characterCount;
					}

					if(needJustify){
						if(a_justify != LEFT){
							auto renderedCharacter = textScene->get(boost::lexical_cast<std::string>(previousLine));
							double lineWidth = renderedCharacter->position().x + renderedCharacter->basicAABB().size().width;
							double adjustBy = (a_maxWidth - lineWidth);
							adjustBy/= (a_justify == CENTER) ? 2.0 : 1.0;
							for(size_t i = oldPreviousLine; i <= previousLine; ++i){
								textScene->get(boost::lexical_cast<std::string>(i))->translate(point(adjustBy, 0.0));
							}
						}
					}
				}
			}else{
				std::cerr << "A text scene was requested for a font identifier which is not loaded! (" << current->fontIdentifier << ")" << std::endl;
			}
		}
		return textScene;
	}*/

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

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const Size<> &a_size):
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		textScene(nullptr),
		boxSize(a_size),
		minimumLineHeight(-1),
		isSingleLine(false){
		firstRun = true;
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, const Size<> &a_size) :
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		textScene(nullptr),
		boxSize(a_size),
		minimumLineHeight(-1),
		isSingleLine(false){
		firstRun = true;
		setText(a_text, a_fontIdentifier);
	}

	void TextBox::setText(const UtfString &a_text, const std::string &a_fontIdentifier){
		if(a_fontIdentifier != ""){fontIdentifier = a_fontIdentifier;}
		text = a_text;

		refreshTextBoxContents();
	}

	bool TextBox::setText( SDL_Event &event ){
		if(event.type == SDL_TEXTINPUT){
			appendText(stringToWide(event.text.text));
		} else if (event.type == SDL_TEXTEDITING) {
			setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
		} else if(event.type == SDL_KEYDOWN){
			if(event.key.keysym.sym == SDLK_BACKSPACE && text.length() > 0){
				text.pop_back();
				refreshTextBoxContents();
			}else if(event.key.keysym.sym == SDLK_v && SDL_GetModState() & KMOD_CTRL){
				appendText(stringToWide(SDL_GetClipboardText()));
			}
		}
		return false;
	}

	void TextBox::setTextBoxSize( Size<> a_size ){
		boxSize = a_size;
		textboxScene->setSize(a_size);

		refreshTextBoxContents();
	}

	void TextBox::refreshTextBoxContents(){
		if(fontIdentifier != ""){
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

	void TextBox::draw() {
		textboxScene->draw();
	}

	std::shared_ptr<TextCharacter> FontDefinition::getCharacter(UtfChar renderChar) {
		std::shared_ptr<TextCharacter> &character = cachedGlyphs[renderChar];
		if(!character){
			character = TextCharacter::make(
				SurfaceTextureDefinition::make("", [=](){
					Uint16 text[] = {static_cast<Uint16>(renderChar), '\0'};
					SDL_Color white;
					white.r = 255; white.g = 255; white.b = 255; white.a = 0;
					return TTF_RenderUNICODE_Blended(font, text, white);
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
		color(a_currentState->color),
		minimumLineHeight(a_currentState->minimumLineHeight) {
	}

	FormattedState::FormattedState(const Color &a_color, const std::shared_ptr<FormattedState> &a_currentState):
		font(a_currentState->font),
		color(a_color),
		minimumLineHeight(a_currentState->minimumLineHeight) {
	}

	FormattedState::FormattedState(int a_minimumLineHeight, const std::shared_ptr<FormattedState> &a_currentState):
		font(a_currentState->font),
		color(a_currentState->color),
		minimumLineHeight(a_minimumLineHeight) {
	}


	FormattedLine::FormattedLine(const std::shared_ptr<FormattedCharacter> &a_firstCharacter, size_t a_characterIndex, size_t a_lineIndex):
		firstCharacterIndex(a_characterIndex),
		lastCharacterIndex(a_characterIndex),
		lineIndex(a_lineIndex),
		lineHeight(std::max(TTF_FontLineSkip(a_firstCharacter->character->font()->font), a_firstCharacter->state->minimumLineHeight)),
		baseLine(TTF_FontAscent(a_firstCharacter->character->font()->font)){
	}

}
