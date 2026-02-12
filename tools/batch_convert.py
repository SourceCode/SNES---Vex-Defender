#!/usr/bin/env python3
"""batch_convert.py - Batch asset conversion driver.

Reads asset_manifest.json and runs convert_sprite.py / convert_background.py
for each entry. Supports --phase flag to only convert assets for a specific phase.

Usage:
    python tools/batch_convert.py                 # Convert all assets
    python tools/batch_convert.py --phase 3       # Only Phase 3 verification set
"""

import argparse
import json
import os
import sys

# Add tools directory to path for imports
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS_DIR)

from convert_sprite import convert_sprite
from convert_background import convert_background


def load_manifest():
    """Load the asset manifest JSON."""
    manifest_path = os.path.join(TOOLS_DIR, "asset_manifest.json")
    with open(manifest_path, "r") as f:
        return json.load(f)


def process_assets(manifest, phase_filter=None):
    """Process all assets in the manifest.

    Args:
        manifest: Parsed manifest dict.
        phase_filter: If set, only process assets with this phase number.
    """
    source_root = manifest["source_root"]
    output_root = manifest["output_root"]

    converted = 0
    skipped = 0

    # Iterate all categories
    for category, assets in manifest["assets"].items():
        for asset in assets:
            # Phase filter
            if phase_filter is not None and asset.get("phase", 999) != phase_filter:
                skipped += 1
                continue

            source_path = os.path.join(source_root, asset["source"])
            output_path = os.path.join(output_root, asset["output"])

            if not os.path.isfile(source_path):
                print(f"  WARNING: Source not found, skipping: {source_path}",
                      file=sys.stderr)
                skipped += 1
                continue

            if asset["type"] == "sprite":
                convert_sprite(source_path, output_path,
                               asset["size"], asset.get("colors", 15))
            elif asset["type"] == "background":
                convert_background(source_path, output_path,
                                   asset["width"], asset["height"],
                                   asset.get("colors", 16))
            else:
                print(f"  WARNING: Unknown type '{asset['type']}' for {asset['id']}",
                      file=sys.stderr)
                skipped += 1
                continue

            converted += 1

    return converted, skipped


def main():
    parser = argparse.ArgumentParser(description="Batch asset conversion for SNES")
    parser.add_argument("--phase", type=int, default=None,
                        help="Only convert assets for this phase number")
    args = parser.parse_args()

    manifest = load_manifest()

    phase_str = f" (phase {args.phase})" if args.phase else " (all phases)"
    print(f"=== Batch Asset Conversion{phase_str} ===")

    converted, skipped = process_assets(manifest, args.phase)

    print(f"=== Done: {converted} converted, {skipped} skipped ===")

    if converted == 0:
        print("WARNING: No assets were converted!", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
