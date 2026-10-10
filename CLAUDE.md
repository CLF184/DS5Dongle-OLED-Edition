# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Raspberry Pi Pico 2 W (RP2350) firmware that bridges a Sony DualSense (DS5) controller over Bluetooth Classic to a USB host. Presents itself to the host as a USB HID + UAC1 audio composite device. This repo is `MarcelineVPQ/DS5Dongle-OLED-Edition`, a personal fork of upstream `awalol/DS5Dongle` that adds an optional Waveshare Pico-OLED-1.3 status display (10 screens) and a 4-slot persistent multi-controller pairing layer.

`README.md` is authoritative for user-facing capability; `CHANGELOG.md` tracks what shipped per release. Read both before proposing changes — much of the design is non-obvious from the code alone (audit history, BT pairing-posture rationale, overclock requirement).

## Build

This is firmware — there are no tests. Verification is hardware-in-the-loop (see "Hardware-in-the-loop workflow" below).

The build is **non-trivial to set up** because it requires a specific TinyUSB version:

```bash
# Pico SDK 2.2.0 must be installed; export PICO_SDK_PATH.
# CRITICAL: TinyUSB must be pinned to 0.20.0. The 0.18.0 that ships with
# Pico SDK 2.2.0 lacks the 4-arg form of TUD_AUDIO_EP_SIZE used by
# src/tusb_config.h — compile errors with no clear pointer to the cause.
cd "$PICO_SDK_PATH/lib/tinyusb"
git fetch --tags && git checkout 0.20.0

# Build
cd /path/to/DS5Dongle
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DPICO_SDK_PATH="$PICO_SDK_PATH"
cmake --build build --target ds5-bridge
# Produces build/ds5-bridge-oled.uf2  (output name set in CMakeLists.txt, not the default)
```

Common build flags (set via `-D` on the cmake invocation):

- `ENABLE_SERIAL=ON` — route printf to USB CDC for debugging. Default OFF (production).
- `ENABLE_VERBOSE=ON` — chatty BT/HID logging. Default OFF.
- `ENABLE_BATT_LED=OFF` — disable the low-battery LED blink. Default ON.
- `PICO_W_BUILD=ON` — build for the original Pico W (drops audio, lower clock). Default targets Pico 2 W.

CI runs four build variants in `.github/workflows/build.yml` (standard / debug / no-batt-led / Pico W). When changing build flags or CMake, update the workflow accordingly.

## Hardware-in-the-loop workflow

There are no software tests. To verify a change:

1. Build → produces `build/ds5-bridge-oled.uf2`.
2. Put Pico into bootloader: hold BOOTSEL, plug USB. It mounts as `/run/media/$USER/RP2350` (Linux) or `RPI-RP2` (others).
3. Copy the UF2 to the mount: `cp build/ds5-bridge-oled.uf2 /run/media/$USER/RP2350/` — Pico auto-reboots into the new firmware.
4. Pair a DualSense: hold Create + PS on the controller until the lightbar pulses; the dongle inquiry picks it up.
5. Verify enumeration host-side: `lsusb | grep 054c:0ce6` should show `Sony Corp. DualSense wireless controller (PS5)`.

The development cadence is **one feature per UF2 + checkpoint with the user before moving on** — the user has hardware in front of them, regressions are caught the same minute, and large multi-feature commits make root-causing hard.

## Architecture

**Two cores, asymmetric workload:**

- **Core 0** runs everything: `main()` infinite loop in `src/main.cpp` drives `cyw43_arch_poll()` → BTstack, `tud_task()` → TinyUSB, `audio_loop()` (USB → BT haptic path), `interrupt_loop()` (BT → USB HID path), and `oled_loop()`.
- **Core 1** runs only `core1_entry()` in `src/audio.cpp`: the Opus speaker encoder and the mic Opus decoder + packet-loss concealment. It runs **from RAM** (see gotcha below). The cores talk only through Pico SDK `queue_t`s: `audio_fifo` (raw speaker PCM → core1), `audio_spk_fifo` (encoded speaker → core0), `mic_fifo` / `mic_decode_fifo` (mic Opus → core1 → decoded PCM → core0). Core0 also owns `haptics_fifo`.

