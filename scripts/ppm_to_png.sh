#!/bin/sh

set -eu

usage() {
    echo "Usage: $0 [input.ppm] [output.png]" >&2
    echo "  Defaults: input=out.ppm, output=<input basename>.png" >&2
}

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    usage
    exit 0
fi

input="${1:-out.ppm}"

if [ ! -f "$input" ]; then
    echo "Error: input file not found: $input" >&2
    exit 1
fi

case "$input" in
    *.ppm) ;;
    *)
        echo "Error: input must be a .ppm file: $input" >&2
        exit 1
        ;;
esac

output="${2:-${input%.ppm}.png}"

case "$output" in
    *.png) ;;
    *)
        echo "Error: output must be a .png file: $output" >&2
        exit 1
        ;;
esac

if ! command -v sips >/dev/null 2>&1; then
    echo "Error: sips not found. This script currently supports macOS only." >&2
    exit 1
fi

sips -s format png "$input" --out "$output" >/dev/null
echo "Wrote $output"
