#pragma once
#include <Arduino.h>

// MX10 "cat printer" over Bluetooth LE (service AE30, writes to AE01).
// Protocol and timings from s8n/cataclysm (MIT), tested on a real MX10:
// docs/mx10-cat-printer-research.md in that repository.
//
// The image is 1 bit per pixel, PRINTER_WIDTH pixels per row, 48 bytes per
// row, least significant bit first, 1 = black.

static constexpr int PRINTER_WIDTH = 384;
static constexpr int PRINTER_ROW_BYTES = PRINTER_WIDTH / 8;

void printerInit();
// Blocks for the whole job: scan (first time), connect, send, wait for the
// paper, disconnect. progress(0..1) is called while sending. On failure,
// error says why, ready for the screen.
bool printerPrint(const uint8_t* rows, int height, void (*progress)(float), String& error);
