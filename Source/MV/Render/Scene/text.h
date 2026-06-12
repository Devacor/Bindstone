#ifndef _MV_SCENE_TEXT_H_
#define _MV_SCENE_TEXT_H_

#include "MV/Render/formattedText.h"
#include "sprite.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		class Text : public jai::property_owner<Text, Drawable> {
			friend Node;
			friend jai::access;

		public:
			typedef void TextSignalSignature(std::shared_ptr<Text>);

		private:

			jai::signal_emitter<TextSignalSignature> onChangeSignal;
			jai::signal_emitter<TextSignalSignature> onEnterSignal;

		public:
			jai::signal<TextSignalSignature> onChange;
			jai::signal<TextSignalSignature> onEnter;

			DrawableDerivedAccessors(Text)

			UtfString text() const {
				return formattedText->string();
			}
			std::shared_ptr<Text> text(const UtfString& a_text);
			std::shared_ptr<Text> text(UtfChar a_char) {
				return text(UtfString() + a_char);
			}

			bool text(SDL_Event& event);

			PointPrecision number() const {
				try {
					return stof(text());
				}
				catch (...) {
					return 0.0f;
				}
			}

			std::shared_ptr<Text> number(PointPrecision a_newText) {
				return text(std::to_string(a_newText));
			}
			std::shared_ptr<Text> number(int a_newText) {
				return text(std::to_string(a_newText));
			}

			template <typename T>
			std::shared_ptr<Text> set(T a_newText) {
				return text(std::to_string(a_newText));
			}

			std::shared_ptr<Text> justification(MV::TextJustification a_newJustification) {
				formattedText->justification(a_newJustification);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextJustification justification() const {
				return formattedText->justification();
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod, MV::PointPrecision a_width) {
				// Debug check
				if (!static_cast<bool>(formattedText)) {
					throw std::runtime_error("Text::wrapping() called with null formattedText");
				}
				
				formattedText->wrapping(a_newWrapMethod, a_width);

				auto adjustedBounds = bounds();
				adjustedBounds.maxPoint.x = adjustedBounds.minPoint.x + a_width;
				bounds(adjustedBounds);

				return std::static_pointer_cast<Text>(shared_from_this());
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod) {
				// Debug check
				if (!static_cast<bool>(formattedText)) {
					throw std::runtime_error("Text::wrapping() called with null formattedText");
				}
				
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				formattedText->wrapping(a_newWrapMethod);
				return self;
			}

			std::shared_ptr<Text> wrappingWidth(MV::PointPrecision a_width) {
				formattedText->width(a_width);

				auto adjustedBounds = bounds();
				adjustedBounds.maxPoint.x = adjustedBounds.minPoint.x + a_width;
				bounds(adjustedBounds);

				return std::static_pointer_cast<Text>(shared_from_this());
			}

			MV::TextWrapMethod wrapping() const {
				return formattedText->wrapping();
			}
			MV::PointPrecision wrappingWidth() const {
				return formattedText->width();
			}

			std::shared_ptr<Text> append(const UtfString& a_text) {
				auto inserted = formattedText->append(a_text);
				if (cursor >= formattedText->size()) {
					incrementCursor(inserted);
				}
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				onChangeSignal(self);
				return self;
			}
			std::shared_ptr<Text> append(UtfChar a_char) {
				formattedText->append(UtfString(1, a_char));
				if (cursor >= formattedText->size()) {
					incrementCursor(1);
				}
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				onChangeSignal(self);
				return self;
			}

			std::shared_ptr<Text> insertAtCursor(const UtfString& a_text) {
				incrementCursor(formattedText->insert(cursor, a_text));
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				onChangeSignal(self);
				return self;
			}
			std::shared_ptr<Text> insertAtCursor(UtfChar a_char) {
				formattedText->insert(cursor, UtfString(1, a_char));
				incrementCursor(1);
				auto self = std::static_pointer_cast<Text>(shared_from_this());
				onChangeSignal(self);
				return self;
			}

			void enableCursor();
			void disableCursor();

			std::shared_ptr<Text> backspace();

			PointPrecision minimumLineHeight() const {
				return formattedText->minimumLineHeight();
			}

			std::shared_ptr<Text> minimumLineHeight(PointPrecision a_newLineHeight) {
				// Debug check
				if (!static_cast<bool>(formattedText)) {
					throw std::runtime_error("Text::minimumLineHeight() called with null formattedText");
				}
				
				formattedText->minimumLineHeight(a_newLineHeight);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			void passwordField(bool a_passwordField) {
				formattedText->passwordField(a_passwordField);
			}

			bool passwordField() const {
				return formattedText->passwordField();
			}

			bool useBoundsForLineHeight() const {
				return usingBoundsForLineHeight;
			}

			std::shared_ptr<Text> useBoundsForLineHeight(bool a_useBounds) {
				usingBoundsForLineHeight = a_useBounds;
				if (a_useBounds) {
					formattedText->minimumLineHeight(bounds().height());
				}
				return std::static_pointer_cast<Text>(shared_from_this());
			}
		protected:
			virtual void detachImplementation() override;
			virtual void boundsImplementation(const BoxAABB<>& a_bounds) override;
			virtual void updateImplementation(double a_dt);

			virtual void initialize() override;

			virtual void defaultDrawImplementation() {
				Drawable::defaultDrawImplementation();
			}

			Text(const std::weak_ptr<Node>& a_owner, TextLibrary& a_textLibrary, const std::string& a_defaultFontIdentifier);

			Text(const std::weak_ptr<Node>& a_owner, TextLibrary& a_textLibrary) :
				Text(a_owner, a_textLibrary, DEFAULT_ID) {
			}
			Text(const Text&) = delete;
			Text& operator=(const Text&) = delete;
			Text(Text&&) = delete;
			Text& operator=(Text&&) = delete;

			template<class Archive>
			void save(Archive& archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				archive(jai::serialization::make_nvp("formattedText", formattedText.get()));
				Drawable::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const version) {
				property_mgr.load(archive);
				archive(jai::serialization::make_nvp("formattedText", formattedText.get()));
				Drawable::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Text>& construct) {
				auto* services = ar.template get_user_context<MV::Services>();
				construct(std::shared_ptr<Node>(), *services->template get<MV::TextLibrary>());
				construct->formattedText->scene()->removeFromParent();
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) {
				return cloneHelper(a_parent->attach<Text>(textLibrary, formattedText->defaultStateId()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component>& a_clone);

		private:
			void setCursor(int64_t a_value) {
				if (a_value != cursor) {
					auto maxCursor = formattedText->size();
					a_value = std::max<int64_t>(std::min<int64_t>(a_value, maxCursor), 0);

					auto characterToTest = formattedText->characterForIndex(a_value);
					if (characterToTest) {
						int iterateDirection = a_value > static_cast<int64_t>(cursor) ? 1 : -1;
						auto referenceState = characterToTest->state;
						while (characterToTest && characterToTest->partOfFormat() && characterToTest->state == referenceState) {
							characterToTest = formattedText->characterForIndex(a_value);
							if (characterToTest && characterToTest->partOfFormat()) {
								a_value += iterateDirection;
							}
						}
					}

					cursor = a_value;
					auto cursorCharacter = (cursor < maxCursor || cursor == 0) ? formattedText->characterForIndex(cursor) : formattedText->characterForIndex(cursor - 1);
					if (cursorCharacter) {
						positionCursorWithCharacter(maxCursor, cursorCharacter);
					}
					else {
						positionCursorWithoutCharacter();
					}
				}
			}

			void positionCursorWithCharacter(size_t a_maxCursor, std::shared_ptr<FormattedCharacter> a_cursorCharacter);

			void positionCursorWithoutCharacter();

			void incrementCursor(int64_t a_change) {
				setCursor(cursor + a_change);
			}

			TextLibrary& textLibrary;

			jai::property<std::shared_ptr<FormattedText>> formattedText;

			JAI_PROPERTY((bool), usingBoundsForLineHeight, false);

			bool displayCursor = false;
			size_t cursor = 0;
			std::shared_ptr<MV::Scene::Sprite> cursorSprite;

			std::string fontIdentifier;
			double accumulatedTime = 0.0;
			static const double BLINK_DURATION;
		};
	}
}

#endif

