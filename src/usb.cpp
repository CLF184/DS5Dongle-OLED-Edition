//
// Created by awalol on 2026/3/4.
//

#include <cstdio>

#include "tusb.h"
#include "bsp/board_api.h"
#include "config.h"
#include "utils.h" // SetStateData (no include guard in utils.h — include first)
#include "bt.h"    // update_state + bt_power_off_controller

uint8_t mute[2]; // 0: SPEAKER(0x02) 1: MIC(0x05)
float volume[2] = {-100.0f, 0.0f}; // 0: SPEAKER(0x02) 1: MIC(0x05) — OLED factory value; upstream releases are also 0dB (48dB only exists on master since a7824d9 2026-07-08, causes Windows level=100 + mic clipping)

#define UAC1_ENTITY_SPK_FEATURE_UNIT    0x02
#define UAC1_ENTITY_MIC_FEATURE_UNIT    0x05

/*int main() {
    board_init();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    board_init_after_tusb();

    while (1) {
        tud_task();
    }
}*/

//--------------------------------------------------------------------+
// Audio Callback Functions
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// UAC1 Helper Functions
//--------------------------------------------------------------------+

static bool audio10_set_req_entity(tusb_control_request_t const *p_request, uint8_t *pBuff) {
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    uint8_t index = entityID == UAC1_ENTITY_SPK_FEATURE_UNIT ? 0 : 1;

    // If request is for our speaker feature unit
    if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT || entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO10_FU_CTRL_MUTE:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_SET_CUR: {
                        // Only 1st form is supported
                        TU_VERIFY(p_request->wLength == 1);

                        mute[index] = pBuff[0];

                        // Upstream parity: push mute state straight to the
                        // controller via a standalone 0x32 SetStateData packet.
                        SetStateData state = {
                            .AllowAudioMute = 1,
                            .MicMute = mute[1],
                            .SpeakerMute = mute[0],
                            .HeadphoneMute = mute[0],
                        };
                        update_state(state);

                        TU_LOG2("    Set Mute: %d of entity: %u\r\n", mute[index], entityID);
                        return true;
                    }

                    default:
                        return false; // not supported
                }

            case AUDIO10_FU_CTRL_VOLUME:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_SET_CUR:
                        // Only 1st form is supported
                        TU_VERIFY(p_request->wLength == 2);

                        volume[index] = static_cast<float>(*reinterpret_cast<int16_t const *>(pBuff)) / 256;
                        // Do NOT sync the USB-side UAC1 volume control into our
                        // flash-persisted config. The host (PipeWire/Pulse) re-applies
                        // its last-known UAC1 volume on every device reconnect, which
                        // would silently override the user's saved speaker_volume.
                        // Fix borrowed from loteran/DS5Dongle commit 03fa1e4.
                        // (update_state below pushes it to the controller but does
                        // not touch flash config — same as upstream.)

                        // Upstream parity: push volume straight to the controller.
                        if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                            SetStateData state = {
                                .AllowHeadphoneVolume = 1,
                                .AllowSpeakerVolume = 1,
                                .VolumeHeadphones = static_cast<uint8_t>(100.0f + volume[index]),
                                .VolumeSpeaker = static_cast<uint8_t>(100.0f + volume[index]),
                            };
                            update_state(state);
                        }
                        if (entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
                            SetStateData state = {
                                .AllowMicVolume = 1,
                                .VolumeMic = static_cast<uint8_t>(volume[index]),
                            };
                            update_state(state);
                        }

                        TU_LOG2("    Set Volume: %d dB of entity: %u\r\n", volume[index], entityID);
                        return true;

                    default:
                        return false; // not supported
                }

            // Unknown/Unsupported control
            default:
                TU_BREAKPOINT();
                return false;
        }
    }

    return false;
}

