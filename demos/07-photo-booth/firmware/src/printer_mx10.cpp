#include "printer_mx10.h"
#include <NimBLEDevice.h>
#include <vector>

// --- Protocol: 0x51 0x78 <cmd> 0x00 <len lo> <len hi> <payload> <crc8(payload)> 0xFF ---
static constexpr uint8_t CMD_BITMAP = 0xA2, CMD_STATE = 0xA3, CMD_QUALITY = 0xA4, CMD_LATTICE = 0xA6,
                         CMD_UPDATE = 0xA9, CMD_ENERGY = 0xAF, CMD_SPEED = 0xBD, CMD_DRAWMODE = 0xBE;
static const uint8_t LATTICE_START[] = { 0xAA, 0x55, 0x17, 0x38, 0x44, 0x5F, 0x5F, 0x5F, 0x44, 0x38, 0x2C };
static const uint8_t LATTICE_END[]   = { 0xAA, 0x55, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x17 };
static constexpr uint16_t ENERGY       = 0xA000;   // "dark": prints well on the MX10 head
static constexpr uint8_t  QUALITY      = 0x32;
static constexpr uint8_t  SPEED_PRINT  = 32;
static constexpr uint8_t  SPEED_FEED   = 0x19;
static constexpr int      FEED_BEFORE  = 48;       // 6 mm: the start-of-motion artifact lands on blank paper
static constexpr int      FEED_AFTER   = 72;       // 9 mm, as blank rows: the 0xA1 feed command is unreliable on MX10
static constexpr size_t   CHUNK        = 200;      // bytes per BLE write...
static constexpr uint32_t CHUNK_MS     = 20;       // ...every 20 ms: the MX10 has no usable flow control mid-job
static constexpr uint32_t DRAIN_MS     = 3000;     // the printer is still burning after the last byte arrives
static constexpr uint32_t SCAN_S       = 5;

static const NimBLEUUID SERVICE("ae30"), TX_CHAR("ae01");
static NimBLEAddress s_addr;
static bool s_known = false;

static uint8_t crc8(const uint8_t* p, size_t n) {
    uint8_t crc = 0;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) crc = crc & 0x80 ? (crc << 1) ^ 0x07 : crc << 1;
    }
    return crc;
}

static void frame(std::vector<uint8_t>& out, uint8_t cmd, const uint8_t* p, size_t n) {
    const uint8_t head[] = { 0x51, 0x78, cmd, 0x00, (uint8_t)(n & 0xFF), (uint8_t)(n >> 8) };
    out.insert(out.end(), head, head + 6);
    out.insert(out.end(), p, p + n);
    out.push_back(crc8(p, n));
    out.push_back(0xFF);
}

static void frame1(std::vector<uint8_t>& out, uint8_t cmd, uint8_t v) { frame(out, cmd, &v, 1); }

// The whole job as one byte stream, in the order the vendor app uses.
static void buildJob(std::vector<uint8_t>& job, const uint8_t* rows, int height) {
    static const uint8_t blank[PRINTER_ROW_BYTES] = {};
    const uint8_t energy[] = { ENERGY & 0xFF, ENERGY >> 8 };
    job.reserve((height + FEED_BEFORE + FEED_AFTER) * (PRINTER_ROW_BYTES + 8) + 128);
    frame1(job, CMD_STATE, 0);
    frame1(job, CMD_STATE, 0);              // "start printing" on old-kind printers
    frame1(job, CMD_QUALITY, QUALITY);
    frame1(job, CMD_SPEED, SPEED_PRINT);
    frame(job, CMD_ENERGY, energy, 2);
    frame1(job, CMD_DRAWMODE, 0);           // 0 = image (1 = text)
    frame1(job, CMD_UPDATE, 0);             // without it the MX10 printed artifacts and crashed
    frame(job, CMD_LATTICE, LATTICE_START, sizeof(LATTICE_START));
    for (int i = 0; i < FEED_BEFORE; i++) frame(job, CMD_BITMAP, blank, PRINTER_ROW_BYTES);
    for (int y = 0; y < height; y++) frame(job, CMD_BITMAP, rows + y * PRINTER_ROW_BYTES, PRINTER_ROW_BYTES);
    frame1(job, CMD_SPEED, SPEED_FEED);     // feed inside the lattice session
    for (int i = 0; i < FEED_AFTER; i++) frame(job, CMD_BITMAP, blank, PRINTER_ROW_BYTES);
    frame(job, CMD_LATTICE, LATTICE_END, sizeof(LATTICE_END));
    frame1(job, CMD_STATE, 0);
    // Never send the 0xBC "prepare camera" frame some apps capture: its broken
    // length makes the MX10 swallow the next ~513 bytes (pale prints).
}

