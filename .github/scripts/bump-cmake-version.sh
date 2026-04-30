#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "Usage: $0 <patch|minor> [file]" >&2
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
  usage
  exit 2
fi

mode="$1"
file="${2:-CMakeLists.txt}"

if [[ "$mode" != "patch" && "$mode" != "minor" ]]; then
  usage
  exit 2
fi

if [[ ! -f "$file" ]]; then
  echo "Version file not found: $file" >&2
  exit 1
fi

version_line="$(
  grep -nE '^[[:space:]]*project[[:space:]]*\([^)]*[[:space:]]VERSION[[:space:]]+[0-9]+(\.[0-9]+){0,3}' "$file" |
    head -n1 |
    cut -d: -f1 ||
    true
)"

if [[ -z "$version_line" ]]; then
  echo "CMake project(... VERSION ...) line not found in $file" >&2
  exit 1
fi

line_text="$(sed -n "${version_line}p" "$file")"
current="$(
  printf '%s\n' "$line_text" |
    sed -E 's/.*VERSION[[:space:]]+([0-9]+(\.[0-9]+){0,3}).*/\1/'
)"

IFS='.' read -r major minor patch _ <<< "$current"
minor="${minor:-0}"
patch="${patch:-0}"

case "$mode" in
  patch)
    patch=$((patch + 1))
    ;;
  minor)
    minor=$((minor + 1))
    patch=0
    ;;
esac

new="${major}.${minor}.${patch}"

if [[ "$current" == "$new" ]]; then
  echo "Version already at $new"
else
  echo "Bumping version $current -> $new"
fi

sed -i -E "${version_line}s/(VERSION[[:space:]]+)[0-9]+(\.[0-9]+){0,3}/\\1${new}/" "$file"
