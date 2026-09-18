//
// Created by awalol on 2026/3/5.
//

#include "audio.h"
#include "bt.h"
#include "resample.h"
#include "tusb.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "opus.h"
#include "utils.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"
#include "pico/time.h"
#include "pico/platform.h"
#include "pico/flash.h"
#include "config.h"
#include "state_mgr.h"
#include "usb.h"

#define INPUT_CHANNELS    4
#define OUTPUT_CHANNELS   2
#define SAMPLE_SIZE       64
#define REPORT_SIZE       398
#define REPORT_ID         0x36
// #define VOLUME_GAIN       2
// #define BUFFER_LENGTH     48

// DualSense microphone, ported from awalol/DS5Dongle's `mic` branch.
// The DS5 sends mic audio as Opus packets embedded in BT input report
// 0x31 when bit 1 of byte 2 is set; payload is 71 bytes of Opus at
// offset 4, decoded to mono 48 kHz 10 ms frames (480 samples).
#define MIC_CHANNELS      1
#define MIC_FRAMES        480
#define MIC_OPUS_SIZE     71

using std::clamp;
using std::max;

static WDL_Resampler resampler;
static uint8_t reportSeqCounter = 0;
static uint8_t packetCounter = 0;
static bool plug_headset = false;
alignas(8) static uint32_t audio_core1_stack[7680]; // 上游 57f185b：8192→7680，腾 RAM 给搬进内存的 crc32 表
queue_t audio_fifo;
static uint8_t opus_buf[200];
critical_section_t opus_cs;

// Mic path — restructured to mirror upstream PR #160 ("Audio path from RAM"):
//   mic_fifo        : BT ingress queue. Filled by mic_add_queue() from
//                     on_bt_data() on core0; drained by mic_proc() on core1.
//   mic_decode_fifo : decoded PCM frames. Filled by mic_proc() on core1;
//                     drained by audio_loop() on core0 into the USB IN EP.
// Both are depth-2 with drop-oldest, exactly like upstream. Decode moved to
// core1 (round-robin with speaker encode) so core0's OLED/VU/auto-haptics/BT
// poll no longer jitter the mic path.
queue_t mic_fifo;
queue_t mic_decode_fifo;
struct mic_element { uint8_t data[MIC_OPUS_SIZE]; };
// Upstream PR #160 parity: decoded frame carries a len field (opus_decode may
// return fewer than MIC_FRAMES); the hardware-throttled USB drain reads it.
struct mic_decode_element {
    int16_t data[MIC_FRAMES * MIC_CHANNELS];
    uint16_t len;
};
static OpusDecoder *mic_decoder = nullptr; // created + owned by core1 (mic_proc)
static volatile uint32_t g_mic_frames = 0;
static volatile int32_t  g_mic_last_decoded = 0;  // opus_decode return value (core1)
static volatile uint16_t g_mic_last_want = 0;     // bytes we asked TinyUSB to send
static volatile uint16_t g_mic_last_wrote = 0;    // bytes TinyUSB accepted
uint32_t audio_mic_frames() { return g_mic_frames; }
int32_t  audio_mic_last_decoded() { return g_mic_last_decoded; }
uint16_t audio_mic_last_want()    { return g_mic_last_want; }
uint16_t audio_mic_last_wrote()   { return g_mic_last_wrote; }
static volatile uint32_t g_mic_decode_failures = 0;  // opus_decode returns <= 0 (bad/missing packets)
uint32_t audio_mic_decode_failures() { return g_mic_decode_failures; }

// Host-gate for the mic: set by tud_audio_set_itf_cb (main.cpp) when the host
// opens the mic IN interface (alt != 0). Mirrors upstream PR #160 — the
// controller only streams mic audio while something is actually recording.
static bool mic_active = false;