static bool findPrinter() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);   // the name comes in the scan response
    NimBLEScanResults res = scan->start(SCAN_S, false);
    Serial.printf("[PRINTER] scan: %d devices\n", res.getCount());
    for (int i = 0; i < res.getCount(); i++) {
        NimBLEAdvertisedDevice d = res.getDevice(i);
        const std::string name = d.getName();
        // Every device seen, so a printer with an unexpected name shows up in the serial log.
        Serial.printf("[PRINTER]   %s rssi %d name '%s'%s\n", d.getAddress().toString().c_str(), d.getRSSI(),
                      name.c_str(), d.isAdvertisingService(SERVICE) ? " [AE30]" : "");
        if (d.isAdvertisingService(SERVICE) || name.rfind("MX", 0) == 0) {
            s_addr = d.getAddress();
            s_known = true;
            Serial.printf("[PRINTER] found %s (%s)\n", name.c_str(), s_addr.toString().c_str());
            break;
        }
    }
    scan->clearResults();
    return s_known;
}

void printerInit() {
    NimBLEDevice::init("stackchan");
    NimBLEDevice::setMTU(247);   // bigger writes; with the default 23 every write carries 20 bytes
}

bool printerPrint(const uint8_t* rows, int height, void (*progress)(float), String& error) {
    if (!s_known && !findPrinter()) {
        error = "Printer not found. Is it on and nearby?";
        return false;
    }
    NimBLEClient* client = NimBLEDevice::createClient();
    client->setConnectTimeout(5);
    // A printer that slept or was swapped: forget it, scan once more and retry.
    if (!client->connect(s_addr) && !(s_known = false, findPrinter() && client->connect(s_addr))) {
        NimBLEDevice::deleteClient(client);
        error = "Can't connect to the printer. Turn it off and on.";
        return false;
    }
    NimBLERemoteService* svc = client->getService(SERVICE);
    NimBLERemoteCharacteristic* tx = svc ? svc->getCharacteristic(TX_CHAR) : nullptr;
    if (!tx) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        error = "This printer doesn't speak the MX10 protocol.";
        return false;
    }

    std::vector<uint8_t> job;
    buildJob(job, rows, height);
    const size_t chunk = min(CHUNK, (size_t)client->getMTU() - 3);
    const uint32_t pause = max<uint32_t>(1, chunk * CHUNK_MS / CHUNK);   // same throughput with smaller writes
    Serial.printf("[PRINTER] %u bytes, %u rows, chunks of %u (MTU %u)\n", (unsigned)job.size(), (unsigned)height,
                  (unsigned)chunk, (unsigned)client->getMTU());
    bool ok = true;
    for (size_t off = 0; off < job.size() && ok; off += chunk) {
        ok = tx->writeValue(job.data() + off, min(chunk, job.size() - off), false) && client->isConnected();
        if (progress) progress((float)(off + chunk) / job.size());
        delay(pause);
    }
    if (ok) delay(DRAIN_MS);   // disconnecting now would cut the print short
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    if (!ok) error = "Lost the printer connection. Is the battery low?";
    return ok;
}
