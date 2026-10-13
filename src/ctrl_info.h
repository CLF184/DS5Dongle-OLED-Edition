//
// Controller health & info for the configurator (protocol 1.12, vendor
// interface only): the controller's firmware versions from its cached feature
// reports, and Sony's read-only test commands (lifetime counters, battery
// voltage) run on the app's behalf. See ctrl_info.cpp for the wire layout.
//

#ifndef DS5_BRIDGE_CTRL_INFO_H
#define DS5_BRIDGE_CTRL_INFO_H

#include <cstdint>

#define PICO_CMD_CTRL_INFO 0xeb  // vendor interface only, GET; wValue high byte = selector

// State of the test request, ctrl_info_get() header [3].
enum : uint8_t {
    CTRL_TEST_IDLE = 0,          // nothing asked since the controller connected
    CTRL_TEST_RUNNING = 1,       // queued or waiting for the controller's answer
    CTRL_TEST_DONE = 2,          // the answer is in the pages
    CTRL_TEST_NO_ANSWER = 3,     // the controller never answered (its firmware lacks the command)
    CTRL_TEST_NO_CONTROLLER = 4, // the controller went away
};

// The two test commands the dongle runs for the app. Read-only and free of
// factory traceability data (serial numbers are blanked before the app sees
// a byte); anything else is refused.
constexpr uint8_t kCtrlTestTelemetryDev = 0x70, kCtrlTestTelemetryAction = 0x01;  // lifetime counters
constexpr uint8_t kCtrlTestBatteryDev = 0x04, kCtrlTestBatteryAction = 0x03;      // battery voltage (mV)

constexpr uint16_t kCtrlTestPageBytes = 56;  // one 0x81 reply: [0x81, dev, action, status, 56 bytes, crc32]
constexpr uint8_t kCtrlTestMaxPages = 6;     // the Edge's counters block (daidr/dualsense-tester)
constexpr uint8_t kCtrlInfoSelHeader = 0x00;
constexpr uint8_t kCtrlInfoSelFirmware = 0x01;
constexpr uint8_t kCtrlInfoSelPage0 = 0x10;  // + page index

// 0xF6 func 0x16 [dev, action, pages]: queue a test command. Returns a
// CMD_RESULT_* (OK = queued). Only records the request: the Bluetooth
// traffic happens in ctrl_test_task(), never inside a USB transfer.
uint8_t ctrl_test_start(const uint8_t *args, uint16_t len);

// Main loop. may_send = the dongle's own connect-time test read (the colorway)
// has finished, so the two never share the controller's single result slot.
void ctrl_test_task(bool may_send);

// While a test runs, and for a moment after (a late reply may hold the
// serial), the controller's 0x81 result slot is the dongle's: a host SET 0x80
// is not forwarded and a host GET 0x81 is refused (stalled) instead of taking
// one of the test's pages or reading it from the feature cache.
bool ctrl_test_owns_0x81();

// GET 0xEB with the selector from wValue's high byte. Never writes past reqlen.
uint16_t ctrl_info_get(uint8_t selector, uint8_t *buffer, uint16_t reqlen);

#endif //DS5_BRIDGE_CTRL_INFO_H
