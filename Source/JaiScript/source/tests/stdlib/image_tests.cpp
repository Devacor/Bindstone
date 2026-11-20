#ifdef JAISCRIPT_ENABLE_IMAGE

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/detail/image_codec.hpp>
#include "stdlib/image_fixtures.hpp"
#include "stdlib/image_test_support.hpp"
#include "fuzz/image_mutation.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace jai::foundry::tests {

using namespace jai::image_test;
namespace codec = jai::image_codec;

class image_codec_tests : public suite {
public:
	image_codec_tests() : suite("Image Codec") {}

	static codec::status decode_bytes(const bytes& a_file, codec::bitmap& a_pixels, codec::limits a_limits = {}) {
		codec::header header;
		return codec::decode(a_file.data(), a_file.size(), a_limits, codec::work_hook {}, header, a_pixels);
	}
	static std::string outcome(const bytes& a_file, codec::limits a_limits = {}) {
		codec::bitmap pixels;
		const codec::status result = decode_bytes(a_file, pixels, a_limits);
		return result == codec::status::ok ? "ok" : codec::status_text(result);
	}

	void forge_tests() override {
		test("checksums", [this]() {
			const char* digits = "123456789";
			check_eq(uint32_t {0xcbf43926u}, codec::crc32(reinterpret_cast<const uint8_t*>(digits), 9));
			check_eq(uint32_t {0}, codec::crc32(nullptr, 0));
			check_eq(uint32_t {0x091e01deu}, codec::adler32(reinterpret_cast<const uint8_t*>(digits), 9));
			check_eq(uint32_t {1}, codec::adler32(nullptr, 0));
			// the running forms agree with the whole
			const uint32_t part = codec::crc32(reinterpret_cast<const uint8_t*>(digits), 4);
			check_eq(uint32_t {0xcbf43926u}, codec::crc32(reinterpret_cast<const uint8_t*>(digits) + 4, 5, part));
			bytes long_run(100000, 0xff);
			uint32_t low = 1, high = 0;
			for (uint8_t b : long_run) { low = (low + b) % 65521; high = (high + low) % 65521; }
			check_eq((high << 16) | low, codec::adler32(long_run.data(), long_run.size()));
		});

		test("inflate_stored_and_fixed_blocks", [this]() {
			const bytes text = {'a', 'b', 'c', 'a', 'b', 'c', 'a', 'b', 'c', 'd'};
			check_eq(std::string("ok"), inflate_outcome(stored_zlib(text), text));
			// an empty stored block before the data, and the data over two blocks
			bytes two = {0x78, 0x01, 0x00, 0x00, 0x00, 0xff, 0xff, 0x00, 0x04, 0x00, 0xfb, 0xff, 'a', 'b', 'c', 'a', 0x01, 0x06, 0x00, 0xf9, 0xff, 'b', 'c', 'a', 'b', 'c', 'd'};
			put_32(two, codec::adler32(text.data(), text.size()));
			check_eq(std::string("ok"), inflate_outcome(two, text));

			// fixed codes: 'a', then a match of length 10 at distance 1
			bit_writer fixed;
			fixed.bits(1, 1);
			fixed.bits(1, 2);
			fixed.fixed_literal('a');
			fixed.fixed_length_symbol(264);
			fixed.code(0, 5);
			fixed.fixed_length_symbol(256);
			check_eq(std::string("ok"), inflate_outcome(zlib_wrap(fixed.finish(), bytes(11, 'a')), bytes(11, 'a')));
		});

		test("inflate_refuses_malformed_streams", [this]() {
			const bytes eleven(11, 'a');
			auto fixed_block = [](auto&& a_body) {
				bit_writer writer;
				writer.bits(1, 1);
				writer.bits(1, 2);
				a_body(writer);
				return writer.finish();
			};
			// a distance before the start of the output
			const bytes far = fixed_block([](bit_writer& w) { w.fixed_literal('a'); w.fixed_length_symbol(257); w.code(1, 5); w.fixed_length_symbol(256); });
			check_eq(std::string("distance_too_far"), inflate_outcome(zlib_wrap(far, bytes(4, 'a')), bytes(4, 'a')));
			// distance symbols 30 and 31 and length symbols 286 and 287 name nothing
			const bytes distance_30 = fixed_block([](bit_writer& w) { w.fixed_literal('a'); w.fixed_length_symbol(257); w.code(30, 5); w.fixed_length_symbol(256); });
			check_eq(std::string("bad_symbol"), inflate_outcome(zlib_wrap(distance_30, bytes(4, 'a')), bytes(4, 'a')));
			const bytes length_286 = fixed_block([](bit_writer& w) { w.fixed_literal('a'); w.fixed_length_symbol(286); w.code(0, 5); w.fixed_length_symbol(256); });
			check_eq(std::string("bad_symbol"), inflate_outcome(zlib_wrap(length_286, bytes(4, 'a')), bytes(4, 'a')));
			// more and less than the caller's size
			const bytes run = fixed_block([](bit_writer& w) { w.fixed_literal('a'); w.fixed_length_symbol(264); w.code(0, 5); w.fixed_length_symbol(256); });
			check_eq(std::string("too_much_output"), inflate_outcome(zlib_wrap(run, eleven), bytes(10, 'a')));
			check_eq(std::string("too_little_output"), inflate_outcome(zlib_wrap(run, eleven), bytes(12, 'a')));
			// the checksum, the block type, the stored length's complement, the stream header
			bytes bad_sum = zlib_wrap(run, eleven);
			bad_sum.back() ^= 1;
			check_eq(std::string("bad_checksum"), inflate_outcome(bad_sum, eleven));
			bit_writer reserved;
			reserved.bits(1, 1);
			reserved.bits(3, 2);
			check_eq(std::string("bad_block_type"), inflate_outcome(zlib_wrap(reserved.finish(), eleven), eleven));
			bytes stored = stored_zlib(eleven);
			stored[5] ^= 0x10;
			check_eq(std::string("bad_stored_length"), inflate_outcome(stored, eleven));
			bytes header = stored_zlib(eleven);
			header[0] = 0x79;
			check_eq(std::string("bad_stream_header"), inflate_outcome(header, eleven));
			header[0] = 0x78;
			header[1] = 0x20 | 0x1b;   // a preset dictionary, which PNG never has
			check_eq(std::string("bad_stream_header"), inflate_outcome(header, eleven));
			// every prefix of a good stream is a failure, never a short success
			const bytes whole = zlib_wrap(run, eleven);
			for (size_t length = 0; length < whole.size(); ++length) {
				check_ne(std::string("ok"), inflate_outcome(bytes(whole.begin(), whole.begin() + length), eleven));
			}
		});

		test("inflate_validates_dynamic_code_tables", [this]() {
			const bytes one(1, 'a');
			// a_lengths: the 19 code-length code lengths in transmission order; a_body writes the coded lengths and the data
			auto dynamic_block = [](unsigned a_literals, unsigned a_distances, const std::vector<unsigned>& a_lengths, auto&& a_body) {
				bit_writer writer;
				writer.bits(1, 1);
				writer.bits(2, 2);
				writer.bits(a_literals - 257, 5);
				writer.bits(a_distances - 1, 5);
				writer.bits(static_cast<unsigned>(a_lengths.size()) - 4, 4);
				for (unsigned length : a_lengths) { writer.bits(length, 3); }
				a_body(writer);
				return writer.finish();
			};
			// the code-length code itself over-subscribed: four codes of length 1
			const bytes over = dynamic_block(257, 1, {1, 1, 1, 1}, [](bit_writer&) {});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(over, one), one));
			// and incomplete: one code of length 2
			const bytes under = dynamic_block(257, 1, {2, 0, 0, 0}, [](bit_writer&) {});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(under, one), one));
			// more literal/length codes than the format has
			const bytes many = dynamic_block(288, 1, {1, 1, 0, 0}, [](bit_writer&) {});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(many, one), one));

			// The code-length code used below: symbol 1 is "0", symbol 18 is "10", symbol 2 is "110", symbol 16 is "111".
			// Transmission order is 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15.
			const std::vector<unsigned> code = {3, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 1};
			auto one_bit = [](bit_writer& w) { w.code(0, 1); };
			auto skip = [](bit_writer& w, unsigned a_zeros) { w.code(2, 2); w.bits(a_zeros - 11, 7); };
			auto two_bits = [](bit_writer& w) { w.code(6, 3); };
			auto repeat = [](bit_writer& w, unsigned a_times) { w.code(7, 3); w.bits(a_times - 3, 2); };

			// literal 'a' and the end of block as the two one-bit codes, one distance code: a good block
			const bytes good = dynamic_block(257, 1, code, [&](bit_writer& w) {
				skip(w, 97); one_bit(w); skip(w, 138); skip(w, 20); one_bit(w); one_bit(w);
				w.code(0, 1); w.code(1, 1);
			});
			check_eq(std::string("ok"), inflate_outcome(zlib_wrap(good, one), one));
			// the same with no code for the end of block
			const bytes endless = dynamic_block(257, 1, code, [&](bit_writer& w) {
				skip(w, 97); one_bit(w); one_bit(w); skip(w, 138); skip(w, 20); one_bit(w);
			});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(endless, one), one));
			// three one-bit literal/length codes: over-subscribed
			const bytes crowded = dynamic_block(257, 1, code, [&](bit_writer& w) {
				skip(w, 97); one_bit(w); one_bit(w); skip(w, 138); skip(w, 19); one_bit(w); one_bit(w);
			});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(crowded, one), one));
			// one two-bit code beside a one-bit code: incomplete
			const bytes sparse = dynamic_block(257, 1, code, [&](bit_writer& w) {
				skip(w, 97); two_bits(w); skip(w, 138); skip(w, 20); one_bit(w); one_bit(w);
			});
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(sparse, one), one));
			// "repeat the previous length" with no previous length
			const bytes first_repeat = dynamic_block(257, 1, code, [&](bit_writer& w) { repeat(w, 3); });
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(first_repeat, one), one));
			// a run of zeros past the last code
			const bytes overrun = dynamic_block(257, 1, code, [&](bit_writer& w) { skip(w, 138); skip(w, 138); });
			check_eq(std::string("bad_code_lengths"), inflate_outcome(zlib_wrap(overrun, one), one));
		});

		test("png_fixtures_decode_to_the_reference_pixels", [this]() {
			size_t seen = 0;
			for (const image_fixtures::fixture& each : image_fixtures::k_fixtures) {
				if (std::strcmp(each.format, "png") != 0) { continue; }
				++seen;
				codec::header header;
				check_eq(std::string(""), std::string(codec::status_text(codec::read_header(each.bytes, each.size, codec::limits {}, header))), each.name);
				check_eq(each.width, header.width, each.name);
				check_eq(each.height, header.height, each.name);
				check_true(header.kind == codec::format::png, each.name);
				codec::header decoded;
				codec::bitmap pixels;
				check_eq(std::string(""), std::string(codec::status_text(codec::decode(each.bytes, each.size, codec::limits {}, codec::work_hook {}, decoded, pixels))), each.name);
				check_eq(each.width, pixels.width, each.name);
				check_eq(each.height, pixels.height, each.name);
				check_eq(each.rgba_hash, fnv(pixels.rgba), each.name);
			}
			check_ge(seen, size_t {26});
		});

		test("png_colour_types_the_reference_does_not_write", [this]() {
			// grey with alpha, 8 and 16 bits
			check_pixels(png_file(2, 1, 8, 4, 0, rows({{10, 20, 200, 255}})), {10, 10, 10, 20, 200, 200, 200, 255});
			check_pixels(png_file(2, 1, 16, 4, 0, rows({{0x12, 0x34, 0x80, 0x00, 0xff, 0xff, 0x00, 0x7f}})), {0x12, 0x12, 0x12, 0x80, 255, 255, 255, 0});
			// tRNS names the one transparent grey level, compared at the file's depth
			png_parts grey2;
			grey2.before_data = {{"tRNS", {0, 2}}};
			check_pixels(png_file(4, 1, 2, 0, 0, rows({{0x1b}}), grey2), {0, 0, 0, 255, 85, 85, 85, 255, 170, 170, 170, 0, 255, 255, 255, 255});
			png_parts grey16;
			grey16.before_data = {{"tRNS", {0x12, 0x34}}};
			check_pixels(png_file(2, 1, 16, 0, 0, rows({{0x12, 0x34, 0x12, 0x35}}), grey16), {0x12, 0x12, 0x12, 0, 0x12, 0x12, 0x12, 255});
			// and the one transparent colour of an RGB image
			png_parts rgb;
			rgb.before_data = {{"tRNS", {0, 9, 0, 8, 0, 7}}};
			check_pixels(png_file(2, 1, 8, 2, 0, rows({{9, 8, 7, 9, 8, 6}}), rgb), {9, 8, 7, 0, 9, 8, 6, 255});
			png_parts rgb16;
			rgb16.before_data = {{"tRNS", {1, 2, 3, 4, 5, 6}}};
			check_pixels(png_file(2, 1, 16, 2, 0, rows({{1, 2, 3, 4, 5, 6, 1, 2, 3, 4, 5, 7}}), rgb16), {1, 3, 5, 0, 1, 3, 5, 255});
			// a tRNS beside an alpha channel, which the format forbids, is ignored as browsers ignore it
			png_parts alpha_key;
			alpha_key.before_data = {{"tRNS", {0, 9, 0, 8, 0, 7}}};
			check_pixels(png_file(1, 1, 8, 6, 0, rows({{9, 8, 7, 200}}), alpha_key), {9, 8, 7, 200});
			// a palette shorter than the depth allows, with fewer tRNS entries than colours
			png_parts palette;
			palette.before_data = {{"PLTE", {1, 2, 3, 4, 5, 6, 7, 8, 9}}, {"tRNS", {0, 128}}};
			check_pixels(png_file(3, 1, 8, 3, 0, rows({{2, 0, 1}}), palette), {7, 8, 9, 255, 1, 2, 3, 0, 4, 5, 6, 128});
		});

		test("png_filters_chunks_and_split_data", [this]() {
			// 5 x 6 RGB, each row under a different filter (the sixth repeats Paeth); the filters are applied here, by the definition
			const size_t width = 5, height = 6;
			bytes plain;
			for (size_t i = 0; i < width * height * 3; ++i) { plain.push_back(static_cast<uint8_t>((i * 37 + (i / 7) * 11) & 255)); }
			bytes expected;
			for (size_t i = 0; i < width * height; ++i) {
				expected.insert(expected.end(), plain.begin() + 3 * i, plain.begin() + 3 * i + 3);
				expected.push_back(255);
			}
			const bytes filtered = filter_rows(plain, width * 3, 3, {0, 1, 2, 3, 4, 4});
			check_pixels(png_file(5, 6, 8, 2, 0, filtered), expected);
			// the first row under Up, Average and Paeth sees a row of zeros above it
			for (uint8_t first : {uint8_t {2}, uint8_t {3}, uint8_t {4}}) {
				check_pixels(png_file(5, 6, 8, 2, 0, filter_rows(plain, width * 3, 3, {first, 4, 3, 2, 1, 0})), expected);
			}
			// the data over many IDAT chunks (one of them empty), ancillary chunks around it, bytes after IEND
			png_parts parts;
			parts.before_data = {{"gAMA", {0, 0, 0xb1, 0x8f}}, {"tEXt", {'k', 0, 'v'}}};
			parts.after_data = {{"tIME", {7, 0xe8, 1, 1, 0, 0, 0}}};
			parts.data_pieces = 7;
			parts.empty_data_chunk = true;
			bytes split = png_file(5, 6, 8, 2, 0, filtered, parts);
			split.push_back(0x55);
			check_pixels(split, expected);
		});

		test("png_refuses_malformed_files", [this]() {
			const bytes row = rows({{1, 2, 3}});
			check_eq(std::string("ok"), outcome(png_file(1, 1, 8, 2, 0, row)));
			check_eq(std::string("PNG header is invalid"), outcome(png_file(0, 1, 8, 2, 0, row)));
			check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 0, 8, 2, 0, row)));
			check_eq(std::string("PNG header is invalid"), outcome(png_file(0x80000000u, 1, 8, 2, 0, row)));
			check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 0xffffffffu, 8, 2, 0, row)));
			// depth and colour type pairs the format does not have
			for (auto [depth, colour] : std::vector<std::pair<int, int>> {{3, 0}, {4, 2}, {16, 3}, {4, 4}, {2, 6}, {8, 1}, {8, 5}, {8, 7}, {0, 0}, {32, 2}}) {
				check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 1, static_cast<uint8_t>(depth), static_cast<uint8_t>(colour), 0, row)));
			}
			check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 1, 8, 2, 2, row)));
			png_parts method;
			method.compression_method = 1;
			check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 1, 8, 2, 0, row, method)));
			png_parts filter_method;
			filter_method.filter_method = 1;
			check_eq(std::string("PNG header is invalid"), outcome(png_file(1, 1, 8, 2, 0, row, filter_method)));

			// the decompression bomb: a header alone decides, before anything is allocated
			check_eq(std::string("Image has more pixels than the pixel limit"), outcome(png_file(0x7fffffffu, 0x7fffffffu, 8, 6, 0, row)));
			check_eq(std::string("Image has more pixels than the pixel limit"), outcome(png_file(5001, 5000, 8, 6, 0, row)));
			codec::header header;
			const bytes bomb = png_file(60000, 60000, 1, 0, 0, row);
			check_true(codec::read_header(bomb.data(), bomb.size(), codec::limits {}, header) == codec::status::too_many_pixels);
			codec::limits wide;
			wide.max_pixels = ~uint64_t {0};
			check_eq(std::string("Image has more pixels than the pixel limit"), outcome(png_file(0x7fffffffu, 0x7fffffffu, 8, 6, 0, row), wide));
			// under the limit, a few bytes of data cannot be 25 million pixels
			check_eq(std::string("PNG image data does not match its header"), outcome(png_file(5000, 5000, 8, 6, 0, row)));
			codec::limits small;
			small.max_pixels = 3;
			check_eq(std::string("ok"), outcome(png_file(3, 1, 8, 0, 0, rows({{1, 2, 3}})), small));
			check_eq(std::string("Image has more pixels than the pixel limit"), outcome(png_file(2, 2, 8, 0, 0, rows({{1, 2}, {3, 4}})), small));
			codec::limits tiny_input;
			tiny_input.max_input_bytes = 20;
			check_eq(std::string("Image input is larger than the input size limit"), outcome(png_file(1, 1, 8, 2, 0, row), tiny_input));

			// the inflated size against the header: a row short, a row over
			check_eq(std::string("PNG image data does not match its header"), outcome(png_file(1, 2, 8, 2, 0, row)));
			check_eq(std::string("PNG image data does not match its header"), outcome(png_file(1, 1, 8, 2, 0, rows({{1, 2, 3}, {4, 5, 6}}))));
			check_eq(std::string("PNG row has an invalid filter type"), outcome(png_file(1, 1, 8, 2, 0, {5, 1, 2, 3})));

			// chunks: a length past the end, a damaged CRC, a critical chunk nobody knows, order
			bytes long_chunk = png_file(1, 1, 8, 2, 0, row);
			long_chunk[33] = 0x7f;
			check_eq(std::string("PNG is truncated"), outcome(long_chunk));
			bytes huge_chunk = png_file(1, 1, 8, 2, 0, row);
			huge_chunk[33] = 0xff;
			check_eq(std::string("PNG header is invalid"), outcome(huge_chunk));
			bytes bad_crc = png_file(1, 1, 8, 2, 0, row);
			bad_crc[29] ^= 1;
			check_eq(std::string("PNG chunk has a bad CRC"), outcome(bad_crc));
			png_parts critical;
			critical.before_data = {{"ABCD", {1}}};
			check_eq(std::string("PNG has a critical chunk this decoder does not know"), outcome(png_file(1, 1, 8, 2, 0, row, critical)));
			png_parts second_header;
			second_header.before_data = {{"IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0}}};
			check_eq(std::string("PNG chunks are missing or out of order"), outcome(png_file(1, 1, 8, 2, 0, row, second_header)));
			png_parts no_data;
			no_data.data_pieces = 0;
			check_eq(std::string("PNG chunks are missing or out of order"), outcome(png_file(1, 1, 8, 2, 0, row, no_data)));
			png_parts apart;
			apart.data_pieces = 2;
			apart.between_data = {{"tEXt", {'k', 0, 'v'}}};
			check_eq(std::string("PNG chunks are missing or out of order"), outcome(png_file(1, 1, 8, 2, 0, row, apart)));
			png_parts no_end;
			no_end.end = false;
			check_eq(std::string("PNG is truncated"), outcome(png_file(1, 1, 8, 2, 0, row, no_end)));
			bytes no_header = png_file(1, 1, 8, 2, 0, row);
			no_header[12] = 'i';
			check_ne(std::string("ok"), outcome(no_header));

			// palettes: none, one of the wrong size, an index past its end, tRNS longer than it
			check_eq(std::string("PNG chunks are missing or out of order"), outcome(png_file(1, 1, 8, 3, 0, rows({{0}}))));
			png_parts ragged;
			ragged.before_data = {{"PLTE", {1, 2, 3, 4}}};
			check_eq(std::string("PNG palette is invalid"), outcome(png_file(1, 1, 8, 3, 0, rows({{0}}), ragged)));
			png_parts wide_palette;
			wide_palette.before_data = {{"PLTE", bytes(15, 9)}};
			check_eq(std::string("PNG palette is invalid"), outcome(png_file(1, 1, 2, 3, 0, rows({{0}}), wide_palette)));
			png_parts short_palette;
			short_palette.before_data = {{"PLTE", {1, 2, 3, 4, 5, 6}}};
			check_eq(std::string("ok"), outcome(png_file(1, 1, 8, 3, 0, rows({{1}}), short_palette)));
			check_eq(std::string("PNG palette is invalid"), outcome(png_file(1, 1, 8, 3, 0, rows({{2}}), short_palette)));
			png_parts long_alpha;
			long_alpha.before_data = {{"PLTE", {1, 2, 3}}, {"tRNS", {1, 2}}};
			check_eq(std::string("PNG palette is invalid"), outcome(png_file(1, 1, 8, 3, 0, rows({{0}}), long_alpha)));
			png_parts grey_palette;
			grey_palette.before_data = {{"PLTE", {1, 2, 3}}};
			check_eq(std::string("PNG palette is invalid"), outcome(png_file(1, 1, 8, 0, 0, rows({{0}}), grey_palette)));

			// damaged compressed data
			png_parts damaged;
			damaged.corrupt_data_byte = 0;
			check_eq(std::string("PNG compressed data is invalid"), outcome(png_file(1, 1, 8, 2, 0, row, damaged)));
			check_eq(std::string("Image data is not a JPEG, PNG or GIF"), outcome(bytes {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
			check_eq(std::string("Image data is not a JPEG, PNG or GIF"), outcome(bytes {}));
		});

		test("png_every_prefix_is_refused", [this]() {
			for (const image_fixtures::fixture& each : image_fixtures::k_fixtures) {
				if (std::strcmp(each.format, "png") != 0 || each.size > 1300) { continue; }
				for (size_t length = 0; length < each.size; ++length) {
					codec::bitmap pixels;
					codec::header header;
					const codec::status result = codec::decode(each.bytes, length, codec::limits {}, codec::work_hook {}, header, pixels);
					if (result == codec::status::ok || codec::status_text(result)[0] == '\0') {
						check_true(false, std::string(each.name) + " cut to " + std::to_string(length) + " bytes");
						break;
					}
				}
			}
		});

		test("decoders_spend_the_work_budget", [this]() {
			for (const image_fixtures::fixture& each : image_fixtures::k_fixtures) {
				if (each.width * each.height < 1000) { continue; }
				size_t spent = 0;
				codec::work_hook counting {[](void* a_context, size_t a_units) { *static_cast<size_t*>(a_context) += a_units; return false; }, &spent};
				codec::header header;
				codec::bitmap pixels;
				check_true(codec::decode(each.bytes, each.size, codec::limits {}, counting, header, pixels) == codec::status::ok, each.name);
				check_gt(spent, size_t {each.width} * each.height / 8, each.name);
				codec::work_hook gone {[](void*, size_t) { return true; }, nullptr};
				check_true(codec::decode(each.bytes, each.size, codec::limits {}, gone, header, pixels) == codec::status::budget, each.name);
			}
		});

		test("mutated_files_never_escape_the_catalogue", [this]() {
			for (const char* format : image_mutation::k_formats) {
				if (image_mutation::fixtures_of(format).empty()) { continue; }
				const image_mutation::summary result = image_mutation::run(format, 0, 4000, 20.0);
				check_eq(uint64_t {0}, result.wrong, image_mutation::describe(format, result));
				check_gt(result.cases, uint64_t {500}, image_mutation::describe(format, result));
			}
		});
	}

private:
	static const char* inflate_name(codec::inflate_status a_status) {
		switch (a_status) {
			case codec::inflate_status::ok: return "ok";
			case codec::inflate_status::budget: return "budget";
			case codec::inflate_status::truncated: return "truncated";
			case codec::inflate_status::bad_stream_header: return "bad_stream_header";
			case codec::inflate_status::bad_block_type: return "bad_block_type";
			case codec::inflate_status::bad_stored_length: return "bad_stored_length";
			case codec::inflate_status::bad_code_lengths: return "bad_code_lengths";
			case codec::inflate_status::bad_symbol: return "bad_symbol";
			case codec::inflate_status::distance_too_far: return "distance_too_far";
			case codec::inflate_status::too_much_output: return "too_much_output";
			case codec::inflate_status::too_little_output: return "too_little_output";
			default: return "bad_checksum";
		}
	}

	// "ok" when a_stream inflates to exactly a_expected, else the failure's name ("wrong bytes" for a quiet difference)
	static std::string inflate_outcome(const bytes& a_stream, const bytes& a_expected) {
		bytes out(a_expected.size(), 0xee);
		const codec::inflate_status result = codec::inflate_zlib(a_stream.data(), a_stream.size(), out.data(), out.size(), codec::work_hook {});
		if (result != codec::inflate_status::ok) { return inflate_name(result); }
		return out == a_expected ? "ok" : "wrong bytes";
	}

	void check_pixels(const bytes& a_file, const bytes& a_expected) {
		codec::bitmap pixels;
		const codec::status result = decode_bytes(a_file, pixels);
		check_eq(std::string(""), std::string(codec::status_text(result)));
		check_true(pixels.rgba == a_expected, "pixels differ: " + hex(pixels.rgba) + " expected " + hex(a_expected));
	}
};

} // namespace jai::foundry::tests

using image_codec_tests = jai::foundry::tests::image_codec_tests;
FOUNDRY_REGISTER(image_codec_tests)

#endif
