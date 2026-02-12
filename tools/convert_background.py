#!/usr/bin/env python3
"""convert_background.py - Convert RGB PNG to SNES-indexed PNG for backgrounds.

Takes a high-res PNG, resizes to target dimensions, reduces to 16 colors,
and outputs an indexed-mode PNG suitable for gfx4snes. No transparency handling.

Usage:
    python convert_background.py INPUT OUTPUT --width 256 --height 256 [--colors 16]
"""

import argparse
import os
import sys
from PIL import Image


def convert_background(input_path, output_path, width, height, max_colors=16):
    """Convert an RGB PNG to a SNES-compatible indexed PNG.

    Args:
        input_path: Path to source PNG.
        output_path: Path for output indexed PNG.
        width: Target width in pixels.
        height: Target height in pixels.
        max_colors: Maximum colors (1-16, default 16).
    """
    if max_colors < 1 or max_colors > 16:
        print(f"Error: colors must be 1-16, got {max_colors}", file=sys.stderr)
        sys.exit(1)

    # Open and convert to RGB (drop alpha if present)
    img = Image.open(input_path).convert("RGB")

    # Resize to target dimensions
    img = img.resize((width, height), Image.LANCZOS)

    # Quantize to max_colors using median cut
    quantized = img.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT)

    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)

    quantized.save(output_path)
    print(f"  Background: {input_path} -> {output_path} ({width}x{height}, {max_colors} colors)")


def main():
    parser = argparse.ArgumentParser(description="Convert RGB PNG to SNES indexed background PNG")
    parser.add_argument("input", help="Source PNG path")
    parser.add_argument("output", help="Output indexed PNG path")
    parser.add_argument("--width", type=int, required=True, help="Target width")
    parser.add_argument("--height", type=int, required=True, help="Target height")
    parser.add_argument("--colors", type=int, default=16,
                        help="Max colors (1-16, default 16)")
    args = parser.parse_args()

    if not os.path.isfile(args.input):
        print(f"Error: Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)

    convert_background(args.input, args.output, args.width, args.height, args.colors)


if __name__ == "__main__":
    main()