// Upstream PR #160 parity: immediately tell the controller to start/stop
// streaming its mic via a 0x32 status report (pkt[4] bit 0 = mic-enable).
// Without this, closing the mic IN interface leaves the sticky mic-enable
// asserted and the controller keeps pushing Opus over BT forever (wasted
// bandwidth + battery).
void update_mic_status() {
    uint8_t pkt[142]{};
    pkt[0] = 0x32;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 0 << 6 | 1 << 7;
    pkt[3] = 1;
    pkt[4] = (mic_active && get_config().bt_mic_enable) ? 0b00000011 : 0b00000010;
    bt_write(pkt, sizeof(pkt));
}

void set_mic_active(bool active) {
    mic_active = active;
    update_mic_status();
}
bool audio_mic_active() { return mic_active; }

struct audio_raw_element {
    float data[512 * 2];
};

void set_headset(bool state) {
    plug_headset = state;
}

// Stubs kept for OLED diag-screen compatibility. Upstream removed the opus
// queue and audio FIFO drop tracking isn't wired here; OLED shows 0.
uint32_t audio_fifo_drops() { return 0; }
uint32_t opus_fifo_drops() { return 0; }

// Monotonic byte-flow counters for the OLED Diagnostics screen and the web
// emulator's USB / BT rate display. Updated below.
static volatile uint32_t g_usb_frames = 0;
static volatile uint32_t g_bt_packets = 0;
uint32_t audio_usb_frames() { return g_usb_frames; }
uint32_t audio_bt_packets() { return g_bt_packets; }

// Rolling-peak meters for the OLED VU screen. Updated in audio_loop once per USB
// frame (written by core1, read by core0 — hence volatile).
//
// The release is a pure function of elapsed time (~256 ms to zero) instead of
// "12.5 % per read". Decay-on-read meant a stale peak sat frozen while nobody was looking
// at the meter — the VU screen opened showing a spike from minutes ago — and every
// extra reader (the web emulator's 0xFB diag payload reads these too) silently
// doubled the fall rate. Now the value on screen is decay(stored peak, age), so it
// expires on its own and all readers see the same number.
static volatile uint16_t g_peak_spk   = 0;
static volatile uint16_t g_peak_hap   = 0;
static volatile uint32_t g_peak_spk_t = 0;  // µs when g_peak_spk was recorded
static volatile uint32_t g_peak_hap_t = 0;

// Linear release: ~1/256 less per ~1 ms unit, fully released at ~256 ms —
// anything older reads 0, so a peak cannot survive a page that was left closed.
// Integer only (no divide): a shift, a multiply, a shift. The u >= 256 guard is
// what keeps the subtraction from going negative (and wrapping in the uint16).
static inline uint16_t peak_decay(uint16_t v, uint32_t dt_us) {
    const uint32_t u = dt_us >> 10;                 // ~1 ms units
    if (u >= 256u) return 0;                        // ≥ ~256 ms old → fully released
    return (uint16_t)(v - (uint32_t)v * u / 256u);  // u < 256 → never negative
}

uint8_t audio_peak_speaker() {
    return (uint8_t)(peak_decay(g_peak_spk, time_us_32() - g_peak_spk_t) >> 7);
}
uint8_t audio_peak_haptic() {
    return (uint8_t)(peak_decay(g_peak_hap, time_us_32() - g_peak_hap_t) >> 7);
}

// Auto-haptics output peak (0-127 = int8 amplitude of the derived waveform).
// Counted only where the auto branch actually writes h_l/h_r (Fallback mode
// stays 0 while native haptics are active), so the OLED "AH" row reflects
// real auto-haptics usage rather than mere audio energy.
static volatile uint16_t g_ah_out_peak = 0;
uint16_t audio_ah_out_peak() { return g_ah_out_peak; }

// Most-recent Opus TOC byte (first byte of the packet). Used by the OLED
// Diagnostics screen to decode the frame's bandwidth + duration config
// without serial.
static volatile uint8_t g_mic_toc = 0;
uint8_t audio_mic_last_toc() { return g_mic_toc; }

