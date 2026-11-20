#ifdef JAISCRIPT_ENABLE_IMAGE

#include <jaiscript/testing/foundry.hpp>
#include <jaiscript/testing/temporary_directory.hpp>
#include <jaiscript/core/engine.hpp>
#include <jaiscript/stdlib/stdlib.hpp>
#include <jaiscript/detail/image_codec.hpp>
#include "stdlib/image_fixtures.hpp"
#include "stdlib/image_test_support.hpp"
#include "fuzz/image_mutation.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace jai::foundry::tests {

using namespace jai::image_test;
namespace codec = jai::image_codec;

inline const image_fixtures::fixture& fixture_named(const std::string& a_name) {
	for (const image_fixtures::fixture& each : image_fixtures::k_fixtures) {
		if (a_name == each.name) { return each; }
	}
	throw std::runtime_error("no fixture " + a_name);
}

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
			check_eq(std::string("Image data is not a JPEG, PNG or GIF (read a file with image::load(file::read(path)))"), outcome(bytes {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}));
			check_eq(std::string("Image data is not a JPEG, PNG or GIF (read a file with image::load(file::read(path)))"), outcome(bytes {}));
		});

		test("jpeg_fixtures_decode_within_three_of_the_reference", [this]() {
			// Windows' decoder is the reference: its inverse DCT rounds differently, so a channel may differ by 2 (3 in the photograph)
			size_t seen = 0;
			for (const image_fixtures::fixture& each : image_fixtures::k_fixtures) {
				if (std::strcmp(each.format, "jpeg") != 0) { continue; }
				++seen;
				codec::header header;
				check_eq(std::string(""), std::string(codec::status_text(codec::read_header(each.bytes, each.size, codec::limits {}, header))), each.name);
				check_eq(each.width, header.width, each.name);
				check_eq(each.height, header.height, each.name);
				codec::header decoded;
				codec::bitmap pixels;
				check_eq(std::string(""), std::string(codec::status_text(codec::decode(each.bytes, each.size, codec::limits {}, codec::work_hook {}, decoded, pixels))), each.name);
				codec::header reference_header;
				codec::bitmap reference;
				check_true(codec::decode(each.reference, each.reference_size, codec::limits {}, codec::work_hook {}, reference_header, reference) == codec::status::ok, each.name);
				const difference measured = compare(pixels, reference);
				check_le(measured.largest, 3, std::string(each.name) + ": " + measured.describe());
				check_true(measured.mean < 0.1, std::string(each.name) + ": " + measured.describe());
			}
			check_ge(seen, size_t {27});
		});

		test("jpeg_exif_orientation_is_read_in_either_byte_order", [this]() {
			for (auto [name, orientation] : std::vector<std::pair<std::string, int>> {{"jpeg_exif_orientation_3_9x5", 3}, {"jpeg_exif_orientation_6_9x5", 6}, {"jpeg_exif_orientation_8_9x5", 8}, {"jpeg_wic_photo_64x48", 1}}) {
				const image_fixtures::fixture& each = fixture_named(name);
				codec::header header;
				check_true(codec::read_header(each.bytes, each.size, codec::limits {}, header) == codec::status::ok, name);
				check_eq(orientation, int {header.orientation}, name);
				check_eq(orientation >= 5 ? each.height : each.width, header.oriented_width(), name);
				codec::header decoded;
				codec::bitmap pixels;
				check_true(codec::decode(each.bytes, each.size, codec::limits {}, codec::work_hook {}, decoded, pixels) == codec::status::ok, name);
				check_eq(orientation, int {decoded.orientation}, name);
				// the decoder hands back the stored pixels; image::load turns them
				check_eq(each.width, pixels.width, name);
			}
		});

		test("jpeg_refuses_hostile_files", [this]() {
			const image_fixtures::fixture& base = fixture_named("jpeg_444_photo_33x17");
			const bytes good(base.bytes, base.bytes + base.size);
			check_eq(std::string("ok"), outcome(good));
			// where the segments are, by marker
			auto find = [&](const bytes& a_file, uint8_t a_marker) {
				size_t at = 2;
				while (at + 4 <= a_file.size() && a_file[at] == 0xff) {
					if (a_file[at + 1] == a_marker) { return at; }
					at += 2 + ((size_t {a_file[at + 2]} << 8) | a_file[at + 3]);
				}
				return size_t {0};
			};
			const size_t frame = find(good, 0xc0);
			const size_t tables = find(good, 0xc4);
			const size_t quantization = find(good, 0xdb);
			const size_t scan = find(good, 0xda);
			check_true(frame && tables && quantization && scan, "the fixture's layout");
			auto changed = [&](size_t a_at, std::initializer_list<uint8_t> a_bytes) {
				bytes file = good;
				size_t at = a_at;
				for (uint8_t b : a_bytes) { file[at++] = b; }
				return file;
			};
			// dimensions of 0 (a DNL height) and past the pixel limit, decided by the frame header alone
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 5, {0, 0})));
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 7, {0, 0})));
			check_eq(std::string("Image has more pixels than the pixel limit"), outcome(changed(frame + 5, {0xff, 0xff, 0xff, 0xff})));
			codec::header header;
			const bytes bomb = changed(frame + 5, {0xff, 0xff, 0xff, 0xff});
			check_true(codec::read_header(bomb.data(), bomb.size(), codec::limits {}, header) == codec::status::too_many_pixels);
			// 12-bit samples, arithmetic coding, lossless and hierarchical processes, CMYK
			check_eq(std::string("JPEG coding process is not supported (only 8-bit baseline and progressive Huffman are)"), outcome(changed(frame + 4, {12})));
			check_eq(std::string("JPEG arithmetic coding is not supported"), outcome(changed(frame + 1, {0xc9})));
			check_eq(std::string("JPEG arithmetic coding is not supported"), outcome(changed(frame + 1, {0xca})));
			check_eq(std::string("JPEG coding process is not supported (only 8-bit baseline and progressive Huffman are)"), outcome(changed(frame + 1, {0xc3})));
			check_eq(std::string("JPEG coding process is not supported (only 8-bit baseline and progressive Huffman are)"), outcome(changed(frame + 1, {0xc5})));
			check_eq(std::string("JPEG CMYK images are not supported"), outcome(changed(frame + 9, {4})));
			// sampling factors of 0 or 5, an unknown quantization table, two components with one id
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 11, {0x01})));
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 11, {0x51})));
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 12, {4})));
			check_eq(std::string("JPEG frame header is invalid"), outcome(changed(frame + 13, {1})));
			// samples that do not divide the frame's (3 against 2)
			check_eq(std::string("JPEG coding process is not supported (only 8-bit baseline and progressive Huffman are)"), outcome(changed(frame + 11, {0x31, 0, 2, 0x21})));
			// a scan naming a component twice, or one the frame lacks, or a table never defined
			check_eq(std::string("JPEG scan header is invalid"), outcome(changed(scan + 7, {1})));
			check_eq(std::string("JPEG scan header is invalid"), outcome(changed(scan + 5, {9})));
			check_eq(std::string("JPEG scan uses a table that was never defined"), outcome(changed(scan + 6, {0x11})));
			check_eq(std::string("JPEG scan uses a table that was never defined"), outcome(changed(frame + 12, {3})));
			// missing tables altogether
			bytes no_huffman = good;
			no_huffman[tables + 1] = 0xfe;
			check_eq(std::string("JPEG scan uses a table that was never defined"), outcome(no_huffman));
			bytes no_quantization = good;
			no_quantization[quantization + 1] = 0xfe;
			check_eq(std::string("JPEG scan uses a table that was never defined"), outcome(no_quantization));
			// an over-subscribed Huffman table: three codes of length 1
			check_eq(std::string("JPEG Huffman table is invalid"), outcome(changed(tables + 5, {3})));
			// a table class past 1, and a DC category past 11
			check_eq(std::string("JPEG Huffman table is invalid"), outcome(changed(tables + 4, {0x20})));
			check_eq(std::string("JPEG Huffman table is invalid"), outcome(changed(tables + 21, {12})));
			// a quantization table with precision 2, or one cut short
			check_eq(std::string("JPEG quantization table is invalid"), outcome(changed(quantization + 4, {0x20})));
			check_eq(std::string("JPEG quantization table is invalid"), outcome(changed(quantization + 2, {0, 40})));
			// a segment length of 1, one past the end of the file, a scan before the frame
			check_eq(std::string("JPEG marker segment is invalid"), outcome(changed(quantization + 2, {0, 1})));
			check_eq(std::string("JPEG is truncated"), outcome(changed(quantization + 2, {0xff, 0xf0})));
			bytes scan_first(good.begin(), good.begin() + 2);
			scan_first.insert(scan_first.end(), good.begin() + scan, good.end());
			check_eq(std::string("JPEG marker segment is invalid"), outcome(scan_first));
			// no frame at all, and a second start of image
			const bytes bare = {0xff, 0xd8, 0xff, 0xd9};
			check_eq(std::string("JPEG has no image"), outcome(bare));
			check_eq(std::string("JPEG marker segment is invalid"), outcome(bytes {0xff, 0xd8, 0xff, 0xd8, 0xff, 0xd9}));
			// image data cut short, or ending at a marker early
			bytes cut(good.begin(), good.begin() + static_cast<long>(scan + 40));
			check_eq(std::string("JPEG is truncated"), outcome(cut));
			bytes early(good.begin(), good.begin() + static_cast<long>(scan + 40));
			early.insert(early.end(), {0xff, 0xd9});
			check_eq(std::string("JPEG image data is invalid"), outcome(early));
			// the end marker alone missing after a whole scan is no loss: the image decodes
			check_eq(std::string("ok"), outcome(bytes(good.begin(), good.end() - 2)));
		});

		test("jpeg_restart_markers_must_come_in_order", [this]() {
			const image_fixtures::fixture& base = fixture_named("jpeg_420_scans_redefined_29x23");
			const bytes good(base.bytes, base.bytes + base.size);
			check_eq(std::string("ok"), outcome(good));
			std::vector<size_t> markers;
			for (size_t i = 0; i + 1 < good.size(); ++i) {
				if (good[i] == 0xff && good[i + 1] >= 0xd0 && good[i + 1] <= 0xd7) { markers.push_back(i + 1); }
			}
			check_ge(markers.size(), size_t {2});
			bytes swapped = good;
			std::swap(swapped[markers[0]], swapped[markers[1]]);
			check_eq(std::string("JPEG restart markers are out of order"), outcome(swapped));
			bytes skipped = good;
			skipped[markers[0]] = 0xd3;
			check_eq(std::string("JPEG restart markers are out of order"), outcome(skipped));
			bytes not_restart = good;
			not_restart[markers[1]] = 0xd9;
			check_ne(std::string("ok"), outcome(not_restart));
		});

		test("jpeg_prefixes_are_refused_until_the_scans_are_whole", [this]() {
			for (const char* name : {"jpeg_wic_photo_64x48", "jpeg_420_restart_35x29", "jpeg_wic_grey_17x33", "jpeg_progressive_approximation_40x30"}) {
				const image_fixtures::fixture& each = fixture_named(name);
				const bool progressive = std::strstr(name, "progressive") != nullptr;
				size_t images = 0;
				for (size_t length = 0; length + 2 < each.size; ++length) {
					codec::bitmap pixels;
					codec::header header;
					const codec::status result = codec::decode(each.bytes, length, codec::limits {}, codec::work_hook {}, header, pixels);
					if (result == codec::status::ok) {
						++images;
						check_true(progressive, std::string(name) + " cut to " + std::to_string(length) + " bytes decoded");
					} else {
						check_ne(std::string(""), std::string(codec::status_text(result)), name);
					}
				}
				// a progressive file cut between its scans is a coarser picture, as a browser shows it
				if (progressive) { check_gt(images, size_t {0}, name); }
			}
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


// The resampler, orientation and cropping, on bitmaps the tests draw
class image_pixels_tests : public suite {
public:
	image_pixels_tests() : suite("Image Pixels") {}

	static codec::bitmap drawn(uint32_t a_width, uint32_t a_height, uint8_t (*a_pixel)(uint32_t, uint32_t, int)) {
		codec::bitmap result;
		result.width = a_width;
		result.height = a_height;
		for (uint32_t y = 0; y < a_height; ++y) {
			for (uint32_t x = 0; x < a_width; ++x) {
				for (int channel = 0; channel < 4; ++channel) { result.rgba.push_back(a_pixel(x, y, channel)); }
			}
		}
		return result;
	}
	static codec::bitmap resized(const codec::bitmap& a_source, uint32_t a_width, uint32_t a_height) {
		codec::bitmap result;
		const codec::status outcome = codec::resize(a_source, a_width, a_height, codec::work_hook {}, result);
		return outcome == codec::status::ok ? result : codec::bitmap {};
	}
	static const uint8_t* at(const codec::bitmap& a_bitmap, uint32_t a_x, uint32_t a_y) { return a_bitmap.rgba.data() + (size_t {a_y} * a_bitmap.width + a_x) * 4; }

	void forge_tests() override {
		test("the_identity_size_is_an_exact_copy", [this]() {
			// colours behind a zero alpha survive too: nothing is resampled
			const codec::bitmap noise = drawn(17, 9, [](uint32_t x, uint32_t y, int c) { return static_cast<uint8_t>((x * 73 + y * 151 + c * 29) * 2654435761u >> 24); });
			check_true(resized(noise, 17, 9).rgba == noise.rgba);
		});

		test("a_flat_image_stays_flat_at_every_size", [this]() {
			const codec::bitmap flat = drawn(23, 11, [](uint32_t, uint32_t, int c) { return static_cast<uint8_t>(c == 3 ? 200 : 40 + 60 * c); });
			for (auto [width, height] : std::vector<std::pair<uint32_t, uint32_t>> {{1, 1}, {5, 3}, {22, 11}, {23, 40}, {100, 7}, {7, 100}, {2, 33}}) {
				const codec::bitmap out = resized(flat, width, height);
				check_eq(size_t {width} * height * 4, out.rgba.size());
				for (size_t i = 0; i < out.rgba.size(); i += 4) {
					check_true(out.rgba[i] == 40 && out.rgba[i + 1] == 100 && out.rgba[i + 2] == 160 && out.rgba[i + 3] == 200, std::to_string(width) + "x" + std::to_string(height));
				}
			}
		});

		test("a_reduction_averages_what_it_drops", [this]() {
			// a one-pixel checkerboard is grey at an eighth of its size: nearest-neighbour or plain bilinear would alias it to stripes
			const codec::bitmap board = drawn(64, 64, [](uint32_t x, uint32_t y, int c) { return static_cast<uint8_t>(c == 3 ? 255 : ((x + y) & 1) * 255); });
			for (uint32_t size : {8u, 7u, 3u}) {
				const codec::bitmap out = resized(board, size, size);
				for (size_t i = 0; i < out.rgba.size(); i += 4) { check_true(out.rgba[i] >= 119 && out.rgba[i] <= 136, std::to_string(out.rgba[i])); }
			}
			// and a ramp stays a ramp, in order and in range
			const codec::bitmap ramp = drawn(256, 4, [](uint32_t x, uint32_t, int c) { return static_cast<uint8_t>(c == 3 ? 255 : x); });
			const codec::bitmap small = resized(ramp, 37, 2);
			for (uint32_t x = 1; x < 37; ++x) { check_true(at(small, x, 0)[0] > at(small, x - 1, 0)[0]); }
			check_le(int {at(small, 0, 0)[0]}, 8);
			check_ge(int {at(small, 36, 0)[0]}, 247);
		});

		test("alpha_is_premultiplied_so_hidden_colour_never_bleeds", [this]() {
			// opaque red beside fully transparent green: no green may reach a visible pixel at any size
			const codec::bitmap half = drawn(20, 6, [](uint32_t x, uint32_t, int c) { return static_cast<uint8_t>(x < 10 ? (c == 0 || c == 3 ? 255 : 0) : (c == 1 ? 255 : 0)); });
			for (auto [width, height] : std::vector<std::pair<uint32_t, uint32_t>> {{7, 3}, {3, 2}, {41, 13}}) {
				const codec::bitmap out = resized(half, width, height);
				for (size_t i = 0; i < out.rgba.size(); i += 4) {
					if (out.rgba[i + 3] == 0) {
						check_true(out.rgba[i] == 0 && out.rgba[i + 1] == 0 && out.rgba[i + 2] == 0, "a transparent pixel is zero");
						continue;
					}
					check_eq(0, int {out.rgba[i + 1]}, "green in a visible pixel");
					check_eq(255, int {out.rgba[i]}, "the red keeps its colour as its alpha falls");
				}
			}
		});

		test("enlarging_a_hard_edge_stays_in_range_and_mirrors", [this]() {
			const codec::bitmap edge = drawn(6, 5, [](uint32_t x, uint32_t y, int c) { return static_cast<uint8_t>(c == 3 ? (y == 2 ? 128 : 255) : x < 3 ? 250 : 5); });
			const codec::bitmap big = resized(edge, 31, 17);
			const codec::bitmap mirrored = drawn(6, 5, [](uint32_t x, uint32_t y, int c) { return static_cast<uint8_t>(c == 3 ? (y == 2 ? 128 : 255) : 5 - x < 3 ? 250 : 5); });
			const codec::bitmap big_mirrored = resized(mirrored, 31, 17);
			for (uint32_t y = 0; y < 17; ++y) {
				for (uint32_t x = 0; x < 31; ++x) {
					check_true(std::memcmp(at(big, x, y), at(big_mirrored, 30 - x, y), 4) == 0, "mirror at " + std::to_string(x) + ", " + std::to_string(y));
				}
			}
		});

		test("the_budget_stops_a_resize", [this]() {
			const codec::bitmap flat = drawn(300, 200, [](uint32_t, uint32_t, int) { return uint8_t {9}; });
			size_t spent = 0;
			codec::work_hook counting {[](void* a_context, size_t a_units) { *static_cast<size_t*>(a_context) += a_units; return false; }, &spent};
			codec::bitmap out;
			check_true(codec::resize(flat, 100, 70, counting, out) == codec::status::ok);
			check_gt(spent, size_t {300 * 200 / 16});
			codec::work_hook gone {[](void*, size_t) { return true; }, nullptr};
			check_true(codec::resize(flat, 100, 70, gone, out) == codec::status::budget);
		});

		test("fit_size_keeps_the_shape_and_never_enlarges", [this]() {
			auto fit = [](uint32_t a_width, uint32_t a_height, uint64_t a_box_width, uint64_t a_box_height) {
				uint32_t width = 0, height = 0;
				codec::fit_size(a_width, a_height, a_box_width, a_box_height, width, height);
				return std::to_string(width) + "x" + std::to_string(height);
			};
			check_eq(std::string("1200x900"), fit(4000, 3000, 1200, 1200));
			check_eq(std::string("900x1200"), fit(3000, 4000, 1200, 1200));
			check_eq(std::string("100x50"), fit(100, 50, 1200, 1200));
			check_eq(std::string("100x50"), fit(100, 50, 100, 50));
			check_eq(std::string("50x25"), fit(100, 50, 50, 1000));
			check_eq(std::string("20x10"), fit(100, 50, 1000, 10));
			check_eq(std::string("1x1"), fit(10000, 1, 1, 1));
			check_eq(std::string("1x1"), fit(1, 10000, 7, 1));
			check_eq(std::string("7x1"), fit(10000, 3, 7, 1));
			check_eq(std::string("100x50"), fit(100, 50, ~uint64_t {0}, ~uint64_t {0}));
			check_eq(std::string("65535x1"), fit(0x7fffffffu, 3, 65535, ~uint64_t {0}));
		});

		test("crop_fill_covers_and_centres", [this]() {
			// a 9 x 3 image with its columns numbered: a square takes the middle three
			const codec::bitmap columns = drawn(9, 3, [](uint32_t x, uint32_t, int c) { return static_cast<uint8_t>(c == 3 ? 255 : x * 20); });
			codec::bitmap square;
			check_true(codec::crop_fill(columns, 3, 3, codec::work_hook {}, square) == codec::status::ok);
			check_eq(uint32_t {3}, square.width);
			check_eq(uint32_t {3}, square.height);
			check_eq(60, int {at(square, 0, 0)[0]});
			check_eq(100, int {at(square, 2, 2)[0]});
			// enlarged when the target is bigger than the source
			codec::bitmap wide;
			check_true(codec::crop_fill(columns, 40, 20, codec::work_hook {}, wide) == codec::status::ok);
			check_eq(size_t {40 * 20 * 4}, wide.rgba.size());
		});

		test("orient_turns_all_eight_ways", [this]() {
			// a 3 x 2 image whose red channel numbers its pixels: each orientation shows the stored pixels in this order
			const codec::bitmap numbered = drawn(3, 2, [](uint32_t x, uint32_t y, int c) { return static_cast<uint8_t>(c == 0 ? y * 3 + x : 255); });
			const char* expected[9] = {"", "3x2 0 1 2 3 4 5", "3x2 2 1 0 5 4 3", "3x2 5 4 3 2 1 0", "3x2 3 4 5 0 1 2",
			                           "2x3 0 3 1 4 2 5", "2x3 3 0 4 1 5 2", "2x3 5 2 4 1 3 0", "2x3 2 5 1 4 0 3"};
			for (uint8_t orientation = 1; orientation <= 8; ++orientation) {
				codec::bitmap turned;
				check_true(codec::orient(numbered, orientation, turned) == codec::status::ok);
				std::string shown = std::to_string(turned.width) + "x" + std::to_string(turned.height);
				for (size_t i = 0; i < turned.rgba.size(); i += 4) { shown += " " + std::to_string(turned.rgba[i]); }
				check_eq(std::string(expected[orientation]), shown, "orientation " + std::to_string(orientation));
			}
		});
	}
};

// The script's image:: module, on the backend the run names
class image_module_tests : public suite {
public:
	image_module_tests() : suite("Image") {}

	// An engine with image:: and the fixtures: fixture(name) as a string, fixture_bytes(name) as an array<uint8>
	std::shared_ptr<engine> image_engine(const stdlib::image_limits& a_limits = {}) {
		auto e = make_engine();
		stdlib::register_all(*e);
		stdlib::register_image(*e, a_limits);
		e->add_function("fixture", [](const std::string& a_name) -> std::string {
			const image_fixtures::fixture& each = fixture_named(a_name);
			return std::string(reinterpret_cast<const char*>(each.bytes), each.size);
		});
		e->add_function("fixture_bytes", [](const std::string& a_name) -> std::vector<uint8_t> {
			const image_fixtures::fixture& each = fixture_named(a_name);
			return std::vector<uint8_t>(each.bytes, each.bytes + each.size);
		});
		return e;
	}

	static std::string run(const std::shared_ptr<engine>& a_engine, const std::string& a_source) { return a_engine->execute(a_source).to_string(); }
	// The message a statement raises, caught by the script
	static std::string caught(const std::shared_ptr<engine>& a_engine, const std::string& a_statement) {
		return a_engine->execute("string m = \"no error\"; try { " + a_statement + " } catch (e) { m = e.message; } m;").to_string();
	}

	void forge_tests() override {
		test("load_and_data_read_bytes_and_arrays", [this]() {
			auto e = image_engine();
			check_eq(std::string("64 48 png"), run(e, "image img = image::load(fixture(\"png_rgb_photo_64x48\")); `${img.width} ${img.height} ${img.format}`;"));
			check_eq(std::string("17 33 png"), run(e, "auto img = image::load(fixture_bytes(\"png_rgba_17x33\")); `${img.width} ${img.height} ${img.format}`;"));
			check_eq(std::string("png 64 48"), run(e, "var d = image::data(fixture(\"png_rgb_photo_64x48\")); `${d[\"format\"]} ${d[\"width\"]} ${d[\"height\"]}`;"));
			check_eq(std::string("3"), run(e, "image::data(fixture_bytes(\"png_grey2_13x5\")).size();"));
			// a JPEG stored on its side loads upright, and its data says so
			check_eq(std::string("5 9 jpeg"), run(e, "image img = image::load(fixture(\"jpeg_exif_orientation_6_9x5\")); `${img.width} ${img.height} ${img.format}`;"));
			check_eq(std::string("jpeg 5 9"), run(e, "var d = image::data(fixture(\"jpeg_exif_orientation_8_9x5\")); `${d[\"format\"]} ${d[\"width\"]} ${d[\"height\"]}`;"));
			check_eq(std::string("9 5"), run(e, "image img = image::load(fixture(\"jpeg_exif_orientation_3_9x5\")); `${img.width} ${img.height}`;"));
			check_eq(std::string("0 0 "), run(e, "image empty; `${empty.width} ${empty.height} ${empty.format}`;"));
		});

		test("a_string_is_always_bytes_and_a_file_comes_through_file_read", [this]() {
			namespace fs = std::filesystem;
			const fs::path dir = test_temp_directory() / "jai_image_files";
			std::error_code error;
			fs::remove_all(dir, error);
			fs::create_directories(dir);
			const image_fixtures::fixture& photo = fixture_named("png_rgb_photo_64x48");
			std::ofstream(dir / "photo.png", std::ios::binary).write(reinterpret_cast<const char*>(photo.bytes), static_cast<std::streamsize>(photo.size));
			const std::string base = dir.generic_string();
			auto e = image_engine();
			// the host's file::read, as the runner and jai_spider bind it
			e->add_function("file::read", [](const std::string& a_path) -> std::string {
				std::ifstream in(a_path, std::ios::binary);
				return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			});
			check_eq(std::string("64 48 png"), run(e, "image img = image::load(file::read(\"" + base + "/photo.png\")); `${img.width} ${img.height} ${img.format}`;"));
			check_eq(std::string("64 48"), run(e, "var d = image::data(file::read(\"" + base + "/photo.png\")); `${d[\"width\"]} ${d[\"height\"]}`;"));
			// a string is the image's bytes whatever it holds: a path is just text that is no image
			const std::string not_an_image = "Image data is not a JPEG, PNG or GIF (read a file with image::load(file::read(path)))";
			check_eq(not_an_image, caught(e, "image::load(\"" + base + "/photo.png\");"));
			check_eq(not_an_image, caught(e, "image::data(\"/etc/passwd\");"));
			check_eq(not_an_image, caught(e, "image::load(\"\");"));
			check_eq(std::string("image::load expects a string or an array<uint8>"), caught(e, "image::load(7);"));
			check_eq(std::string("image::data expects a string or an array<uint8>"), caught(e, "array<int> a = [137]; image::data(a);"));
			stdlib::image_limits small;
			small.max_input_bytes = 1000;
			auto limited = image_engine(small);
			check_eq(std::string("Image input is larger than the input size limit"), caught(limited, "image::load(fixture(\"png_rgb_photo_64x48\"));"));
			// the module touches no file, so it needs no filesystem permission
			e->disable(permissions::filesystem);
			check_eq(std::string("64"), run(e, "image::load(fixture(\"png_rgb_photo_64x48\")).width;"));
			fs::remove_all(dir, error);
		});

		test("the_pixel_limit_refuses_a_header_before_decoding", [this]() {
			stdlib::image_limits small;
			small.max_pixels = 1000;
			auto e = image_engine(small);
			check_eq(std::string("Image has more pixels than the pixel limit"), caught(e, "image::load(fixture(\"png_rgb_photo_64x48\"));"));
			check_eq(std::string("Image has more pixels than the pixel limit"), caught(e, "image::data(fixture(\"png_rgb_photo_64x48\"));"));
			check_eq(std::string("561"), run(e, "auto img = image::load(fixture(\"png_rgba_17x33\")); img.width * img.height;"));
			check_eq(std::string("Image has more pixels than the pixel limit"), caught(e, "auto img = image::load(fixture(\"png_rgba_17x33\")); img.crop_fill(40, 26);"));
			check_eq(std::string("40 25"), run(e, "auto img = image::load(fixture(\"png_rgba_17x33\")); img.crop_fill(40, 25); `${img.width} ${img.height}`;"));
		});

		test("fit_and_crop_fill_change_the_image_and_their_twins_copy", [this]() {
			auto e = image_engine();
			e->execute("image photo = image::load(fixture(\"png_rgb_photo_64x48\"));");
			check_eq(std::string("32 24"), run(e, "image a = photo; a.fit(32, 32); `${a.width} ${a.height}`;"));
			check_eq(std::string("64 48"), run(e, "image a = photo; a.fit(1000, 1000); `${a.width} ${a.height}`;"));
			check_eq(std::string("10 10 64 48"), run(e, "image a = image::crop_fill(photo, 10, 10); `${a.width} ${a.height} ${photo.width} ${photo.height}`;"));
			check_eq(std::string("200 100 png"), run(e, "image a = photo; a.crop_fill(200, 100); `${a.width} ${a.height} ${a.format}`;"));
			check_eq(std::string("16 12 64 48"), run(e, "image a = image::fit(photo, 16, 100); `${a.width} ${a.height} ${photo.width} ${photo.height}`;"));
			check_eq(std::string("3 4 64 48"), run(e, "image a = photo; a.crop(60, 44, 3, 4); `${a.width} ${a.height} ${photo.width} ${photo.height}`;"));
			check_eq(std::string("5 6"), run(e, "image a = image::crop(photo, 0, 0, 5, 6); `${a.width} ${a.height}`;"));
			check_eq(std::string("image::fit expects a width and a height of at least 1"), caught(e, "photo.fit(0, 10);"));
			check_eq(std::string("image::fit expects a width and a height of at least 1"), caught(e, "image::fit(photo, 10, -1);"));
			check_eq(std::string("image::crop_fill expects a width and a height of at least 1"), caught(e, "photo.crop_fill(10, 0);"));
			check_eq(std::string("Image has more pixels than the pixel limit"), caught(e, "photo.crop_fill(100000, 100000);"));
			check_eq(std::string("Image has more pixels than the pixel limit"), caught(e, "photo.crop_fill(4611686018427387904, 2);"));
			check_eq(std::string("image: the image is empty (image::load makes one)"), caught(e, "image empty; empty.fit(1, 1);"));
			check_eq(std::string("image: the image is empty (image::load makes one)"), caught(e, "image empty; image::crop(empty, 0, 0, 1, 1);"));
		});

		test("crop_is_bounds_checked", [this]() {
			auto e = image_engine();
			e->execute("image photo = image::load(fixture(\"png_rgb_photo_64x48\"));");
			check_eq(std::string("image::crop: the rectangle at (60, 44) of 5 x 4 is not inside the 64 x 48 image"), caught(e, "photo.crop(60, 44, 5, 4);"));
			check_eq(std::string("image::crop: the rectangle at (-1, 0) of 5 x 4 is not inside the 64 x 48 image"), caught(e, "image::crop(photo, -1, 0, 5, 4);"));
			check_eq(std::string("image::crop: the rectangle at (0, 0) of 0 x 4 is not inside the 64 x 48 image"), caught(e, "photo.crop(0, 0, 0, 4);"));
			check_eq(std::string("image::crop: the rectangle at (64, 0) of 1 x 1 is not inside the 64 x 48 image"), caught(e, "photo.crop(64, 0, 1, 1);"));
			check_eq(std::string("image::crop: the rectangle at (1, 1) of 9223372036854775807 x 1 is not inside the 64 x 48 image"), caught(e, "photo.crop(1, 1, 9223372036854775807, 1);"));
			check_eq(std::string("64 48"), run(e, "`${photo.width} ${photo.height}`;"));
		});

		test("the_budget_and_the_memory_cap_stop_a_decode", [this]() {
			auto e = image_engine();
			e->execute("string big = fixture(\"png_rgb_photo_64x48\");");
			e->memory_cap(5000);
			check_true(caught(e, "image::load(big);").find("memory") != std::string::npos, "a decode past the memory cap is refused");
			e->memory_cap(0);
			e->execute("image photo = image::load(big);");
			e->execution_step_budget(1);
			std::string raised = "returned normally";
			try { e->execute("photo.crop_fill(3000, 3000);"); } catch (const std::exception& a_error) { raised = a_error.what(); }
			check_true(raised.find("Script execution budget exceeded") != std::string::npos, raised);
			e->execution_step_budget(0);
			check_eq(std::string("64"), run(e, "photo.width;"));
		});

		test("mutated_files_raise_catalogued_errors_through_the_script", [this]() {
			auto e = image_engine();
			const auto pool = image_mutation::fixtures_of("png");
			e->add_function("mutated", [pool](script_int a_seed) -> std::vector<uint8_t> { return image_mutation::mutated_case("png", pool, static_cast<uint64_t>(a_seed)); });
			e->add_function("catalogued", [](const std::string& a_text) -> bool {
				for (size_t i = 0; i < static_cast<size_t>(codec::status::status_count); ++i) {
					if (a_text == codec::status_text(static_cast<codec::status>(i))) { return true; }
				}
				return false;
			});
			const std::string outcome = run(e, R"(
				int images = 0;
				int errors = 0;
				string strange = "";
				for (int seed = 0; seed < 400; ++seed) {
					try {
						image img = image::load(mutated(seed));
						if (img.width > 0 && img.height > 0) { images += 1; }
						img.fit(7, 7);
					} catch (e) {
						if (catalogued(e.message)) { errors += 1; } else { strange = e.message; }
					}
				}
				`${images + errors} ${strange}`;
			)");
			check_eq(std::string("400 "), outcome);
		});
	}
};

} // namespace jai::foundry::tests

using image_codec_tests = jai::foundry::tests::image_codec_tests;
FOUNDRY_REGISTER(image_codec_tests)
using image_pixels_tests = jai::foundry::tests::image_pixels_tests;
FOUNDRY_REGISTER(image_pixels_tests)
using image_module_tests = jai::foundry::tests::image_module_tests;
FOUNDRY_REGISTER(image_module_tests)

#endif
