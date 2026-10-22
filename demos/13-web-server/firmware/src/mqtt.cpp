#include "mqtt.h"
#include "shared.h"
#include "face.h"
#include <Preferences.h>
#include <mqtt_client.h>

static esp_mqtt_client_handle_t s_client = nullptr;
static volatile int s_status = 0;            // index into STATUS_NAMES
static const char* const STATUS_NAMES[] = { "off", "connecting", "connected", "error" };
static char s_host[64] = "", s_broker[80] = "", s_uri[96] = "";
static int s_port = 1883;
static char s_base[32], s_id[8];             // "stackchan/4F48", "4F48"
static char s_topicState[48], s_topicCmd[48], s_topicAvail[48];

// Home Assistant MQTT discovery: one retained config message per entity. The robot shows
// up as a device with sensors, a mood selector, the head angles, a screen text and a button.
static void publishDiscovery() {
    char dev[200], topic[96], payload[640];
    snprintf(dev, sizeof(dev),
             "\"dev\":{\"ids\":[\"%s\"],\"name\":\"Stack-chan %s\",\"mf\":\"M5Stack\",\"mdl\":\"Stack-chan K151\"},"
             "\"avty_t\":\"%s\"", robotName(), s_id, s_topicAvail);
    struct Sensor { const char* key; const char* name; const char* field; const char* unit; const char* cls; };
    static const Sensor SENSORS[] = {
        { "battery", "Battery", "pct", "%", "battery" },
        { "current", "Battery current", "ma", "mA", "current" },
        { "light", "Light", "lux", "lx", "illuminance" },
        { "proximity", "Proximity", "prox", "", nullptr },
        { "chip_temp", "Chip temperature", "temp", "\xC2\xB0" "C", "temperature" },
        { "wifi", "Wi-Fi signal", "rssi", "dBm", "signal_strength" },
        { "pitch", "Head pitch (IMU)", "pitch", "\xC2\xB0", nullptr },
    };
    for (const Sensor& s : SENSORS) {
        char extra[80] = "";
        if (*s.unit) snprintf(extra, sizeof(extra), ",\"unit_of_meas\":\"%s\"", s.unit);
        if (s.cls) snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), ",\"dev_cla\":\"%s\"", s.cls);
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", robotName(), s.key);
        int n = snprintf(payload, sizeof(payload),
                         "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s\",\"val_tpl\":\"{{ value_json.%s }}\"%s,%s}",
                         s.name, robotName(), s.key, s_topicState, s.field, extra, dev);
        esp_mqtt_client_publish(s_client, topic, payload, n, 1, 1);
    }

    char options[120] = "";
    for (int i = 0; i < MOOD_COUNT; i++)
        snprintf(options + strlen(options), sizeof(options) - strlen(options), "%s\"%s\"", i ? "," : "", MOOD_NAMES[i]);
    snprintf(topic, sizeof(topic), "homeassistant/select/%s/mood/config", robotName());
    int n = snprintf(payload, sizeof(payload),
                     "{\"name\":\"Mood\",\"uniq_id\":\"%s_mood\",\"stat_t\":\"%s\",\"val_tpl\":\"{{ value_json.mood }}\","
                     "\"cmd_t\":\"%s\",\"cmd_tpl\":\"{\\\"cmd\\\":\\\"mood\\\",\\\"mood\\\":\\\"{{ value }}\\\"}\",\"options\":[%s],%s}",
                     robotName(), s_topicState, s_topicCmd, options, dev);
    esp_mqtt_client_publish(s_client, topic, payload, n, 1, 1);

    struct Number { const char* key; const char* name; int lo, hi; };
    static const Number NUMBERS[] = { { "pan", "Head pan", -180, 180 }, { "tilt", "Head tilt", -7, 73 } };
    for (const Number& x : NUMBERS) {
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/%s/config", robotName(), x.key);
        n = snprintf(payload, sizeof(payload),
                     "{\"name\":\"%s\",\"uniq_id\":\"%s_%s\",\"stat_t\":\"%s\",\"val_tpl\":\"{{ value_json.%s | round(0) }}\","
                     "\"cmd_t\":\"%s\",\"cmd_tpl\":\"{\\\"cmd\\\":\\\"head\\\",\\\"%s\\\":{{ value }}}\","
                     "\"min\":%d,\"max\":%d,\"step\":5,\"unit_of_meas\":\"\xC2\xB0\",%s}",
                     x.name, robotName(), x.key, s_topicState, x.key, s_topicCmd, x.key, x.lo, x.hi, dev);
        esp_mqtt_client_publish(s_client, topic, payload, n, 1, 1);
    }

    snprintf(topic, sizeof(topic), "homeassistant/text/%s/screen/config", robotName());
    n = snprintf(payload, sizeof(payload),
                 "{\"name\":\"Screen text\",\"uniq_id\":\"%s_text\",\"stat_t\":\"%s\",\"val_tpl\":\"{{ value_json.text }}\","
                 "\"cmd_t\":\"%s\",\"cmd_tpl\":\"{\\\"cmd\\\":\\\"text\\\",\\\"text\\\":\\\"{{ value }}\\\"}\",\"max\":40,%s}",
                 robotName(), s_topicState, s_topicCmd, dev);
    esp_mqtt_client_publish(s_client, topic, payload, n, 1, 1);

    snprintf(topic, sizeof(topic), "homeassistant/button/%s/center/config", robotName());
    n = snprintf(payload, sizeof(payload),
                 "{\"name\":\"Center head\",\"uniq_id\":\"%s_center\",\"cmd_t\":\"%s\",\"payload_press\":\"{\\\"cmd\\\":\\\"center\\\"}\",%s}",
                 robotName(), s_topicCmd, dev);
    esp_mqtt_client_publish(s_client, topic, payload, n, 1, 1);
}

