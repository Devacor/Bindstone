// Text measuring: a font measures and fits a string without laying it out.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/formattedText.h"

using namespace jai::foundry;

namespace mv_tests {

	class text_measure_suite : public suite {
	public:
		text_measure_suite() : suite("MV Text Measure") {}

		void forge_tests() override {
			test("a font measures a string as the sum of its glyphs, one line high", [] {
				SceneHost host;
				host.loadFont("measured");
				auto font = host.textLibrary.get("measured");
				check_not_null(font.get(), "the font loads without a renderer");
				check_near(0.0f, font->measure("").width, 1e-3f);
				check_true(font->measure("a").width > 0.0f, "a glyph has width without a renderer");
				check_near(font->measure("a").width + font->measure("b").width, font->measure("ab").width, 1e-3f);
				check_true(font->measure("WWW").width > font->measure("iii").width, "wide glyphs measure wider than narrow ones");
				check_near(font->height(), font->measure("anything").height, 1e-3f);
			});

			test("text that fits is left alone", [] {
				SceneHost host;
				host.loadFont("measured");
				auto font = host.textLibrary.get("measured");
				std::string text = "position";
				check_eq(text, font->fit(text, font->measure(text).width));
				check_eq(text, font->fit(text, 10000.0f));
			});

			test("text that does not fit keeps the longest start that does, then an ellipsis", [] {
				SceneHost host;
				host.loadFont("measured");
				auto font = host.textLibrary.get("measured");
				std::string text = "maximumSpawnRate";
				float width = font->measure(text).width * 0.6f;
				std::string fitted = font->fit(text, width);
				check_true(fitted.size() > 3 && fitted.size() < text.size(), "something is cut: " + fitted);
				check_eq(std::string("..."), fitted.substr(fitted.size() - 3));
				check_eq(text.substr(0, fitted.size() - 3), fitted.substr(0, fitted.size() - 3), "what is kept is the start of the text");
				check_true(font->measure(fitted).width <= width, "the result fits");
				std::string oneMore = text.substr(0, fitted.size() - 2) + "...";
				check_true(font->measure(oneMore).width > width, "one more character would not fit");
			});

			test("a width that cannot hold the ellipsis fits nothing", [] {
				SceneHost host;
				host.loadFont("measured");
				auto font = host.textLibrary.get("measured");
				check_eq(std::string(), font->fit("maximumSpawnRate", font->measure("...").width - 1.0f));
				check_eq(std::string("-"), font->fit("maximumSpawnRate", font->measure("-").width, "-"), "a narrower ellipsis is honoured");
			});

			test("a multi-byte character is kept whole", [] {
				SceneHost host;
				host.loadFont("measured");
				auto font = host.textLibrary.get("measured");
				std::string accented = "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9";
				check_near(font->measure("\xC3\xA9").width * 6.0f, font->measure(accented).width, 1e-3f);
				std::string fitted = font->fit(accented, font->measure("\xC3\xA9").width * 3.5f, "");
				check_eq(size_t(6), fitted.size(), "three two-byte characters fit, none split");
			});
		}
	};

}

using text_measure_suite = mv_tests::text_measure_suite;
FOUNDRY_REGISTER(text_measure_suite)
