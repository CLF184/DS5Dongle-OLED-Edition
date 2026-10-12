# Changelog

All notable changes to **Pico2W DualSense 5 Bridge — OLED Edition** are documented here. This fork tracks [awalol/DS5Dongle](https://github.com/awalol/DS5Dongle) (upstream) and adds an optional OLED status display plus a security/correctness audit pass on the core bridge.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/). Versioning approximates [SemVer](https://semver.org/) with the upstream version stream — the fork shares a major.minor with whatever upstream tag it is rebased on.

---

## [Unreleased]

### Added

- **Controller health & info (protocol 1.12):** the configurator can show the connected controller's own firmware (build date, hardware, main / SBL / DSP versions, Bluetooth patch), its lifetime counters (play and charge time, haptics and trigger time, charge cycles, connections, button presses, stick travel) and its battery voltage. GET 0xEB (vendor interface only; wValue's high byte picks the header, the firmware info or a result page) and 0xF6 0x16 [dev, action, pages], which queues one of two read-only Sony test commands (counters 0x70/1, battery voltage 0x04/3; anything else is refused) and runs it from the main loop after the connect-time colorway read, one 0x81 request at a time, never inside a USB transfer. The controller's serial number and the Edge stick modules' serials in the counters block are blanked before the app can read them, and the device-info bytes of report 0x20 are withheld. Identity [46] bit 0 announces it. Counter layout from daidr/dualsense-tester (MIT).

### Fixed

- **Core1's health never reached the app:** the 0xF5 audio counters wanted 68 bytes, but both the configurator interface and the HID path answer at most 63, so the speaker-encoder failure flag and the free heap added in 0.9.4 (issue #15) were cut off. 0xF5 is now 63 bytes: the 15 counters stay where they were, [60] holds core1's flags (bit 7 set marks this layout) and [61..62] the free heap at core1 start in KiB. Older apps read the counters as before; `scripts/audio_diag.py` reads the new layout. A host test checks that no reply needs more than 63 bytes (it found 0xFE, the mic-debug frame dump, wants 82; left as is for now).

## [0.9.5-dev.5-oled-edition] - 2026-10-12

### Added

