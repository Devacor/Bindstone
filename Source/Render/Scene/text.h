#ifndef _MV_SCENE_TEXT_H_
#define _MV_SCENE_TEXT_H_

#include "Render/formattedText.h"
#include "Render/Scene/node.h"
#include "Render/Scene/clipped.h"

namespace MV{
	namespace Scene {

		class Text :
			public Node{

			friend cereal::access;
			friend Node;
		public:

			SCENE_MAKE_FACTORY_METHODS(Text)

			static std::shared_ptr<Text> make(Draw2D* a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size);
			static std::shared_ptr<Text> make(Draw2D* a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size, const std::string &a_fontIdentifier);
			
			UtfString text() const{
				return formattedText.string();
			}
			double number() const{
				return stod(text());
			}

			std::shared_ptr<Text> justification(MV::TextJustification a_newJustification){
				formattedText.justification(a_newJustification);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextJustification justification() const{
				return formattedText.justification();
			}

			std::shared_ptr<Text> wrapping(MV::TextWrapMethod a_newWrapMethod){
				formattedText.wrapping(a_newWrapMethod);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			MV::TextWrapMethod wrapping() const{
				return formattedText.wrapping();
			}

			std::shared_ptr<Text> text(const UtfString &a_text, const std::string &a_fontIdentifier = "");
			std::shared_ptr<Text> text(UtfChar a_char, const std::string &a_fontIdentifier = ""){
				return text(UtfString() + a_char, a_fontIdentifier);
			}
			std::shared_ptr<Text> text(Uint16 a_char, const std::string &a_fontIdentifier = ""){
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return text(*character, a_fontIdentifier);
			}
			//any further development to input handling should probably be broken out elsewhere.
			bool text(SDL_Event &event);

			void setTemporaryText(const UtfString &a_text, size_t a_cursorStart, size_t a_cursorEnd){
				editText = a_text;
			}

			std::shared_ptr<Text> append(const UtfString &a_text){
				formattedText.append(a_text);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(const UtfChar a_char){
				formattedText.append(UtfString(1, a_char));
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(Uint16 a_char){
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return append(*character);
			}

			std::shared_ptr<Text> prepend(Uint16 a_char){
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return prepend(*character);
			}

			std::shared_ptr<Text> backspace(){
				formattedText.popCharacters(1);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			void textBoxSize(Size<> a_size);

			void scrollPosition(Point<> a_position, bool a_overScroll = false);
			void translateScrollPosition(Point<> a_position, bool a_overScroll = false){
				scrollPosition((contentScrollPosition + a_position), a_overScroll);
			}

			Point<> scrollPosition() const{
				return contentScrollPosition;
			}

			Size<> contentSize() const;
			PointPrecision getMinimumLineHeight() const{
				return minimumLineHeight;
			}
			std::shared_ptr<Text> setMinimumLineHeight(PointPrecision a_newLineHeight){
				minimumLineHeight = a_newLineHeight;
				formattedText.minimumLineHeight(a_newLineHeight);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			std::shared_ptr<Text> makeSingleLine(){
				isSingleLine = true;
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> makeManyLine(){
				isSingleLine = false;
				return std::static_pointer_cast<Text>(shared_from_this());
			}
		private:
			Text(Draw2D *a_renderer, TextLibrary *a_textLibrary, const Size<> &a_size, const std::string &a_fontIdentifier);

			template <class Archive>
			void serialize(Archive & archive){
				archive(
					CEREAL_NVP(fontIdentifier),
					CEREAL_NVP(boxSize),
					CEREAL_NVP(contentScrollPosition),
					CEREAL_NVP(wrapMethod),
					CEREAL_NVP(textJustification)
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Text> &construct){
				TextLibrary *textLibrary = nullptr;
				Draw2D *renderer = nullptr;
				Size<> boxSize;
				archive(
					cereal::make_nvp("boxSize", boxSize)
				).extract(
					cereal::make_nvp("textLibrary", textLibrary),
					cereal::make_nvp("renderer", renderer)
				);
				construct(renderer, textLibrary, boxSize, "");
				archive(
					cereal::make_nvp("fontIdentifier", construct->fontIdentifier),
					cereal::make_nvp("contentScrollPosition", construct->contentScrollPosition),
					cereal::make_nvp("wrapMethod", construct->wrapMethod),
					cereal::make_nvp("textJustification", construct->textJustification)
				);
			}

			Size<> boxSize;
			Point<> contentScrollPosition;
			
			std::shared_ptr<Scene::Node> textScene;
			std::shared_ptr<Scene::Clipped> textboxScene;
			TextLibrary *textLibrary;
			Draw2D *render;

			TextJustification textJustification = LEFT;
			TextWrapMethod wrapMethod = SOFT;

			FormattedText formattedText;

			size_t cursor;
			UtfString editText;
			std::string fontIdentifier;
			PointPrecision minimumLineHeight;
			bool isSingleLine;
		};

	}
}

#endif
