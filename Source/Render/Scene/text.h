#ifndef _MV_SCENE_TEXT_H_
#define _MV_SCENE_TEXT_H_

#include "Render/formattedText.h"
#include "sprite.h"

namespace MV{
	namespace Scene {

		class Text : public Drawable {
			friend Node;
			friend cereal::access;

		public:
			typedef void TextSlotSignature(const std::shared_ptr<Text> &);

		private:

			Slot<TextSlotSignature> onEnterSlot;

		public:
			SlotRegister<TextSlotSignature> onEnter;

			DrawableDerivedAccessors(Text)

			UtfString text() const {
				return formattedText.string();
			}
			std::shared_ptr<Text> text(const UtfString &a_text, const std::string &a_fontIdentifier = "");
			std::shared_ptr<Text> text(UtfChar a_char, const std::string &a_fontIdentifier = "") {
				return text(UtfString() + a_char, a_fontIdentifier);
			}
			std::shared_ptr<Text> text(Uint16 a_char, const std::string &a_fontIdentifier = "") {
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return text(*character, a_fontIdentifier);
			}

			bool text(SDL_Event &event);

			PointPrecision number() const {
				try {
					return stof(text());
				} catch (...) {
					return 0.0f;
				}
			}
			std::shared_ptr<Text> number(PointPrecision a_newText) {
				UtfString str = toWide(std::to_string(a_newText));
				return text(str);
			}
			std::shared_ptr<Text> number(int a_newText) {
				UtfString str = toWide(std::to_string(a_newText));
				return text(str);
			}

			std::shared_ptr<Text> justification(MV::TextJustification a_newJustification) {
				formattedText.justification(a_newJustification);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextJustification justification() const {
				return formattedText.justification();
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod) {
				formattedText.wrapping(a_newWrapMethod);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextWrapMethod wrapping() const {
				return formattedText.wrapping();
			}

			std::shared_ptr<Text> append(const UtfString &a_text) {
				if (cursor >= formattedText.size()) {
					incrementCursor(a_text.size());
				}
				formattedText.append(a_text);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(UtfChar a_char) {
				if (cursor >= formattedText.size()) {
					incrementCursor(1);
				}
				formattedText.append(UtfString(1, a_char));
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(Uint16 a_char) {
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return append(*character);
			}

			std::shared_ptr<Text> insertAtCursor(const UtfString &a_text) {
				formattedText.insert(cursor, a_text);
				incrementCursor(a_text.size());
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> insertAtCursor(UtfChar a_char) {
				formattedText.insert(cursor, UtfString(1, a_char));
				incrementCursor(1);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> insertAtCursor(Uint16 a_char) {
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				formattedText.insert(cursor, UtfString(1, *character));
				incrementCursor(1);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			void enableCursor();
			void disableCursor();

			std::shared_ptr<Text> backspace();

			virtual void update(double a_dt);

			PointPrecision minimumLineHeight() const {
				return formattedText.minimumLineHeight();
			}

			std::shared_ptr<Text> minimumLineHeight(PointPrecision a_newLineHeight) {
				formattedText.minimumLineHeight(a_newLineHeight);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

		protected:
			virtual void defaultDrawImplementation() {
				Drawable::defaultDrawImplementation();
			}

			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const Size<> &a_size, const std::string &a_defaultFontIdentifier);

			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary) :
				Text(a_owner, a_textLibrary, Size<>(), DEFAULT_ID) {
			}
			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const Size<> &a_size) :
				Text(a_owner, a_textLibrary, a_size, DEFAULT_ID) {
			}
			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) :
				Text(a_owner, a_textLibrary, Size<>(), a_defaultFontIdentifier) {
			}

			template <class Archive>
			void serialize(Archive & archive) {
				archive(
					CEREAL_NVP(boxSize),
					CEREAL_NVP(contentScrollPosition),
					CEREAL_NVP(textJustification),
					CEREAL_NVP(wrapMethod),
					CEREAL_NVP(fontIdentifier),
					CEREAL_NVP(cursorScene),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Text> &construct) {
				TextLibrary *library = nullptr;
				archive.extract(cereal::make_nvp("library", library));
				MV::require<PointerException>(library != nullptr, "Null TextLibrary in Text::load_and_construct.");

				std::string fontIdentifier;
				Size<> boxSize;
				archive(
					cereal::make_nvp("fontIdentifier", fontIdentifier),
					cereal::make_nvp("boxSize", boxSize)
				);

				construct(std::shared_ptr<Node>(), *library, boxSize, fontIdentifier);

				archive(
					cereal::make_nvp("contentScrollPosition", construct->contentScrollPosition),
					cereal::make_nvp("textJustification", construct->textJustification),
					cereal::make_nvp("wrapMethod", construct->wrapMethod),
					cereal::make_nvp("cursorScene", construct->cursorScene),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

		private:

			void setCursor(int64_t a_value) {
				auto maxCursor = formattedText.size();
				a_value = std::max<int64_t>(std::min<int64_t>(a_value, maxCursor), 0);
				cursor = a_value;
				auto cursorCharacter = (cursor < maxCursor || cursor == 0) ? formattedText.characterForIndex(cursor) : formattedText.characterForIndex(cursor - 1);
				if (cursorCharacter) {
					positionCursorWithCharacter(maxCursor, cursorCharacter);
				} else {
					positionCursorWithoutCharacter();
				}
			}

			void positionCursorWithCharacter(size_t a_maxCursor, std::shared_ptr<FormattedCharacter> a_cursorCharacter);

			void positionCursorWithoutCharacter();

			void incrementCursor(int64_t a_change) {
				setCursor(cursor + a_change);
			}

			Size<> boxSize;
			Point<> contentScrollPosition;

			TextLibrary& textLibrary;
			TextJustification textJustification = LEFT;
			TextWrapMethod wrapMethod = SOFT;

			FormattedText formattedText;

			bool displayCursor = false;
			size_t cursor;
			std::shared_ptr<MV::Scene::Sprite> cursorScene;

			std::string fontIdentifier;
			double accumulatedTime = 0.0;
			static const double BLINK_DURATION;
		};
	}
}

#endif
