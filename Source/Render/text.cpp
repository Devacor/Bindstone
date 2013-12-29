#include "text.h"
#include <boost/algorithm/string.hpp>

namespace MV {

	Color parseColorString(std::string a_colorString){
		if(a_colorString == ""){return Color();}
		std::vector<std::string> colorStrings;
		boost::split(colorStrings, a_colorString, boost::is_any_of(":"));
		Color result;
		if(!colorStrings.empty()){
			if(colorStrings.size() >= 1){
				try{result.R = boost::lexical_cast<float>(colorStrings[0]);}catch(boost::bad_lexical_cast){result.R = 1.0;}
			}
			if(colorStrings.size() >= 2){
				try{result.G = boost::lexical_cast<float>(colorStrings[1]);}catch(boost::bad_lexical_cast){result.G = 1.0;}
			}
			if(colorStrings.size() >= 3){
				try{result.B = boost::lexical_cast<float>(colorStrings[2]);}catch(boost::bad_lexical_cast){result.B = 1.0;}
			}
			if(colorStrings.size() >= 4){
				try{result.A = boost::lexical_cast<float>(colorStrings[3]);}catch(boost::bad_lexical_cast){result.A = 1.0;}
			}
		}
		return result;
	}

	void parseExpression(const UtfString &a_expression, Color &a_currentColor, std::string &a_currentFontIdentifier, const std::string &a_defaultFontIdentifier){
		if(a_expression[0] == 'f'){
			a_currentFontIdentifier = wideToString(a_expression.substr(2));
			if(a_currentFontIdentifier == ""){a_currentFontIdentifier = a_defaultFontIdentifier;}
		}else if(a_expression[0] == 'c'){
			a_currentColor = parseColorString(wideToString(a_expression.substr(2)));
		}
	}

	std::vector<TextState> parseTextStateList(std::string a_defaultFontIdentifier, UtfString a_text){
		UtfString commit;
		Color currentColor;
		std::string currentFontIdentifier = a_defaultFontIdentifier;
		std::vector<TextState> textList;
		std::size_t found = 0, end;
		while(found != std::string::npos){
			found = std::min(a_text.find(UTF_CHAR_STR("[[f|")), a_text.find(UTF_CHAR_STR("[[c|")));
			commit = (a_text.substr(0, found));
			if(!commit.empty()){
				textList.push_back(TextState(commit, currentColor, currentFontIdentifier));
			}

			if(found != std::string::npos){
				end = a_text.find(UTF_CHAR_STR("]]"), found);
				if(end == std::string::npos){
					commit = a_text.substr(found);
					if(!commit.empty()){
						textList.push_back(TextState(a_text.substr(found), currentColor, currentFontIdentifier));
					}
					break;
				}

				parseExpression(a_text.substr(found+2, end-found-2), currentColor, currentFontIdentifier, a_defaultFontIdentifier);
				a_text = a_text.substr(end+2);
			}
		}

		return textList;
	}

	/*************************\
	| -----TextCharacter----- |
	\*************************/

	bool TextCharacter::isSet() const{
		return glyphTexture != nullptr;
	}

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

	void TextCharacter::setCharacter(std::shared_ptr<SurfaceTextureDefinition> a_texture, UtfChar a_glyphCharacter) {
		glyphTexture = a_texture;
		glyphHandle = glyphTexture->makeHandle();
		glyphHandle->setBounds(Point<int>(), glyphTexture->surfaceSize());
		glyphCharacter = a_glyphCharacter;
	}


	/*************************\
	| ----------Text--------- |
	\*************************/

	TextLibrary::TextLibrary( Draw2D *a_rendering ){
		require(a_rendering != nullptr, PointerException("TextLibrary::TextLibrary was passed a null Draw2D pointer."));
		white.r = 255; white.g = 255; white.b = 255; white.a = 0;
		render = a_rendering;
		if(!TTF_WasInit()){
			TTF_Init();
		}
	}

