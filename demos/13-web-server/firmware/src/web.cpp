#include "web.h"
#include "shared.h"
#include "camera.h"
#include "camera_extra.h"
#include <esp_http_server.h>

// The page, built into the firmware by board_build.embed_txtfiles (platformio.ini).
extern const char index_html_start[] asm("_binary_src_web_index_html_start");
extern const char index_html_end[] asm("_binary_src_web_index_html_end");

static httpd_handle_t s_http = nullptr, s_stream = nullptr;
static int s_clients = 0;

static bool capture(uint8_t quality, uint8_t** jpg, size_t* len) {
    cameraLock();
    bool ok = cameraCaptureJpeg(quality, jpg, len);
    cameraUnlock();
    return ok;
}

static esp_err_t onIndex(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start - 1);   // without the final 0
}

static esp_err_t onPhoto(httpd_req_t* req) {
    uint8_t* jpg;
    size_t len;
    const bool hdr = strstr(req->uri, "hdr") != nullptr;   // /hdr.jpg: three exposures fused
    if (!(hdr ? cameraHdrJpeg(90, &jpg, &len) : cameraPhotoJpeg(90, &jpg, &len))) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=stackchan.jpg");
    esp_err_t e = httpd_resp_send(req, (const char*)jpg, len);
    free(jpg);
    return e;
}

// Every text frame from the page is a command: {"cmd":"head","pan":90}...
static esp_err_t onWs(httpd_req_t* req) {
    if (req->method == HTTP_GET) return ESP_OK;   // the handshake: nothing else to do
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t e = httpd_ws_recv_frame(req, &f, 0);   // first only the length
    if (e != ESP_OK || f.len == 0 || f.len > 512) return e;
    uint8_t buf[513];
    f.payload = buf;
    e = httpd_ws_recv_frame(req, &f, f.len);
    if (e == ESP_OK && f.type == HTTPD_WS_TYPE_TEXT) commandSubmit((const char*)buf, f.len);
    return e;
}

#define BOUNDARY "stackchanframe"
static esp_err_t onStream(httpd_req_t* req) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=" BOUNDARY);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    char head[100];
    for (;;) {
        uint8_t* jpg;
        size_t len;
        if (!capture(60, &jpg, &len)) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int n = snprintf(head, sizeof(head), "\r\n--" BOUNDARY "\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                         (unsigned)len);
        esp_err_t e = httpd_resp_send_chunk(req, head, n);
        if (e == ESP_OK) e = httpd_resp_send_chunk(req, (const char*)jpg, len);
        free(jpg);
        if (e != ESP_OK) break;            // the browser closed the page
        vTaskDelay(pdMS_TO_TICKS(50));     // about 10 fps: leaves CPU for the rest
    }
    return ESP_OK;
}

void webStart() {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.server_port = 80;
    c.ctrl_port = 32768;
    c.core_id = 0;                 // network on core 0, as in the other demos
    c.stack_size = 8192;           // the photo encodes a JPEG here
    c.max_open_sockets = 7;
    c.lru_purge_enable = true;     // a phone that leaves without closing doesn't keep a slot
    if (httpd_start(&s_http, &c) == ESP_OK) {
        httpd_uri_t index = { "/", HTTP_GET, onIndex, nullptr };
        httpd_uri_t photo = { "/photo.jpg", HTTP_GET, onPhoto, nullptr };
        httpd_uri_t hdr = { "/hdr.jpg", HTTP_GET, onPhoto, nullptr };
        httpd_uri_t ws = { "/ws", HTTP_GET, onWs, nullptr };
        ws.is_websocket = true;
        httpd_register_uri_handler(s_http, &index);
        httpd_register_uri_handler(s_http, &photo);
        httpd_register_uri_handler(s_http, &hdr);
        httpd_register_uri_handler(s_http, &ws);
    }

    httpd_config_t s = HTTPD_DEFAULT_CONFIG();
    s.server_port = 81;
    s.ctrl_port = 32769;
    s.core_id = 0;
    s.stack_size = 8192;
    if (httpd_start(&s_stream, &s) == ESP_OK) {
        httpd_uri_t stream = { "/stream", HTTP_GET, onStream, nullptr };
        httpd_register_uri_handler(s_stream, &stream);
    }
}

void webBroadcast(const char* json, size_t len) {
    if (!s_http) return;
    size_t n = 8;
    int fds[8];
    if (httpd_get_client_list(s_http, &n, fds) != ESP_OK) return;
    httpd_ws_frame_t f = {};
    f.type = HTTPD_WS_TYPE_TEXT;
    f.payload = (uint8_t*)json;
    f.len = len;
    int clients = 0;
    for (size_t i = 0; i < n; i++) {
        if (httpd_ws_get_fd_info(s_http, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) continue;
        httpd_ws_send_frame_async(s_http, fds[i], &f);
        clients++;
    }
    s_clients = clients;
}

int webClients() { return s_clients; }