// Push a 71-byte Opus mic packet from the BT handler into the mic_fifo.
// Called from src/main.cpp's on_bt_data() when the DS5 sends a mic-tagged
// 0x31 input report (data points at the Opus payload, len is the bytes
// available there). Mirrors upstream PR #160: validates len so a short or
// malformed report can't over-read past the packet buffer, and drops the
// oldest queued packet if the FIFO is full — preferring fresh audio.
void __not_in_flash_func(mic_add_queue)(uint8_t *data, uint16_t len) {
    // Host-gate: only queue mic audio while the host has the mic IN interface
    // open (alt != 0). Mirrors upstream PR #160's mic_add_queue — without this,
    // a sticky DS5 mic keeps streaming into the USB IN endpoint after Windows
    // releases the device (alt=0) and the mic "never turns off".
    if (!mic_active) return;
    if (len < MIC_OPUS_SIZE) return;
    static mic_element packet{};
    memcpy(packet.data, data, MIC_OPUS_SIZE);
    g_mic_toc = data[0]; // first byte of the Opus packet
    if (queue_is_full(&mic_fifo)) queue_try_remove(&mic_fifo, NULL);
    queue_try_add(&mic_fifo, &packet);
}

// Re-assert the DS5 mic-enable (pkt[4] bit 0) so the controller streams its mic
// even when no audio is being output to it. Normally the enable only rides the
// 0x36 audio frames, which are gated on active USB audio — so without this, mic
// only works while a game plays sound. The enable is sticky (the DS5 keeps
// streaming once it starts), so we send a control-only 0x36 (enable + the
// load-bearing SetStateData sub-report + a silent haptic block, no speaker
// payload → makes no sound) at ~4 Hz ONLY until mic frames start arriving, then
// stop — minimizing BT traffic and DS5 battery. Resumes if the stream stalls.
static void mic_enable_keepalive() {
    // Only while the host is actually recording (mic_active) — mirrors the
    // upstream gating; this just fixes upstream's blind spot where a silent
    // game never emits 0x36 audio frames, so the sticky mic-enable never gets
    // (re-)asserted and recording stays silent until audio plays.
    if (!bt_is_connected() || !get_config().bt_mic_enable || !mic_active) return;
    const uint64_t now = time_us_64();
    static uint32_t last_frames = 0;
    static uint64_t last_frame_us = 0;
    static uint64_t last_send_us = 0;
    const uint32_t frames = g_mic_frames;
    if (frames != last_frames) { last_frames = frames; last_frame_us = now; }
    if (last_frame_us != 0 && (now - last_frame_us) < 1000000ULL) return; // streaming → sticky, no resend
    if (last_send_us != 0 && (now - last_send_us) < 250000ULL) return;    // throttle to ~4 Hz while arming
    last_send_us = now;

    uint8_t pkt[REPORT_SIZE]{};
    pkt[0] = REPORT_ID;
    pkt[1] = reportSeqCounter << 4;
    reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
    pkt[2] = 0x11 | 1 << 7;
    pkt[3] = 7;
    pkt[4] = 0b11111111; // mic-enable (bit 0)
    const auto buf_len = get_config().audio_buffer_length;
    pkt[5] = pkt[6] = pkt[7] = pkt[8] = pkt[9] = buf_len;
    pkt[10] = packetCounter++;
    pkt[11] = 0x10 | 1 << 7; // SetStateData sub-report (load-bearing — keeps actuators alive)
    pkt[12] = 63;
    state_set(pkt + 13, 63);
    pkt[76] = 0x12 | 1 << 7;  // haptic sub-report; samples left zero = silent
    pkt[77] = SAMPLE_SIZE;
    // no speaker sub-report (pkt[142..] stays zero) → control-only, no audio out
    bt_write(pkt, sizeof(pkt));
    g_bt_packets++;
}