**Two parallel data paths through the firmware:**

1. **HID** (DS5 input → host, host output → DS5): `src/bt.cpp` registers `on_bt_data()` → `src/main.cpp:on_bt_data()` → updates `interrupt_in_data[63]` → `tud_hid_report()`. Output reports come the other way through `tud_hid_set_report_cb`.
2. **Audio** (host USB audio → DS5 speaker + haptics): `src/audio.cpp:audio_loop()` reads 4-channel USB UAC1, splits ch1/2 (speaker → Opus encode on core1) and ch3/4 (haptic → resample 48k→3k), and `audio_bt_task()` sends one **`0x39`** report once two haptic + two speaker frames are queued (upstream's packed format). Controller **state** (triggers, LED, rumble) is *not* in audio frames: every host `0x02` output report is forwarded immediately as a `0x31` (`tud_hid_set_report_cb`), and volume (`state_set_volume`, driven by the host UAC1 volume) and mic-stream on/off (`update_mic_status`, a `0x32`) go out as one-off reports. Mic: DS5 mic Opus arrives in mic-tagged `0x31` input reports → core1 decode → `mic_drain()` feeds USB IN in 1 ms slices, only while the host has the mic interface open.

**BT layer:** Uses Pico SDK's bundled BTstack (no separate clone). `src/bt.cpp` is the only file that touches HCI/L2CAP. PSMs are `PSM_HID_CONTROL` and `PSM_HID_INTERRUPT`. Link keys live in BTstack's TLV NVM with `NVM_NUM_LINK_KEYS=4` (configured in `src/btstack_config.h`).

**OLED add-on is optional and self-contained:** All 10 screens, the SH1107 SPI driver, the 5×7 font, the icon table, and the input handling live in `src/oled.cpp` (~30 KB). The only outward dependencies are read-only accessors (`bt_is_connected`, `bt_get_addr`, `audio_peak_*`, `bt_get_signal_strength`, the new slot accessors) and the global `interrupt_in_data[63]` (read-only for input visualization). When no OLED is wired, the SPI writes go nowhere — no init check needed.

**Idle power ladder (`oled_loop` tail):** Three-stage state machine (`OLED_ACTIVE` → `OLED_DIM` → `OLED_OFF`) driven by `last_activity_us`. `kAutoDimUs = 2 min` enters Dim — the regular per-screen render is replaced with `render_dim_pulse()`, which clears the framebuffer and walks a 2×2 dot through 8 positions every 30 s, blinking 1 s on / 1 s off. `kAutoOffUs = 15 min` enters Off — `cmd(0xAE)` puts the SH1107 to sleep and `oled_loop` returns early before rendering. Wakes on KEY0/KEY1 (via `handle_buttons` bumping `last_activity_us`), `bt_is_connected()` rising edge, or any change in the `interrupt_in_data[0..9]` hash. The dim tier uses `flush_fb_raw()` (the chrome-less variant) since there's no nav target while asleep. **Why this shape:** the SH1107 contrast register has a heavily non-linear perceptual curve on the Waveshare panel — even `0x02` looks ~90 % as bright as `0xFF`. The only reliable "dim" available is rendering fewer pixels.

**Multi-slot pairing (Phase G):** Storage is two-tier:

- **Link keys** stay in BTstack's TLV NVM (4 slots, unchanged from upstream).
- **bd_addrs + occupancy** in a custom flash sector owned by `src/slots.cpp` — sector below the config sector (`PICO_FLASH_SIZE_BYTES - 2*FLASH_SECTOR_SIZE`).

Slot routing is enforced in `src/bt.cpp`'s `HCI_EVENT_INQUIRY_RESULT` handler: skip devices owned by other slots, accept only the current slot's bd_addr if occupied, auto-assign on first `L2CAP_EVENT_CHANNEL_OPENED` for HID_CONTROL when the current slot is empty. Switching slots flows: `bt_set_slot()` → persist new index → `bt_disconnect()` → `HCI_EVENT_DISCONNECTION_COMPLETE` → restart inquiry under new filter.

**Flash layout** (top of flash, growing downward):

| Sector | Owner | Contents |
|---|---|---|
| `PICO_FLASH_SIZE_BYTES - 1*FLASH_SECTOR_SIZE` | `src/config.cpp` | `Config{magic, version, crc32, size, body}` — 9 user-tunable fields incl. `current_slot` |
| `PICO_FLASH_SIZE_BYTES - 2*FLASH_SECTOR_SIZE` | `src/slots.cpp` | `SlotsData{magic, addrs[4][6], occupied[4]}` |

`config_save()`, `save_slots_to_flash()` and the remap save all go through `flash_sector_write()` (`src/flash_safe.h`), which uses `flash_safe_execute()` to park core1 in RAM during erase/program (`PICO_FLASH_ASSUME_CORE1_SAFE=0`; core1 calls `flash_safe_execute_core_init()`). Boot-time writes before core1 starts fall back to plain IRQ masking.

**Sectors live in `src/flash_layout.h`, directly below BTstack's TLV bank** (`PICO_FLASH_BANK_STORAGE_OFFSET`, which on RP2350 is the 2 sectors below the last): config at bank −1, slots −2, remap −3. They used to be at −1/−2/−3 of flash, overlapping the bank — never put anything inside `[PICO_FLASH_BANK_STORAGE_OFFSET, +PICO_FLASH_BANK_TOTAL_SIZE)`. `config_load()` migrates a valid config from the old last-sector location once. `flash_safe_execute` is wrapped at link time (`__wrap_flash_safe_execute` in `src/main.cpp`): before core1 registers for lockout it runs the write with core0 IRQs masked (the SDK would refuse with `NOT_PERMITTED` and BTstack's write would be lost), and it counts calls/failures for `0xF5`.

