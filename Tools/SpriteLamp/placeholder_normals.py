"""Approximate normal maps from painted images, as stand-ins for real Sprite Lamp exports. Each image gets a
normal map beside it under Sprite Lamp's export name (Map/Rock.webp -> Map/Rock_Normal.webp), which the engine loads
for a drawable with the lit shader; a real export saved over it later drops in with no code change.

	python Tools/SpriteLamp/placeholder_normals.py Assets/Map/MapRightSide_0*.webp [--size 2048] [--quality 80]

Height is the image's luminance, smoothed at a fine and a broad scale, and pulled down to nothing where the image is
transparent, so silhouettes round off at their edges. The normal is the height's slope, encoded as Sprite Lamp encodes
it: red is +x (image right), green +y (image up), blue toward the viewer, each from -1..1 to 0..255. Transparent
pixels get the flat normal. Needs numpy, Pillow and scipy.
"""
import argparse, os
import numpy as np
from PIL import Image
from scipy import ndimage

SUFFIX = "_Normal"

def normalMap(a_image, a_size, a_strength, a_fineBlur, a_broadBlur, a_edgeBlur):
	pixels = np.asarray(a_image.convert("RGBA").resize((a_size, a_size), Image.LANCZOS)).astype(np.float32) / 255.0
	luminance = 0.299 * pixels[..., 0] + 0.587 * pixels[..., 1] + 0.114 * pixels[..., 2]
	alpha = pixels[..., 3]
	height = 0.65 * ndimage.gaussian_filter(luminance, a_fineBlur) + 0.35 * ndimage.gaussian_filter(luminance, a_broadBlur)
	height *= ndimage.gaussian_filter(alpha, a_edgeBlur)
	# Strength is given for a 2048 image: a smaller map's pixels are wider, so its slopes per pixel are steeper.
	slopeScale = a_strength * a_size / 2048.0
	slopeDown, slopeRight = np.gradient(height)
	right = -slopeRight * slopeScale
	up = slopeDown * slopeScale
	length = np.sqrt(right * right + up * up + 1.0)
	result = np.stack([right / length, up / length, 1.0 / length], axis=-1)
	result[alpha <= 0.0] = (0.0, 0.0, 1.0)
	return Image.fromarray(np.clip(np.round(result * 127.5 + 127.5), 0, 255).astype(np.uint8), "RGB")

def exportName(a_path):
	stem, extension = os.path.splitext(a_path)
	return stem + SUFFIX + extension

def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("images", nargs="+")
	parser.add_argument("--size", type=int, default=2048, help="width and height of the normal map")
	parser.add_argument("--strength", type=float, default=24.0, help="how steep a change of brightness makes the surface")
	parser.add_argument("--fine-blur", type=float, default=1.5, help="smoothing of small detail, in normal map pixels")
	parser.add_argument("--broad-blur", type=float, default=8.0, help="smoothing of large shapes, in normal map pixels")
	parser.add_argument("--edge-blur", type=float, default=3.0, help="how far silhouettes round off, in normal map pixels")
	parser.add_argument("--quality", type=int, default=80, help="lossy webp quality; 100 writes lossless")
	arguments = parser.parse_args()
	for path in arguments.images:
		if os.path.splitext(path)[0].endswith(SUFFIX):
			continue
		output = exportName(path)
		result = normalMap(Image.open(path), arguments.size, arguments.strength, arguments.fine_blur, arguments.broad_blur, arguments.edge_blur)
		if output.lower().endswith(".webp"):
			if arguments.quality >= 100:
				result.save(output, lossless=True, method=6)
			else:
				result.save(output, quality=arguments.quality, method=6)
		else:
			result.save(output)
		print(f"{output}: {os.path.getsize(output) / 1024.0:.0f} KiB")

if __name__ == "__main__":
	main()
