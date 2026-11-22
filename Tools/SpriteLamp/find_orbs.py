"""Finds the glowing orbs painted into images: bright warm pixels, joined into blobs, each blob's brightness-weighted
centre and size. Prints JSON (one entry per orb, in image pixels) and writes a debug image with each orb marked.

	python Tools/SpriteLamp/find_orbs.py Assets/Map/MapRightSide_0*.webp [--debug out.png] [--json orbs.json]

An orb is "large" (a big glowing sphere or swirl) or "small" (a lamp's cluster of little spheres) by its lit area.
Needs numpy, Pillow and scipy.
"""
import argparse, json, math, os
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

WORK_SIZE = 1024
LARGE_AREA = 2000
SMALL_AREA = 300

def warmGlowMask(a_pixels):
	r, g, b, a = (a_pixels[..., channel] for channel in range(4))
	luminance = 0.299 * r + 0.587 * g + 0.114 * b
	result = (a > 200) & (r > 225) & (g > 170) & (r >= g) & (r - b > 25) & (luminance > 190)
	return ndimage.binary_opening(result, iterations=1), luminance

def findOrbs(a_path):
	image = Image.open(a_path).convert("RGBA")
	scale = image.size[0] / WORK_SIZE
	pixels = np.asarray(image.resize((WORK_SIZE, WORK_SIZE), Image.BILINEAR)).astype(np.float32)
	mask, luminance = warmGlowMask(pixels)
	# Swirls and lamp clusters break into many specks; growing the mask joins the specks of one orb.
	grownLabels, count = ndimage.label(ndimage.binary_dilation(mask, iterations=6))
	labels = grownLabels * mask
	r, g, b, a = (pixels[..., channel] for channel in range(4))
	warm = (a > 200) & (r - b > 40)
	result = []
	for label in range(1, count + 1):
		lit = labels == label
		area = int(lit.sum())
		if area < SMALL_AREA:
			continue
		ys, xs = np.nonzero(lit)
		weights = luminance[lit]
		centreX = float((xs * weights).sum() / weights.sum())
		centreY = float((ys * weights).sum() / weights.sum())
		# The glow's colour: the warm pixels of the orb and the halo around its white-hot core, brightest channel at 1.
		halo = (grownLabels == label) & warm
		colour = pixels[halo][:, :3].mean(axis=0) if halo.any() else pixels[lit][:, :3].mean(axis=0)
		colour = colour / colour.max()
		result.append({
			"x": round(centreX * scale),
			"y": round(centreY * scale),
			"radius": round(math.sqrt(area / math.pi) * scale),
			"area": round(area * scale * scale),
			"kind": "large" if area >= LARGE_AREA else "small",
			"colour": [round(float(channel), 3) for channel in colour],
			"box": [int(xs.min() * scale), int(ys.min() * scale), int((xs.max() + 1) * scale), int((ys.max() + 1) * scale)]
		})
	result.sort(key=lambda a_orb: (a_orb["y"], a_orb["x"]))
	return image, result

def drawMarks(a_image, a_orbs, a_title, a_size):
	backdrop = Image.new("RGBA", a_image.size, (40, 40, 40, 255))
	result = Image.alpha_composite(backdrop, a_image).convert("RGB").resize((a_size, a_size), Image.BILINEAR)
	scale = a_size / a_image.size[0]
	draw = ImageDraw.Draw(result)
	for index, orb in enumerate(a_orbs):
		x, y, radius = orb["x"] * scale, orb["y"] * scale, max(6, orb["radius"] * scale)
		colour = (0, 255, 255) if orb["kind"] == "large" else (255, 0, 255)
		draw.ellipse((x - radius, y - radius, x + radius, y + radius), outline=colour, width=3)
		draw.line((x - 8, y, x + 8, y), fill=colour, width=2)
		draw.line((x, y - 8, x, y + 8), fill=colour, width=2)
		draw.text((x + radius + 4, y - 6), f"{index} ({orb['x']},{orb['y']})", fill=colour)
	draw.text((8, 8), a_title, fill=(255, 255, 255))
	return result

def main():
	parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
	parser.add_argument("images", nargs="+")
	parser.add_argument("--debug", help="write a sheet of the images with every orb marked")
	parser.add_argument("--json", help="write the orbs here as well as to stdout")
	arguments = parser.parse_args()
	found = {}
	sheets = []
	for path in arguments.images:
		image, orbs = findOrbs(path)
		found[os.path.basename(path)] = orbs
		if arguments.debug:
			sheets.append(drawMarks(image, orbs, os.path.basename(path), 768))
	text = json.dumps(found, indent="\t")
	print(text)
	if arguments.json:
		with open(arguments.json, "w") as output:
			output.write(text + "\n")
	if sheets:
		columns = min(3, len(sheets))
		rows = (len(sheets) + columns - 1) // columns
		sheet = Image.new("RGB", (columns * 768, rows * 768), (0, 0, 0))
		for index, tile in enumerate(sheets):
			sheet.paste(tile, ((index % columns) * 768, (index // columns) * 768))
		sheet.save(arguments.debug)

if __name__ == "__main__":
	main()
