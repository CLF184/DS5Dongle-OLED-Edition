//
// Configurator vendor interface (ENABLE_COMPANION_VENDOR).
//
// A control-only, vendor-class USB interface that carries the same 0xF2-0xFE
// pico commands as the HID feature reports, for the desktop configurator.
// Why not HID: the DS descriptor is byte-matched to a real DualSense (no
// F6-F9 declared, so WebHID/Windows refuse them), and Linux hid_playstation
// claims every HID interface on 054C:0CE6. A vendor-class interface is ignored
// by both, and Windows binds WinUSB to it via MS OS 1.0 descriptors.
//
// Wire protocol (wIndex = VENDOR_ITF_NUM, wValue = pico command id):
//   GET  bmRequestType 0xC1, bRequest VENDOR_REQ_GET, wLength <= 63
//        -> the pico_cmd_get payload, WITHOUT the leading report-id byte.
//   SET  bmRequestType 0x41, bRequest VENDOR_REQ_SET, 1 <= wLength <= 63
//        -> pico_cmd_set(id, data); data[0] is the function id (0x01 apply...).
//        Runs after the status stage, so commands that drop the bus (0x03
//        reconnect) still complete successfully on the host.
//

#ifndef DS5_BRIDGE_VENDOR_ITF_H
#define DS5_BRIDGE_VENDOR_ITF_H

#include <stdint.h>

// Interface order in usb_descriptors.cpp: audio x3, HID, [CDC x2], vendor.
#define VENDOR_ITF_NUM (4 + 2 * ENABLE_SERIAL)

#define VENDOR_REQ_GET 0x01
#define VENDOR_REQ_SET 0x02
#define VENDOR_MAX_PAYLOAD 63

#define TUD_VENDOR_ITF_DESC_LEN 9

#define MS_OS_STRING_INDEX 0xEE
#define MS_OS_VENDOR_CODE 0x20

uint16_t const *ms_os_string_descriptor();

#endif //DS5_BRIDGE_VENDOR_ITF_H
