#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <filesystem>
#include <fstream>
#include <string>

namespace jai::foundry::tests {

class web_text_tests : public suite {
public:
	web_text_tests() : suite("Web Text Tests") {}

	void forge_tests() override {
		auto es = [this](const std::string& a_source) { auto engine = make_engine(); stdlib::register_all(*engine); stdlib::register_web(*engine); return engine->execute(a_source).as<std::string>(); };
		auto caught = [es](const std::string& a_source) { return es("string m = \"\"; try { " + a_source + " } catch (e) { m = e; } m;"); };

		test("html_escape_escapes_the_five_and_keeps_everything_else", [this, es]() {
			check_eq(std::string("&lt;a href=&quot;x&quot;&gt;Tom &amp; Jerry&#39;s&lt;/a&gt;"), es("web::html_escape(\"<a href=\\\"x\\\">Tom & Jerry's</a>\");"));
			check_eq(std::string(""), es("web::html_escape(\"\");"));
			check_eq(std::string("plain text, no change"), es("web::html_escape(\"plain text, no change\");"));
			check_eq(std::string("&amp;amp;"), es("web::html_escape(\"&amp;\");"));
			check_eq(std::string("caf\xC3\xA9 &lt;\xF0\x9F\x99\x82&gt; \xFF\x00x", 22), es("web::html_escape(\"caf\xC3\xA9 <\xF0\x9F\x99\x82> \\xFF\\x00x\");"));
			check_eq(std::string("utf8_string 10 \xC3\xA9&lt;b&gt;"), es("var u = web::html_escape(utf8_string(\"\xC3\xA9<b>\")); type_of(u) + \" \" + u.size() + \" \" + u;"));
			check_eq(std::string("string utf8_string"), es("type_of(web::html_escape(\"a\")) + \" \" + type_of(web::html_escape(utf8_string(\"a\")));"));
			check_eq(std::string("x untouched"), es("string s = \"x\"; string t = web::html_escape(s); t += \" untouched\"; s + \" untouched\";"));
		});

		test("html_unescape_reads_references_as_a_browser_does", [this, es]() {
			const auto unescape = [es](const std::string& a_text) { return es("web::html_unescape(\"" + a_text + "\");"); };
			check_eq(std::string("<>&\"''"), unescape("&lt;&gt;&amp;&quot;&apos;&#39;"));
			check_eq(std::string("& < > \" \xC2\xA9 \xC2\xAE & < > \" \xC2\xA9 \xC2\xAE"), unescape("&amp &lt &gt &quot &copy &reg &AMP &LT &GT &QUOT &COPY &REG"));
			check_eq(std::string("\xE2\x88\x89 \xC2\xACit; \xC2\xAC \xC2\xACi &x; &x <x"), unescape("&notin; &notit; &not &noti &ampx; &amp;x &ltx"));
			check_eq(std::string("\xE2\x89\xAA\xE2\x83\x92 \xE2\x89\xAB\xE2\x83\x92 \xE2\x89\x82\xCC\xB8 \xE2\x88\xB3 \xE2\x80\x8C \xC3\x86 \xC3\x86"),
				unescape("&nLt; &nGt; &NotEqualTilde; &CounterClockwiseContourIntegral; &zwnj; &AElig &AElig;"));
			check_eq(std::string(";:,.!?#$%*"), unescape("&semi;&colon;&comma;&period;&excl;&quest;&num;&dollar;&percnt;&ast;"));
			check_eq(std::string("fj \xE2\x81\x9F\xE2\x80\x8A =\xE2\x83\xA5 \xE2\x88\xBE\xCC\xB3"), unescape("&fjlig; &ThickSpace; &bne; &acE;"));
			check_eq(std::string("A A A AB AG A A"), unescape("&#65; &#x41; &#X41; &#65B &#x41G &#0065; &#x0000041;"));
			check_eq(std::string("\xE2\x82\xAC \xE2\x82\xAC \xE2\x80\x93 \xC5\xB8 \xC2\x81 \xC2\x8D \xC5\xB8"), unescape("&#128; &#x80; &#150; &#x9F; &#x81; &#x8D; &#159;"));
			check_eq(std::string("& &; &#; &#x; &#xG; &bogus; &Amp; &#-1; && &&"), unescape("& &; &#; &#x; &#xG; &bogus; &Amp; &#-1; && &&amp;"));
			check_eq(std::string("trailing &"), unescape("trailing &amp"));
			check_eq(std::string("&"), unescape("&"));
			check_eq(std::string("a&b&c"), unescape("a&b&c"));
			check_eq(std::string("&&#&#x"), unescape("&&#&#x"));
		});

		test("html_unescape_turns_impossible_code_points_into_the_replacement_character", [this, es]() {
			check_eq(std::string("\xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD"),
				es("web::html_unescape(\"&#0; &#xD800; &#xDFFF; &#x110000; &#99999999999999999999; &#x0;\");"));
			check_eq(std::string("\xF4\x8F\xBF\xBF \xF4\x8F\xBF\xBF \x01 \x7F"), es("web::html_unescape(\"&#x10FFFF; &#1114111; &#1; &#127;\");"));
		});

		test("html_unescape_keeps_the_text_kind_and_bytes_it_does_not_read", [this, es]() {
			check_eq(std::string("utf8_string 4 \xC3\xA9<\xE2\x82\xAC>"), es("var u = web::html_unescape(utf8_string(\"\xC3\xA9&lt;&euro;&gt;\")); type_of(u) + \" \" + u.size() + \" \" + u;"));
			check_eq(std::string("\xFF<\x00", 3), es("web::html_unescape(\"\\xFF&lt;\\x00\");"));
			check_eq(std::string("string no references"), es("type_of(web::html_unescape(\"no references\")) + \" \" + web::html_unescape(\"no references\");"));
		});

		test("html_escape_then_unescape_round_trips_every_byte_string", [this, es]() {
			check_eq(std::string("0"), es(R"(
				string alphabet = "&;#xX<>\"'amp lt gt quot 39 0123456789 \xC3\xA9\xFF\x00z";
				int seed = 7;
				int bad = 0;
				for (int round = 0; round < 400; ++round) {
					string s = "";
					for (int i = 0; i < round % 37; ++i) {
						seed = (seed * 1103515245 + 12345) % 2147483648;
						s += alphabet[seed % alphabet.size()];
					}
					if (web::html_unescape(web::html_escape(s)) != s) { ++bad; }
					utf8_string u = s;
					if (web::html_unescape(web::html_escape(u)) != u) { ++bad; }
				}
				to_string(bad);)"));
		});

		test("sha256_matches_the_nist_vectors", [this, es]() {
			check_eq(std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), es("sha256(\"\");"));
			check_eq(std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), es("sha256(\"abc\");"));
			check_eq(std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), es("sha256(\"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq\");"));
			check_eq(std::string("cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"),
				es("sha256(\"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu\");"));
			check_eq(std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"), es("sha256(string::repeat(\"a\", 1000000));"));
		});

