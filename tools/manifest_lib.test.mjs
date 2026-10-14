// node --test tools/manifest_lib.test.mjs
import assert from "node:assert/strict";
import { test } from "node:test";
import { selectProton, selectRelease, sumFor, versionKey } from "./manifest_lib.mjs";

const asset = (name) => ({ name, size: 1, browser_download_url: `https://github.com/x/releases/download/t/${name}` });
const rel = (tag_name, names, extra = {}) => ({ tag_name, html_url: `https://github.com/x/releases/tag/${tag_name}`, draft: false, prerelease: false, assets: names.map(asset), ...extra });

const unified = rel("v0.8.0-oled-edition", [
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-linux-x86_64.AppImage",
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-win-x64.exe",
  "DS5Dongle-OLED-Edition-Configurator-0.8.0-mac-universal.zip",
  "ds5-bridge-oled-v0.8.0-oled-edition.uf2",
  "SHA256SUMS.txt",
]);
const fwOnly = rel("v0.7.0-oled-edition", ["ds5-bridge-oled-v0.7.0-oled-edition.uf2", "SHA256SUMS.txt"]);
const proton = rel("proton-GE-Proton11-7-DualSense", ["GE-Proton11-7-DualSense.tar.gz", "SHA256SUMS.txt", "dualsense-serial-colon-sanitize.patch"]);

test("picks the newest unified release and pairs its app with its own firmware", () => {
  const s = selectRelease([proton, unified, fwOnly]);
  assert.equal(s.version, "0.8.0");
  assert.deepEqual(Object.keys(s.platforms).sort(), ["linux", "macos", "windows"]);
  assert.equal(s.uf2.name, "ds5-bridge-oled-v0.8.0-oled-edition.uf2");
});

test("skips firmware-only, draft and prerelease releases", () => {
  assert.equal(selectRelease([fwOnly, proton]), null);
  assert.equal(selectRelease([{ ...unified, draft: true }, fwOnly]), null);
  assert.equal(selectRelease([{ ...unified, prerelease: true }]), null);
});

test("never treats a proton release as the app/firmware release", () => {
  const strayUf2 = rel("proton-GE-Proton11-7-DualSense", ["ds5-bridge-oled-proton-GE-Proton11-7-DualSense.uf2"]);
  assert.equal(selectRelease([strayUf2]), null);
});

test("finds the proton tarball and its checksum line", () => {
  const p = selectProton([unified, proton]);
  assert.equal(p.tarball.name, "GE-Proton11-7-DualSense.tar.gz");
  const sums = "5c73  GE-Proton11-7-DualSense.tar.gz\ne2f9  dualsense-serial-colon-sanitize.patch\n";
  assert.equal(sumFor(sums, "GE-Proton11-7-DualSense.tar.gz"), "5c73");
  assert.equal(sumFor(sums, "missing.tar.gz"), null);
  assert.equal(sumFor("aa *GE-Proton11-7-DualSense.tar.gz\n", "GE-Proton11-7-DualSense.tar.gz"), "aa");
});

const devBuild = (v) =>
  rel(`v${v}-oled-edition`, [`DS5Dongle-OLED-Edition-Configurator-${v}-linux-x86_64.AppImage`, `ds5-bridge-oled-v${v}-oled-edition.uf2`], { prerelease: true });
const stable090 = rel("v0.9.0-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.0-linux-x86_64.AppImage", "ds5-bridge-oled-v0.9.0-oled-edition.uf2"]);

test("orders development builds before their release", () => {
  assert.deepEqual(versionKey("0.9.1-dev.2"), [0, 9, 1, 2]);
  assert.equal(versionKey("0.9.1")[3], Infinity);
  assert.equal(versionKey("nope"), null);
});

test("the stable feed never offers a development build", () => {
  assert.equal(selectRelease([devBuild("0.9.1-dev.1"), stable090]).version, "0.9.0");
});

test("the dev feed offers the newest of development and stable releases", () => {
  const s = selectRelease([devBuild("0.9.1-dev.2"), devBuild("0.9.1-dev.1"), stable090], "dev");
  assert.equal(s.version, "0.9.1-dev.2");
  assert.equal(s.uf2.name, "ds5-bridge-oled-v0.9.1-dev.2-oled-edition.uf2");
  // Once 0.9.1 ships, it wins over its own development builds.
  const stable091 = rel("v0.9.1-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.1-linux-x86_64.AppImage"]);
  assert.equal(selectRelease([stable091, devBuild("0.9.1-dev.2"), stable090], "dev").version, "0.9.1");
  // No development build yet: the dev feed is the stable release.
  assert.equal(selectRelease([stable090], "dev").version, "0.9.0");
});

test("the Windows entry is the per-user installer (or an older release's portable exe)", () => {
  const installer = rel("v0.9.6-oled-edition", [
    "DS5Dongle-OLED-Edition-Configurator-0.9.6-linux-x86_64.AppImage",
    "DS5Dongle-OLED-Edition-Configurator-0.9.6-win-x64-setup.exe",
    "ds5-bridge-oled-v0.9.6-oled-edition.uf2",
  ]);
  assert.equal(selectRelease([installer]).platforms.windows.name, "DS5Dongle-OLED-Edition-Configurator-0.9.6-win-x64-setup.exe");
  assert.equal(selectRelease([unified]).platforms.windows.name, "DS5Dongle-OLED-Edition-Configurator-0.8.0-win-x64.exe");
  const stray = rel("v0.9.7-oled-edition", ["DS5Dongle-OLED-Edition-Configurator-0.9.7-win-x64-setup.exe.blockmap", "ds5-bridge-oled-v0.9.7-oled-edition.uf2"]);
  assert.equal(selectRelease([stray]), null);
});
