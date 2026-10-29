// Demo 18 - Hardware lab: the web server (esp_http_server, its tasks on core 0).
//   port 80   /           the page (index.html, built into the firmware)
//             /state      everything the robot measures, as JSON (the page asks 4 times a second)
//             /cmd        POST a text command: "leds 255 0 0", "head 30 10"... (main.cpp runs it)
//             /photo.jpg  one photo
//             /rec.wav    the last microphone recording
//   port 81   /stream     the camera as MJPEG (a stream keeps its connection busy: its own server)
// Nothing here touches I2C or the screen: commands go to loop() through labCommand().
#include "lab.h"
#include "camera.h"
#include <esp_http_server.h>

extern const char index_html_start[] asm("_binary_src_index_html_start");
extern const char index_html_end[] asm("_binary_src_index_html_end");

static httpd_handle_t s_http, s_stream;
static SemaphoreHandle_t s_camMutex;           // the photo and the stream share the camera

static bool capture(uint8_t quality, uint8_t** jpg, size_t* len) {
    xSemaphoreTake(s_camMutex, portMAX_DELAY);
    const bool ok = cameraCaptureJpeg(quality, jpg, len);
    xSemaphoreGive(s_camMutex);
    return ok;
}

static esp_err_t onIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);   // without the final 0
}

static esp_err_t onState(httpd_req_t* req) {
    static char json[2048];                    // one handler at a time: the server has one task
    const size_t n = labStateJson(json, sizeof(json));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json, n);
}

static esp_err_t onCmd(httpd_req_t* req) {
    char line[128];
    const int n = httpd_req_recv(req, line, min(req->content_len, sizeof(line) - 1));
    if (n <= 0) return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty command");
    line[n] = 0;
    labCommand(line);
    return httpd_resp_sendstr(req, "ok");
}

static esp_err_t onPhoto(httpd_req_t* req) {
    uint8_t* jpg;
    size_t len;
    if (!capture(90, &jpg, &len)) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=stackchan.jpg");
    const esp_err_t e = httpd_resp_send(req, (const char*)jpg, len);
    free(jpg);
    return e;
}

// 44-byte WAV header + the samples as they are (PCM16 little-endian, like the ESP32).
static esp_err_t onWav(httpd_req_t* req) {
    const int16_t* pcm;
    const size_t n = audioRecording(&pcm);
    if (!n) return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no recording yet");
    const uint32_t bytes = n * 2;
    struct __attribute__((packed)) {
        char riff[4] = { 'R', 'I', 'F', 'F' }; uint32_t size; char wave[4] = { 'W', 'A', 'V', 'E' };
        char fmt[4] = { 'f', 'm', 't', ' ' }; uint32_t fmtSize = 16; uint16_t format = 1, channels = 1;
        uint32_t rate = AUDIO_RATE, byteRate = AUDIO_RATE * 2; uint16_t align = 2, bits = 16;
        char data[4] = { 'd', 'a', 't', 'a' }; uint32_t dataSize;
    } h;
    h.size = 36 + bytes;
    h.dataSize = bytes;
    httpd_resp_set_type(req, "audio/wav");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t e = httpd_resp_send_chunk(req, (const char*)&h, sizeof(h));
    for (size_t off = 0; e == ESP_OK && off < bytes; off += 4096)
        e = httpd_resp_send_chunk(req, (const char*)pcm + off, min<size_t>(4096, bytes - off));
    if (e == ESP_OK) e = httpd_resp_send_chunk(req, nullptr, 0);
    return e;
}

#define BOUNDARY "stackchanframe"
static esp_err_t onStream(httpd_req_t* req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
    char head[100];
    for (;;) {
        uint8_t* jpg;
        size_t len;
        if (!capture(60, &jpg, &len)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        const int n = snprintf(head, sizeof(head), "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                               (unsigned)len);
        esp_err_t e = httpd_resp_send_chunk(req, head, n);
        if (e == ESP_OK) e = httpd_resp_send_chunk(req, (const char*)jpg, len);
        free(jpg);
        if (e != ESP_OK) break;                // the page closed the stream
        vTaskDelay(pdMS_TO_TICKS(50));         // about 10 fps: leaves CPU for the rest
    }
    return ESP_OK;
}

void webStart() {
    s_camMutex = xSemaphoreCreateMutex();
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = 80;
    c.ctrl_port = 32768;
    c.core_id = 0;                 // network on core 0
    c.stack_size = 8192;           // the photo encodes a JPEG here
    c.max_open_sockets = 7;
    c.lru_purge_enable = true;     // a browser that leaves without closing doesn't keep a slot
    if (httpd_start(&s_http, &c) == ESP_OK) {
        const httpd_uri_t uris[] = {
            { "/", HTTP_GET, onIndex, nullptr },
            { "/state", HTTP_GET, onState, nullptr },
            { "/cmd", HTTP_POST, onCmd, nullptr },
            { "/photo.jpg", HTTP_GET, onPhoto, nullptr },
            { "/rec.wav", HTTP_GET, onWav, nullptr },
        };
        for (const auto& u : uris) httpd_register_uri_handler(s_http, &u);
    }
    httpd_config_t s = HTTPD_DEFAULT_CONFIG();
    s.server_port = 81;
    s.ctrl_port = 32769;
    s.core_id = 0;
    s.stack_size = 8192;
    if (httpd_start(&s_stream, &s) == ESP_OK) {
        const httpd_uri_t stream = { "/stream", HTTP_GET, onStream, nullptr };
        httpd_register_uri_handler(s_stream, &stream);
    }
}
