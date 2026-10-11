// node --test tools/manifest_lib.test.mjs
import assert from "node:assert/strict";
import { test } from "node:test";
import { selectProton, selectRelease, sumFor } from "./manifest_lib.mjs";

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