## Critical gotchas

These are non-obvious from the code; they cost time when forgotten.

- **Overclock 320 MHz @ 1.20 V is load-bearing.** Dropping voltage to 1.10 V *or* clock to stock breaks the CYW43 PIO SPI bus (BT chip becomes unreachable). Tested. Don't "optimize" `src/main.cpp:main()`'s `vreg_set_voltage(VREG_VOLTAGE_1_20)` + `set_sys_clock_khz(SYS_CLOCK_KHZ, true)` block.
- **Render functions are `__attribute__((noinline))` on purpose.** With aggressive inlining the compiler folds all 10 `render_screen_*` into `oled_loop()`, which then exceeds Thumb's 4 KB literal-pool reach and the linker emits `dangerous relocation: unsupported relocation`. If you remove the noinline attributes or merge two render functions into a single huge one, the build will fail at link time. The symbol size grows over time as screens get richer — the lambda inside `render_screen_settings` was already hoisted to a noinline `format_settings_item()` for the same reason.
- **`config_default()` is implemented in this fork**, not upstream. Upstream declares it in `config.h:30` but never defines it. The implementation in `src/config.cpp` fills the body with `0xFF` and re-runs `config_valid()` (matches a freshly-erased flash sector). Don't assume any header declaration is implemented just because it compiles — check the .cpp.
- **Screen indices are symbolic.** `kScreenStatus`, `kScreenSlots`, `kScreenLightbar`, … are defined in a single block near the top of `src/oled.cpp`. `oled_loop()`'s switch and `handle_buttons()`' KEY1 contextual checks (Trigger preset cycle, Lightbar mode cycle) refer to them by name. Never hard-code integers — reorder = one-block edit.
- **OLED `kPin*` pinout is hardcoded** to the Waveshare Pico-OLED-1.3 standard (`kPinMOSI=11`, `kPinCLK=10`, `kPinCS=9`, `kPinDC=8`, `kPinRST=12`, `kPinKey0=15`, `kPinKey1=17`). SPI1 instance. Conflicts with anything else on those pins. Pico's BT chip uses internal SPI to CYW43 (different bus), and USB / UART pins (GP0/GP1) are also free — no conflicts in default config.
- **DS5 PS+Mute hold-2-seconds → `watchdog_reboot(0,0,0)`** is the headless soft-reboot path (see `src/main.cpp:interrupt_loop()`). It checks `interrupt_in_data[9]` bits 0+2. If you change input report layout, this combo breaks silently.
- **Inactivity-disconnect uses `packet[3..12]`** in the L2CAP interrupt data path (`src/bt.cpp:l2cap_packet_handler`). It's looking at sticks and DPad/buttons to decide "idle." Don't touch those bytes' layout without updating the heuristic.
- **Core1's audio path must stay in RAM.** Running libopus / the resampler from flash made core1 miss its encode deadline (core0's BTstack/TinyUSB/OLED evict the shared XIP cache) and drop ~7 % of speaker chunks — the long-standing speaker crackle. CMakeLists.txt renames libopus `.text`/`.rodata` and the two `WDL_Resampler` hot methods into `.time_critical.*`, `relocate_to_ram()` moves SDK queue ops, `src/ram_mem.c` overrides `memcpy`/`memset`/`memmove`, and core1 functions are `__not_in_flash_func`. Anything new core1 calls per frame must be RAM-resident too. The OLED Diagnostics `Drop r/e/h` row is the tripwire: `r` climbing = core1 too slow.
- **Core1 must stay registered for flash lockout** (`flash_safe_execute_core_init()` in `core1_entry`, `PICO_FLASH_ASSUME_CORE1_SAFE=0`). Its RAM code still calls flash every frame (soft-double helpers, libm, the resampler's malloc), so an unparked flash write kills core1 — `0xF5` shows `c1_loops` frozen and every chunk dropped.
- **Use the `0xF5` counters first** (`scripts/audio_diag.py [secs]`): they show USB-in, BT-out, drops, core1 liveness and flash write failures from the host, no OLED reading needed.
  More host-side tools: `scripts/session_monitor.py LOG` logs all counters every 2 s and tags each line with the running game (auto-detected); `scripts/out02_dump.py` decodes the last host output report and the last one carrying trigger effects (`0xF3`/`0xF4`), e.g. to see whether a game is sending zero-force triggers; `scripts/get_ring.py` lists the last 16 feature-report GETs and how many bytes the dongle answered (`0xF2`).
- **The `0x39` audio report is 547 bytes** (+1 header byte): `send_element` in `src/bt.cpp` is sized to the interrupt MTU (672). Shrinking it overflows silently (silent speaker, hung USB audio). The DS5 amp needs `AllowSpeakerVolume` + a volume: clearing those bits makes the speaker go silent.

- **The DualSense HID descriptor must stay byte-identical to a real DS5 (289 bytes), and `0x09`/`0x20`/`0x05` feature reports must return the controller's *real* cached data — or native game triggers silently break.** A game's native DualSense detection (Cyberpunk, etc.) validates *both* the descriptor shape and the feature-report *content*, then drives adaptive triggers over the native hidraw path. If either differs from genuine hardware the game falls back to a generic/Xbox pad and triggers never fire. Linux's `hid_playstation` is far more lenient (size + CRC only), so the dongle still *binds* and the on-dongle OLED trigger-test still works — which masked this break for weeks. Two load-bearing things: (1) **don't re-declare config reports `0xF6`–`0xF9`** in `desc_hid_report_ds` (or bump its `wDescriptorLength` runtime patch in `tud_descriptor_configuration_cb` back to `0x41`) — that's what made the descriptor 321 bytes and killed triggers; (2) keep `tud_hid_get_report_cb` serving the **real `init_feature()` cache** for `0x09`/`0x20`/`0x05`, with the CRC-valid synthetic stub only as the pre-BT-link probe fallback. `desc_hid_report_dse` (DSE mode) still declares F6-F9 and needs the same cut. Host side requires a Proton with the Wine `winebus.sys` #9034 fix + `PROTON_ENABLE_HIDRAW=0x054c/0x0ce6` + Steam Input off; diff a dongle vs real-DS5 `PROTON_LOG=+hid` capture to debug (a GET retry storm on `0x20`/`0x09` = the dongle's feature content is being rejected).

