#ifdef JAISCRIPT_ENABLE_IMAGE

#include <jaiscript/detail/image_codec.hpp>
#include <cstring>
#include <new>

namespace jai {
namespace image_codec {

	const char* status_text(status a_status) {
		switch (a_status) {
			case status::out_of_memory: return "Image needs more memory than is available";
			case status::not_an_image: return "Image data is not a JPEG, PNG or GIF";
			case status::input_too_large: return "Image input is larger than the input size limit";
			case status::too_many_pixels: return "Image has more pixels than the pixel limit";
			case status::png_truncated: return "PNG is truncated";
			case status::png_bad_crc: return "PNG chunk has a bad CRC";
			case status::png_bad_header: return "PNG header is invalid";
			case status::png_bad_chunk_order: return "PNG chunks are missing or out of order";
			case status::png_unknown_critical_chunk: return "PNG has a critical chunk this decoder does not know";
			case status::png_bad_palette: return "PNG palette is invalid";
			case status::png_bad_compressed_data: return "PNG compressed data is invalid";
			case status::png_size_mismatch: return "PNG image data does not match its header";
			case status::png_bad_filter: return "PNG row has an invalid filter type";
			case status::jpeg_truncated: return "JPEG is truncated";
			case status::jpeg_bad_segment: return "JPEG marker segment is invalid";
			case status::jpeg_bad_frame: return "JPEG frame header is invalid";
			case status::jpeg_arithmetic: return "JPEG arithmetic coding is not supported";
			case status::jpeg_unsupported: return "JPEG coding process is not supported (only 8-bit baseline and progressive Huffman are)";
			case status::jpeg_bad_quantization_table: return "JPEG quantization table is invalid";
			case status::jpeg_bad_huffman_table: return "JPEG Huffman table is invalid";
			case status::jpeg_missing_table: return "JPEG scan uses a table that was never defined";
			case status::jpeg_bad_scan: return "JPEG scan header is invalid";
			case status::jpeg_bad_data: return "JPEG image data is invalid";
			case status::jpeg_bad_restart: return "JPEG restart markers are out of order";
			case status::jpeg_no_image: return "JPEG has no image";
			case status::gif_truncated: return "GIF is truncated";
			case status::gif_bad_header: return "GIF header is invalid";
			case status::gif_no_image: return "GIF has no image";
			case status::gif_bad_frame: return "GIF frame is invalid";
			case status::gif_bad_data: return "GIF image data is invalid";
			default: return "";
		}
	}

	const char* format_name(format a_format) {
		switch (a_format) {
			case format::jpeg: return "jpeg";
			case format::png: return "png";
			case format::gif: return "gif";
			default: return "";
		}
	}

	format sniff(const uint8_t* a_data, size_t a_size) {
		static constexpr uint8_t k_png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
		if (a_size >= 3 && a_data[0] == 0xff && a_data[1] == 0xd8 && a_data[2] == 0xff) { return format::jpeg; }
		if (a_size >= 8 && std::memcmp(a_data, k_png, 8) == 0) { return format::png; }
		if (a_size >= 6 && (std::memcmp(a_data, "GIF87a", 6) == 0 || std::memcmp(a_data, "GIF89a", 6) == 0)) { return format::gif; }
		return format::none;
	}

	status read_header(const uint8_t* a_data, size_t a_size, const limits& a_limits, header& a_header) {
		if (a_size > a_limits.max_input_bytes) { return status::input_too_large; }
		switch (sniff(a_data, a_size)) {
			case format::png: return read_png_header(a_data, a_size, a_limits, a_header);
			default: return status::not_an_image;
		}
	}

	status decode(const uint8_t* a_data, size_t a_size, const limits& a_limits, const work_hook& a_work, header& a_header, bitmap& a_bitmap) {
		if (a_size > a_limits.max_input_bytes) { return status::input_too_large; }
		switch (sniff(a_data, a_size)) {
			case format::png: return decode_png(a_data, a_size, a_limits, a_work, a_header, a_bitmap);
			default: return status::not_an_image;
		}
	}

} // namespace image_codec
} // namespace jai

#endif