static void onEvent(void*, esp_event_base_t, int32_t id, void* data) {
    auto* e = (esp_mqtt_event_handle_t)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_status = 2;
        Serial.printf("[MQTT] connected to %s, topics %s/...\n", s_broker, s_base);
        esp_mqtt_client_publish(s_client, s_topicAvail, "online", 0, 1, 1);
        esp_mqtt_client_subscribe(s_client, s_topicCmd, 1);
        publishDiscovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        if (s_status == 2) Serial.println("[MQTT] disconnected, retrying...");
        s_status = 1;
        break;
    case MQTT_EVENT_ERROR:
        s_status = 3;
        break;
    case MQTT_EVENT_DATA:
        // Small commands only: they arrive in one piece.
        if (e->current_data_offset == 0 && e->data_len == e->total_data_len &&
            e->topic_len == (int)strlen(s_topicCmd) && !strncmp(e->topic, s_topicCmd, e->topic_len))
            commandSubmit(e->data, e->data_len);
        break;
    default:
        break;
    }
}

static void connect() {
    if (s_client) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
    }
    s_status = 0;
    s_broker[0] = 0;
    if (!s_host[0]) return;
    snprintf(s_broker, sizeof(s_broker), "%s:%d", s_host, s_port);
    snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%d", s_host, s_port);
    esp_mqtt_client_config_t cfg = {};
    cfg.uri = s_uri;
    cfg.client_id = robotName();
    cfg.lwt_topic = s_topicAvail;    // the broker says "offline" for us if we vanish
    cfg.lwt_msg = "offline";
    cfg.lwt_qos = 1;
    cfg.lwt_retain = 1;
    cfg.keepalive = 15;
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, onEvent, nullptr);
    s_status = 1;
    esp_mqtt_client_start(s_client);
    Serial.printf("[MQTT] connecting to %s...\n", s_broker);
}

void mqttStart() {
    strncpy(s_id, robotName() + strlen("stackchan-"), sizeof(s_id) - 1);
    snprintf(s_base, sizeof(s_base), "stackchan/%s", s_id);
    snprintf(s_topicState, sizeof(s_topicState), "%s/state", s_base);
    snprintf(s_topicCmd, sizeof(s_topicCmd), "%s/cmd", s_base);
    snprintf(s_topicAvail, sizeof(s_topicAvail), "%s/availability", s_base);
    Preferences p;
    p.begin("demo13", false);   // read-write: creates the namespace the first time, without an error
    strncpy(s_host, p.getString("mqtt_host", "").c_str(), sizeof(s_host) - 1);
    s_port = p.getInt("mqtt_port", 1883);
    p.end();
    connect();
}

void mqttConfigure(const char* host, int port) {
    strncpy(s_host, host ? host : "", sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = 0;
    s_port = port > 0 && port < 65536 ? port : 1883;
    Preferences p;
    p.begin("demo13", false);
    p.putString("mqtt_host", s_host);
    p.putInt("mqtt_port", s_port);
    p.end();
    connect();
}

void mqttPublishState(const char* json, size_t len) {
    if (s_client && s_status == 2) esp_mqtt_client_publish(s_client, s_topicState, json, len, 0, 0);
}

const char* mqttStatus() { return STATUS_NAMES[s_status]; }
const char* mqttBroker() { return s_broker; }
