#include "text.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include "MV/Utility/services.hpp"

// JaiScript binding for Text
static jai::registrar<MV::Scene::Text, MV::Services> _hookText("Text",
	[](jai::dynamic_binder<MV::Scene::Text>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Drawable>();
	builder.auto_bind();

	// Text content
	builder.method("text", static_cast<MV::UtfString(MV::Scene::Text::*)() const>(&MV::Scene::Text::text));
	builder.method("text", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(const MV::UtfString&)>(&MV::Scene::Text::text));

	// Number
	builder.method("number", static_cast<MV::PointPrecision(MV::Scene::Text::*)() const>(&MV::Scene::Text::number));
	builder.method("number", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::PointPrecision)>(&MV::Scene::Text::number));
	builder.method("number", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(int)>(&MV::Scene::Text::number));

	// Justification
	builder.method("justification", static_cast<MV::TextJustification(MV::Scene::Text::*)() const>(&MV::Scene::Text::justification));
	builder.method("justification", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::TextJustification)>(&MV::Scene::Text::justification));

	// Wrapping
	builder.method("wrapping", static_cast<MV::TextWrapMethod(MV::Scene::Text::*)() const>(&MV::Scene::Text::wrapping));
	builder.method("wrapping", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::TextWrapMethod)>(&MV::Scene::Text::wrapping));
	builder.method("wrapping", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::TextWrapMethod, MV::PointPrecision)>(&MV::Scene::Text::wrapping));
	builder.method("wrappingWidth", static_cast<MV::PointPrecision(MV::Scene::Text::*)() const>(&MV::Scene::Text::wrappingWidth));
	builder.method("wrappingWidth", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::PointPrecision)>(&MV::Scene::Text::wrappingWidth));

	// Append/Insert
	builder.method("append", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(const MV::UtfString&)>(&MV::Scene::Text::append));
	builder.method("insertAtCursor", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(const MV::UtfString&)>(&MV::Scene::Text::insertAtCursor));
	builder.method("backspace", &MV::Scene::Text::backspace);

	// Cursor
	builder.method("enableCursor", &MV::Scene::Text::enableCursor);
	builder.method("disableCursor", &MV::Scene::Text::disableCursor);

	// Line height
	builder.method("minimumLineHeight", static_cast<MV::PointPrecision(MV::Scene::Text::*)() const>(&MV::Scene::Text::minimumLineHeight));
	builder.method("minimumLineHeight", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(MV::PointPrecision)>(&MV::Scene::Text::minimumLineHeight));

	// Password field
	builder.method("passwordField", static_cast<bool(MV::Scene::Text::*)() const>(&MV::Scene::Text::passwordField));
	builder.method("passwordField", static_cast<void(MV::Scene::Text::*)(bool)>(&MV::Scene::Text::passwordField));

	// Bounds for line height
	builder.method("useBoundsForLineHeight", static_cast<bool(MV::Scene::Text::*)() const>(&MV::Scene::Text::useBoundsForLineHeight));
	builder.method("useBoundsForLineHeight", static_cast<std::shared_ptr<MV::Scene::Text>(MV::Scene::Text::*)(bool)>(&MV::Scene::Text::useBoundsForLineHeight));
});

CEREAL_REGISTER_TYPE(MV::Scene::Text);
CEREAL_CLASS_VERSION(MV::Scene::Text, 1);
CEREAL_REGISTER_DYNAMIC_INIT(mv_scenetext);

namespace MV{
	namespace Scene {

		const double Text::BLINK_DURATION = .35;


		Text::Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) :
			jai::property_owner<Text, Drawable>(a_owner),
			textLibrary(a_textLibrary),
			onEnter(onEnterSignal),
			onChange(onChangeSignal),
			fontIdentifier(a_defaultFontIdentifier),
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
					return true;
				} else if (event.key.keysym.sym == SDLK_LEFT) {
					if (cursor > 0) {
						incrementCursor(-1);
					}
					return true;
				} else if (event.key.keysym.sym == SDLK_RIGHT) {
					if (cursor < formattedText->size()) {
						incrementCursor(1);
					}
					return true;
				} else if (event.key.keysym.sym == SDLK_RETURN) {
					auto self = std::static_pointer_cast<Text>(shared_from_this());
					onEnterSignal(self);
					return true;
				}
			}
			return false;
		}

		std::shared_ptr<Text> Text::text(const UtfString &a_text) {
			formattedText->string(a_text);
			this->setCursor(formattedText->size());
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