## Versioning — single source of truth

The release tag is the **only** place the version is written. Everything else flows from it:

- **Release tag** (e.g. `v0.6.2-oled-edition`) → created with `git tag` then `gh release create`.
- **`.github/workflows/release.yml`** picks the tag up as `$FIRMWARE_VERSION` and passes it to CMake via `-DVERSION="$FIRMWARE_VERSION"`.
- **`CMakeLists.txt`** exposes it to C++ as a compile-time macro:
  ```cmake
  target_compile_definitions(ds5-bridge PRIVATE FIRMWARE_VERSION="${VERSION}")
  ```
  Local builds without `-DVERSION=...` get the default `"dev"` — that's a deliberate visual signal so an untagged build is obvious on the OLED Status header.
- **`src/oled.cpp` `render_screen()`** renders `"DS5 Bridge " FIRMWARE_VERSION` for the Status screen header. No string literal for the version exists anywhere else in the firmware source.
- **Web preview** (`DS5Dongle-OLED-Config-Web`) reads the same tag at runtime from `public/firmware-latest.json`, which CI bundles in `.github/workflows/deploy.yml`'s "Bundle latest firmware UF2 from GitHub releases" step (it pulls from `MarcelineVPQ/DS5Dongle-OLED-Edition/releases/latest` via the GitHub API). `OledEmulator.tsx` fetches that JSON on mount and writes the short form (suffix `-oled-edition` stripped) into `state.firmwareVersionLabel`, which `screens.ts:renderStatus` consumes.

