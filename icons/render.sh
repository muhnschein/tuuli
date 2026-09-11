#!/bin/bash
# Regenerate the launcher icons from icons/harbour-tuuli.svg at the sizes Harbour expects.
# The PNGs are committed so the device build needs no SVG tooling.
set -euo pipefail
cd "$(dirname "$0")"
for size in 86 108 128 172; do
    mkdir -p "${size}x${size}"
    rsvg-convert -w "$size" -h "$size" harbour-tuuli.svg -o "${size}x${size}/harbour-tuuli.png"
done
echo "icons rendered"
