#include "text.h"
#include "MV/Utility/tinyutf8.h"

#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal.hpp>

namespace MV{
	namespace Scene {
		void Text::jai_auto_bind(jai::dynamic_binder<Text>& a_builder) {
			a_builder.transient_property("onEnter", &Text::onEnter);
			a_builder.transient_property("onChange", &Text::onChange);

			a_builder.method("text", static_cast<UtfString(Text::*)() const>(&Text::text));
			a_builder.method("text", static_cast<std::shared_ptr<Text>(Text::*)(const UtfString&)>(&Text::text), jai::serial);

			a_builder.method("number", static_cast<PointPrecision(Text::*)() const>(&Text::number));
			a_builder.method("number", static_cast<std::shared_ptr<Text>(Text::*)(PointPrecision)>(&Text::number), jai::serial);
			a_builder.method("number", static_cast<std::shared_ptr<Text>(Text::*)(int)>(&Text::number), jai::serial);

			a_builder.method("justification", static_cast<TextJustification(Text::*)() const>(&Text::justification));
			a_builder.method("justification", static_cast<std::shared_ptr<Text>(Text::*)(TextJustification)>(&Text::justification), jai::serial);

			a_builder.method("wrapping", static_cast<TextWrapMethod(Text::*)() const>(&Text::wrapping));
			a_builder.method("wrapping", static_cast<std::shared_ptr<Text>(Text::*)(TextWrapMethod)>(&Text::wrapping), jai::serial);
			a_builder.method("wrapping", static_cast<std::shared_ptr<Text>(Text::*)(TextWrapMethod, PointPrecision)>(&Text::wrapping), jai::serial);
			a_builder.method("wrappingWidth", static_cast<PointPrecision(Text::*)() const>(&Text::wrappingWidth));
			a_builder.method("wrappingWidth", static_cast<std::shared_ptr<Text>(Text::*)(PointPrecision)>(&Text::wrappingWidth), jai::serial);

			a_builder.method("append", static_cast<std::shared_ptr<Text>(Text::*)(const UtfString&)>(&Text::append), jai::serial);
			a_builder.method("insertAtCursor", static_cast<std::shared_ptr<Text>(Text::*)(const UtfString&)>(&Text::insertAtCursor), jai::serial);
			a_builder.method("backspace", &Text::backspace, jai::serial);

			a_builder.method("enableCursor", &Text::enableCursor, jai::serial);
			a_builder.method("disableCursor", &Text::disableCursor, jai::serial);

			a_builder.method("minimumLineHeight", static_cast<PointPrecision(Text::*)() const>(&Text::minimumLineHeight));
			a_builder.method("minimumLineHeight", static_cast<std::shared_ptr<Text>(Text::*)(PointPrecision)>(&Text::minimumLineHeight), jai::serial);

			a_builder.method("passwordField", static_cast<bool(Text::*)() const>(&Text::passwordField));
			a_builder.method("passwordField", static_cast<void(Text::*)(bool)>(&Text::passwordField), jai::serial);

			a_builder.method("useBoundsForLineHeight", static_cast<bool(Text::*)() const>(&Text::useBoundsForLineHeight));
			a_builder.method("useBoundsForLineHeight", static_cast<std::shared_ptr<Text>(Text::*)(bool)>(&Text::useBoundsForLineHeight), jai::serial);
		}


		const double Text::BLINK_DURATION = .35;


		Text::Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) :
			jai::property_owner<Text, Drawable>(a_owner),
			textLibrary(a_textLibrary),
			onEnter(onEnterSignal),
			onChange(onChangeSignal),
			fontIdentifier(property_mgr, "fontIdentifier", a_defaultFontIdentifier),
			formattedText(property_mgr, "formattedText", std::make_shared<FormattedText>(a_textLibrary, a_defaultFontIdentifier), [](auto& source, auto& destination) {
				if (source.get() && destination.get()) {
					*destination.get() = *source.get();
				} else if (source.get() && !destination.get()) {
					destination = std::make_shared<FormattedText>(*source.get());
				}
			}) {
			
			for (auto&& point : points) {
				point = Color(1.0f, 1.0f, 1.0f, 0.0f);
			}
			clearTexturePoints(*points);
			appendQuadVertexIndices(*vertexIndices, 0);
		}

		void Text::initialize() {
			Drawable::initialize();
			attachGlyphNodes();
		}

