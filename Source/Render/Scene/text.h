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
				if(cursor >= formattedText.size()){
					incrementCursor(a_text.size());
				}
				formattedText.append(a_text);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(UtfChar a_char){
				if(cursor >= formattedText.size()){
					incrementCursor(1);
				}
				formattedText.append(UtfString(1, a_char));
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> append(Uint16 a_char){
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				return append(*character);
			}

			std::shared_ptr<Text> insertAtCursor(const UtfString &a_text){
				formattedText.insert(cursor, a_text);
				incrementCursor(a_text.size());
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> insertAtCursor(UtfChar a_char){
				formattedText.insert(cursor, UtfString(1, a_char));
				incrementCursor(1);
				return std::static_pointer_cast<Text>(shared_from_this());
			}
			std::shared_ptr<Text> insertAtCursor(Uint16 a_char){
				UtfChar *character = reinterpret_cast<UtfChar*>(&a_char);
				formattedText.insert(cursor, UtfString(1, *character));
				incrementCursor(1);
				return std::static_pointer_cast<Text>(shared_from_this());
			}

			std::shared_ptr<Text> backspace(){
				if(cursor > 0){
					formattedText.erase(cursor - 1, 1);
					incrementCursor(-1);
				}
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
				return formattedText.minimumLineHeight();
			}
			std::shared_ptr<Text> setMinimumLineHeight(PointPrecision a_newLineHeight){
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

			void enableCursor(){
				displayCursor = true;
				setCursor(cursor);
				cursorScene->show();
			}
			void disableCursor(){
				displayCursor = false;
				setCursor(cursor);
				cursorScene->hide();
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

			void incrementCursor(int64_t a_change){
				setCursor(cursor + a_change);
			}
			void setCursor(int64_t a_value){
				auto maxCursor = formattedText.size();
				a_value = std::max<int64_t>(std::min<int64_t>(a_value, maxCursor), 0);
				cursor = a_value;
				auto cursorCharacter = (cursor < maxCursor || cursor == 0) ? formattedText.characterForIndex(cursor) : formattedText.characterForIndex(cursor - 1);
				if(cursorCharacter){
					cursorScene->position(cursorCharacter->position() + cursorCharacter->offset());
					cursorScene->size({2, cursorCharacter->characterSize().height});
					if(cursor >= maxCursor) {
						cursorScene->translate({cursorCharacter->characterSize().width, 0.0f});
					}
					if(displayCursor){
						cursorScene->show();
					}
				} else{
					std::shared_ptr<FormattedLine> line;
					size_t characterIndex;
					std::tie(line, characterIndex) = formattedText.lineForCharacterIndex(cursor);
					float xPosition = 0.0f;
					if(justification() == TextJustification::CENTER){
						xPosition = formattedText.width()/2.0f-1.0f;
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
			}

			bool displayCursor;
			size_t cursor;
			std::shared_ptr<Scene::Rectangle> cursorScene;
			UtfString editText;
			std::string fontIdentifier;
			bool isSingleLine;
		};

	}
}

#endif