void __not_in_flash_func(audio_loop)() {
    // --- BUGFIX: HARDWARE-THROTTLED DIRECT SLICE DRAINING (upstream parity) ---
    // Slaves the microphone transmission speed to the USB host clock.
    // Instead of pushing entire decoded frames at once (which causes buffer
    // overflows and digital echo on strict OS stacks like macOS CoreAudio),
    // we query TinyUSB's transmit FIFO capacity and feed it 1ms slices (192
    // bytes) precisely when the host is ready to consume them.
    const bool mic_enabled = mic_active && get_config().bt_mic_enable;

    // Streaming state for hardware-throttled USB microphone transmission
    static mic_decode_element active_mic_frame{};
    static uint32_t active_frame_offset = 0;
    static bool has_active_frame = false;

    if (mic_enabled) {
        tu_fifo_t* tx_fifo = tud_audio_get_ep_in_ff();

        while (tx_fifo && tu_fifo_remaining(tx_fifo) >= 192) {
            if (!has_active_frame) {
                if (queue_try_remove(&mic_decode_fifo, &active_mic_frame)) {
                    has_active_frame = true;
                    active_frame_offset = 0;
                } else {
                    // Buffer Underrun Safety: If the decode queue runs dry, we MUST
                    // feed the USB interface with silence to keep the stream alive.
                    // This prevents macOS CoreAudio from resetting the driver.
                    int16_t silence[96] = {0};
                    tud_audio_write(silence, sizeof(silence));
                    break;
                }
            }

            if (has_active_frame) {
                int16_t usb_tx_buf[96]; // 48 Stereo-Frames (192 Bytes)
                const int16_t* src = active_mic_frame.data;
                const uint32_t total_samples = active_mic_frame.len / sizeof(int16_t);
                const uint32_t samples_needed = 48;

                for (uint32_t i = 0; i < samples_needed; i++) {
                    uint32_t src_idx = active_frame_offset + i;
                    if (src_idx < total_samples) {
                        int16_t sample = src[src_idx];
                        usb_tx_buf[i * 2] = sample;     // Duplicate mono to Left
                        usb_tx_buf[i * 2 + 1] = sample; // Duplicate mono to Right
                    } else {
                        usb_tx_buf[i * 2] = 0;
                        usb_tx_buf[i * 2 + 1] = 0;
                    }
                }

                const uint16_t wrote = tud_audio_write(usb_tx_buf, sizeof(usb_tx_buf));
                g_mic_last_want  = (uint16_t)sizeof(usb_tx_buf);
                g_mic_last_wrote = wrote;
                active_frame_offset += samples_needed;

                if (active_frame_offset >= total_samples) {
                    has_active_frame = false; // Current frame completely drained
                    g_mic_frames++;
                }
            }
        }
    } else {
        has_active_frame = false;
    }

    // 1. 读取 USB 音频数据
    if (!tud_audio_available()) {
        // Keep the DS5 mic streaming even without output audio — but ONLY once
        // the host has enumerated us (tud_mounted). Running it during the
        // fresh-pair feature handshake floods BT TX and delays controller-type
        // detection past the connection watchdog's timeout, which then tears the
        // link down (~10-15s "shutdown" on fresh pair). After enumeration the
        // handshake is done, so it's safe — and always-on mic still works.
        if (tud_mounted()) mic_enable_keepalive();
        return;
    }

    int16_t raw[192];
    uint32_t bytes_read = tud_audio_read(raw, sizeof(raw)); // 每次读入 384 bytes
    int frames = bytes_read / (INPUT_CHANNELS * sizeof(int16_t));
    if (frames == 0) {
        return;
    }
    g_usb_frames += (uint32_t)frames;

    static float audio_buf[512 * 2];
    static uint audio_buf_pos = 0;
    // 2. 从4ch中提取ch3/ch4，转换为float输入重采样器
    WDL_ResampleSample *in_buf;
    int nframes = resampler.ResamplePrepare(frames, OUTPUT_CHANNELS, &in_buf);

    const float audio_gain = mute[0] ? 0.0f : powf(10.0f, get_config().speaker_volume / 20.0f);
    const float haptics_gain = get_config().haptics_gain;
    // Peak meters: start from the *decayed* stored peak (not the raw stored one),
    // so a new, smaller peak can still take over once the old big one has aged out.
    const uint32_t peak_now = time_us_32();
    const uint16_t peak_base_spk = peak_decay(g_peak_spk, peak_now - g_peak_spk_t);
    const uint16_t peak_base_hap = peak_decay(g_peak_hap, peak_now - g_peak_hap_t);
    uint16_t spk_max = peak_base_spk;
    uint16_t hap_max = peak_base_hap;
    uint16_t native_max = 0;  // 本帧 ch3/ch4 实际峰值（Fallback 静默判断用，不继承 VU 显示缓存）

    // ---- Audio Auto Haptics (borrowed from loteran/DS5Dongle 5d6bc2f) ----
    // Derives a haptic-feedback waveform from the speaker audio so games that
    // never write haptic data (e.g. Ghost of Tsushima on Linux+Steam) still
    // produce rumble. Mode 1 (Fallback, default) fires only when native is
    // silent → preserves native HD haptics in games that do send them.
    const uint8_t auto_mode = get_config().auto_haptics_enable;
    const float auto_gain   = (auto_mode > 0) ? (get_config().auto_haptics_gain / 100.0f) * haptics_gain : 0.0f;
    static const float LP_COEFF[4] = { 0.01039f, 0.02074f, 0.03095f, 0.05123f };
    const float lp_a = LP_COEFF[get_config().auto_haptics_lowpass & 3];
    static float lp_l = 0.0f, lp_r = 0.0f;
    static float env_l = 0.0f, env_r = 0.0f;
    constexpr float ENV_ATK = 0.40f;
    constexpr float ENV_REL = 0.025f;
    constexpr int     NATIVE_SILENT_TIMEOUT = 100;
    constexpr uint16_t NATIVE_THRESHOLD     = 256;
    static int native_silent_count = NATIVE_SILENT_TIMEOUT * 2;
    const bool fallback_active = (auto_mode == 1) && (native_silent_count >= NATIVE_SILENT_TIMEOUT);
    float ah_peak = 0.0f;  // auto-haptics actual-output peak for this USB frame

    for (int i = 0; i < nframes; i++) {
        // VU peak tracking
        {
            int16_t sl = raw[i * INPUT_CHANNELS];
            int16_t sr = raw[i * INPUT_CHANNELS + 1];
            int16_t hl = raw[i * INPUT_CHANNELS + 2];
            int16_t hr = raw[i * INPUT_CHANNELS + 3];
            uint16_t a = (uint16_t)(sl < 0 ? -sl : sl);
            uint16_t b = (uint16_t)(sr < 0 ? -sr : sr);
            if (a > spk_max) spk_max = a;
            if (b > spk_max) spk_max = b;
            a = (uint16_t)(hl < 0 ? -hl : hl);
            b = (uint16_t)(hr < 0 ? -hr : hr);
            if (a > hap_max) hap_max = a;
            if (b > hap_max) hap_max = b;
            if (a > native_max) native_max = a;
            if (b > native_max) native_max = b;
        }
 #if !DISABLE_SPEAKER_PROC
        audio_buf[audio_buf_pos++] = raw[i * INPUT_CHANNELS] / 32768.0f * audio_gain;
        audio_buf[audio_buf_pos++] = raw[i * INPUT_CHANNELS + 1] / 32768.0f * audio_gain;
        if (audio_buf_pos == 512 * 2) {
            static audio_raw_element element{};
            memcpy(element.data, audio_buf, 512 * 2 * 4);
            if (queue_is_full(&audio_fifo)) {
                queue_try_remove(&audio_fifo,NULL);
            }
            if (!queue_try_add(&audio_fifo, &element)) {
                printf("[Audio] Warning: audio_fifo add failed\n");
            }
            audio_buf_pos = 0;
        }
#endif
        // 上游 a55fd46：haptics 增益改到重采样之后再施加（见下面的 int8 转换）。
        float h_l = raw[i * INPUT_CHANNELS + 2] / 32768.0f;
        float h_r = raw[i * INPUT_CHANNELS + 3] / 32768.0f;

        if (auto_mode > 0) {
            const float spk_l = raw[i * INPUT_CHANNELS    ] / 32768.0f;
            const float spk_r = raw[i * INPUT_CHANNELS + 1] / 32768.0f;
            lp_l += lp_a * (spk_l - lp_l);
            lp_r += lp_a * (spk_r - lp_r);
            const float abs_l = lp_l < 0.0f ? -lp_l : lp_l;
            const float abs_r = lp_r < 0.0f ? -lp_r : lp_r;
            env_l = (abs_l > env_l) ? env_l + ENV_ATK * (abs_l - env_l)
                                    : env_l + ENV_REL * (abs_l - env_l);
            env_r = (abs_r > env_r) ? env_r + ENV_ATK * (abs_r - env_r)
                                    : env_r + ENV_REL * (abs_r - env_r);
            float al = lp_l * (1.0f + 3.0f * env_l) * auto_gain;
            float ar = lp_r * (1.0f + 3.0f * env_r) * auto_gain;
            al = al / (1.0f + (al < 0.0f ? -al : al));
            ar = ar / (1.0f + (ar < 0.0f ? -ar : ar));

            // Track the derived waveform only where it actually lands in the
            // output (Replace/Mix always, Fallback only while native is silent).
            auto track_ah = [&](float a, float b) {
                const float aa = a < 0.0f ? -a : a;
                const float bb = b < 0.0f ? -b : b;
                const float a_m = aa > bb ? aa : bb;
                if (a_m > ah_peak) ah_peak = a_m;
            };

            if (auto_mode == 3) {              // Replace
                h_l = al; h_r = ar;
                track_ah(al, ar);
            } else if (auto_mode == 2) {       // Mix
                float m_l = h_l + al, m_r = h_r + ar;
                h_l = m_l / (1.0f + (m_l < 0.0f ? -m_l : m_l));
                h_r = m_r / (1.0f + (m_r < 0.0f ? -m_r : m_r));
                track_ah(al, ar);
            } else if (auto_mode == 1 && fallback_active) {  // Fallback (default)
                h_l = al; h_r = ar;
                track_ah(al, ar);
            }
        }

        in_buf[i * 2]     = static_cast<WDL_ResampleSample>(clamp(h_l, -1.0f, 1.0f));
        in_buf[i * 2 + 1] = static_cast<WDL_ResampleSample>(clamp(h_r, -1.0f, 1.0f));
    }
    // 只有真的出现更高峰值才刷新（并盖时间戳）；否则保持原值+原时间戳不动，
    // 让它按年龄自然过期——避免"旧的大峰值"永久压制后续的小峰值。
    if (spk_max > peak_base_spk) { g_peak_spk = spk_max; g_peak_spk_t = peak_now; }
    if (hap_max > peak_base_hap) { g_peak_hap = hap_max; g_peak_hap_t = peak_now; }
    g_ah_out_peak = (uint16_t)(ah_peak * 127.0f);
    if (native_max > NATIVE_THRESHOLD) {
        native_silent_count = 0;
    } else if (native_silent_count < NATIVE_SILENT_TIMEOUT * 2) {
        native_silent_count++;
    }

    // 3. 48kHz -> 3kHz 重采样
    static WDL_ResampleSample out_buf[SAMPLE_SIZE]; // 64 floats = 32帧 × 2ch
    const int out_frames = resampler.ResampleOut(out_buf, nframes, nframes / 4, OUTPUT_CHANNELS);

    static int8_t haptic_buf[SAMPLE_SIZE];
    static int haptic_buf_pos = 0;

    // 4. 转换为int8并缓冲，满64字节即组包发送
    for (int i = 0; i < out_frames; i++) {
        int val_l = static_cast<int>(out_buf[i * 2] * 127.0f * haptics_gain);
        int val_r = static_cast<int>(out_buf[i * 2 + 1] * 127.0f * haptics_gain);
        haptic_buf[haptic_buf_pos++] = static_cast<int8_t>(clamp(val_l, -128, 127));
        haptic_buf[haptic_buf_pos++] = static_cast<int8_t>(clamp(val_r, -128, 127));

        if (haptic_buf_pos != SAMPLE_SIZE) {
            continue;
        }
        uint8_t pkt[REPORT_SIZE]{};
        pkt[0] = REPORT_ID;
        pkt[1] = reportSeqCounter << 4;
        reportSeqCounter = (reportSeqCounter + 1) & 0x0F;
        pkt[2] = 0x11 | 0 << 6 | 1 << 7;
        pkt[3] = 7;
        // bit 0 = mic-enable: tells the DS5 to stream its mic over BT (awalol
        // confirmed this is the key). Bits 1-7 are the pre-existing speaker/
        // haptic audio-enable flags. Gated on the host having opened the mic
        // IN interface (mic_active) AND the bt_mic_enable config toggle —
        // mirrors upstream PR #160's (mic_active && !disable_mic).
        pkt[4] = (mic_active && get_config().bt_mic_enable) ? 0b11111111 : 0b11111110;
        const auto buf_len = get_config().audio_buffer_length;
        pkt[5] = buf_len;
        pkt[6] = buf_len;
        pkt[7] = buf_len;
        pkt[8] = buf_len; // 这 4 个字节的作用未知，调整没有效果
        pkt[9] = buf_len; // audio buffer length 只有调整这个字节生效。
        pkt[10] = packetCounter++;
        // SetStateData
        pkt[11] = 0x10 | 0 << 6 | 1 << 7;
        pkt[12] = 63;
        state_set(pkt + 13,63);
        // Haptics Audio Data
        pkt[76] = 0x12 | 0 << 6 | 1 << 7;
        pkt[77] = SAMPLE_SIZE;
        memcpy(pkt + 78, haptic_buf, SAMPLE_SIZE);
#if !DISABLE_SPEAKER_PROC
        // Speaker Audio Data
        pkt[142] = (plug_headset ? 0x16 : 0x13) | 0 << 6 | 1 << 7; // Speaker: 0x13
        // L Headset Mono: 0x14
        // L Headset R Speaker: 0x15
        // Headset: 0x16
        pkt[143] = 200;
        critical_section_enter_blocking(&opus_cs);
        memcpy(pkt + 144, opus_buf, 200);
        critical_section_exit(&opus_cs);
#endif

        bt_write(pkt, sizeof(pkt));
        g_bt_packets++;
        haptic_buf_pos = 0;
    }
}