		void Text::postLoadInitialize() {
			Drawable::postLoadInitialize();
			attachGlyphNodes();
		}

		bool Text::ownerHasChild(const std::shared_ptr<Node>& a_node) const {
			auto parent = owner();
			return std::find(parent->cbegin(), parent->cend(), a_node) != parent->cend();
		}

		void Text::attachGlyphNodes() {
			if (!ownerIsAlive()) {
				return;
			}
			auto glyphs = formattedText->scene();
			bool glyphsAttached = ownerHasChild(glyphs);
			bool cursorAlive = cursorSprite && cursorSprite->ownerIsAlive();
			bool cursorAttached = cursorAlive && ownerHasChild(cursorSprite->owner());
			if (glyphsAttached && cursorAttached) {
				return;
			}
			auto silenceSelf = owner()->silence();
			if (!glyphsAttached) {
				// Runtime-generated glyph container: never serialize it (the persistent data is
				// the string on formattedText; baked glyph sprites reload with dead
				// SurfaceTextureDefinitions and render garbage).
				glyphs->id(guid("TEXT_"))->serializable(false)->position(points[0]);
				owner()->add(glyphs);
			}
			if (!cursorAlive) {
				cursorSprite = owner()->make(guid("CURSOR_"))->serializable(false)->attach<Sprite>()->bounds(size(2.0f, 5.0f));
				cursorSprite->hide();
			} else if (!cursorAttached) {
				owner()->add(cursorSprite->owner());
			}
			if (!selectionSprite || !selectionSprite->ownerIsAlive()) {
				auto selectionNode = owner()->make(guid("SELECTION_"))->serializable(false);
				// behind the glyphs it marks
				selectionNode->depth(glyphs->depth() - 0.5f);
				selectionSprite = selectionNode->attach<Sprite>()->color(Color(0.16f, 0.42f, 0.86f, 0.75f));
				selectionSprite->hide();
			} else if (!ownerHasChild(selectionSprite->owner())) {
				owner()->add(selectionSprite->owner(), false);
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

		std::shared_ptr<Text> Text::selectAll() {
			selectionAnchor = 0;
			setCursor(formattedText->size());
			refreshSelection();
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		std::shared_ptr<Text> Text::clearSelection() {
			selectionAnchor.reset();
			refreshSelection();
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		UtfString Text::selectedText() const {
			if (!hasSelection()) {
				return {};
			}
			utf8_string all(text());
			return all.substr(selectionStart(), selectionEnd() - selectionStart()).cpp_str();
		}

		std::shared_ptr<Text> Text::copyToClipboard() {
			SDL_SetClipboardText(hasSelection() ? selectedText().c_str() : text().c_str());
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		std::shared_ptr<Text> Text::cutToClipboard() {
			auto self = copyToClipboard();
			if (eraseSelection()) {
				onChangeSignal(self);
			}
			return self;
		}

		std::shared_ptr<Text> Text::pasteFromClipboard() {
			char* clipboard = SDL_GetClipboardText();
			UtfString pasted = clipboard ? clipboard : "";
			SDL_free(clipboard);
			insertAtCursor(pasted);
			refreshSelection();
			return std::static_pointer_cast<Text>(shared_from_this());
		}

		bool Text::eraseSelection() {
			if (!hasSelection()) {
				selectionAnchor.reset();
				return false;
			}
			const size_t start = selectionStart();
			const size_t count = selectionEnd() - start;
			selectionAnchor.reset();
			formattedText->erase(start, count);
			cursor = std::min(cursor, formattedText->size()) + 1;   // setCursor places only on a change
			setCursor(static_cast<int64_t>(start));
			refreshSelection();
			return true;
		}

		void Text::moveCursor(int64_t a_to, bool a_extend) {
			if (a_extend) {
				if (!selectionAnchor) {
					selectionAnchor = cursor;
				}
			} else {
				selectionAnchor.reset();
			}
			setCursor(a_to);
			refreshSelection();
		}

		void Text::refreshSelection() {
			if (!selectionSprite || !selectionSprite->ownerIsAlive()) {
				return;
			}
			if (!hasSelection() || formattedText->size() == 0) {
				selectionSprite->hide();
				return;
			}
			const size_t maxCursor = formattedText->size();
			// the left edge of the character at an index, or the right edge of the last one
			auto edge = [&](size_t a_index, PointPrecision& a_height) {
				auto character = a_index < maxCursor ? formattedText->characterForIndex(a_index) : formattedText->characterForIndex(maxCursor - 1);
				if (!character) {
					return Point<>();
				}
				auto result = (character->position() + character->offset()) * character->scale();
				if (a_index >= maxCursor) {
					result.x += character->characterSize().width * character->scale().x;
				}
				a_height = character->characterSize().height * character->scale().y;
				return result;
			};
			PointPrecision height = 0.0f;
			PointPrecision endHeight = 0.0f;
			auto from = edge(selectionStart(), height);
			auto to = edge(selectionEnd(), endHeight);
			// one line's worth is drawn: a selection that runs over lines is still selected, only not marked
			if (std::abs(from.y - to.y) > 0.5f || to.x <= from.x) {
				selectionSprite->hide();
				return;
			}
			auto selectionSilence = selectionSprite->owner()->silence();
			selectionSprite->owner()->position(formattedText->scene()->position() + from);
			selectionSprite->bounds(MV::size(to.x - from.x, std::max(height, endHeight)));
			selectionSprite->show();
		}

		std::shared_ptr<Text> Text::backspace() {
			auto self = std::static_pointer_cast<Text>(shared_from_this());
			if (eraseSelection()) {
				onChangeSignal(self);
				return self;
			}
			if (cursor > 0) {
				formattedText->erase(cursor - 1, 1);
				incrementCursor(-1);
				onChangeSignal(self);
			}
			return self;
		}

		bool Text::text(SDL_Event &event) {
			if (owner()->renderer().headless()) { return false; }

			if (event.type == SDL_EVENT_TEXT_INPUT) {
				insertAtCursor(event.text.text);
				refreshSelection();
				return true;
			} else if (event.type == SDL_EVENT_TEXT_EDITING) {
				//setTemporaryText(stringToWide(event.edit.text), event.edit.start, event.edit.length);
			} else if (event.type == SDL_EVENT_KEY_DOWN) {
				const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
				const bool control = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				if (event.key.key == SDLK_BACKSPACE && !formattedText->empty()) {
					backspace();
					return true;
				} else if (event.key.key == SDLK_DELETE && hasSelection()) {
					eraseSelection();
					onChangeSignal(self);
					return true;
				} else if (event.key.key == SDLK_DELETE && !formattedText->empty() && cursor < formattedText->size()) {
					++cursor; //this is okay because backspace will reposition the cursor.
					backspace();
					return true;
				} else if (event.key.key == SDLK_A && control) {
					selectAll();
					return true;
				} else if (event.key.key == SDLK_V && control) {
					pasteFromClipboard();
					return true;
				} else if (event.key.key == SDLK_C && control) {
					copyToClipboard();
					return true;
				} else if (event.key.key == SDLK_X && control) {
					cutToClipboard();
					return true;
				} else if (event.key.key == SDLK_LEFT) {
					// without Shift a selection collapses to the end the arrow points at
					moveCursor(!shift && hasSelection() ? static_cast<int64_t>(selectionStart()) : static_cast<int64_t>(cursor) - 1, shift);
					return true;
				} else if (event.key.key == SDLK_RIGHT) {
					moveCursor(!shift && hasSelection() ? static_cast<int64_t>(selectionEnd()) : static_cast<int64_t>(cursor) + 1, shift);
					return true;
				} else if (event.key.key == SDLK_HOME) {
					moveCursor(0, shift);
					return true;
				} else if (event.key.key == SDLK_END) {
					moveCursor(static_cast<int64_t>(formattedText->size()), shift);
					return true;
				} else if (event.key.key == SDLK_RETURN) {
					onEnterSignal(self);
					return true;
				}
			}
			return false;
		}

		std::shared_ptr<Text> Text::text(const UtfString &a_text) {
			selectionAnchor.reset();
			formattedText->string(a_text);
			this->setCursor(formattedText->size());
			refreshSelection();
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
				clearSelection();
				if (self) {
					onEnterSignal(self);
				}
			}
		}

		std::shared_ptr<Component> Text::cloneHelper(const std::shared_ptr<Component>& a_clone) {
			Drawable::cloneHelper(a_clone);
			auto textClone = std::static_pointer_cast<Text>(a_clone);
			textClone->cursor = cursor;
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
