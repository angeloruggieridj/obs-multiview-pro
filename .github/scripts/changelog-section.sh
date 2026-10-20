#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Prints the body of the "## <version>" section of a change log (up to the
# next "## " heading). "## 1.2.0", "## v1.2.0", "## [1.2.0]" and a heading
# with text after the version ("## 1.2.0 - 2026-10-21") all match.
# Fails with a clear message when the section is missing or empty.
#
# Usage: changelog-section.sh <version> <CHANGELOG.md>
set -euo pipefail

version="${1:?usage: changelog-section.sh <version> <file>}"
file="${2:?usage: changelog-section.sh <version> <file>}"

if [ ! -f "$file" ]; then
  echo "::error::$file not found" >&2
  exit 1
fi

notes="$(awk -v ver="$version" '
  /^## / {
    if (found) exit
    h = $2
    gsub(/^\[|\]$/, "", h)
    sub(/^v/, "", h)
    if (h == ver) { found = 1; next }
  }
  found { print }
' "$file")"

if ! printf '%s\n' "$notes" | grep -q '[^[:space:]]'; then
  echo "::error::$file has no \"## $version\" section, or it is empty. Rename \"## Unreleased\" to \"## $version\" before tagging." >&2
  exit 1
fi

printf '%s\n' "$notes"