void audio_init() {
    resampler.SetMode(true, 0, false);
    resampler.SetRates(48000, 3000);
    resampler.SetFeedMode(true);
    resampler.Prealloc(2, 24, 6);
    // Mic queues are consumed by core1's mic_proc from the moment core1 starts,
    // so they must be initialized BEFORE multicore_launch_core1 below.
    // BUGFIX (upstream parity): depth 8 elastic buffer — absorbs initial Opus
    // encoder/decoder warm-up delays and mitigates startup crackling/stuttering.
    queue_init(&mic_fifo, sizeof(mic_element), 8);
    queue_init(&mic_decode_fifo, sizeof(mic_decode_element), 8);
 #if !DISABLE_SPEAKER_PROC
    queue_init(&audio_fifo, sizeof(audio_raw_element), 2);
    critical_section_init(&opus_cs);
    multicore_launch_core1_with_stack(core1_entry, audio_core1_stack, sizeof(audio_core1_stack));
#endif
}

static OpusEncoder *encoder;
static WDL_Resampler resampler_audio;

// Speaker path (upstream parity): USB OUT PCM (core0 audio_fifo) → resample →
// opus encode → opus_buf for the haptics/speaker BT report. Non-blocking so
// core1 can also service the mic path in the same loop.
static void __not_in_flash_func(speaker_proc)() {
    static audio_raw_element audio_element{};
    if (!queue_try_remove(&audio_fifo, &audio_element)) {
        return;
    }
    // 将 512 frames 重采样成 480 frames 以解决噪音问题。感谢 @Junhoo
    WDL_ResampleSample *in_buf;
    int nframes = resampler_audio.ResamplePrepare(512, 2, &in_buf);
    for (int i = 0; i < nframes * 2; i++) {
        in_buf[i] = audio_element.data[i];
    }
    static WDL_ResampleSample out_buf[480 * 2];
    resampler_audio.ResampleOut(out_buf, nframes, 480, 2);

    static uint8_t out[200];
    (void) opus_encode_float(encoder, out_buf, 480, out, 200);
    critical_section_enter_blocking(&opus_cs);
    memcpy(opus_buf, out, 200);
    critical_section_exit(&opus_cs);
}

