// Desktop stand-in for src/usb_dump.cpp (no USB, no FreeRTOS here). The
// simulator toggles g_simUsbDump with the D key so the cockpit's
// "CAN DUMP USB" row can be previewed; frames pushed by the demo replay go
// nowhere.
#include "usb_dump.h"

bool g_simUsbDump = false;

namespace UsbDump {
void begin() {}
bool active() { return g_simUsbDump; }
void push(uint32_t, bool, bool, uint8_t, const uint8_t *) {}
} // namespace UsbDump
