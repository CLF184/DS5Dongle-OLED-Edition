// Pure part of tools/build_manifest.mjs: picks the release(s) and asset files
// that latest.json describes. Kept separate so it can be tested on fixture
// release lists without touching the network.

// Unified releases: firmware X.Y.Z + the configurator app X.Y.Z; development
// builds (X.Y.Z-dev.N, GitHub pre-releases) only for the dev channel.
export const RELEASE_TAG = /^v(\d+\.\d+\.\d+)-oled-edition$/;
export const DEV_TAG = /^v(\d+\.\d+\.\d+-dev\.\d+)-oled-edition$/;
const V = String.raw`\d+\.\d+\.\d+(?:-dev\.\d+)?`;
export const APP_FILES = {
  linux: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-linux-x86_64\\.AppImage$`),
  // The per-user installer (app 0.9.5-dev.12 on); older releases shipped a portable exe.
  windows: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-win-x64(?:-setup)?\\.exe$`),
  macos: new RegExp(`^DS5Dongle-OLED-Edition-Configurator-${V}-mac-universal\\.zip$`),
};

// X.Y.Z or X.Y.Z-dev.N -> sortable [X, Y, Z, N] (a release sorts after its dev builds).
export function versionKey(v) {
  const m = /^(\d+)\.(\d+)\.(\d+)(?:-dev\.(\d+))?$/.exec(v);
  return m ? [Number(m[1]), Number(m[2]), Number(m[3]), m[4] === undefined ? Infinity : Number(m[4])] : null;
}
const newer = (a, b) => {
  const x = versionKey(a), y = versionKey(b);
  for (let i = 0; i < 4; i++) if (x[i] !== y[i]) return x[i] > y[i];
  return false;
};
// Same naming rule as the app (PROTON_NAME in the app's src/update/manifest.ts).
export const PROTON_TARBALL = /^GE-Proton[\w.-]*DualSense\.tar\.gz$/;

export const pick = (release, pattern) => release?.assets.find((a) => pattern.test(a.name)) ?? null;
const published = (r) => !r.draft && !r.prerelease;

// The newest published unified release that carries the app. Older
// firmware-only releases (v0.7.0 and before) are skipped: the feed always
// pairs an app with the firmware it was built and tested with.
// channel "stable": only published stable releases. "dev": the newest of the
// development pre-releases and the stable releases (dev users get those too).
export function selectRelease(releases, channel = "stable") {
  let best = null;
  for (const r of releases) {
    if (r.draft) continue;
    const stable = !r.prerelease ? RELEASE_TAG.exec(r.tag_name) : null;
    const dev = channel === "dev" && r.prerelease ? DEV_TAG.exec(r.tag_name) : null;
    const m = stable ?? dev;
    if (!m) continue;
    const platforms = Object.fromEntries(Object.entries(APP_FILES).flatMap(([os, re]) => (pick(r, re) ? [[os, pick(r, re)]] : [])));
    if (Object.keys(platforms).length === 0) continue;
    const uf2 = pick(r, new RegExp(`^ds5-bridge-oled-${r.tag_name.replace(/[.]/g, "\\.")}\\.uf2$`));
    const found = { release: r, version: m[1], platforms, uf2 };
    if (channel === "stable") return found; // the API lists newest first
    if (!best || newer(found.version, best.version)) best = found;
  }
  return best;
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