- **Per-stage timings (protocol 1.11):** the dongle times its own work: each pass of the main loop and its parts (Bluetooth, USB, audio, input reports, OLED) and, on core1, every speaker frame encoded and every mic frame decoded, keeping the average and the worst case. Read with 0xFC (63 bytes from a host that asks for them; 11 as before otherwise), reset with 0xF6 0x15; identity [29] bit 7 announces it. For the app's dashboard and for comparing builds on real hardware.
- **Host unit tests** (`tests/host/run.sh`, also in CI): calibration, stick curves (checked against the app's sample values), button remapping, the config codec, the configurator command parser (every report at every length stays inside its buffer) and the OLED transfer, built on a PC with AddressSanitizer and UndefinedBehaviorSanitizer. `tools/analyze.sh` reports firmware size, RAM use, stack depth and clang-tidy / cppcheck findings.

### Changed

- **Lighter OLED refresh:** a frame goes to the display in 129 SPI transfers instead of one per byte (~1,150), with exactly the same bytes, and an unchanged frame isn't sent at all (still refreshed every 2 s), so the main loop spends far less time on the screen.
- **The whole version on the OLED Status screen:** the title is now "DS5 B. " and the version without "-oled-edition" ("DS5 B. v0.9.5-dev.4" fits exactly; it used to be cut off after "DS5 Bridge v0.9.5-d"). A longer local build version scrolls slowly back and forth, never over the link icon. The boot splash shows the real version and board too (it said "v0.6.0" and "Pico2W").
- **Leaner speaker encoder:** libopus is built without its desktop hardening (its fortified `memcpy`/`memset` ran from flash on core1 every frame, the stall the RAM relocation exists to avoid; ~17 KB less RAM and ~13 KB less flash), and the speaker encoder runs CELT-only (`OPUS_APPLICATION_RESTRICTED_CELT`): the same packets the controller got before at these settings, from half the encoder memory (~24 KB more free heap on core1) and 4 ms sooner. `-DOPUS_ENCODER_CELT_ONLY=OFF` brings back the previous encoder mode.

## [0.9.5-dev.4-oled-edition] - 2026-10-12

### Added

- **Stick response curves (protocol 1.10):** a response curve per stick, per pairing slot, applied in the dongle to the report games receive (after calibration, so it works in every game): Linear (default), Precise, Steady, Aggressive, Digital, or Custom with four points. The tilt goes through the curve, the direction stays. Stored with the calibration (older saves read as Linear, and older firmware still reads the calibration). Read with 0xEC, set with 0xF6 0x14 (saved, or temporary for a game's profile: RAM only, ended by 0x12, a reboot or a saved set); identity [29] bit 6 announces it, [19] bit 2 says temporary curves are on.
- **Remap the DualSense Edge's Fn buttons and back buttons (LB / RB) (protocol 1.9):** four more remap entries (Fn L/R, LB/RB): each can act as any standard button, stay itself (default) or do nothing; a remapped one presses its target instead of itself. Read with 0xED, set with 0xF6 0x13 (saved or temporary, like game layouts). Older saved remaps keep working (the new entries read as "itself").

## [0.9.5-dev.3-oled-edition] - 2026-10-12

## [0.9.5-dev.2-oled-edition] - 2026-10-12

## [0.9.5-dev.1-oled-edition] - 2026-10-12

## [0.9.4-oled-edition] - 2026-10-12

### Fixed

- **No haptics and failing saves when the speaker encoder couldn't start (issue #15):** if core1 couldn't get memory for the Opus encoder at startup it returned and stopped for good, so audio frames were never encoded, haptics waited forever for speaker frames, and every flash write (which parks core1) timed out (`Dongle command 0x02 failed`). Core1 now keeps running without the encoder, haptics go out alone (the Pico W layout; the narrator still speaks), saving works, and 0xF5 reports core1's health ([15] flags: started, speaker encoder failed, mic decoder failed; [16] free heap at core1 start). The diag scripts also find a DualSense Edge dongle (054C:0DF2).

### Added

- **Temporary remaps (protocol 1.8):** 0xF6 func `0x11` applies a remap table in RAM only (a game's button layout from the app, so starting and stopping games never rewrites flash), `0x12` goes back to the saved table (as does a reboot); identity [19] bit 1 says a temporary table is active, [29] bit 4 announces the feature. A normal `0x10` set still saves and ends the temporary state.
- **Dev builds are stamped from Git:** `tools/dev_firmware.sh [--flash]` and any build without `-DVERSION` report e.g. `v0.9.3-oled-edition+3.a1b2c3d[.dirty]` (OLED, app, picotool) instead of a stale "dev" version kept in an old build folder. `tools/bootsel.py` puts the dongle into BOOTSEL over HID.

## [0.9.3-oled-edition] - 2026-10-12

### Added

- **Board name in the identity report (protocol 1.7):** identity [29] bit 3, then [30..45] the board's name as ASCII ("Pico 2 W", "Pico W"), set per build from `PICO_BOARD` (`DS5_BOARD_NAME` in CMake), so the app names the hardware without an app update for each new board.

## [0.9.2-oled-edition] - 2026-10-12

The firmware is unchanged from 0.9.0. This release fixes the app on Windows PCs where a real DualSense was plugged in by USB before: the dongle never connected, and the app now offers **Repair USB driver** (details in the app's CHANGELOG).

## [0.9.1-oled-edition] - 2026-10-11

The firmware is unchanged from 0.9.0. This release brings the app's opt-in **Development update channel**.

### Added

- **Development builds** (`vX.Y.Z-dev.N-oled-edition`): published as GitHub pre-releases and listed only in the new `latest-dev.json` feed, which the app's Development channel reads. The stable feed (`latest.json`) never lists them.

## [0.9.0-oled-edition] — 2026-10-11

### Added

- **Left and right grip haptics strength (protocol 1.6).** `haptics_gain` is now the left grip and a new `haptics_gain_right` the right one (older saved settings start with both equal, so nothing changes until you set them apart). A host settings write never changes the narrator setting. The OLED Hap Gain setting moves both; it shows L / R when they differ. Classic rumble (the vibration most games send) now follows the grip strengths too, not just HD haptics. A grip test (0xF6 0x0E) buzzes one grip at its strength for half a second.
- **Calibration in the dongle (protocol 1.5), per pairing slot.** Stick center offset, deadzone and full-tilt radius (each half-axis scaled to its own reach, so full tilt reaches both ends even when the center leans), trigger travel and gyro zero, one set per pairing slot in their own flash sector, applied to the report games receive (the live input the app reads stays raw). Untouched sticks and triggers pass straight through. The gyro zero removes only what's left after the controller's factory bias (which games already subtract). Nothing is written to the controller. Set with 0xF6 function 0x0D ['C','L',2, slot, 18-byte body], read with 0xEE ([slot] + body); identity [29] bit 1 announces it.
- **Screen mirror for the app (protocol 1.4).** A read-only vendor request (`0xEF`) returns the OLED image exactly as last sent to the panel, with the current screen, contrast and power state, so the app shows the dongle's screen pixel for pixel. Works without the OLED add-on fitted (the firmware draws the same frames either way). Announced by a new feature byte in the identity report ([29] bit 0).

### Fixed

- The OLED brightness saved in Settings now applies from the first frame (the panel started at a fixed contrast the screen mirror didn't report).

## [0.8.1-oled-edition] — 2026-10-11

### Fixed

- **Rumble no longer cancelled by the dongle's own reports.** The lightbar colour (sent ~10× a second while charging or on a non-HOST lightbar mode), volume changes, the trigger preview and the connect state now carry the game's current rumble instead of "no rumble", which could stop rumble in some games (reported in Crimson Desert) while haptics and triggers kept working. The game's rumble is forgotten on disconnect so a reconnect never resumes it.
- **Output could stay dead after a reconnect.** Packets still queued when the controller dropped are now discarded (upstream a1492fd/697ade0); before, rumble, triggers, lightbar and speaker could all stay silent until the dongle restarted.
- The controller's last input no longer lingers after it disconnects (a neutral report, upstream 8c8b527).
- Idle disconnect works with the mic on (only input reports count, upstream d7fb163).
- SetStateData: the motor power nibbles were swapped, and `UseRumbleNotHaptics2` (byte 38 bit 3, Ninja Gaiden 4) is declared (upstream 8d8255c).
- **Faster, steadier connecting.** A first-time pair opens its second Bluetooth channel as soon as the first is up instead of waiting for a later event, which could stall pairing until the watchdog retried (upstream 83b4df9); a faster interlaced page scan while no controller is connected makes a known controller reconnect quicker after PS (upstream ade9ea1; back to the normal scan once connected, so it doesn't take radio time from the audio).
- Pico W SMPS in PWM mode: no coil whine (upstream 23a55da, #207); a short delay before the boot-time USB disconnect (upstream 4f9f485).

## [0.8.0-oled-edition] — 2026-10-10

### Changed

- **Configurator: only `0xF6` runs commands.** A SET to any other configurator id (e.g. `0xF2`, `0xF4`, which are also real DualSense feature reports) no longer runs `0xF6` functions such as wipe, BOOTSEL or factory reset. While a command is pending, every other one answers `BUSY`. The `0xF2`–`0xF4` debug reports are on the HID path only in serial (debug) builds; release builds pass `0xF2`/`0xF4` through to the controller like a real DualSense.
- **The controller's serial number is not kept.** The cached reply that carries it is dropped as soon as the colorway is read, and the read only happens in builds with the configurator interface.
- A config write or factory reset from the app no longer changes the active pairing slot (factory reset used to switch to slot 0 at the next boot).

- **Releases now ship the configurator app with the firmware.** Each `vX.Y.Z-oled-edition` release has the app for Linux, Windows and macOS (with this firmware built in) plus the UF2. Release builds turn on the configurator USB interface. Built by `.github/workflows/release.yml` from one tag pushed by `tools/release.sh`; other releases (Proton builds) are no longer touched by it. The update feed (`latest.json`) is signed (`tools/sign_manifest.mjs`, the app refuses an unsigned one), every release file carries a GitHub build-provenance attestation, and a manual dry run of the release workflow builds everything without publishing. The app is tested on Linux; its Windows and macOS builds are included but untested.
- The app's update feed (`latest.json`) now lives in this repo (`tools/build_manifest.mjs`).

### Added

- **Narrator on the dongle.** Short voice lines on the controller's speaker, in 7 languages: "Connected" when the controller connects, the new pairing slot when you switch (before it drops), and a battery warning at 10 % (once per connection). They work without the app, which turns them on and sets the language (off by default). Pre-encoded Opus in flash (`src/voice_bank.bin`, ~600 KB); while a line plays it takes the place of the game's speaker audio for a second or two. Configurator protocol 1.3: feature bit `0x80`, `0xF6` functions `0x0B` (narrator, saved on its own) and `0x0C` (say a line), config byte 37 `narrator_voice`.
- **Configurator interface (build flag `ENABLE_COMPANION_VENDOR`, off by default).** A control-only vendor USB interface for the upcoming desktop configurator. Linux's `hid_playstation` leaves it alone, and Windows binds WinUSB to it with no driver install. The gamepad descriptor stays byte-identical to a real DualSense.
- **Speaker boost up to +12 dB.** The speaker level (app, OLED Settings) now goes above 0 dB, as digital gain on the game audio before encoding; anything past full scale is clipped cleanly. +12 dB was the loudest clean setting on a mastered music test with the desktop volume at 85 %. The UAC volume reported to the PC stays capped at 0 dB.
- **Controller model, colorway and player indicator (protocol 1.2).** The identity report adds which controller is connected (DualSense or Edge), its colorway code (read once per connection from the controller's factory serial with Sony's read-only test command; only the two colorway characters are kept) and the player-indicator LEDs the PC set.
- **Live input report (protocol 1.2).** `0xF0` on the configurator interface returns the controller's current input (buttons, sticks, triggers, touchpad, motion, battery) for the desktop app's live controller view.
- **Identity report 1.1.** `0xF1` also carries charging state, the controller's current lightbar color and battery level, for the desktop app's tray icon.
- **Configurator commands.** An identity report (`0xF1`) with protocol version and last-command result; switch/forget/wipe pairing slots, reboot, reboot into BOOTSEL, factory reset and trigger preset preview over `0xF6`. Saving now reports success or failure.

### Fixed

- The OLED's speaker level steps on a 5 dB grid again, so it can come back down from +12 dB to 0 dB.
- The identity report no longer shows the previous controller's model, battery or input for the first moments after a different controller connects.
- An empty USB output transfer is ignored instead of re-sending stale data to the controller.

- **Host config changes now show on the dongle without a reboot.** Lightbar mode and favourites, and OLED brightness, were only read at boot; a dongle-side Settings save could also write its stale snapshot over changes made from the PC.

---

## [0.7.0-oled-edition] — 2026-10-10

> **After updating, re-pair your controller(s)** (hold Create + PS). The config, pairing-slot and remap flash sectors moved (see Fixed); saved settings are migrated automatically, but pairing slots and button remaps start fresh.

Headline: **the controller speaker is clean over the dongle.** The residual crackle left after the 0.6.x RX-buffer fix traced to core1 missing its Opus-encode deadline: libopus ran from flash, and core0's BT/USB/OLED code kept evicting the shared XIP cache, so ~7 % of 10.7 ms speaker chunks were dropped (measured with the new `Drop` diagnostic row). This release ports upstream awalol's audio-path work (RAM-resident audio, packed `0x39` audio reports, core1 mic decode) and keeps the OLED Edition's own additions on top. A/B'd by ear against upstream's current build on the same controller.

### Fixed

- **Speaker crackle.** Core1's audio path now runs from RAM: libopus `.text`/`.rodata`, the two per-frame `WDL_Resampler` methods, `core1_entry` and its speaker/mic routines, the inter-core queue ops, and RAM `memcpy`/`memset`/`memmove` (upstream Phases A.1–A.3, B.4, B.5; `cmake/relocate_to_ram.cmake`, `src/ram_mem.c`). ~380 KB of 520 KB RAM in use; clock unchanged at 320 MHz.
- **Speaker no longer pinned at full amp volume.** Every audio frame forced the DS5 amp to `VolumeSpeaker=0x64` regardless of the host slider. The host's UAC1 speaker volume now drives the amp (0 dB → 0x64 … −100 dB → 0), as upstream (`aa5e012`).
- **Mic stutter with the mic on (#13).** Mic Opus decode moved from core0's USB/BT pump to core1. Mic PCM now goes to USB in 1 ms slices as the IN FIFO frees up, following the host clock — the old 1920-byte frame writes never fit the ~784-byte FIFO and were silently truncated. The mic only streams while the host has the mic interface open (upstream `370032e`, `8d25a38`). Packet-loss concealment (Opus PLC) is kept and now runs on core1.
- **Constant trigger buzz (#11).** Host output reports are forwarded to the controller as-is, with the host's own allow bits, instead of a latched copy of trigger state re-sent at the audio rate.
- **Saving settings during audio could crash the dongle.** Config, remap and slot flash writes now park core1 via `flash_safe_execute()` (upstream `6a52b5e`); BTstack's link-key storage gets the same protection. Required now that core1's audio path runs from RAM: without it, core1 died on the first flash write after pairing.
- **Flash sectors overlapped Bluetooth's link-key storage.** On RP2350 BTstack keeps pairing keys in the two sectors below the last one, exactly where the pairing slots and button remap lived, so each side overwrote the other (flaky pairing, BTstack rewriting its storage at every boot). Config, slots and remap now sit below BTstack's bank (`src/flash_layout.h`), as upstream did for its config (#244). Saved config is migrated from the old location once. Flash writes made before core1 starts no longer get silently refused.
- **Lightbar stayed on the player indicator after connecting.** The controller ignores the startup state sent while it is still answering the dongle's feature requests; the state is now sent again once the host has enumerated the dongle.

### Changed

- **Audio goes out as upstream's packed `0x39` report** — two haptic + two speaker frames per BT packet, half the packet rate of one `0x36` per frame, with no controller state inside (upstream `8eb8612`). Controller state reaches the DS5 as `0x31` output reports (every host report, forwarded immediately), and volume / mic-stream changes go out as one-off reports. The OLED lightbar now pushes its color during audio too.
- **Audio Auto Haptics defaults to Off.** In Fallback mode the actuators followed any speaker audio when a game sent no haptics, so music and desktop sound made the controller vibrate constantly. Haptics now come only from the game; Fallback / Mix / Replace stay available in the OLED Settings menu. Existing saved settings are kept — set `AutoHap` to Off on the OLED if yours is on Fallback.
- Haptics gain is applied after the 48 kHz → 3 kHz resampler, as upstream (`a55fd46`).
- **Haptics gain now goes up to 6.0** (was 2.0), in 0.5 steps on the OLED `Gain` item, for when HD haptics feel weaker than wired.

### Known issues

- Saving a setting while audio is playing causes one short pop: core1 is paused for the flash erase and one 10 ms audio frame is lost. (Before this release, saving during audio could crash the dongle.)
- After the controller stops charging, the lightbar keeps the last amber frame of the charging pulse until a lightbar mode is chosen or the game sets a color.

### Added

- **Audio pipeline diagnostics over USB:** new `0xF5` feature report (USB-in / BT-out rates, drops, core1 liveness, queue counts, mic frames, flash writes / failures, last host feature report), read with `scripts/audio_diag.py` (one snapshot, or `scripts/audio_diag.py 5` for rates over 5 s). The Diagnostics screen gains matching `c1` and `raw/hap` rows.
- **`Drop r/e/h` row on the OLED Diagnostics screen** — audio chunks dropped because core1 fell behind (`r`), because Bluetooth fell behind (`e`), or while a haptic frame waited for its speaker pair (`h`). Replaces the `trig fold` row, which no longer applies.

---

## [0.6.12-oled-edition] — 2026-06-07

> **Supersedes the withdrawn v0.6.11.** v0.6.11 bundled the two OLED quality-of-life features below with two regressions (constant haptics, issue #11; an audio retiming change, issue #12) and is not recommended. v0.6.12 is built on the verified-working native-trigger firmware and folds in **only** the two separable good features — `CtrlWake` and brightness persistence — leaving the trigger and audio code paths byte-identical to the tested build. The v0.6.11 regression changes (the `state_mgr.cpp` trigger-FFB allow-bit mirror and the `audio.cpp` resampler retiming) are intentionally **not** carried over.

Headline: **native DualSense adaptive triggers now fire in real PC games on Linux/Proton — through the dongle, 1:1 with a wired controller.** For weeks the triggers only ever worked in the on-dongle OLED self-test, never in a game: the dongle was *recognised*, but games fell back to a generic/Xbox pad and never sent trigger effects. The root cause was three ways the dongle's USB presentation differed from genuine Sony hardware — each one accepted by Linux's `hid_playstation` (which only checks size + CRC) but **rejected by a game's native DualSense detection, which validates the actual content**. Closing all three makes the dongle byte-for-byte indistinguishable from a real DS5 to the game.

**Host-side recipe (required alongside this firmware):** run the game on a Proton that carries the Wine `winebus.sys` **#9034** fix (Wine 11 / current Proton-GE — the bug suppresses the SDL gamepad device when a hidraw device exists for the same VID/PID, so the pad shows up everywhere *except* in-game) and **disable Steam Input** (native path). **No launch option is needed** — Wine 11 enables the hidraw native path by default. Works on **both Steam and Heroic** (on Heroic, fully quit Steam so it can't grab the pad, and keep any global `PROTON_PREFER_SDL` off). The game must natively support DualSense — XInput-only titles give rumble but no adaptive triggers. With a real DS5 this "just works"; the firmware changes below make the dongle match.

### Added

- **Native adaptive triggers & haptics in games on Linux/Proton, through the dongle.** Games with native DualSense support (Cyberpunk 2077, etc.) now drive the controller's adaptive triggers via the dongle, identical to a directly-wired DualSense — verified 1:1 by A/B against the same controller plugged in over USB-C. No Steam Input, no per-game hacks beyond the host recipe above. The firmware already proxies the host's trigger output reports to the controller (unchanged); the missing piece was getting games to *recognise* the dongle as a genuine DS5 in the first place (below).
- **`CtrlWake` setting — let the OLED sleep while you play.** New Settings toggle (default **on**, preserving the old behaviour). With it **off**, controller input no longer keeps the panel awake — only the OLED's own KEY0/KEY1 do — so the auto-dim / auto-off timers actually count down during gameplay and the screen can sleep while the controller is in active use. (folded from v0.6.11; issues #8/#9)
- **OLED brightness now persists across a power cycle.** The KEY1-long-press brightness choice is saved to config and restored on boot, instead of resetting to full every power-on. (folded from v0.6.11; issue #9)

### Changed

- **The DualSense HID report descriptor is now byte-identical to a real DS5 (289 bytes, was 321).** The OLED Edition declared four extra feature reports — `0xF6`–`0xF9`, the WebHID config/remap IDs — inside the gamepad collection, making the descriptor 32 bytes longer than genuine hardware. Games' native DualSense parser rejects that mismatch and falls back to a generic pad (no triggers); `hid_playstation` didn't care, which is why the OLED self-test always worked but games never did. The four declarations are removed (plus the runtime `wDescriptorLength` patch that overrode the static array, `0x41`→`0x21`). The firmware **still handles** `0xF6`–`0xF9`; they're simply undeclared now and work over Linux `hidraw` exactly like the existing `0xFD` diagnostic report.
- **Feature reports `0x09` (pairing), `0x20` (firmware) and `0x05` (calibration) now return the controller's real cached data, not zeros.** The earlier native-recognition fix answered these with zeros + a valid CRC — enough for `hid_playstation` to bind, but games validate the *content*: Cyberpunk GET-read `0x20`/`0x09` ~156× in a retry storm rejecting the zeros, never completed the DualSense handshake, and showed no controller. The dongle already fetches and caches the genuine reports from the connected controller over BT (`init_feature()`); `tud_hid_get_report_cb` now serves that cache (real firmware string / pairing / calibration), keeping the CRC-valid synthetic stub only as a fallback for the USB-enumeration probe before the BT link is up (so `hid_playstation` still binds then). Builds on the `hid_playstation` binding fix and the serial-=-MAC identity fix.

### Known tradeoff

- **The browser (WebHID) Config and Remap tabs no longer reach the dongle.** They drove config over reports `0xF6`/`0xF7`, which are no longer *declared* in the HID descriptor — and WebHID refuses undeclared report IDs (declaring them is exactly what broke game triggers). Mitigations, no firmware change needed: **on-dongle OLED config still works**, and because undeclared IDs are fine over Linux `hidraw`, a small `hidraw` CLI/script can read & write all of `0xF6`–`0xF9` (the same mechanism `scripts/mic_diag.sh` uses for `0xFD`). Restoring the *browser* tool would require moving the custom reports into a separate vendor HID collection — which risks the descriptor diverging from a real DS5 again, re-breaking triggers, so it's deliberately not done here.

---

## [0.6.10-oled-edition] — 2026-05-25

Headline: the **Gyro Tilt screen** is now actually usable — it applies the controller's per-unit factory **IMU calibration**, the dot **centres when the controller lies flat**, and tilt **tracks the direction** you move it. Also adds an **L3 / R3 stick-click indicator** on the Status screen and makes the **charge-ETA** robust so it no longer over-reports off a single slow charge step. Everything here is on-dongle display only — **what games receive is unchanged** (the full gyro/accel stream is still forwarded byte-for-byte). UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.10-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **L3 / R3 click indicator on the OLED Status screen.** Clicking a stick in now flashes its analog-stick box inverse (white box, black dot) for as long as it's held — previously the stick clicks had no on-screen feedback. Mirrored in the web config tool's OLED Preview.

### Changed

- **Gyro Tilt screen reworked: per-unit IMU calibration, a tilt dot that centres when flat, and intuitive direction.** Three things, all display-only — the host input report (all gyro + accel axes) is still forwarded byte-for-byte, so in-game motion is unaffected:
  - **Calibration.** The DualSense ships factory gyro/accel bias + sensitivity in feature report `0x05` (which the dongle already fetches and caches at connect). Previously the tilt visuals used raw accel counts; now the cached `0x05` is parsed once per connection (re-read per controller, so it stays correct across the 4 pairing slots) and applied as `(raw − bias) × sensitivity`, keeping the same ±8192 ≈ 1 g scale. A bad/short read is rejected (same sanity gate SDL uses) and it falls back to raw — no regression when calibration is unavailable. The tilt→RGB lightbar mode uses the corrected accel too. Parse/apply mirror SDL's `SDL_hidapi_ps5.c` (zlib-licensed; credit).
  - **Centred when flat.** The dot is now driven by the X (roll) and **Z** (pitch) axes — the two that read ~0 when the controller lies flat — instead of X/Y. Gravity rests on Y when flat, so the old Y mapping pegged the dot to the bottom edge at rest; it now sits centred.
  - **Direction follows the controller.** Both axes are negated so tilting left moves the dot left and tilting forward moves it up, instead of mirrored.
  - The dot centring + direction are mirrored in the web config tool's OLED Preview (its mock IMU now also rests gravity on Y to match real hardware).

### Fixed

- **Charge-ETA no longer balloons off a single slow 10% step.** The Status-screen `~Nm` charge estimate timed each 10% battery step and projected the rest, but one anomalously slow step (e.g. ~47 min — observed reading `~222m` at 70% on the dock) used to drag the whole projection up because the rate was a mean over only 3 steps. Each timed step's bulk-equivalent is now clamped to a 30-min ceiling and the rate is taken as the **median** over the last 5 steps, so a single under-load/anomalous reading can't dominate. Mirrored in the web OLED Preview emulator.

---

## [0.6.9-oled-edition] — 2026-05-24

Headline feature: **on-dongle button remapping** — reassign any of the 16 digital controls, stored on the dongle so it works in every game and on every OS with no host-side software, edited visually in the web config tool's new **Remap** tab (a click-the-controller diagram built on Zacksly's CC BY 3.0 DualSense art). Also ships **packet-loss concealment** for the BT microphone. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.9-oled-edition) (built by `.github/workflows/release.yml`); this is the first release without the debug UF2 as a download.

### Added

- **Button remapping.** Any of the 16 digital controls (face buttons, D-pad, shoulders/triggers, stick clicks, Create/Options) can be remapped to any other — stored on the dongle, applied transparently before the host sees the report, so it works on every OS and every game with no host-side software. The remap table lives in its own dedicated flash sector (`PICO_FLASH_SIZE_BYTES - 3·FLASH_SECTOR_SIZE`, magic `DS5\x03`, below the slots sector) and survives reboot; identity (no remap) is the default. Multiple sources mapping to one target OR together (analog L2/R2 take the max); a source can be set to *disabled* (`0xFF`). The remap acts on the **outgoing host report copy only** — the raw input the OLED screens and the PS+Mute reboot combo read is untouched. Edited over the existing `0xF6`/`0xF7` vendor reports with a hardened `RM`+version frame (**no HID-descriptor change**, so Windows enumeration is unaffected) and a revision counter the host polls to confirm a write landed. New `src/remap.{h,cpp}`; apply logic + button set ported from [SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge) (credit). Dev helper `scripts/remap_test.py` exercises the path over `/dev/hidraw` without the web tool.

### Changed

- **BT microphone now has packet-loss concealment (PLC).** The mic decode path gained a small decoded-frame jitter buffer (8 frames) drained at a steady 10 ms playout cadence: bursty BT delivery is smoothed, and a dropped mic frame during an active session is concealed with an Opus PLC frame (`opus_decode(decoder, NULL, 0, …)`) instead of leaving a hole the host hears as a click/dropout. Playout pre-buffers 3 frames and stops after 300 ms of no real frames (so it never emits comfort noise when the mic is idle). A new **`Mic PLC:`** counter on the Diagnostics screen climbs only when concealment fires — effectively a live BT link-quality gauge. Verified: forced BT loss kept the captured audio gap-free (longest zero-run ~0 ms) while the counter climbed. Design ported from [SundayMoments/DS5_Bridge](https://github.com/SundayMoments/DS5_Bridge) (credit). Adds ~30 ms mic latency (the pre-buffer).

### Companion web tool

- **Visual button-remapping editor (new Remap tab).** `DS5Dongle-OLED-Config-Web` gains a dedicated **Remap** tab built on the new firmware remap protocol: click a button on a live, theme-aware DualSense diagram to reassign it, with the shoulders/triggers (L1/L2/R1/R2) floated to the corners as labeled glyphs + leader lines (they have no target in a front view). Remapped/selected buttons glow; a collapsible full dropdown list is kept as a fallback. Reads the remap block appended to the `0xF7` config response and writes over `0xF6` (func `0x10`), independent of `Config_body`. Controller outline + button glyphs are [Zacksly's "PS5 Button Icons and Controls"](https://zacksly.itch.io/ps5-button-icons-and-controls) (CC BY 3.0, recolored to `currentColor` and cropped), credited in the footer, each asset file, and a bundled license. Strings translated across all 7 locales.

---

## [0.6.8-oled-edition] — 2026-05-24

Two big items: **DualSense microphone over Bluetooth** (long believed impossible — turned out to be a single enable bit; credit [awalol](https://github.com/awalol/DS5Dongle) upstream) and a **USB 3.0 connection-interference watchdog**. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.8-oled-edition) (built by `.github/workflows/release.yml`). The companion `DS5Dongle-OLED-Config-Web` config tool gains a **BT microphone** toggle.

### Added

- **DualSense microphone over Bluetooth.** The controller's built-in mic now works over the dongle's BT pairing — decoded from the DS5's Opus stream and presented to the host as the standard DualSense USB capture device, usable by any app (Discord, OBS, in-game voice). This fork had previously documented BT mic as a hard Sony-firmware limitation (likely encrypted); that conclusion was **wrong** — it hinged on a single enable bit (`pkt[4]` bit 0 in the outbound `0x36` audio report). Credit to **[awalol](https://github.com/awalol/DS5Dongle)** (upstream) for identifying it. The DS5 streams mic as 71-byte Opus packets tagged in `0x31` reports (`(data[2]>>1)&1`); `src/audio.cpp` decodes mono→stereo to the UAC1 endpoint. **Always-on:** the enable is sticky once streaming, so a control-only `0x36` keep-alive asserts it at ~4 Hz only until frames arrive, then backs off — mic works with no game audio, at minimal BT traffic. **Toggle:** new `bt_mic_enable` config field (default on; off saves DS5 battery since always-on keeps its audio subsystem awake) — OLED **Settings → BT Mic** and the web config tool's **BT microphone** switch. `BLUETOOTH_AUDIO_NOTES.md` rewritten from "dead end" to the working mechanism; README gains a user-facing **"DualSense Microphone over Bluetooth"** section.

### Fixed

- **Connection no longer hangs permanently on the amber lightbar (USB 3.0 interference recovery).** Users reported the DualSense getting stuck mid-connect (solid amber/yellow, never enumerates) on USB 3.0 host ports while USB 2.0 worked — caused by USB 3.0's broadband ~2.4 GHz RF noise desensitizing the CYW43 Bluetooth radio. The firmware's connection flow had dead-end states (ACL-fail and auth-fail re-inquiry were commented out; the controller-type feature-packet wait had no timeout), so a single lost packet stalled forever until a replug. Added a **connection-attempt watchdog** (`src/bt.cpp`): a 10 s timeout armed when a connection commits to a device and cleared when it reaches USB enumeration; on expiry it tears down via the existing `HCI_EVENT_DISCONNECTION_COMPLETE` path and restarts inquiry, so a stalled connect auto-retries instead of hanging. Re-enabled the ACL-fail / create-connection-reject / auth-fail recovery paths for faster recovery when the controller *does* report a failure. The watchdog is inert during a healthy established session (no effect on normal play, slot-switching, or idle-disconnect). Helps any marginal-RF setup, not just USB 3.0.

### Documentation

- New README section **"USB 3.0 ports & Bluetooth interference"** + a Known Issues bullet: explains the 2.4 GHz RFI cause (referencing Intel's white paper) and lists mitigations (USB 2.0 port, short USB 2.0 extension cable, powered USB 2.0 hub, ferrite bead, distance/line-of-sight).

---

## [0.6.7-oled-edition] — 2026-05-23

UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.7-oled-edition) (built by `.github/workflows/release.yml`).

### Changed

- **Charge ETA now shows a provisional estimate immediately on plug-in.** Instead of sitting on `~--m` for the ~15-20 min until the first 10% step is timed, the Status screen shows a default-rate estimate `~Nm?` (the trailing `?` marks it provisional) the moment charging starts. The `?` drops and the number switches to the measured rate once a clean 10% step completes. Default is ~15 min per 10% step (`kDefaultStepUs`), taper-weighted exactly like the measured path, so the provisional figure is in the right ballpark and self-corrects.

### Companion web tool

- `DS5Dongle-OLED-Config-Web` gains **lightbar controls** (mode dropdown + four favorite-color pickers) in the config view, the provisional charge-ETA token in the OLED preview to match this firmware, and translations for two preview notes that were English-only. Build housekeeping: `tsconfig.tsbuildinfo` is no longer tracked.

---

## [0.6.6-oled-edition] — 2026-05-23

Community-issue follow-ups: configurable OLED idle-ladder thresholds (#5) and a diagnostic counter clarifying the trigger-flow numbers (#6). UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.6-oled-edition) (built by `.github/workflows/release.yml`). The companion `DS5Dongle-OLED-Config-Web` config tool gains matching screen-timeout controls and is synced to the v0.6.5+ `Config_body` layout (fixes a latent issue where saving via the old web tool would zero the lightbar fields).

### Added

- **Configurable OLED idle-ladder thresholds (issue #5, requested by @TerryFrench).** The dim and off tiers are no longer hardcoded at 2 / 15 min — two new `Config_body` fields `screen_dim_timeout` / `screen_off_timeout` (minutes, `0 = that tier disabled`, range `[0,250]`) are editable on the Settings screen (`ScrDim`/`ScrOff`) and persist to flash. Defaults preserve the previous 2 / 15 ladder; on upgrade the unset fields read as those defaults via the `config_valid()` clamp. The idle timer moved from `time_us_32()` to 64-bit µs so the full 250-min range is representable without the ~71-min wrap. Power users with always-on dongles can bias shorter; status-watchers can bias longer or set `0` to keep a tier lit.
- **`trig fold` counter on the Diagnostics screen (issue #6).** Counts trigger-bearing `0x02` host reports that arrived while the speaker stream was active and were therefore folded into the `0x36` audio frames (via `state[]`) instead of sent as a standalone `0x31`. Makes `trig_allow == to_bt(trig) + fold` visible, confirming the apparent `trig`/`tx` gap is audio-path folding, not dropped trigger reports.

---

## [0.6.5-oled-edition] — 2026-05-23

Charging UX (Status-screen battery ETA + amber lightbar pulse), persistent and screen-sticky lightbar control, and a charging-aware idle power ladder. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.5-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **Charge ETA on the OLED Status screen.** While the DualSense is charging, the battery line shows an estimated time-to-full (`~43m`) to the right of the battery icon. The DS5 only reports battery in 10 % steps over BT (`interrupt_in_data[52]` low nibble, 0–10; high nibble is power-state, 1 = charging), so a smooth countdown is impossible — instead `sample_charge_eta()` times how long each 10 % step takes and extrapolates the remaining steps. It shows `~--m` while calibrating (the first estimate can't appear until one full step has been timed, ~15–20 min after plug-in), then refines on each subsequent notch. The partial step in progress at plug-in is discarded so the first estimate isn't skewed by a half-measured step; a 3-entry moving average smooths the rest. **Li-ion taper correction:** a flat "time-per-step × steps-left" runs optimistic in the constant-voltage tail, so each measured step is normalised to a bulk-equivalent duration (divide out a per-step weight: 1.0× in the bulk region, 1.5× for 80→90 %, 2.2× for 90→100 %) and the remaining steps are re-weighted — keeping the estimate consistent whether the user plugs in near-empty or near-full. Sampled once per frame from `oled_loop` ahead of the idle power-ladder early-returns, so step timing stays correct even while the panel is dimmed/off or the user is on another screen.
- **Lightbar settings persist across reboot and stick across every screen.** The selected lightbar mode and the four favorite colors are now saved to the config flash sector (new `Config_body` fields `lightbar_mode` + `lb_fav_{r,g,b}[4]`), so a chosen mode/color survives a power cycle. A new **HOST** mode (the default) hands the LED back to the host/game so the dongle doesn't hijack player-indicator LEDs out of the box; on upgrade from ≤0.6.4 the unset field reads as HOST, preserving prior behavior. Mode/favorite edits made on the Lightbar screen are batched into a single flash write when you navigate away (tracked by a dirty flag) to spare flash endurance.
- **Lightbar pulses amber-orange while charging.** A slow ~4.6 s breathing pulse (base `(255,100,0)`, sine-enveloped from dim to bright via the existing 32-step LUT) shows charging at a glance from any screen. Implemented in the unified `lightbar_service()` (below) as the top-priority owner of the LED, so it overrides the selected mode while charging and reverts to it when unplugged.

### Changed

- **The OLED no longer fully sleeps while the controller is charging.** The idle power ladder is capped at the Dim tier (the low-power breathing dot) instead of advancing to full Off (`cmd(0xAE)`) when `g_charge_eta.charging` is true. The charge-ETA tracker already runs while the panel is off, but users were unplugging the controller to "wake" the dongle — which reset the ETA calibration and restarted the wait-for-the-next-10%-notch. Capping at the dot tier (which draws ~no current) removes the reason to unplug. Normal Active→Dim→Off behavior resumes once charging stops.
- **A single `lightbar_service()` now owns the controller LED, every frame, on every screen.** Previously the OLED only drove the lightbar via a transient `0x31` packet sent from inside `render_screen_lightbar()` — so the color was only asserted while that screen was open. The service (run from `oled_loop` ahead of the power-ladder early-returns) instead writes the chosen color into the persistent `state[]` block (`SetStateData` `LedRed/Green/Blue`, via new `state_set_led()`), so it rides every outbound host/audio packet, and also actively pushes a `0x31` when audio is idle so animations keep moving. A new `g_lightbar_override` flag gates `state_update()` so the host's `AllowLedColor` writes can't stomp a firmware-chosen mode. During audio the active `0x31` push is suppressed — the `0x36` frames already carry `state[]`'s LED, and slipping a `0x31` between them would intrude on the load-bearing audio path.

### Fixed

- **OLED idle dim/dot tier now actually engages while a controller is connected.** The activity detector hashed `interrupt_in_data[0..9]` with an exact compare, but the analog sticks jitter by ±1 LSB at rest, so the hash changed every few frames and reset the idle timer — meaning the breathing-dot/dim tier only ever kicked in when no controller was paired. Now it mirrors `bt.cpp`'s inactivity heuristic: the stick bytes' rest band `[120,140]` is collapsed to a constant and the volatile counter byte (`idata[6]`) is skipped, so a resting controller reads as idle. Confirmed against a live `/dev/hidraw` capture (only the left-stick X byte was flickering 129↔128).
- **Lightbar no longer reverts the instant you leave the Lightbar screen.** Root cause: the OLED's `send_lightbar_color()` wrote a one-off `0x31` packet and never touched the persistent `state[]` block, while the host's `0x02` output reports, every audio frame, and reconnect all re-stamp `state[]` (incl. the LED) into the controller. Off the Lightbar screen the OLED stopped pushing, so the next `state[]`-based packet overwrote the color — which is why saved favorites and animated modes (Rainbow/Breathing/Fade) never "stuck." Now that the lightbar is owned through `state[]` with a host override gate (see Changed), the selected mode holds across screens and through active gameplay/audio.

---

## [0.6.4-oled-edition] — 2026-05-19

Trigger-flow diagnostics (in response to issue #3) + the OLED idle power ladder. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.4-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **OLED idle power ladder.** Replaces the single-tier 5-min auto-dim with a three-stage state machine: at 2 min idle the panel wipes black and a 2×2 "breathing dot" (1 s on / 1 s off) walks through 8 evenly-spaced positions every 30 s; at 15 min idle the SH1107 is sent `cmd(0xAE)` (display off) entirely. Wakes instantly on KEY0/KEY1, controller pair (BT-connect rising edge), or any input-report change. Why this shape: on the Waveshare panel, bench-testing `kDimContrast = 0x10` and `0x02` both produced only ~10 % perceptual reduction (SH1107's contrast register vs apparent brightness is heavily non-linear on this hardware), so the only reliable per-pixel dim available is *rendering fewer pixels*. The breathing dot lights ~4 of 8 192 pixels half the time — roughly a 1 000× drop in cumulative current — while still indicating "the dongle is alive," and the rotating position spreads OLED wear across the panel.
- **Trigger-flow diagnostic counters on the Diagnostics screen.** `host02` (total `0x02` HID OUT reports from host) / `trig` (those where the host set `AllowRight|LeftTriggerFFB` in `valid_flag0`) / `tx` (forwarded as BT `0x31` sub-`0x10`). Added in response to issue #3 ("trigger tension missing in Death Stranding 2"). Lets the user triage in one game session whether the dongle, the host driver, or the controller is the source of the missing adaptive-trigger effect — without a UART or BT sniffer.
- **Diagnostics screen now scrolls with the controller D-pad.** Refactored to a row-list (10 rows currently: Uptime / BT state / host02 / trig+tx / BT31 in/s / USB aud/s / BT32 out/s / Mic in/s / Mic dec=&w= / Mic prefix). 5 rows visible at a time; ▲/▼ glyphs at the right edge mark "more above/below." Read-only — no cursor, unlike Settings, since there's nothing to select.
- **Host-side trigger-flow triage via `scripts/mic_diag.sh bt-trace`.** The firmware's `0xFD` vendor feature report grew a second section (bytes 32–43) with the trigger counters; `bt-trace`'s Python decoder now reads them and prints a one-line verdict — "host driver isn't setting Allow*TriggerFFB" / "trigger Allow bits set but speaker path stole the BT pipe" / "full chain reached the controller". Lets the user diagnose issue #3 without a UART cable or OLED-relay-per-flash.
- **`README.md` "Diagnostics & debug tooling" section** documents `scripts/mic_diag.sh` and its subcommands. The script existed but was only mentioned inside `BLUETOOTH_AUDIO_NOTES.md` — invisible to anyone who hadn't already read the parked-mic notes.

### Changed

- **`flush_fb()` split.** Internal refactor: `flush_fb_raw()` writes just the framebuffer; `flush_fb()` is now `draw_button_chrome() + flush_fb_raw()`. Lets the dim-tier renderer push the breathing dot without the K0/K1 chrome arrows (no navigation target while the panel is asleep).
- **Diagnostics row order re-prioritized.** The first 5 rows (always visible without scrolling) cover the most common triage path: Uptime / BT state / `host02` / `trig`+`tx` / `BT31 in/s`. Audio + parked-mic-investigation counters live below the fold.

---

## [0.6.3-oled-edition] — 2026-05-18

Small follow-up to v0.6.2. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.3-oled-edition) (built by `.github/workflows/release.yml`).

### Fixed

- **OLED Status header was stuck on `"DS5 Bridge v0.6.0"`.** The string was hardcoded in `src/oled.cpp` and never got bumped per release, so v0.6.1 and v0.6.2 both shipped with stale text on the Status screen. Now driven by a compile-time `FIRMWARE_VERSION` macro set from `CMakeLists.txt`'s `${VERSION}` (which `release.yml` already passes as `-DVERSION="$FIRMWARE_VERSION"`). Single source of truth: the release tag. Local builds without `-DVERSION` show `"dev"` so an untagged build is obvious at a glance.
- **Web preview's Status header had the same bug.** `src/oled/screens.ts` hardcoded `"v0.5.4"`. Now reads `firmware-latest.json` (already CI-bundled from the GitHub API) at runtime in `OledEmulator.tsx` and writes the short tag (suffix `-oled-edition` stripped) into `state.firmwareVersionLabel`, which `renderStatus()` consumes.

### Documentation

- New `CLAUDE.md` "Versioning — single source of truth" section documents the release ritual (CHANGELOG bump → tag → push → `gh release create`) and the single-source-of-truth flow from tag → CMake → C++ macro → web `firmware-latest.json`. Includes a note about the still-pending `WEB_REPO_DISPATCH_PAT` secret on the firmware repo.

---

## [0.6.2-oled-edition] — 2026-05-18

OLED button-model + visual chrome refactor on top of v0.6.1. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.2-oled-edition) (built by `.github/workflows/release.yml`).

### Changed (button model)

- **KEY0 / KEY1 are now strictly navigation on every screen.** KEY0 short-press = next screen, KEY1 short-press = previous screen. KEY1 long-press still cycles OLED brightness (unchanged). The old contextual K1=cycle behavior on Trigger Test (cycle trigger preset) and Lightbar (cycle lightbar mode) moved to **DualSense controller buttons** — Triangle on Trigger Test, R1 on Lightbar. Source of "the Mode label didn't change when I clicked K1" confusion eliminated.
- **KEY0 double-click reboot → KEY0 + KEY1 simultaneous hold (≥ 1 s).** Rapid forward-navigation kept tripping the double-click timer by accident, soft-rebooting the dongle mid-session. The new two-button chord can't be fat-fingered. `kDoubleClickUs` + `key0_pending_single` state removed; new `chord_held_since_us` + `kChordHoldUs = 1 s`. DS5 PS+Mute hold-2 s remains the headless backup.
- **Per-screen contextual actions on the controller** mirror the existing Slots / Settings conventions (where Triangle has always meant "commit / switch / save"):
  - **Trigger Test** — △ rising edge cycles `trigger_preset` and re-applies via `send_trigger_effect()`.
  - **Lightbar** — R1 rising edge cycles `lb_mode`. (Triangle stays as "save current RGB to favorite slot 0" — the existing favorite-save UX.)

### Changed (visual chrome)

- **Arrow chrome on the left edge of every screen.** `flush_fb()` now paints `>` at `(0, 8)` and `<` at `(0, 49)` so the on-screen labels physically pair with the KEY0 (top) and KEY1 (bottom) buttons. The horizontal `"K0=next K1=back"` footer at y=56 is removed from all 11 screens. Trigger Test footer = `"Tri=cycle"`; Lightbar footer = `"R1=mode"`; Slots and Settings keep their existing contextual hints (`"Tri=switch Sq hold=wipe"`, `"DP nav/adj Tri=save"`). New `kContentX = 6` shifts every screen's content right by 6 px to clear the chrome strip; rectangles, sticks, and the L1/L2 column on Status all repositioned to avoid the chrome `<` glyph painting inside the live left-stick area.

### Added (web preview parity)

- **`src/protocol/ds5BridgeHid.ts` `sendTriggerPreset(preset)`** — builds the DS5 SetStateData payload byte-for-byte from `src/oled.cpp send_trigger_effect()` and pushes via `device.sendReport(0x02, ...)`. The dongle relays it over BT to the paired controller, so cycling Trigger Test in the web preview actually drives the real adaptive triggers.
- **Web preview mirrors the firmware refactor.** `key1Action()` collapsed to back-nav-only. New rising-edge handlers in `OledEmulator.tsx` detect Triangle / R1 / D-pad from the live controller's input report and dispatch to the appropriate per-screen action. `drawButtonChrome(fb)` paints the `>` / `<` arrows after every render. `flush()` accepts an optional tint color: Slots / Diagnostics / CPU/Clock render in **orange** (`#f59e0b`) when a controller is connected — Chrome WebHID can't expose those reports on a stock DualSense descriptor, so the orange tint + an explanatory paragraph below the canvas flag the values as mock. KEY0/KEY1 buttons in the UI moved to sit visually next to the rendered Pico-OLED-1.3, mirroring the physical board.
- **Settings cursor on the web** — new `settingsSel` state, `>` cursor mark on the selected row; D-pad up/down on the connected controller moves the cursor (web-preview-only — actual edits + save happen via the dedicated Config tab on the website).
- **Mock-data temperature tweak** — web Preview's CPU/Clock screen no longer drifts 41–47 °C; jitter is now ±0.4 °C around 33.6 °C (realistic Pico 2 W idle).

### Documentation

- **README "Web Config Tool" section** added near the top, linking https://marcelinevpq.github.io/DS5Dongle-OLED-Config-Web/#config and explaining the three tabs (Flash / Config / OLED Preview). Includes a BOOTSEL mode primer for first-time flashers.
- **OLED Display Add-on section rewritten.** Screen count 10 → 11 (CPU/Clock added). Cycle order updated. New "Button reference" table covers the strict K0/K1 nav, K1 long-press brightness, and K0+K1 chord reboot. ASCII mockups dropped in favor of consistent web-preview screenshots under `assets/oled/`.
- **Performance / Overclocking section reworded** to lead with "you don't need to do anything — the overclock is baked into the firmware". The "raise voltage / lower clock if it fails to boot" line is scoped to users compiling from source.

---

## [0.6.1-oled-edition] — 2026-05-18

Tagged release of the v0.6.0-oled-edition follow-up. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.1-oled-edition) (built by `.github/workflows/release.yml`).

### Added

- **CPU / Clock diagnostics screen** (`kScreenCpu`, inserted between Diagnostics and BT Signal in the K0 cycle). Shows the configured system clock (`SYS_CLOCK_KHZ` — the overclock target), the *actually running* `clk_sys` measured live by the RP2350 on-chip frequency counter against the crystal reference, the core voltage read back from the regulator (`vreg_get_voltage()`, not the compile-time constant), and the RP2350 on-die temperature (ADC input 4). Pure read-only instrumentation; one-time ADC bring-up and no other code path uses the ADC, so it is conflict-free. `render_screen_cpu()` is `noinline` like the other render functions (Thumb literal-pool reach). Adds `hardware_adc` to `target_link_libraries`. The frequency-counter measurement (a multi-ms busy-wait) runs **once on screen entry** and is cached — `clk_sys` is fixed at boot, so only the temperature refreshes per frame, avoiding a per-frame BT/audio hitch while the screen is visible. `oled_loop()` gained a generic `screen_entered` flag for this. Hardware-verified on Pico 2 W + OLED. Also exported over a new HID feature report **`0xfc`** (`src/cmd.cpp`) — 11 bytes: set_khz, cached real_khz, vreg code, ADC ch4 raw — so the web config emulator can show live CPU telemetry (volts/temp math done web-side to keep the firmware HID path float-free).

### Fixed (web telemetry — latent since the slots/diag reports landed)

- **CPU/Clock temperature was a single noisy ADC sample.** The RP2350 temp sensor has a shallow slope (−1.721 mV/°C, ~1 LSB ≈ 0.47 °C) so a lone 12-bit reading swings several tenths of a degree per frame — the displayed value just mirrored the latest noisy sample instead of the true die temperature. New `cpu_temp_raw_smoothed()` in `src/cmd.cpp` averages a 256-sample block then runs a slow EMA (α=0.15, seeded on first call). It is the **single source of truth**: both `render_screen_cpu()` and the `0xfc` web telemetry call it, and the duplicated per-site ADC bring-up was removed (ADC now initialised in exactly one place). `oled.cpp` no longer touches the ADC directly (drops `hardware/adc.h`, adds `cmd.h`).
- **Live web telemetry over WebHID: not feasible on the target setup; abandoned.** A browser-side read-only diagnostic proved Chrome WebHID returns `NotAllowedError` for any report ID **not declared** in the parsed HID report descriptor (declared `0xF7`/`0xF8`/`0xF9` read fine; undeclared `0xFA`/`0xFB`/`0xFC` fail). Declaring them is therefore mandatory for the web read — but doing so (even applied atomically with a matching `wDescriptorLength`, correct bytes identical in shape to the working `0xF6`–`0xF9`, and a `bcdDevice` cache-bust bump) made the device fail to enumerate as a usable HID device on the user's real Windows machine in **two** independent attempts (Device Manager showed it; WebHID and the PlayStation Accessories app did not). The cloned DualSense HID report descriptor cannot be safely extended on this environment. Reverted to the original descriptor (`0x0141`/`0x01B5`, no vendor feature reports, `bcdDevice 0x0100`). Retained with **no USB impact**: the `0xfc` firmware handler and the temperature smoothing/`cpu_temp_raw_smoothed()`. The on-device CPU/Clock OLED screen is fully working and hardware-verified; the web preview's CPU screen stays on representative mock values (the slots/diagnostics web screens were never readable for the same root cause and are likewise mock-only when connected).

### Fixed

- **Low-battery LED keeps blinking after controller disconnect** (`fb68ea5`). When the DualSense's battery dropped low enough to trigger `battery_led_tick`'s blink and the controller subsequently disconnected (typically: battery fully depletes and the BT link drops), the Pico's onboard LED stayed frozen in whichever half-cycle it was in at the moment of disconnect; reconnect-retry windows could even briefly resume blinking. New `battery_led_on_disconnect()` clears blink state, forces LED off, and zeros `last_report_us` so the stale-check early-return blocks any new blink until a fresh 0x31 report arrives on the next connection. Stale-check in the tick also now forces LED off when it fires mid-blink (defense in depth for ungraceful disconnects). Reported by Sura Academy on Discord. Same bug present in upstream — sent back as [awalol/DS5Dongle#101](https://github.com/awalol/DS5Dongle/pull/101).

---

## [0.6.0-oled-edition] — 2026-05-17

Tagged release of the rebase below. UF2s attached to [the GitHub release](https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.6.0-oled-edition) (built by `.github/workflows/release.yml`). No code changes vs the rebase; tag exists so users can install from a stable artifact.

---

## [0.6.0-rebase] — 2026-05-17

Rebased onto upstream `awalol/DS5Dongle` `v0.6.0-hotfix`. All OLED Edition features preserved with no user-visible regression.

### Changed

- **Adopted upstream's `state_mgr` refactor** (`awalol/DS5Dongle#93`) as the new base for controller-state and audio-packet construction. The local speaker / HD-haptic regression fix we shipped in `bff65d6` (re-introducing a 63-byte `state_data` block into every audio packet) is **dropped** — upstream's `state_mgr.cpp` is the proper architectural fix and supersedes the hack. Speaker, HD haptics, and basic rumble all verified working on the rebased firmware. Same fix loteran/DS5Dongle shipped as `c7a8d3c` ~6 h before ours.
- All upstream commits between our prior base (`a2e3a33`) and `v0.6.0-hotfix` are now in our tree: `0a33aae` PR#93 merge, `b1019fb` README update, `54a4b69` config struct comment typo, `f882ff1` adjust default state-init volume, `77bbee5` rumble transfer fix, `c2e0d84` state init after connected, `7bbe37b` state_mgr refactor itself, `9a2c2b4` discoverable / connectable off when connected, `508a841` DISABLE_SPEAKER_PROC option, `b308545` audio.cpp comment, `0ed05d3` rumble hotfix.
- Our `update_discoverable()` helper (gates `gap_discoverable_control` on `slots_any_empty()`) replaces upstream's stricter `gap_*_control(false)` pair on L2CAP connect. Effect: dongle stays discoverable while at least one slot is empty (needed for slot-1/2/3 pairing); only goes dark when all 4 are full. Strictly looser than upstream's rule, but correct for the multi-slot use case.

### Fixed

- **`awalol/DS5Dongle#100` "(v0.6.0) Dongle is detected as a new device on Windows when using different USB ports"** — upstream's `e79c762 remove usb serialnumber #32` zeroed the device descriptor's `iSerialNumber` to work around a SpecialK compat issue, but that broke Windows device identity: users lost per-device volume / app settings every time they moved the dongle to a different USB port. The OLED Edition restores `iSerialNumber = STRID_SERIAL` (per-board unique serial from flash chip ID via `board_usb_get_serial`). Trade-off documented in `src/usb_descriptors.cpp`: re-introduces SpecialK incompatibility (`#32`) for the narrower set of Windows users with that specific tool, in exchange for stable device identity for the broader Windows population.

### Verification

End-to-end on user's hardware after the rebase:
- DS5 pairs cleanly.
- Speaker audio via `scripts/test_speaker.sh --tone 440 3` — audible (load-bearing check that upstream's state_mgr does what we expect).
- Basic rumble works in games (validates we didn't disturb upstream's `0ed05d3` hotfix).
- All 10 OLED screens render correctly with live data.
- Multi-slot pairing: switch between slots, slot_assign on empty, wipe-from-Settings all work.
- PS + Mute hold-2s combo reboot triggers `watchdog_reboot` (validates our `interrupt_loop` addition coexists with upstream's `state_update` flow).

### Unchanged

The full prior CHANGELOG history for `[0.5.4]` and earlier sessions remains accurate and below.

## Post-0.5.4 work before the 0.6.0 rebase — 2026-05-16

### Fixed

- **DualSense speaker + HD haptic actuator regression** — upstream commit `3a31bd7` (2026-05-12, "refactor: add SetStateData and audio send priority") moved the `0x10` SetStateData sub-report out of every `0x36` audio packet and into a one-time L2CAP-open setup. The DS5 hardware requires that sub-report to be re-asserted on every audio frame (the `0x7f 0x7f` Headphones+Speaker volume bytes specifically) or the speaker and HD haptic actuators silently stop producing output. Restored in `src/audio.cpp` with the pre-3a31bd7 packet layout (state_data at `pkt[11..75]`, haptic at `pkt[76..141]`, speaker at `pkt[142..343]`). Same fix shipped independently by loteran/DS5Dongle as commit `c7a8d3c` ~6 h before ours; credit to loteran for the clearer hardware-side explanation in their commit message.
- **USB UAC1 SET_CUR Volume request no longer overrides flash-persisted speaker_volume.** PipeWire / PulseAudio re-apply their last-known UAC1 volume on every device reconnect, which had silently overridden whatever the user had saved in the OLED Settings menu. The in-memory `volume[]` array still tracks live host volume; only the flash sync was removed. Fix borrowed from loteran/DS5Dongle commit `03fa1e4`.

### Added

- **Audio Auto Haptics** — derive haptic feedback from the speaker audio (UAC channels 0/1) for games that send sound but no per-frame haptic data, e.g. Ghost of Tsushima on Linux + Steam. DSP is a 1-pole low-pass + envelope follower + modulation + soft-clip (`x / (1 + |x|)`, avoids `tanhf` on Cortex-M33), borrowed from loteran/DS5Dongle commit `5d6bc2f`. Four modes selectable from the OLED Settings menu: **Off** / **Fallback** / **Mix** / **Replace**. Default is **Fallback** — derived rumble fires only after the game has been silent on the native haptic path (channels 2/3) for ~1 s, so games that send native HD haptics (Spider-Man Remastered) are not overridden, while games that don't (Ghost of Tsushima) get derived haptics out of the box. Gain (0–200 %) and LP cutoff (80 / 160 / 250 / 400 Hz) are also tunable from the Settings menu.
- **Audio Diagnostics counters** on the OLED Diagnostics screen: `USB aud N/s` (UAC1 frames per second arriving from the host) and `BT 0x32 N/s` (audio packets emitted to the DS5 per second). Lets the user verify the speaker/haptic path is actually moving bytes without needing a UART cable. Used to triage the speaker regression above.

---

## [0.5.4] — 2026-05-16

First full OLED Edition release. Includes upstream's v0.5.4 base plus the audit pass and the entire OLED add-on feature set.

### Added — OLED display add-on

Requires a Waveshare [Pico-OLED-1.3](https://www.waveshare.com/wiki/Pico-OLED-1.3) (128×64 SH1107). Firmware drives it automatically when present and no-ops gracefully when absent.

- **Boot splash** (1.5 s) showing firmware version on power-on.
- **10 screens**, cycled with KEY0 (forward) / KEY1 (back, except where contextual):
  1. **Status** — connection state, paired BD address, battery % with pixel-icon battery, live stick / D-pad / face-button / L1-R1 / L2-R2 trigger visualization.
  2. **Slots** — persistent 4-slot multi-controller pairing (see below).
  3. **Lightbar Color Picker** — tilt-to-RGB live preview on the controller's lightbar, with 4 user-savable favorite slots (△ ○ ✕ □) and three effect presets (Breathing, Rainbow, Fade).
  4. **Trigger Test** — cycles 7 DS5 adaptive trigger effects (Off / Feedback / Weapon / Vibration / Bow / Galloping / Machine Gun) on both L2 and R2, bitpacked per [dualsensectl](https://github.com/nowrep/dualsensectl)'s reverse-engineering.
  5. **Gyro Tilt** — live X/Y/Z accelerometer values + 40×40 crosshair box that tracks tilt in real time.
  6. **Touchpad** — live render of finger positions on the touchpad surface, with a finger count.
  7. **Diagnostics** — uptime, BT state, HCI / audio-FIFO / opus-FIFO counter stubs (kept for future wiring).
  8. **RSSI** — live BT signal strength of the active link, dBm + bar.
  9. **VU Meters** — live peak meters for the speaker + haptic audio paths.
  10. **Settings** — persistent on-device editor for the 8 firmware config fields, plus "Reset to defaults" (hold △ 2 s) and "Wipe all slots" (hold △ 2 s).
- **OLED brightness control** — KEY1 long-press cycles brightness levels.
- **Auto-dim** after 5 minutes of input idle (lifespan / burn-in protection).
- **Soft-reboot recovery** without unplugging USB:
  - OLED KEY0 **double-click** (~400 ms window) → `watchdog_reboot`.
  - DS5 `PS + Mute` held for 2 seconds → `watchdog_reboot` (works without the OLED).
- **Pixel-art icons** in the Status screen header (link indicator + battery icon).

### Added — 4-slot persistent multi-controller pairing

- Bond up to 4 DualSenses; switch between them from the **Slots** OLED screen.
- Active slot persisted in the existing flash-backed config; dongle reconnects to the last-used controller on boot.
- D-pad ▲▼ to move cursor, △ to switch to the cursor slot (disconnect current ACL, restart inquiry filtered to the new slot's bd_addr), □ hold 1.5 s to wipe a single slot.
- "Wipe all slots" item in the Settings menu drops all 4 stored bd_addrs + all BTstack link keys in one shot.
- Inquiry filter on `HCI_EVENT_INQUIRY_RESULT` enforces slot ownership: devices stored in other slots are skipped; empty slots auto-assign on the first L2CAP HID_CONTROL channel open.
- Dongle stops being BT-discoverable once all 4 slots are full (security tightening; was permanently discoverable in upstream).
- Storage: BTstack's TLV link-key DB holds the keys (`NVM_NUM_LINK_KEYS=4`, unchanged from upstream); a new dedicated flash sector holds the 4 bd_addrs + occupancy bits + magic word.
- Multi-slot UX modeled on [zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED). Credit to zurce.

### Added — security / correctness audit pass

Critical and high-severity fixes on the core bridge code. Many of these have since landed upstream independently; this changelog captures what this fork shipped.

- **C1**: 4.8× stack-overflow in `core1_entry`'s `out_buf` (200 floats vs. 960 floats the resampler/encoder actually writes/reads). Root cause of the long-standing "audio may experience slight stuttering" known issue. Buffer resized and Opus return value now checked.
- **C2 + H5**: Variable-length stack array in `set_feature_data` sized by host-controlled length (potential stack blowup). Replaced with a bounded fixed buffer + length validation; CRC bounds tightened so `len < 4` no longer wraps to a huge `size_t`.
- **C3**: Unbounded `memcpy` into a 78-byte stack buffer in `tud_hid_set_report_cb` for HID report 0x02 (overflow if `bufsize > 76`). Bounded.
- **H1**: `tud_hid_get_report_cb` wrote `feature_data.size() - 1` bytes into the host-supplied buffer without clamping to `reqlen` (host-buffer overflow). Clamped.
- **H2**: OOB read in `on_bt_data` (`data[56]` / `memcpy(.., data+3, 63)` with no length check on the incoming BT frame). Bounded.
- **H3**: Same pattern in `l2cap_packet_handler` (`packet[3]` read with no size check). Bounded.
- **H4**: UAC1 volume parsed as `uint16` instead of signed `int16` — non-conformant hosts could crank haptic gain to 256× and saturate everything. Fixed sign extension.
- **H7**: BT report sequence counter in `main.cpp` cycled with period 16 (incremented as `int`, written as `(c<<4)`). Replaced with the correct `(c+1) & 0x0F` wrap used in `audio.cpp`.
- **N1**: Pairing-failure recovery: three `gap_inquiry_start(30)` calls were commented out in `bt.cpp`'s error paths (HCI command status, connection complete, authentication complete). Dongle would soft-brick after any pairing failure that didn't end in a disconnect. Uncommented.
- **N2**: Removed printf of the bonding link-key to UART on every reconnect.
- **N3**: CRC-helper bounds: `fill_output_report_checksum` / `fill_feature_report_checksum` underflowed `size_t` and wrote at negative offsets on `len < 4`. Guarded.
- **N4**: HCI command-send return values logged so a future stuck-dongle report has observability.
- **N5**: Watchdog enabled (8 s). Hangs now recover automatically instead of needing a power-cycle.

### Added — pairing posture hardening

- Tightened `gap_discoverable_control(1)` — the dongle now only advertises as pairable when at least one slot is empty. Once all 4 slots are full, it goes non-discoverable (still connectable to bonded controllers).
- (Carried over from upstream's own hardening; documented here for completeness.)

### Changed

- Project rebranded as **OLED Edition** to differentiate from upstream. UF2 artifact renamed: `ds5-bridge.uf2` → `ds5-bridge-oled.uf2`.
- README rewritten with full hardware section (Pico 2 W + Pico-OLED-1.3 SKUs, vendor links, prices), all 10 OLED screen mockups in the new cycle order, and explicit `TINYUSB 0.20.0` pin requirement (the 0.18.0 bundled with Pico SDK 2.2.0 lacks the 4-arg form of `TUD_AUDIO_EP_SIZE` used by this project's `tusb_config.h`).
- KEY1 short-press: was a 250 ms test-rumble burst on most screens; now cycles **backward** through screens (mirror of KEY0). Still cycles trigger preset on Trigger Test and lightbar mode on Lightbar (their primary in-screen interactions).
- Screen cycle order reorganized: **Status → Slots → Lightbar → Trigger Test → Gyro Tilt → Touchpad → Diagnostics → RSSI → VU Meters → Settings**. Settings last; the three "test" stimulus screens grouped together; diagnostic screens grouped together. Screen indices are symbolic constants (`kScreenStatus`, `kScreenSlots`, …) so future reorders are a one-block edit.

### Fixed

- `config_default()` was declared in `config.h` but never defined upstream (latent "undefined symbol" → linker "dangerous relocation" if anything called it). Implemented (fills body with `0xFF`, runs `config_valid()` — same path as a freshly-erased flash sector).
- GitHub Actions workflow `cp` paths corrected to match the `ds5-bridge-oled.uf2` produced after the CMake `OUTPUT_NAME` rename. (Workflows had failed on every push since the rebrand commit.)

### Build

- TinyUSB pinned to `0.20.0` (required; see Build Instructions in README).
- Pico SDK 2.2.0 + toolchain `14_2_Rel1` validated. Pico 2 W (RP2350) is the primary target.
- `CMakeLists.txt` adds `src/oled.cpp` + `src/slots.cpp` to the executable target, links `hardware_spi`.
- `OUTPUT_NAME` set to `ds5-bridge-oled` so the UF2 reflects the project name.

### Acknowledgements

- **[awalol/DS5Dongle](https://github.com/awalol/DS5Dongle)** — upstream base. This fork is a strict superset that tracks upstream and layers add-on features.
- **[zurce/DS5Dongle-OLED](https://github.com/zurce/DS5Dongle-OLED)** — pixel-art icon approach, the "hold for factory reset" UX pattern, and the multi-slot persistent pairing model.

---

[0.5.4]: https://github.com/MarcelineVPQ/DS5Dongle-OLED-Edition/releases/tag/v0.5.4
