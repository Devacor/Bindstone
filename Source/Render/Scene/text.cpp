#include "text.h"

CEREAL_REGISTER_TYPE(MV::Scene::Text);
namespace MV{
	namespace Scene {
		/*************************\
		| ----------Text--------- |
		\*************************/

		std::shared_ptr<Text> Text::make(Draw2D* a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size) {
			auto text = std::shared_ptr<Text>(new Text(a_renderer, a_textLibrary, a_size, DEFAULT_ID));
			text->add("TextBox", text->textboxScene);
			a_renderer->registerShader(text);
			return text;
		}

		std::shared_ptr<Text> Text::make(Draw2D* a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size, const std::string &a_fontIdentifier) {
			auto text = std::shared_ptr<Text>(new Text(a_renderer, a_textLibrary, a_size, a_fontIdentifier));
			text->add("TextBox", text->textboxScene);
			a_renderer->registerShader(text);
			return text;
		}

		Text::Text(Draw2D *a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size, const std::string &a_fontIdentifier):
			Node(a_renderer),
			textLibrary(a_textLibrary),
			render(a_textLibrary->getRenderer()),
			fontIdentifier(a_fontIdentifier),
			textboxScene(Scene::Clipped::make(a_textLibrary->getRenderer(), a_size)),
			textScene(nullptr),
			boxSize(a_size),
			isSingleLine(false),
			formattedText(*a_textLibrary, a_size.width, a_fontIdentifier),
			cursor(0),
			displayCursor(false),
			onEnter(onEnterSlot){
			
			textScene = textboxScene->make<Scene::Node>("ScrollPortion");
			textScene->add("Text", formattedText.scene());
			cursorScene = textScene->make<Scene::Rectangle>("Cursor", size(2.0f, 12.0f))->hide();
			setCursor(0);
		}

		std::shared_ptr<Text> Text::text(const UtfString &a_text, const std::string &a_fontIdentifier){
			if(a_fontIdentifier != ""){ fontIdentifier = a_fontIdentifier; }
			formattedText.clear();
			formattedText.append(a_text);
			setCursor(a_text.size());
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		bool Text::text(SDL_Event &event){
			if(event.type == SDL_TEXTINPUT){
				insertAtCursor(stringToWide(event.text.text));
				return true;
			} else if(event.type == SDL_TEXTEDITING) {
				setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
			} else if(event.type == SDL_KEYDOWN){
				if(event.key.keysym.sym == SDLK_BACKSPACE && !formattedText.empty()){
					backspace();
					return true;
				}if(event.key.keysym.sym == SDLK_DELETE && !formattedText.empty() && cursor < formattedText.size()){
					++cursor; //this is okay because backspace will reposition the cursor.
					backspace();
					return true;
				} else if(event.key.keysym.sym == SDLK_v && SDL_GetModState() & KMOD_CTRL){
					append(stringToWide(SDL_GetClipboardText()));
					return true;
				} else if(event.key.keysym.sym == SDLK_c && SDL_GetModState() & KMOD_CTRL){
					SDL_SetClipboardText(wideToString(text()).c_str());
				} else if(event.key.keysym.sym == SDLK_LEFT){
					if(cursor > 0){
						incrementCursor(-1);
					}
				} else if(event.key.keysym.sym == SDLK_RIGHT){
					if(cursor < formattedText.size()){
						incrementCursor(1);
					}
				} else if(event.key.keysym.sym == SDLK_RETURN){
					onEnterSlot(std::static_pointer_cast<Text>(shared_from_this()));
				}
			}
			return false;
		}

		void Text::textBoxSize(Size<> a_size){
			boxSize = a_size;
			textboxScene->size(a_size);
			formattedText.width(a_size.width);
		}

		void Text::scrollPosition(Point<> a_position, bool a_overScroll /*= false*/) {
			if(!a_overScroll){
				auto ourContentSize = contentSize();
				if(ourContentSize.height < boxSize.height){
					a_position.y = 0;
				} else if(a_position.y < -(ourContentSize.height - boxSize.height)){
					a_position.y = -(ourContentSize.height - boxSize.height);
				} else if(a_position.y > 0){
					a_position.y = 0;
				}

				a_position.x = 0;
			}
			contentScrollPosition = a_position;
			textScene->position(contentScrollPosition);
		}

		Size<> Text::contentSize() const {
			return textScene->localAABB().size();
		}

		void Text::setCursor(int64_t a_value) {
			auto maxCursor = formattedText.size();
			a_value = std::max<int64_t>(std::min<int64_t>(a_value, maxCursor), 0);
			cursor = a_value;
			auto cursorCharacter = (cursor < maxCursor || cursor == 0) ? formattedText.characterForIndex(cursor) : formattedText.characterForIndex(cursor - 1);
			if(cursorCharacter){
				positionCursorWithCharacter(maxCursor, cursorCharacter);
			} else{
				positionCursorWithoutCharacter();
			}
		}

		void Text::positionCursorWithCharacter(size_t a_maxCursor, std::shared_ptr<FormattedCharacter> a_cursorCharacter) {
			cursorScene->position(a_cursorCharacter->position() + a_cursorCharacter->offset());
			cursorScene->size({2, a_cursorCharacter->characterSize().height});
			if(cursor >= a_maxCursor) {
				cursorScene->translate({a_cursorCharacter->characterSize().width, 0.0f});
			}
			if(displayCursor){
				cursorScene->show();
			}
		}

		void Text::positionCursorWithoutCharacter() {
			std::shared_ptr<FormattedLine> line;
			size_t characterIndex;
			std::tie(line, characterIndex) = formattedText.lineForCharacterIndex(cursor);
			float xPosition = 0.0f;
			if(justification() == TextJustification::CENTER){
				xPosition = formattedText.width() / 2.0f - 1.0f;
			} else if(justification() == TextJustification::RIGHT){
				xPosition = formattedText.width() - 2.0f;
			}
			auto cursorHeight = formattedText.defaultState()->font->height();
			auto linePositionY = formattedText.positionForLine(line->index());
			auto cursorLineHeight = std::max<float>(formattedText.minimumLineHeight(), (line) ? line->height() : cursorHeight);
			linePositionY += cursorLineHeight / 2.0f - cursorHeight / 2.0f;

			cursorScene->position({xPosition, linePositionY});
			cursorScene->size({2, cursorHeight});

			if(displayCursor){
				cursorScene->show();
			}
		}

		void Text::incrementCursor(int64_t a_change) {
			setCursor(cursor + a_change);
		}

	}
}