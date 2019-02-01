#include "text.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Text);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenetext);

namespace MV{
	namespace Scene {

		const double Text::BLINK_DURATION = .35;


		Text::Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) :
			Drawable(a_owner),
			textLibrary(a_textLibrary),
			onEnter(onEnterSignal),
			onChange(onChangeSignal),
			fontIdentifier(a_defaultFontIdentifier),
			formattedText(std::make_shared<FormattedText>(a_textLibrary, a_defaultFontIdentifier)) {
			
			for (auto&& point : points) {
				point = Color(1.0f, 1.0f, 1.0f, 0.0f);
			}
			clearTexturePoints(points);
			appendQuadVertexIndices(vertexIndices, 0);
		}

		void Text::initialize() {
			Drawable::initialize();
			if (ownerIsAlive()) {
				auto silenceSelf = owner()->silence();
				formattedText->scene()->id(guid("TEXT_"))->position(points[0]);
				owner()->add(formattedText->scene());

				cursorSprite = owner()->make(guid("CURSOR_"))->serializable(false)->attach<Sprite>()->bounds(size(2.0f, 5.0f));
				cursorSprite->hide();
			}
		}

		void Text::positionCursorWithoutCharacter() {
			std::shared_ptr<FormattedLine> line;
			size_t characterIndex;
			std::tie(line, characterIndex) = formattedText->lineForCharacterIndex(cursor);
			float xPosition = 0.0f;
			if (justification() == TextJustification::CENTER) {
				xPosition = formattedText->width() / 2.0f - 1.0f;
			} else if (justification() == TextJustification::RIGHT) {
				xPosition = formattedText->width() - 2.0f;
			}
			auto textSilence = formattedText->scene()->silence();
			auto cursorHeight = formattedText->defaultState()->font->height();
			auto linePositionY = formattedText->positionForLine(line->index());
			auto cursorLineHeight = std::max<float>(formattedText->minimumLineHeight(), (line) ? line->height() : cursorHeight);
			linePositionY += cursorLineHeight / 2.0f - cursorHeight / 2.0f;

			auto cursorSilence = cursorSprite->owner()->silence();
			cursorSprite->owner()->position(formattedText->scene()->position() + MV::Point<>(xPosition, linePositionY));
			cursorSprite->bounds(MV::size(2.0f, cursorHeight));

			if (displayCursor) {
				cursorSprite->show();
			}
		}

		void Text::positionCursorWithCharacter(size_t a_maxCursor, std::shared_ptr<FormattedCharacter> a_cursorCharacter) {
			if (cursorSprite && cursorSprite->ownerIsAlive()) {
				auto cursorSilence = cursorSprite->owner()->silence();
				cursorSprite->owner()->position(formattedText->scene()->position() + ((a_cursorCharacter->position() + a_cursorCharacter->offset()) * a_cursorCharacter->scale()));
				cursorSprite->bounds(MV::size(2.0f, a_cursorCharacter->characterSize().height));
				if (cursor >= a_maxCursor) {
					cursorSprite->owner()->translate({ a_cursorCharacter->characterSize().width * a_cursorCharacter->scale().x, 0.0f });
				}
				if (displayCursor) {
					cursorSprite->show();
				}
			}
		}

		void Text::updateImplementation(double a_dt) {
			if (displayCursor) {
				accumulatedTime += a_dt;
				if (accumulatedTime > BLINK_DURATION) {
					accumulatedTime = 0.0;
					if (cursorSprite->visible()) {
						cursorSprite->hide();
					} else {
						cursorSprite->show();
					}
				}
			} else {
				cursorSprite->hide();
			}
		}

		std::shared_ptr<Text> Text::backspace() {
			auto self = std::static_pointer_cast<Text>(shared_from_this());
			if (cursor > 0) {
				formattedText->erase(cursor - 1, 1);
				incrementCursor(-1);
				onChangeSignal(self);
			}
			return self;
		}

		bool Text::text(SDL_Event &event) {
			if (owner()->renderer().headless()) { return false; }

			if (event.type == SDL_TEXTINPUT) {
				insertAtCursor(event.text.text);
				return true;
			} else if (event.type == SDL_TEXTEDITING) {
				//setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
			} else if (event.type == SDL_KEYDOWN) {
				if (event.key.keysym.sym == SDLK_BACKSPACE && !formattedText->empty()) {
					backspace();
					return true;
				} else if (event.key.keysym.sym == SDLK_DELETE && !formattedText->empty() && cursor < formattedText->size()) {
					++cursor; //this is okay because backspace will reposition the cursor.
					backspace();
					return true;
				} else if (event.key.keysym.sym == SDLK_v && SDL_GetModState() & KMOD_CTRL) {
					insertAtCursor(SDL_GetClipboardText());
					return true;
				} else if (event.key.keysym.sym == SDLK_c && SDL_GetModState() & KMOD_CTRL) {
					SDL_SetClipboardText(text().c_str());
				} else if (event.key.keysym.sym == SDLK_LEFT) {
					if (cursor > 0) {
						incrementCursor(-1);
					}
				} else if (event.key.keysym.sym == SDLK_RIGHT) {
					if (cursor < formattedText->size()) {
						incrementCursor(1);
					}
				} else if (event.key.keysym.sym == SDLK_RETURN) {
					auto self = std::static_pointer_cast<Text>(shared_from_this());
					onEnterSignal(self);
				}
			}
			return false;
		}

		std::shared_ptr<Text> Text::text(const UtfString &a_text) {
			formattedText->string(a_text);
			setCursor(a_text.size());
			auto self = std::static_pointer_cast<Text>(shared_from_this());
			onChangeSignal(self);
			return self;
		}

		void Text::enableCursor() {
			if (!displayCursor) {
				displayCursor = true;
				setCursor(cursor);
				cursorSprite->show();
			}
		}

		void Text::disableCursor() {
			if (displayCursor) {
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				displayCursor = false;
				setCursor(cursor);
				cursorSprite->hide();
				if (self) {
					onEnterSignal(self);
				}
			}
		}

		std::shared_ptr<Component> Text::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Drawable::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Text>(a_clone);
			(*textClone->formattedText) = *formattedText;
			textClone->cursor = cursor;
			textClone->usingBoundsForLineHeight = usingBoundsForLineHeight;
			return a_clone;
		}

		void Text::detachImplementation() {
			Drawable::detachImplementation();
			formattedText->scene()->removeFromParent();
			if (cursorSprite && cursorSprite->ownerIsAlive()) {
				cursorSprite->owner()->removeFromParent();
			}
		}

		void Text::boundsImplementation(const BoxAABB<> &a_bounds) {
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;

			formattedText->scene()->position(a_bounds.minPoint);
			if (usingBoundsForLineHeight) {
				formattedText->minimumLineHeight(a_bounds.height());
			}
			formattedText->width(a_bounds.size().width);
			
			setCursor(cursor);

			refreshBounds();
		}

	}
}