// Mic path (upstream parity): Opus packets from the controller (core0 mic_fifo)
// → opus decode → PCM into mic_decode_fifo for audio_loop to push to the USB
// IN endpoint. Drop-oldest on overflow, like upstream. Diag counters kept for
// the OLED screen (g_mic_last_decoded now written from core1).
static void __not_in_flash_func(mic_proc)() {
    static mic_element mic_packet{};
    if (!queue_try_remove(&mic_fifo, &mic_packet)) {
        return;
    }
    static mic_decode_element decode_element{};
    const int n = opus_decode(mic_decoder, mic_packet.data, MIC_OPUS_SIZE,
                              decode_element.data, MIC_FRAMES, 0);
    g_mic_last_decoded = n;
    if (n <= 0) {
        g_mic_decode_failures++;  // bad/missing Opus packet — surfaced on the OLED Diag screen
        return;
    }
    decode_element.len = (uint16_t)(n * MIC_CHANNELS * sizeof(int16_t));
    if (queue_is_full(&mic_decode_fifo)) {
        queue_try_remove(&mic_decode_fifo, NULL);
    }
    queue_try_add(&mic_decode_fifo, &decode_element);
}

void __not_in_flash_func(core1_entry)() {
    // Register core1 as a flash-safe victim so core0's flash_safe_execute()
    // (config_save) actually parks this core while flash is erased/programmed,
    // instead of letting it fault on XIP. Requires PICO_FLASH_ASSUME_CORE1_SAFE=0.
    flash_safe_execute_core_init();

    // Allow Core 0 to fully initialize Bluetooth and USB stacks before Core 1
    // starts processing — otherwise the dongle could shut down at initialization.
    sleep_ms(300);

    int error = 0;
    encoder = opus_encoder_create(48000, 2,OPUS_APPLICATION_AUDIO, &error);
    if (error != 0) {
        printf("[Audio] OpusEncoder create failed\n");
        return;
    }
    opus_encoder_ctl(encoder,OPUS_SET_EXPERT_FRAME_DURATION(OPUS_FRAMESIZE_10_MS));
    opus_encoder_ctl(encoder,OPUS_SET_BITRATE(200 * 8 * 100));
    opus_encoder_ctl(encoder,OPUS_SET_VBR(false));
    opus_encoder_ctl(encoder,OPUS_SET_COMPLEXITY(0)); // max 4
    resampler_audio.SetMode(true, 0, false);
    resampler_audio.SetRates(51200, 48000);
    resampler_audio.SetFeedMode(true);
    resampler_audio.Prealloc(2, 512, 480);
    mic_decoder = opus_decoder_create(48000, MIC_CHANNELS, &error);
    if (error != 0 || mic_decoder == nullptr) {
        printf("[Audio] OpusDecoder create failed\n");
        mic_decoder = nullptr;
    }

    while (true) {
        bool work_done = false;

        // Only enter processing if data is actually waiting.
        // This avoids constantly acquiring queue locks (spinlocks) when idle,
        // which would otherwise thrash the RP2350 system bus and starve Core 0.
        if (queue_get_level(&audio_fifo) > 0) {
            speaker_proc();
            work_done = true;
        }
        if (queue_get_level(&mic_fifo) > 0) {
            mic_proc();
            work_done = true;
        }

        // If both queues are empty, we can safely sleep.
        // This prevents 100% CPU usage while maintaining sub-millisecond response times.
        if (!work_done) {
            sleep_us(10);
        }
    }
}
