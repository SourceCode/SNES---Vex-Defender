#!/usr/bin/env python3
"""convert_sprite.py - Convert RGBA PNG to SNES-indexed PNG for sprites.

Takes a high-res RGBA PNG, resizes to target dimensions, reduces to 15 colors
plus transparent index 0, and outputs an indexed-mode PNG suitable for gfx4snes.

Usage:
    python convert_sprite.py INPUT OUTPUT --size 32 [--colors 15]
"""

import argparse
import os
import sys
from PIL import Image


def convert_sprite(input_path, output_path, size, max_colors=15):
    """Convert an RGBA PNG to a SNES-compatible indexed PNG.

    Args:
        input_path: Path to source RGBA PNG.
        output_path: Path for output indexed PNG.
        size: Target width and height (square).
        max_colors: Maximum opaque colors (1-15). Index 0 is transparent.
    """
    if max_colors < 1 or max_colors > 15:
        print(f"Error: colors must be 1-15, got {max_colors}", file=sys.stderr)
        sys.exit(1)

    # Open and convert to RGBA
    img = Image.open(input_path).convert("RGBA")

    # Resize to target dimensions
    img = img.resize((size, size), Image.LANCZOS)

    # Split into RGB and alpha
    r, g, b, a = img.split()
    rgb = Image.merge("RGB", (r, g, b))

    # Build transparency mask: True where pixel is transparent
    alpha_data = list(a.getdata())
    transparent_mask = [val < 128 for val in alpha_data]

    # Quantize RGB to max_colors using median cut
    quantized = rgb.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT)
    quant_data = list(quantized.getdata())
    quant_palette = quantized.getpalette()  # flat [R,G,B,R,G,B,...]

    # Build new palette: index 0 = black (transparent), indices 1-N = quantized colors
    new_palette = [0, 0, 0]  # index 0: transparent (black)
    for i in range(max_colors):
        idx = i * 3
        new_palette.extend(quant_palette[idx:idx + 3])
    # Pad palette to 256 entries (768 bytes)
    while len(new_palette) < 768:
        new_palette.append(0)

    # Create output indexed image
    out = Image.new("P", (size, size))
    out.putpalette(new_palette)

    # Map pixels: transparent -> index 0, opaque -> quantized index + 1
    out_data = []
    for i in range(len(quant_data)):
        if transparent_mask[i]:
            out_data.append(0)
        else:
            out_data.append(quant_data[i] + 1)

    out.putdata(out_data)

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    out.save(output_path)
    print(f"  Sprite: {input_path} -> {output_path} ({size}x{size}, {max_colors}+1 colors)")


def main():
    parser = argparse.ArgumentParser(description="Convert RGBA PNG to SNES indexed sprite PNG")
    parser.add_argument("input", help="Source RGBA PNG path")
    parser.add_argument("output", help="Output indexed PNG path")
    parser.add_argument("--size", type=int, required=True,
                        choices=[8, 16, 32, 64], help="Target size (square)")
    parser.add_argument("--colors", type=int, default=15,
                        help="Max opaque colors (1-15, default 15)")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    convert_sprite(args.input, args.output, args.size, args.colors)


if __name__ == "__main__":
    main()
