#!/usr/bin/env bash
# Release the DS5 Dongle OLED Edition: firmware X.Y.Z + configurator app X.Y.Z,
# one tag (vX.Y.Z-oled-edition) on both repos. The tag push starts
# .github/workflows/release.yml here, which builds the firmware, builds the
# app for every OS with that firmware bundled, publishes one release with
# everything attached, and refreshes latest.json.
#
#   tools/release.sh 0.8.0 [--app ../DS5Dongle-OLED-Config-Web]
#   tools/release.sh 0.9.1-dev.1      a development build: a GitHub pre-release,
#                                     offered only on the app's Development channel
#
# Before running a stable release: both CHANGELOGs have a section for the
# version (firmware: "## [0.8.0-oled-edition]", app: "## [0.8.0]"). A
# development build's notes are the CHANGELOGs' [Unreleased] sections.
set -euo pipefail

VERSION="${1:?usage: tools/release.sh X.Y.Z[-dev.N] [--app PATH]}"
APP_DIR="$(cd "$(dirname "$0")/../.." && pwd)/DS5Dongle-OLED-Config-Web"
[ "${2:-}" = "--app" ] && APP_DIR="$(cd "${3:?--app needs a path}" && pwd)"
FW_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TAG="v$VERSION-oled-edition"

die() { echo "release: $*" >&2; exit 1; }
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-dev\.[0-9]+)?$ ]] || die "version must be X.Y.Z or X.Y.Z-dev.N"
DEV=false
[[ "$VERSION" == *-dev.* ]] && DEV=true
: "${PICO_SDK_PATH:=$HOME/pico-sdk}"
export PICO_SDK_PATH
[ -d "$PICO_SDK_PATH" ] || die "Pico SDK not found at $PICO_SDK_PATH (set PICO_SDK_PATH)"
[ -f "$APP_DIR/package.json" ] || die "app repo not found at $APP_DIR (use --app PATH)"

for dir in "$FW_DIR" "$APP_DIR"; do
  [ "$(git -C "$dir" branch --show-current)" = master ] || die "$dir is not on master"
  [ -z "$(git -C "$dir" status --porcelain --untracked-files=no)" ] || die "$dir has uncommitted changes"
  git -C "$dir" rev-parse -q --verify "refs/tags/$TAG" >/dev/null && die "$TAG already exists in $dir"
done
if [ "$DEV" = false ]; then
  grep -q "^## \[$VERSION-oled-edition\]" "$FW_DIR/CHANGELOG.md" || die "firmware CHANGELOG.md has no '## [$VERSION-oled-edition]' section"
  grep -q "^## \[$VERSION\]" "$APP_DIR/CHANGELOG.md" || die "app CHANGELOG.md has no '## [$VERSION]' section"
fi

echo "== App checks"
(cd "$APP_DIR" && npm run check)
echo "== Firmware build check (release configuration)"
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
cmake -S "$FW_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_COMPANION_VENDOR=ON -DVERSION="$TAG" >/dev/null
cmake --build "$BUILD_DIR" --target ds5-bridge >/dev/null

echo "== App version $VERSION"
(cd "$APP_DIR" && node tools/update_version.mjs "$VERSION" && node tools/update_version.mjs --verify)

cat <<MSG

Ready to release $TAG:
  - app repo:      commit "chore(release): $TAG", tag $TAG, push master + tag
  - firmware repo: tag $TAG, push master + tag
GitHub Actions then builds and publishes the release on MarcelineVPQ/DS5Dongle-OLED-Edition.
MSG
read -r -p "Push now? [y/N] " answer
if [ "$answer" != y ]; then
  # Leave nothing behind, so the next run starts from a clean tree.
  git -C "$APP_DIR" checkout -- package.json package-lock.json
  echo "Not pushed; the app version bump was undone."
  exit 0
fi

git -C "$APP_DIR" commit -am "chore(release): $TAG"
git -C "$APP_DIR" tag -a "$TAG" -m "DS5 Dongle OLED Edition $TAG"
git -C "$APP_DIR" push origin master "$TAG"
git -C "$FW_DIR" tag -a "$TAG" -m "DS5 Dongle OLED Edition $TAG"
git -C "$FW_DIR" push origin master "$TAG"

echo "Pushed. Watch: gh run watch -R MarcelineVPQ/DS5Dongle-OLED-Edition"