**The release ritual** is therefore:

1. Update `CHANGELOG.md` — move `[Unreleased]` content into a new `[X.Y.Z-oled-edition]` section dated today.
2. Commit the CHANGELOG bump.
3. `git tag -a vX.Y.Z-oled-edition -m "..."`
4. `git push origin master && git push origin vX.Y.Z-oled-edition`
5. `gh release create vX.Y.Z-oled-edition -R MarcelineVPQ/DS5Dongle-OLED-Edition --title "vX.Y.Z — OLED Edition" --notes "..."`
6. CI builds the UF2s (~5–7 min), uploads them with SHA256SUMS, edits the release notes to append checksums, and (when `WEB_REPO_DISPATCH_PAT` is configured on the firmware repo — currently unset, see below) fires a `repository_dispatch` event to the web repo to refresh `firmware-latest.json`. Without the secret, the next push to the web repo's `master` does the refresh instead.

There is **no other place** to edit the version. If you find a hardcoded version string in source (`"v0.6.0"`, `"v0.5.4"`, etc.), it's a bug — replace it with the macro / JSON lookup.

**Known follow-up:** `WEB_REPO_DISPATCH_PAT` secret is unset on the firmware repo, so the firmware-release → web-rebuild dispatch is currently silently no-op'd (peter-evans/repository-dispatch with continue-on-error). The web bundle still updates via push events to the web repo's master, but not automatically on every firmware release.

## Git / branch model