	bool TextLibrary::loadFont( std::string a_identifier, int a_pointSize, std::string a_fontFileLocation ){
		if(loadedFonts.find(a_identifier) == loadedFonts.end()){
			TTF_Font* newFont = TTF_OpenFont(a_fontFileLocation.c_str(), a_pointSize);
			if(newFont) {
				loadedFonts[a_identifier].font = newFont;
				return true;
			}else{
				std::cerr << "Error loading font: " << TTF_GetError() << std::endl;
			}
		}else{
			std::cerr << "Error, font identifier already used, cannot assign another font to the same id: " << a_identifier << std::endl;
		}
		return false;
	}

	//TODO: organize this method better.  This method is a beast.
	std::shared_ptr<Scene::Node> TextLibrary::composeScene(std::vector<TextState> a_textStateList, double a_maxWidth, TextWrapMethod a_wrapMethod){
		auto textScene = Scene::Node::make(render);
		if(a_textStateList.empty()){return textScene;}

		Color currentColor;
		int lineHeight, baseLine;
		int offset = 0;
		double characterLocationX = 0, nextCharacterLocationX = 0;
		double characterLocationY = 0;
		size_t characterCount = 0;
		size_t previousLine = 0;
		std::vector<UtfChar> currentLineContent;
		std::vector<double> currentLineCharacterSizes;
		for(auto current = a_textStateList.begin();current != a_textStateList.end();++current){
			auto specifiedFont = loadedFonts.find(current->fontIdentifier);
			currentColor = current->color;
			if(specifiedFont != loadedFonts.end()){
				CachedGlyphs *characterList = initGlyphs(current->fontIdentifier, current->text);
				if(characterCount == 0){
					lineHeight = TTF_FontLineSkip(specifiedFont->second.font);
					baseLine = TTF_FontAscent(specifiedFont->second.font);
				}else{
					int newLineHeight = TTF_FontLineSkip(specifiedFont->second.font);
					int newBaseLine = TTF_FontAscent(specifiedFont->second.font);
					if(newBaseLine > baseLine){
						for(size_t i = previousLine;i < characterCount;++i){
							textScene->get(boost::lexical_cast<std::string>(i))->translate(Point<>(0, (newBaseLine-baseLine)));
						}
						offset = 0;
						lineHeight = newLineHeight;
						baseLine = newBaseLine;
					}else{
						offset = baseLine-newBaseLine;
					}
				}

				for(auto renderChar = current->text.begin();renderChar != current->text.end();++renderChar){
					nextCharacterLocationX += (*characterList)[*renderChar].characterSize().width;
					bool lineWidthExceeded = (a_maxWidth!=0 && nextCharacterLocationX > a_maxWidth);
					if(*renderChar == '\n' || (lineWidthExceeded && a_wrapMethod != NONE)){
						characterLocationX = 0;
						nextCharacterLocationX = (!lineWidthExceeded)?0:(*characterList)[*renderChar].characterSize().width;
						characterLocationY+=lineHeight;
						offset = 0;
						lineHeight = TTF_FontLineSkip(specifiedFont->second.font);
						baseLine = TTF_FontAscent(specifiedFont->second.font);
						if(a_wrapMethod == SOFT && lineWidthExceeded && *renderChar != '\n'){
							auto lastSpace = std::find(currentLineContent.rbegin(), currentLineContent.rend(), UTF_CHAR_STR(' '));
							size_t distance = std::distance(currentLineContent.rbegin(), lastSpace);
							if(distance != currentLineContent.size()){
								previousLine = characterCount - distance;
								for(;distance > 0;--distance){
									auto renderedCharacter = textScene->get(boost::lexical_cast<std::string>(characterCount-distance));
									renderedCharacter->locate(Point<>(characterLocationX, renderedCharacter->getPosition().y + lineHeight));
									characterLocationX+=currentLineCharacterSizes[currentLineContent.size()-distance];
								}
								nextCharacterLocationX+=characterLocationX;
							}else{
								previousLine = characterCount;
							}
						}else{
							previousLine = characterCount;
						}
						
						currentLineContent.clear();
						currentLineCharacterSizes.clear();
					}
					if(*renderChar != '\n'){
						auto character = Scene::Rectangle::make(render, Point<>(), castSize<double>((*characterList)[*renderChar].characterSize()), false);
						character->locate(Point<>(characterLocationX, characterLocationY + offset));
						character->setTexture((*characterList)[*renderChar].texture());
						character->setColor(currentColor);
						textScene->add(boost::lexical_cast<std::string>(characterCount), character);
						characterLocationX = nextCharacterLocationX;
						currentLineContent.push_back(*renderChar);
						currentLineCharacterSizes.push_back((*characterList)[*renderChar].characterSize().width);
						characterCount++;
					}
				}
			}else{
				std::cerr << "A text scene was requested for a font identifier which is not loaded! (" << current->fontIdentifier << ")" << std::endl;
			}
		}
		return textScene;
	}

	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.
	void TextLibrary::reloadTextures(){
		std::for_each(cachedGlyphs.begin(), cachedGlyphs.end(), [&](std::pair<const std::string, CachedGlyphs> &glyphList){
			TTF_Font *currentFont = loadedFonts[glyphList.first].font;
			std::for_each(glyphList.second.begin(), glyphList.second.end(), [&](std::pair<const UtfChar, TextCharacter> &glyph){
				if(glyph.second.isSet()){
					Uint16 text[] = {static_cast<Uint16>(glyph.first), '\0'};
					glyph.second.setCharacter(
						SurfaceTextureDefinition::make("", [=](){
							Uint16 text[] = {static_cast<Uint16>(glyph.first), '\0'};
							return TTF_RenderUNICODE_Blended(currentFont, text, white);
						}),
						glyph.first
					);
				}
			});
		});
	}

