#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Publishes a GitHub release safely and idempotently with gh:
#   1. a published (non-draft) release for the tag already exists -> fail;
#   2. otherwise create a draft (or reuse the draft a failed run left),
#      set title and notes;
#   3. upload every asset (--clobber replaces a partial upload);
#   4. check that the draft holds exactly the expected asset names;
#   5. publish it (--draft=false).
# Until step 5 the release is a draft, which the plugin's update check skips.
#
# Usage: publish-release.sh <tag> <title> <notes-file> <asset>...
set -euo pipefail

tag="${1:?usage: publish-release.sh <tag> <title> <notes-file> <asset>...}"
title="${2:?missing title}"
notes="${3:?missing notes file}"
shift 3
if [ "$#" -eq 0 ]; then
  echo "::error::no assets given" >&2
  exit 1
fi

if state="$(gh release view "$tag" --json isDraft --jq '.isDraft' 2>/dev/null)"; then
  if [ "$state" != "true" ]; then
    echo "::error::A published release for $tag already exists. Delete it (or use a new version) before running the release again." >&2
    exit 1
  fi
  echo "Reusing the existing draft release for $tag"
  gh release edit "$tag" --title "$title" --notes-file "$notes"
else
  gh release create "$tag" --draft --verify-tag --title "$title" --notes-file "$notes"
fi

gh release upload "$tag" --clobber "$@"

expected="$(for f in "$@"; do basename "$f"; done | LC_ALL=C sort)"
actual="$(gh release view "$tag" --json assets --jq '.assets[].name' | LC_ALL=C sort)"
if [ "$expected" != "$actual" ]; then
  echo "::error::The draft release for $tag does not hold exactly the expected assets." >&2
  echo "expected:" >&2
  printf '%s\n' "$expected" >&2
  echo "found:" >&2
  printf '%s\n' "$actual" >&2
  exit 1
fi

gh release edit "$tag" --draft=false
echo "Published $tag with $(printf '%s\n' "$expected" | wc -l) assets."