- **`master` (origin)** = `MarcelineVPQ/DS5Dongle-OLED-Edition` (this fork's primary branding, what users download).
- **`upstream` / `upstream-real`** = `awalol/DS5Dongle` (read-only mirror for `git fetch upstream && git rebase` cycles).
- **`upstream-fork`** = `MarcelineVPQ/DS5Dongle-upstream-fork` (a separate cleanroom fork retained from earlier PR contributions; not the active development branch).

Commits in this repo include a `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` trailer for AI-assisted work.

## Where features live

When asked to modify behavior, the *first* file to read is usually one of:

- New BT pairing / connection state behavior → `src/bt.cpp` (HCI + L2CAP event handlers).
- New OLED screen or change to existing one → `src/oled.cpp`.
- New diagnostic counter on the OLED Diagnostics screen → bump `kNumDiagRows` in `src/oled.cpp` and add a `case` to `format_diag_row()` (single switch, one row per case). The screen scrolls automatically; no D-pad wiring needed. Counters that need rate-per-second arithmetic should be sampled in `sample_diag_rates()` and read from `g_diag_rates`. Counter globals themselves typically live in `src/main.cpp` next to `g_bt_31_packets` etc. with `extern` declarations near the top of `src/oled.cpp`.
- Host-side diagnostics → `scripts/mic_diag.sh` (Linux only, reads `/dev/hidraw` directly). Subcommands: `status` / `capture [secs]` / `watch` / `bt-trace`. `bt-trace` reads the firmware's `0xFD` vendor feature report (defined in `src/cmd.cpp`'s `tud_hid_get_report_cb`), which currently exposes BT-input counters + the host-output trigger-flow counters. To add a new counter visible to `bt-trace`: extend the `0xFD` payload in `src/cmd.cpp` (bump `want`, write at the new offset), bump `IOCTL_SIZE` in `bt_trace()` of the script, and add the field to its `decode()` dict. The `0xFD` report ID is not declared in the HID descriptor (Linux hidraw ioctls don't enforce that; WebHID would reject undeclared IDs, which is why config goes through `0xF6`).
- New persistent config field → `src/config.h` (struct), `src/config.cpp:config_valid()` (defaults + clamping), `src/oled.cpp:format_settings_item()` (UI), `src/oled.cpp:settings_adjust()` (D-pad ▶◀ behavior). Update `CHANGELOG.md`.
- USB descriptor or interface change → `src/usb_descriptors.cpp` + `src/tusb_config.h`.
- Desktop configurator transport → `src/vendor_itf.{h,cpp}` (CMake `ENABLE_COMPANION_VENDOR`, default OFF). A control-only, class-0xFF interface appended after every other interface, so `CONFIG_DESC_LEN_BASE` stays the patch anchor and the 289-byte DS descriptor is untouched. It carries the same `0xF2`–`0xFE` pico commands as the HID feature reports: GET = `0xC1`/bRequest `0x01`/wValue=id (payload has no report-id byte), SET = `0x41`/bRequest `0x02`, max 63 bytes, SET runs after the status stage. Windows binds WinUSB through **MS OS 1.0** descriptors (string `0xEE`, compat ID, `DeviceInterfaceGUIDs` = `{CD1F1B79-799E-44AC-ADFC-A3F2090D1D4A}`); not 2.0, which needs bcdUSB ≥ 0x0201 + BOS and would change the device descriptor. Every VENDOR-type request goes to `tud_vendor_control_xfer_cb` before TinyUSB's recipient dispatch, so the class driver (`usbd_app_driver_get_cb`) only claims the interface. Linux probe: `~/projects/ds5-companion-probe/vendor_probe.mjs` / `vendor_cmd.mjs` (npm `usb`).
- Configurator protocol → `src/cmd.cpp`. `0xF1` identity (vendor interface only; a real DS5 uses `0xF1` for firmware update, so the HID path never routes it): magic `DS5O`, protocol major.minor (`PICO_PROTO_*` in `cmd.h`), config body length, feature bits, u16 SET sequence, last function + result (`CMD_RESULT_*`), u32 host config generation, slot count/current slot/BT connected. `0xF6` functions: `0x01` config to RAM, `0x02` save, `0x03` USB reconnect, `0x04`/`0x05` switch/forget slot, `0x06` wipe slots, `0x07` reboot, `0x08` BOOTSEL, `0x09` factory reset, `0x0A` trigger preset preview, `0x10` remap. Save, slot, reboot, BOOTSEL, reset and trigger preview are deferred to `cmd_task()` in the main loop (reboot/BOOTSEL 50 ms later so the host gets its ACK); poll `0xF1` until the result leaves `PENDING`. Bump `PICO_PROTO_MINOR` for additions, `MAJOR` for layout breaks (the app refuses a major mismatch). Host config writes bump the generation counter and `oled.cpp:sync_host_config_changes()` re-reads the lightbar, brightness and Settings snapshot, so a dongle-side △ save can't write a stale copy over the host's changes.
- Audio / haptic path → `src/audio.cpp`. **Don't** add stack arrays sized smaller than the resampler / Opus expects (this is the C1 bug that caused the long-standing "audio stuttering" issue — fix landed in upstream `5b04cbd`, but the lesson stands).

## Don't do these

- **Don't disable the watchdog** without explicit user authorization. `watchdog_enable(1000, true)` in `src/main.cpp:main()` is the safety net for hangs.
- **Don't add fallbacks for missing OLED** beyond what `oled.cpp` already does (no init checks, SPI writes harmlessly to nothing) — the firmware must boot identically with or without the display.
- **Don't write a new flash sector backend** without updating the `static_assert(SECTOR_OFFSET % FLASH_SECTOR_SIZE == 0)` pattern from `slots.cpp` and confirming no overlap with `config.cpp`'s sector.
- **Don't push to `upstream` or `upstream-fork`** by accident — only `origin/master` is the personal fork's deployment target.