static bool audio10_get_req_entity(uint8_t rhport, tusb_control_request_t const *p_request) {
    uint8_t channelNum = TU_U16_LOW(p_request->wValue);
    uint8_t ctrlSel = TU_U16_HIGH(p_request->wValue);
    uint8_t entityID = TU_U16_HIGH(p_request->wIndex);
    uint8_t index = entityID == UAC1_ENTITY_SPK_FEATURE_UNIT ? 0 : 1;

    // If request is for our speaker feature unit
    if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT || entityID == UAC1_ENTITY_MIC_FEATURE_UNIT) {
        switch (ctrlSel) {
            case AUDIO10_FU_CTRL_MUTE:
                // Audio control mute cur parameter block consists of only one byte - we thus can send it right away
                // There does not exist a range parameter block for mute
                TU_LOG2("    Get Mute of entity: %u\r\n", entityID);
                return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &mute[index], 1);

            case AUDIO10_FU_CTRL_VOLUME:
                switch (p_request->bRequest) {
                    case AUDIO10_CS_REQ_GET_CUR:
                        TU_LOG2("    Get Volume of entity: %u\r\n", entityID); {
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                volume[index] = get_config().speaker_volume;
                            }
                            int16_t vol = volume[index] * 256; // convert to 1/256 dB units
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &vol, sizeof(vol));
                        }

                    case AUDIO10_CS_REQ_GET_MIN:
                        TU_LOG2("    Get Volume min of entity: %u\r\n", entityID); {
                            uint8_t min[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                min[0] = 0x00;
                                min[1] = 0x9c;
                            }else {
                                min[0] = 0x00;
                                min[1] = 0x00;
                            }
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &min, sizeof(min));
                        }

                    case AUDIO10_CS_REQ_GET_MAX:
                        TU_LOG2("    Get Volume max of entity: %u\r\n", entityID); {
                            uint8_t max[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                max[0] = 0x00;
                                max[1] = 0x00;
                            }else {
                                max[0] = 0x00;
                                max[1] = 0x30;
                            }
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &max, sizeof(max));
                        }

                    case AUDIO10_CS_REQ_GET_RES:
                        TU_LOG2("    Get Volume res of entity: %u\r\n", entityID); {
                            uint8_t res[2];
                            if (entityID == UAC1_ENTITY_SPK_FEATURE_UNIT) {
                                res[0] = 0x00;
                                res[1] = 0x01;
                            }else {
                                res[0] = 0x7a;
                                res[1] = 0x00;
                            }
                            return tud_audio_buffer_and_schedule_control_xfer(rhport, p_request, &res, sizeof(res));
                        }
                    // Unknown/Unsupported control
                    default:
                        TU_BREAKPOINT();
                        return false;
                }
                break;

            // Unknown/Unsupported control
            default:
                TU_BREAKPOINT();
                return false;
        }
    }

    return false;
}

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request) {
    (void) rhport;

    return audio10_get_req_entity(rhport, p_request);
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf) {
    (void) rhport;

    return audio10_set_req_entity(p_request, buf);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len) {
    (void) instance;
    (void) len;
}

// 上游 9d4a552 + 9923ce3：主机睡眠（USB 挂起）时顺手把手柄也关掉——否则手柄
// 会空耗一整夜。fork 没有 ENABLE_WAKE_HID（无 wake 分支），无条件编译。
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    printf("[USB PM] invoke tud_suspend_cb\n");
    bt_power_off_controller();
}

// 上游 edec7f7：PC 睡眠唤醒后出现"幽灵设备"（BIOS 开了 USB 持续供电时，主机
// 休眠期间 dongle 一直没掉电，唤醒后残留旧枚举）。唤醒时若手柄没连上，主动断开
// USB 让主机重新枚举。fork 无 wake 分支，去掉上游的 enable_wake 判断。
void tud_resume_cb(void) {
#if !ENABLE_SERIAL
    if (!bt_is_connected()) {
        tud_disconnect();
    }
#endif
}