	TextLibrary::CachedGlyphs* TextLibrary::initGlyphs( const std::string &a_identifier, const UtfString &a_text ){
		CachedGlyphs *characterList = &(cachedGlyphs[a_identifier]);
		loadIndividualGlyphs(a_identifier, a_text, *characterList);
		return characterList;
	}

	//SDL Supports 16 bit unicode characters, ensure glyph only ever contains characters of that size unless that changes.
	void TextLibrary::loadIndividualGlyphs( const std::string &a_identifier, const UtfString &a_text, CachedGlyphs &a_characterList ){
		TTF_Font* fontFace = loadedFonts[a_identifier].font;
		std::for_each(a_text.begin(), a_text.end(), [&](UtfChar renderChar){
			if(!a_characterList[renderChar].isSet()){
				a_characterList[renderChar].setCharacter(
					SurfaceTextureDefinition::make("", [=](){
						Uint16 text[] = {static_cast<Uint16>(renderChar), '\0'};
						return TTF_RenderUNICODE_Blended(fontFace, text, white); 
					}),
					renderChar
				);
			}
		});
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, Size<> a_size):
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		textScene(nullptr),
		boxSize(a_size){
		firstRun = true;
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, Size<> a_size):
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		textScene(nullptr),
		boxSize(a_size){
		firstRun = true;
	}

	TextBox::TextBox(TextLibrary *a_textLibrary, const std::string &a_fontIdentifier, const UtfString &a_text, Size<> a_size) :
		textLibrary(a_textLibrary),
		render(a_textLibrary->getRenderer()),
		fontIdentifier(a_fontIdentifier),
		textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
		textScene(nullptr),
		boxSize(a_size){
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
			//whothefuckknows?
			#pragma message( "WARNING: Figure out how SDL_TEXTEDITING works.")
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
			textScene = textboxScene->add("Text", textLibrary->composeScene(parseTextStateList(fontIdentifier, text), boxSize.width));
			textScene->locate(contentScrollPosition);
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
		textScene->locate(contentScrollPosition);
	}

}
