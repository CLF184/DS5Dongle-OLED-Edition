// Pure part of tools/build_manifest.mjs: picks the release(s) and asset files
// that latest.json describes. Kept separate so it can be tested on fixture
// release lists without touching the network.

// Unified releases: firmware X.Y.Z + the configurator app X.Y.Z.
export const RELEASE_TAG = /^v(\d+\.\d+\.\d+)-oled-edition$/;
export const APP_FILES = {
  linux: /^DS5Dongle-OLED-Edition-Configurator-\d+\.\d+\.\d+-linux-x86_64\.AppImage$/,
  windows: /^DS5Dongle-OLED-Edition-Configurator-\d+\.\d+\.\d+-win-x64\.exe$/,
  macos: /^DS5Dongle-OLED-Edition-Configurator-\d+\.\d+\.\d+-mac-universal\.zip$/,
};
// Same naming rule as the app (PROTON_NAME in the app's src/update/manifest.ts).
export const PROTON_TARBALL = /^GE-Proton[\w.-]*DualSense\.tar\.gz$/;

export const pick = (release, pattern) => release?.assets.find((a) => pattern.test(a.name)) ?? null;
const published = (r) => !r.draft && !r.prerelease;

// The newest published unified release that carries the app. Older
// firmware-only releases (v0.7.0 and before) are skipped: the feed always
// pairs an app with the firmware it was built and tested with.
export function selectRelease(releases) {
  for (const r of releases.filter(published)) {
    const m = RELEASE_TAG.exec(r.tag_name);
    if (!m) continue;
    const platforms = Object.fromEntries(Object.entries(APP_FILES).flatMap(([os, re]) => (pick(r, re) ? [[os, pick(r, re)]] : [])));
    if (Object.keys(platforms).length === 0) continue;
    const uf2 = pick(r, new RegExp(`^ds5-bridge-oled-${r.tag_name.replace(/[.]/g, "\\.")}\\.uf2$`));
    return { release: r, version: m[1], platforms, uf2 };
  }
  return null;
}

export function selectProton(releases) {
  const r = releases.filter(published).find((rel) => rel.tag_name.startsWith("proton-")) ?? null;
  const tarball = pick(r, PROTON_TARBALL);
  return r && tarball ? { release: r, tarball, sums: pick(r, /^SHA256SUMS\.txt$/) } : null;
}

// The hash SHA256SUMS.txt lists for `name`, or null.
export function sumFor(sumsText, name) {
  const line = sumsText.split("\n").find((l) => l.trim().split(/\s+/)[1]?.replace(/^\*/, "") === name);
  return line ? line.trim().split(/\s+/)[0] : null;
}
