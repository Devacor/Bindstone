// The fixture generator image_fixtures.ps1 compiles: see that file. Everything here runs Windows' codecs, never JaiScript's.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Windows.Media;
using System.Windows.Media.Imaging;

public static class ImageFixtures {
	static StringBuilder arrays = new StringBuilder();
	static StringBuilder rows = new StringBuilder();
	static string dump = "";
	static uint seed = 1;
	static int count = 0;

	static uint Next() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	}

	static ulong Fnv(byte[] a_bytes) {
		ulong hash = 14695981039346656037UL;
		foreach (byte b in a_bytes) {
			hash ^= b;
			hash = unchecked(hash * 1099511628211UL);
		}
		return hash;
	}

	// Pixels as B, G, R, A rows: "gradient", "alpha" (a gradient whose alpha runs 0 to 255), "noise", "edges" (hard blocks
	// of saturated colour) or "photo" (smooth waves with a little noise, the shape of a photograph)
	public static byte[] Pattern(string a_kind, int a_width, int a_height) {
		byte[] p = new byte[a_width * a_height * 4];
		for (int y = 0; y < a_height; ++y) {
			for (int x = 0; x < a_width; ++x) {
				int i = (y * a_width + x) * 4;
				int r, g, b, a = 255;
				if (a_kind == "noise") {
					r = (int)(Next() & 255); g = (int)(Next() & 255); b = (int)(Next() & 255);
				} else if (a_kind == "edges") {
					int cell = ((x / 3) + (y / 2) * 3) % 8;
					r = (cell & 1) != 0 ? 255 : 0; g = (cell & 2) != 0 ? 255 : 0; b = (cell & 4) != 0 ? 255 : 0;
				} else if (a_kind == "photo") {
					double u = (double)x / a_width, v = (double)y / a_height;
					r = (int)(128 + 100 * Math.Sin(6.0 * u + 2.0 * v) + (Next() % 9) - 4);
					g = (int)(128 + 90 * Math.Cos(4.0 * v - 3.0 * u) + (Next() % 9) - 4);
					b = (int)(128 + 110 * Math.Sin(5.0 * (u + v)) + (Next() % 9) - 4);
					r = Math.Max(0, Math.Min(255, r)); g = Math.Max(0, Math.Min(255, g)); b = Math.Max(0, Math.Min(255, b));
				} else {
					r = a_width > 1 ? x * 255 / (a_width - 1) : 200;
					g = a_height > 1 ? y * 255 / (a_height - 1) : 100;
					b = (x * 7 + y * 13) & 255;
					if (a_kind == "alpha") { a = ((x + y) % 5 == 0) ? 0 : ((x * 37 + y * 11) & 255); if ((x + y) % 7 == 0) { a = 255; } }
				}
				p[i] = (byte)b; p[i + 1] = (byte)g; p[i + 2] = (byte)r; p[i + 3] = (byte)a;
			}
		}
		return p;
	}

	static byte[] Encode(BitmapEncoder a_encoder, BitmapSource a_source) {
		a_encoder.Frames.Add(BitmapFrame.Create(a_source));
		using (MemoryStream stream = new MemoryStream()) {
			a_encoder.Save(stream);
			return stream.ToArray();
		}
	}

	static BitmapSource FirstFrame(byte[] a_file) {
		BitmapDecoder decoder = BitmapDecoder.Create(new MemoryStream(a_file), BitmapCreateOptions.PreservePixelFormat | BitmapCreateOptions.IgnoreColorProfile, BitmapCacheOption.OnLoad);
		return decoder.Frames[0];
	}

	static byte Narrow(ushort a_value) { return (byte)((a_value * 255u + 32895u) >> 16); }

	// The file's pixels as R, G, B, A, by WIC. A 16-bit file's samples are read as they are stored and narrowed to the 8-bit
	// value nearest each (the rule the module documents), since WIC's own 16-bit conversions apply a gamma.
	public static byte[] ReferenceRgba(byte[] a_file, out int a_width, out int a_height) {
		BitmapSource frame = FirstFrame(a_file);
		a_width = frame.PixelWidth;
		a_height = frame.PixelHeight;
		byte[] rgba = new byte[a_width * a_height * 4];
		PixelFormat format = frame.Format;
		if (format == PixelFormats.Gray16 || format == PixelFormats.Rgb48 || format == PixelFormats.Rgba64) {
			int channels = format == PixelFormats.Gray16 ? 1 : format == PixelFormats.Rgb48 ? 3 : 4;
			ushort[] samples = new ushort[a_width * a_height * channels];
			frame.CopyPixels(samples, a_width * channels * 2, 0);
			for (int i = 0; i < a_width * a_height; ++i) {
				if (channels == 1) {
					rgba[4 * i] = rgba[4 * i + 1] = rgba[4 * i + 2] = Narrow(samples[i]);
					rgba[4 * i + 3] = 255;
				} else {
					for (int c = 0; c < channels; ++c) { rgba[4 * i + c] = Narrow(samples[i * channels + c]); }
					if (channels == 3) { rgba[4 * i + 3] = 255; }
				}
			}
			return rgba;
		}
		FormatConvertedBitmap converted = new FormatConvertedBitmap(frame, PixelFormats.Bgra32, null, 0);
		converted.CopyPixels(rgba, a_width * 4, 0);
		for (int i = 0; i < rgba.Length; i += 4) {
			byte b = rgba[i];
			rgba[i] = rgba[i + 2];
			rgba[i + 2] = b;
		}
		return rgba;
	}

	static string Describe(byte[] a_file) {
		if (a_file.Length > 26 && a_file[1] == 'P') {
			return "png colour " + a_file[25] + " depth " + a_file[24] + (a_file[28] != 0 ? " interlaced" : "");
		}
		if (a_file.Length > 4 && a_file[0] == 0xff) {
			StringBuilder text = new StringBuilder("jpeg");
			int at = 2;
			while (at + 4 <= a_file.Length && a_file[at] == 0xff) {
				int marker = a_file[at + 1];
				int size = (a_file[at + 2] << 8) | a_file[at + 3];
				if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
					text.Append(" SOF" + (marker - 0xc0) + " components " + a_file[at + 9] + " sampling");
					for (int c = 0; c < a_file[at + 9]; ++c) { text.Append(" " + (a_file[at + 11 + 3 * c] >> 4) + "x" + (a_file[at + 11 + 3 * c] & 15)); }
				}
				if (marker == 0xdd) { text.Append(" restart " + ((a_file[at + 4] << 8) | a_file[at + 5])); }
				if (marker == 0xda) { break; }
				at += 2 + size;
			}
			return text.ToString();
		}
		return "gif";
	}

	static void AppendArray(string a_name, byte[] a_bytes) {
		arrays.Append("\tinline constexpr unsigned char " + a_name + "[] = {");
		for (int i = 0; i < a_bytes.Length; ++i) {
			if (i % 32 == 0) { arrays.Append("\n\t\t"); }
			arrays.Append(a_bytes[i].ToString());
			arrays.Append(',');
		}
		arrays.Append("\n\t};\n");
	}

	// One fixture: a file, and either the checksum of its reference pixels (a_exact) or the reference pixels themselves as a
	// PNG, for a decoder that is allowed a tolerance
	static void Add(string a_name, string a_format, byte[] a_file, byte[] a_rgba, int a_width, int a_height, bool a_exact) {
		AppendArray("k_" + a_name, a_file);
		string reference = "nullptr, 0";
		if (!a_exact) {
			byte[] bgra = (byte[])a_rgba.Clone();
			for (int i = 0; i < bgra.Length; i += 4) { byte t = bgra[i]; bgra[i] = bgra[i + 2]; bgra[i + 2] = t; }
			byte[] png = Encode(new PngBitmapEncoder(), BitmapSource.Create(a_width, a_height, 96, 96, PixelFormats.Bgra32, null, bgra, a_width * 4));
			AppendArray("k_" + a_name + "_reference", png);
			reference = "k_" + a_name + "_reference, sizeof(k_" + a_name + "_reference)";
		}
		rows.Append("\t\t{\"" + a_name + "\", \"" + a_format + "\", k_" + a_name + ", sizeof(k_" + a_name + "), " + a_width + ", " + a_height + ", 0x" + Fnv(a_rgba).ToString("x16") + "ull, " + reference + "},\n");
		Console.WriteLine(a_name + ": " + a_file.Length + " bytes, " + a_width + "x" + a_height + ", " + Describe(a_file));
		if (dump != "") {
			File.WriteAllBytes(Path.Combine(dump, a_name + "." + a_format), a_file);
			File.WriteAllBytes(Path.Combine(dump, a_name + ".rgba"), a_rgba);
		}
		++count;
	}

	static void AddPng(string a_name, BitmapSource a_source, bool a_interlaced) {
		PngBitmapEncoder encoder = new PngBitmapEncoder();
		encoder.Interlace = a_interlaced ? PngInterlaceOption.On : PngInterlaceOption.Off;
		byte[] file = Encode(encoder, a_source);
		int width, height;
		byte[] rgba = ReferenceRgba(file, out width, out height);
		Add(a_name, "png", file, rgba, width, height, true);
	}

	static BitmapSource Bgra(string a_kind, int a_width, int a_height) {
		return BitmapSource.Create(a_width, a_height, 96, 96, PixelFormats.Bgra32, null, Pattern(a_kind, a_width, a_height), a_width * 4);
	}

	static BitmapSource Converted(BitmapSource a_source, PixelFormat a_format) { return new FormatConvertedBitmap(a_source, a_format, null, 0); }

	static BitmapSource Packed(PixelFormat a_format, BitmapPalette a_palette, int a_width, int a_height, int a_bits) {
		int stride = (a_width * a_bits + 7) / 8;
		byte[] data = new byte[stride * a_height];
		for (int i = 0; i < data.Length; ++i) { data[i] = (byte)(Next() & 255); }
		return BitmapSource.Create(a_width, a_height, 96, 96, a_format, a_palette, data, stride);
	}

	static BitmapSource Deep(PixelFormat a_format, int a_width, int a_height, int a_channels) {
		ushort[] data = new ushort[a_width * a_height * a_channels];
		for (int i = 0; i < data.Length; ++i) { data[i] = (ushort)(Next() & 65535); }
		// some samples at the ends of the range and at the rounding boundaries
		data[0] = 0; data[1] = 65535; data[2] = 128; data[3] = 129; data[4] = 65407; data[5] = 65408;
		return BitmapSource.Create(a_width, a_height, 96, 96, a_format, null, data, a_width * a_channels * 2);
	}

	static BitmapPalette Palette(int a_count, bool a_alpha) {
		List<Color> colours = new List<Color>();
		for (int i = 0; i < a_count; ++i) {
			byte alpha = a_alpha ? (byte)(i % 4 == 0 ? 0 : i % 4 == 1 ? 255 : (Next() & 255)) : (byte)255;
			colours.Add(Color.FromArgb(alpha, (byte)(Next() & 255), (byte)(Next() & 255), (byte)(Next() & 255)));
		}
		return new BitmapPalette(colours);
	}

	static void Png() {
		AddPng("png_rgba_17x33", Bgra("alpha", 17, 33), false);
		AddPng("png_rgba_17x33_interlaced", Bgra("alpha", 17, 33), true);
		AddPng("png_rgba_1x1", Bgra("alpha", 1, 1), false);
		AddPng("png_rgba_1x1_interlaced", Bgra("gradient", 1, 1), true);
		AddPng("png_rgba_noise_24x24", Bgra("noise", 24, 24), false);
		AddPng("png_rgb_gradient_40x30", Converted(Bgra("gradient", 40, 30), PixelFormats.Bgr24), false);
		AddPng("png_rgb_edges_3x5_interlaced", Converted(Bgra("edges", 3, 5), PixelFormats.Bgr24), true);
		AddPng("png_rgb_noise_48x48", Converted(Bgra("noise", 48, 48), PixelFormats.Bgr24), false);
		AddPng("png_rgb_photo_64x48", Converted(Bgra("photo", 64, 48), PixelFormats.Bgr24), false);
		AddPng("png_grey8_17x33", Converted(Bgra("photo", 17, 33), PixelFormats.Gray8), false);
		AddPng("png_grey8_9x9_interlaced", Converted(Bgra("noise", 9, 9), PixelFormats.Gray8), true);
		AddPng("png_grey4_19x7", Packed(PixelFormats.Gray4, null, 19, 7, 4), false);
		AddPng("png_grey2_13x5", Packed(PixelFormats.Gray2, null, 13, 5, 2), false);
		AddPng("png_grey1_33x17", Packed(PixelFormats.BlackWhite, null, 33, 17, 1), false);
		AddPng("png_grey1_33x17_interlaced", Packed(PixelFormats.BlackWhite, null, 33, 17, 1), true);
		AddPng("png_grey16_9x7", Deep(PixelFormats.Gray16, 9, 7, 1), false);
		AddPng("png_rgb48_9x7", Deep(PixelFormats.Rgb48, 9, 7, 3), false);
		AddPng("png_rgba64_9x7", Deep(PixelFormats.Rgba64, 9, 7, 4), false);
		AddPng("png_rgba64_11x13_interlaced", Deep(PixelFormats.Rgba64, 11, 13, 4), true);
		AddPng("png_palette8_32x32", Packed(PixelFormats.Indexed8, Palette(256, false), 32, 32, 8), false);
		AddPng("png_palette8_alpha_21x10", Packed(PixelFormats.Indexed8, Palette(256, true), 21, 10, 8), false);
		AddPng("png_palette8_alpha_21x10_interlaced", Packed(PixelFormats.Indexed8, Palette(256, true), 21, 10, 8), true);
		AddPng("png_palette4_17x9", Packed(PixelFormats.Indexed4, Palette(16, false), 17, 9, 4), false);
		AddPng("png_palette4_alpha_17x9_interlaced", Packed(PixelFormats.Indexed4, Palette(16, true), 17, 9, 4), true);
		AddPng("png_palette2_13x5", Packed(PixelFormats.Indexed2, Palette(4, false), 13, 5, 2), false);
		AddPng("png_palette1_33x17", Packed(PixelFormats.Indexed1, Palette(2, false), 33, 17, 1), false);
	}

	public static void Run(string a_header, string a_dump) {
		dump = a_dump;
		if (dump != "") { Directory.CreateDirectory(dump); }
		Png();
		StringBuilder text = new StringBuilder();
		text.Append("// Generated by image_fixtures.ps1 from Windows' own codecs: do not edit. Each row is a file and the pixels the reference\n");
		text.Append("// decodes it to: the FNV-1a hash of its R, G, B, A bytes, and for a lossy file the pixels themselves, as a PNG.\n");
		text.Append("#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace jai {\nnamespace image_fixtures {\n\n");
		text.Append("\tstruct fixture {\n\t\tconst char* name;\n\t\tconst char* format;\n\t\tconst unsigned char* bytes;\n\t\tsize_t size;\n\t\tuint32_t width;\n\t\tuint32_t height;\n\t\tuint64_t rgba_hash;\n\t\tconst unsigned char* reference;\n\t\tsize_t reference_size;\n\t};\n\n");
		text.Append(arrays);
		text.Append("\n\tinline constexpr fixture k_fixtures[] = {\n");
		text.Append(rows);
		text.Append("\t};\n\n} // namespace image_fixtures\n} // namespace jai\n");
		File.WriteAllText(a_header, text.ToString(), new UTF8Encoding(false));
		Console.WriteLine(count + " fixtures, " + new FileInfo(a_header).Length + " bytes of header");
	}
}
