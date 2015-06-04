#include "text.h"
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Text);
namespace MV{
	namespace Scene {

		const double Text::BLINK_DURATION = .35;


		Text::Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const Size<> &a_size, const std::string &a_defaultFontIdentifier) :
			Drawable(a_owner),
			textLibrary(a_textLibrary),
			onEnter(onEnterSlot),
			fontIdentifier(a_defaultFontIdentifier),
			formattedText(a_textLibrary, a_size.width, DEFAULT_ID),
			boxSize(a_size) {
			
			points.resize(4);
			for (auto&& point : points) {
				point = Color(1.0f, 1.0f, 1.0f, 0.0f);
			}
			clearTexturePoints(points);
			appendQuadVertexIndices(vertexIndices, 0);

			formattedText.scene()->id(guid("TEXT_"));
			owner()->add(formattedText.scene());

			cursorScene = owner()->make(guid("CURSOR_"))->attach<Sprite>()->size({ 1.0f, 5.0f });
			cursorScene->hide();
		}

		void Text::positionCursorWithoutCharacter() {
			std::shared_ptr<FormattedLine> line;
			size_t characterIndex;
			std::tie(line, characterIndex) = formattedText.lineForCharacterIndex(cursor);
			float xPosition = 0.0f;
			if (justification() == TextJustification::CENTER) {
				xPosition = formattedText.width() / 2.0f - 1.0f;
			} else if (justification() == TextJustification::RIGHT) {
				xPosition = formattedText.width() - 2.0f;
			}
			auto cursorHeight = formattedText.defaultState()->font->height();
			auto linePositionY = formattedText.positionForLine(line->index());
			auto cursorLineHeight = std::max<float>(formattedText.minimumLineHeight(), (line) ? line->height() : cursorHeight);
			linePositionY += cursorLineHeight / 2.0f - cursorHeight / 2.0f;

			cursorScene->owner()->position({ xPosition, linePositionY });
			cursorScene->size({ 2.0f, cursorHeight });

			if (displayCursor) {
				cursorScene->show();
			}
		}

		void Text::positionCursorWithCharacter(size_t a_maxCursor, std::shared_ptr<FormattedCharacter> a_cursorCharacter) {
			cursorScene->owner()->position(a_cursorCharacter->position() + a_cursorCharacter->offset());
			cursorScene->size({ 2.0f, a_cursorCharacter->characterSize().height });
			if (cursor >= a_maxCursor) {
				cursorScene->owner()->translate({ a_cursorCharacter->characterSize().width, 0.0f });
			}
			if (displayCursor) {
				cursorScene->show();
			}
		}

		void Text::updateImplementation(double a_dt) {
			if (displayCursor) {
				accumulatedTime += a_dt;
				if (accumulatedTime > BLINK_DURATION) {
					accumulatedTime = 0.0;
					if (cursorScene->visible()) {
						cursorScene->hide();
					} else {
						cursorScene->show();
					}
				}
			} else {
				cursorScene->hide();
			}
		}

		std::shared_ptr<Text> Text::backspace() {
			if (cursor > 0) {
				formattedText.erase(cursor - 1, 1);
				incrementCursor(-1);
			}
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		bool Text::text(SDL_Event &event) {
			if (event.type == SDL_TEXTINPUT) {
				insertAtCursor(toWide(event.text.text));
				return true;
			} else if (event.type == SDL_TEXTEDITING) {
				//setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
			} else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_BACKSPACE && !formattedText.empty()) {
					backspace();
					return true;
				}if (event.key.keysym.sym == SDLK_DELETE && !formattedText.empty() && cursor < formattedText.size()) {
					++cursor; //this is okay because backspace will reposition the cursor.
					backspace();
					return true;
				} else if (event.key.keysym.sym == SDLK_v && SDL_GetModState() & KMOD_CTRL) {
					append(toWide(SDL_GetClipboardText()));
					return true;
				} else if (event.key.keysym.sym == SDLK_c && SDL_GetModState() & KMOD_CTRL) {
					SDL_SetClipboardText(toString(text()).c_str());
				} else if (event.key.keysym.sym == SDLK_LEFT) {
					if (cursor > 0) {
						incrementCursor(-1);
					}
				} else if (event.key.keysym.sym == SDLK_RIGHT) {
					if (cursor < formattedText.size()) {
						incrementCursor(1);
					}
				} else if (event.key.keysym.sym == SDLK_RETURN) {
					auto self = std::static_pointer_cast<Text>(shared_from_this());
					onEnterSlot(self);
				}
			}
			return false;
		}

		std::shared_ptr<Text> Text::text(const UtfString &a_text, const std::string &a_fontIdentifier /*= ""*/) {
			if (a_fontIdentifier != "") { fontIdentifier = a_fontIdentifier; }
			formattedText.clear();
			formattedText.append(a_text);
			setCursor(a_text.size());
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		void Text::enableCursor() {
			displayCursor = true;
			setCursor(cursor);
			cursorScene->show();
		}

		void Text::disableCursor() {
			displayCursor = false;
			setCursor(cursor);
			cursorScene->hide();
		}

		std::shared_ptr<Component> Text::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Text>(a_clone);
			textClone->wrapping(wrapMethod);
			textClone->contentScrollPosition = contentScrollPosition;
			return a_clone;
		}

	}
}