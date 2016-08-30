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
			typedef void TextSignalSignature(std::shared_ptr<Text>);

		private:

			Signal<TextSignalSignature> onEnterSignal;

		public:
			SignalRegister<TextSignalSignature> onEnter;

			DrawableDerivedAccessors(Text)

			UtfString text() const {
				return formattedText.string();
			}
			std::shared_ptr<Text> text(const UtfString &a_text);
			std::shared_ptr<Text> text(UtfChar a_char) {
				return text(UtfString() + a_char);
			}
			std::shared_ptr<Text> text(Uint16 a_char) {
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return text(*character);
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
				return text(std::to_string(a_newText));
			}
			std::shared_ptr<Text> number(int a_newText) {
				return text(std::to_string(a_newText));
			}

			std::shared_ptr<Text> justification(MV::TextJustification a_newJustification) {
				formattedText.justification(a_newJustification);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextJustification justification() const {
				return formattedText.justification();
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod, MV::PointPrecision a_width) {
				formattedText.wrapping(a_newWrapMethod, a_width);

				auto adjustedBounds = bounds();
				adjustedBounds.maxPoint.x = adjustedBounds.minPoint.x + a_width;
				bounds(adjustedBounds);

				return std::static_pointer_cast<Text>(shared_from_this());
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod) {
				formattedText.wrapping(a_newWrapMethod);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			std::shared_ptr<Text> wrappingWidth(MV::PointPrecision a_width) {
				formattedText.width(a_width);

				auto adjustedBounds = bounds();
				adjustedBounds.maxPoint.x = adjustedBounds.minPoint.x + a_width;
				bounds(adjustedBounds);

				return std::static_pointer_cast<Text>(shared_from_this());
			}

			MV::TextWrapMethod wrapping() const {
				return formattedText.wrapping();
			}
			MV::PointPrecision wrappingWidth() const {
				return formattedText.width();
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

			PointPrecision minimumLineHeight() const {
				return formattedText.minimumLineHeight();
			}

			std::shared_ptr<Text> minimumLineHeight(PointPrecision a_newLineHeight) {
				formattedText.minimumLineHeight(a_newLineHeight);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			static chaiscript::ChaiScript& hook(chaiscript::ChaiScript &a_script) {
				a_script.add(chaiscript::user_type<Text>(), "Text");
				a_script.add(chaiscript::base_class<Drawable, Text>());
				a_script.add(chaiscript::base_class<Component, Text>());

				a_script.add(chaiscript::fun([](Node &a_self, TextLibrary& a_textLibrary) { return a_self.attach<Text>(a_textLibrary); }), "attachText");
				a_script.add(chaiscript::fun([](Node &a_self, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) { return a_self.attach<Text>(a_textLibrary, a_defaultFontIdentifier); }), "attachText");

				a_script.add(chaiscript::fun(&Text::enableCursor), "enableCursor");
				a_script.add(chaiscript::fun(&Text::disableCursor), "disableCursor");

				a_script.add(chaiscript::fun(static_cast<UtfString(Text::*)() const>(&Text::text)), "text");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(const UtfString &)>(&Text::text)), "text");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(UtfChar)>(&Text::text)), "text");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(Uint16)>(&Text::text)), "text");

				a_script.add(chaiscript::fun(static_cast<PointPrecision(Text::*)() const>(&Text::number)), "number");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(PointPrecision)>(&Text::number)), "number");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(int)>(&Text::number)), "number");

				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(const UtfString &)>(&Text::append)), "append");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(UtfChar a_char)>(&Text::append)), "append");
				a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(Uint16 a_char)>(&Text::append)), "append");

				a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Text>>([](const SafeComponent<Text> &a_item) { return a_item.self(); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Drawable>>([](const SafeComponent<Text> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
				a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Component>>([](const SafeComponent<Text> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));

				return a_script;
			}
		protected:
			virtual void boundsImplementation(const BoxAABB<> &a_bounds) override;
			virtual void updateImplementation(double a_dt);

			virtual void defaultDrawImplementation() {
				Drawable::defaultDrawImplementation();
			}

			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier);

			Text(const std::weak_ptr<Node> &a_owner, TextLibrary& a_textLibrary) :
				Text(a_owner, a_textLibrary, DEFAULT_ID) {
			}

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					CEREAL_NVP(formattedText),
					CEREAL_NVP(cursorScene),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Text> &construct, std::uint32_t const /*version*/) {
				TextLibrary *library = nullptr;
				archive.extract(cereal::make_nvp("library", library));
				MV::require<PointerException>(library != nullptr, "Null TextLibrary in Text::load_and_construct.");

				construct(std::shared_ptr<Node>(), *library);

				archive(
					cereal::make_nvp("formattedText", construct->formattedText),
					cereal::make_nvp("cursorScene", construct->cursorScene),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}
			
			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Text>(textLibrary, formattedText.defaultStateId()).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

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

			TextLibrary& textLibrary;

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
