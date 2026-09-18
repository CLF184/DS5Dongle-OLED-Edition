//
// Created by awalol on 2026/3/4.
//

#ifndef DS5_BRIDGE_BT_H
#define DS5_BRIDGE_BT_H

#include <cstdint>
#include <vector>

enum CHANNEL_TYPE {
    INTERRUPT,
    CONTROL
};

// utils.h has no include guard, so only forward-declare SetStateData here.
// Callers that construct one must include utils.h themselves.
struct SetStateData;

typedef void (*bt_data_callback_t)(CHANNEL_TYPE channel, uint8_t *data, uint16_t len);

int bt_init();
void bt_register_data_callback(bt_data_callback_t callback);
void bt_send_packet(uint8_t *data, uint16_t len);
void bt_send_control(uint8_t *data, uint16_t len);
void bt_power_off_controller();
void bt_write(const uint8_t *data, uint16_t len);
void bt_get_signal_strength(int8_t *rssi);   // pure: returns the last measured value
void bt_rssi_request();                      // explicit refresh ask (10 Hz cap inside)
std::vector<uint8_t> get_feature_data(uint8_t reportId,uint16_t len);
// Side-effect-free read of an already-cached feature report (empty vector if it
// hasn't arrived yet). Unlike get_feature_data(), never issues an L2CAP request,
// so it is safe to poll every frame — used by the OLED IMU-calibration parse.
std::vector<uint8_t> bt_peek_feature(uint8_t reportId);
void init_feature();
void set_feature_data(uint8_t reportId, uint8_t* data,uint16_t len);

// Upstream parity (bt.cpp:917): send an out-of-band SetStateData packet (0x32)
// straight to the controller. Used by USB audio SET_CUR (mute/volume) so host
// audio controls reach the DS5 exactly like a wired pad.
void update_state(const SetStateData& state);

// Connection-attempt watchdog: call once per main-loop iteration. Recovers a
// stalled connection (auto re-inquiry) so a transient RF glitch — e.g. USB 3.0
// 2.4 GHz interference — doesn't hang the dongle on the amber lightbar.
void bt_connection_watchdog_tick();

// OLED add-on accessors.
bool bt_is_connected();
void bt_get_addr(uint8_t out[6]);
uint32_t bt_hci_err_count();

// Multi-slot persistent pairing (Phase G). Modeled on zurce/DS5Dongle-OLED.
int  bt_get_slot();
void bt_set_slot(int slot);
void bt_forget_slot(int slot);
void bt_wipe_all_slots();
bool bt_slot_occupied(int slot);
void bt_slot_get_addr(int slot, uint8_t out[6]);

#endif //DS5_BRIDGE_BT_H