		test("sha256_pads_at_every_block_boundary_and_hashes_bytes", [this, es]() {
			const std::pair<int, const char*> lengths[] = {
				{55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"}, {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
				{63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"}, {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
				{65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"}, {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
				{120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"}, {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
			};
			for (const auto& [length, digest] : lengths) {
				check_eq(std::string(digest), es("sha256(string::repeat(\"a\", " + std::to_string(length) + "));"), std::to_string(length));
			}
			check_eq(std::string("2c8d07cd986f58eb210bd800133d6645c7340c59865377c8ea431cebca0b3113"), es("sha256(\"\\x00\\xFF\\x00\");"));
			check_eq(std::string("4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c 4a99557e4033c3539de2eb65472017cad5f9557f7a0625a09f1c3f6e2ba69c4c"),
				es("sha256(utf8_string(\"\xC3\xA9\")) + \" \" + sha256(\"\xC3\xA9\");"));
			check_eq(std::string("string"), es("type_of(sha256(\"a\"));"));
		});

		test("arguments_are_checked_at_load_and_at_run_time", [this, caught]() {
			check_eq(std::string("web::html_escape expects a string"), caught("var n = 5; web::html_escape(n);"));
			check_eq(std::string("web::html_unescape expects a string"), caught("var n = null; web::html_unescape(n);"));
			check_eq(std::string("No overload of 'sha256' takes (string, string)"), caught("var f = sha256; f(\"a\", \"b\");"));
			check_eq(std::string("No overload of 'sha256_file' takes ()"), caught("var f = sha256_file; f();"));
			auto engine = make_engine();
			stdlib::register_all(*engine);
			stdlib::register_web(*engine);
			for (const char* source : {"web::html_escape(5);", "sha256();", "sha256(\"a\", \"b\");", "int n = sha256(\"a\");", "sha256_file(1.5);"}) {
				bool rejected = false;
				try { engine->execute(source); } catch (const std::exception&) { rejected = true; }
				check_true(rejected, source);
			}
		});

		test("sha256_file_streams_the_file_and_names_what_it_cannot_read", [this, es, caught]() {
			namespace fs = std::filesystem;
			const fs::path dir = test_temp_directory() / "jai_sha256_file";
			std::error_code error;
			fs::remove_all(dir, error);
			fs::create_directories(dir);
			std::string pattern(200000, '\0');
			for (size_t i = 0; i < pattern.size(); ++i) { pattern[i] = static_cast<char>((i * 7 + 3) % 256); }
			std::ofstream(dir / "pattern.bin", std::ios::binary) << pattern;
			std::ofstream(dir / "empty.bin", std::ios::binary).flush();
			const std::string base = dir.generic_string();
			check_eq(std::string("ec0ebf98b6f2954bf0f7b839402b1ba245996c39d18e155414e91a2b4353c157"), es("sha256_file(\"" + base + "/pattern.bin\");"));
			check_eq(std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), es("sha256_file(utf8_string(\"" + base + "/empty.bin\"));"));
			check_eq("Cannot read file '" + base + "/missing.bin': not found", caught("sha256_file(\"" + base + "/missing.bin\");"));
			check_eq("Cannot read file '" + base + "': not a file", caught("sha256_file(\"" + base + "\");"));
			fs::remove_all(dir, error);
		});

		test("sha256_file_needs_the_filesystem_privilege_and_the_rest_do_not", [this]() {
			auto engine = make_engine();
			stdlib::register_all(*engine);
			stdlib::register_web(*engine);
			engine->disable(privilege::filesystem);
			check_eq(std::string("Insufficient privilege: filesystem."), engine->execute("string m = \"\"; try { sha256_file(\"anything.bin\"); } catch (e) { m = e; } m;").as<std::string>());
			check_eq(std::string("&lt; < ba7816bf"), engine->execute("web::html_escape(\"<\") + \" \" + web::html_unescape(\"&lt;\") + \" \" + sha256(\"abc\").substr(0, 8);").as<std::string>());
		});

		test("the_pure_functions_run_in_parallel_bodies_and_sha256_file_stays_serial", [this, es, caught]() {
			check_eq(std::string("true true"), es(R"(
				array<string> texts = [];
				for (int i = 0; i < 64; ++i) { texts.push("<" + i + "> & '" + i + "'"); }
				array<string> serial = [];
				for (auto t : texts) { serial.push(sha256(web::html_escape(t)) + web::html_unescape(web::html_escape(t))); }
				var parallel = parallel_transform(texts, [](string t) { return sha256(web::html_escape(t)) + web::html_unescape(web::html_escape(t)); });
				array<string> looped = texts;
				parallel_for (auto& t : looped) { t = sha256(web::html_escape(t)) + web::html_unescape(web::html_escape(t)); }
				to_string(to_json(parallel) == to_json(serial)) + " " + to_string(to_json(looped) == to_json(serial));)"));
			check_true(caught("array<string> paths = [\"a\"]; parallel_for (auto& p : paths) { p = sha256_file(p); }").find("host function 'sha256_file' is marked serial_function() and cannot be called in a parallel body") != std::string::npos);
		});

		test("url_encode_and_url_decode_are_rfc_3986_percent_encoding", [this, es]() {
			check_eq(std::string("a%20b%26c%2Fd~-._%C3%A9%3F%3D"), es("web::url_encode(\"a b&c/d~-._\xC3\xA9?=\");"));
			check_eq(std::string("a b/c+d%zz%4"), es("web::url_decode(\"a%20b%2fc+d%zz%4\");"));
			check_eq(std::string("100%"), es("web::url_decode(\"100%\");"));
			check_eq(std::string("0 bad"), es(R"(
				int bad = 0;
				for (int b = 0; b < 256; ++b) {
					string s = "x" + to_char(b) + "y";
					if (web::url_decode(web::url_encode(s)) != s) { ++bad; }
				}
				to_string(bad) + " bad";)"));
		});

		test("form_encode_and_form_decode_are_the_form_encoding", [this, es]() {
			check_eq(std::string("a+b*-._%7E%2B%26"), es("web::form_encode(\"a b*-._~+&\");"));
			check_eq(std::string("a b+&"), es("web::form_decode(\"a+b%2B%26\");"));
			check_eq(std::string("0 bad"), es(R"(
				int bad = 0;
				for (int b = 0; b < 256; ++b) {
					string s = to_char(b) + " " + to_char(b);
					if (web::form_decode(web::form_encode(s)) != s) { ++bad; }
				}
				to_string(bad) + " bad";)"));
		});

		test("parse_query_reads_pairs_and_parse_query_all_keeps_every_value", [this, es]() {
			check_eq(std::string(R"({"a":"last","b":"two words","c":"&","empty":""})"), es(R"(to_json(web::parse_query("?a=1&b=two+words&c=%26&empty&a=last&=x&&"));)"));
			check_eq(std::string(R"({"t":["a","b c"],"u":[""]})"), es(R"(to_json(web::parse_query_all("t=a&t=b+c&u="));)"));
			check_eq(std::string("1 0"), es(R"(map<string, string> m = web::parse_query("a=1"); m["a"] + " " + to_string(web::parse_query("").size());)"));
		});

		test("build_query_writes_the_map_in_key_order", [this, es, caught]() {
			check_eq(std::string("a=1&b=two+words&f=2.5&ok=true&t=x&t=y+z"),
				es(R"(web::build_query({"b": "two words", "a": 1, "n": null, "t": ["x", "y z"], "f": 2.5, "ok": true});)"));
			check_eq(std::string(R"({"k&":"v=1","x":"a b"})"), es(R"(to_json(web::parse_query(web::build_query({"k&": "v=1", "x": "a b"})));)"));
			check_eq(std::string("web::build_query: the value of 'm' is a map"), caught(R"(web::build_query({"m": {"x": 1}});)"));
			check_eq(std::string(""), es("web::build_query({});"));
		});

		test("parse_url_splits_rfc_3986_references_and_to_string_joins_them", [this, es, caught]() {
			check_eq(std::string("https|user|pw|example.com|8443|/a/b|x=1&y=2|frag|https://user:pw@example.com:8443/a/b?x=1&y=2#frag"), es(R"(
				web::url u = web::parse_url("https://user:pw@example.com:8443/a/b?x=1&y=2#frag");
				u.scheme + "|" + u.user + "|" + u.password + "|" + u.host + "|" + u.port + "|" + u.path + "|" + u.query + "|" + u.fragment + "|" + u.to_string();)"));
			check_eq(std::string("||0|/relative/path|q|"), es(R"(web::url u = web::parse_url("/relative/path?q"); u.scheme + "|" + u.host + "|" + u.port + "|" + u.path + "|" + u.query + "|" + u.fragment;)"));
			check_eq(std::string("[::1] 80 /"), es(R"(web::url u = web::parse_url("http://[::1]:80/"); u.host + " " + u.port + " " + u.path;)"));
			check_eq(std::string("mailto||ada@example.org"), es(R"(web::url u = web::parse_url("mailto:ada@example.org"); u.scheme + "|" + u.host + "|" + u.path;)"));
			check_eq(std::string("web::parse_url: 'http://host:99999/' has a malformed host or port"), caught(R"(web::parse_url("http://host:99999/");)"));
			check_eq(std::string("https://x.org/p?q=a+b"), es(R"(
				web::url u;
				u.scheme = "https";
				u.host = "x.org";
				u.path = "/p";
				u.query = web::build_query({"q": "a b"});
				u.to_string();)"));
		});

		test("the_web_library_is_registered_only_by_register_web_and_its_calls_are_checked", [this]() {
			auto plain = make_engine();
			stdlib::register_all(*plain);
			bool missing = false;
			try { plain->execute("web::url_encode(\"a\");"); } catch (const std::exception&) { missing = true; }
			check_true(missing, "register_all registers no web:: functions");
			auto engine = make_engine();
			stdlib::register_all(*engine);
			stdlib::register_web(*engine);
			for (const char* source : {"web::url_encode(1);", "int n = web::url_encode(\"a\");", "web::parse_query();", "web::build_query(\"a=1\");", "web::html_escape(\"a\", \"b\");"}) {
				bool rejected = false;
				try { engine->execute(source); } catch (const std::exception&) { rejected = true; }
				check_true(rejected, source);
			}
		});

		test("the_web_functions_run_in_parallel_bodies", [this, es]() {
			check_eq(std::string("true true"), es(R"(
				string mix(string t) {
					web::url u = web::parse_url("http://h/" + web::form_encode(t));
					return web::url_encode(t) + web::html_escape(t) + web::build_query(web::parse_query(t)) + u.path;
				}
				array<string> texts = [];
				for (int i = 0; i < 64; ++i) { texts.push("a=" + i + "&b=<" + i + "> & '" + i + "'"); }
				array<string> serial = [];
				for (auto t : texts) { serial.push(mix(t)); }
				var parallel = parallel_transform(texts, [](string t) { return mix(t); });
				array<string> looped = texts;
				parallel_for (auto& t : looped) { t = mix(t); }
				to_string(to_json(parallel) == to_json(serial)) + " " + to_string(to_json(looped) == to_json(serial));)"));
		});
	}
};

} // namespace jai::foundry::tests

using web_text_tests = jai::foundry::tests::web_text_tests;
FOUNDRY_REGISTER(web_text_tests)
