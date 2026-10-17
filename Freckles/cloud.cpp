#include "cloud.h"
#include "config.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>

namespace cloud {

namespace {

FrecklesSettings g_cfg;
String g_deviceId;

std::atomic<bool> g_busy{false};
std::atomic<bool> g_ready{false};
Reply g_reply;

const uint8_t* g_reqData = nullptr;
size_t g_reqLen          = 0;
bool g_reqWake           = false;

// Collects the response body into PSRAM, growing as needed.
class PsramSink : public Stream {
public:
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    uint8_t* buf = nullptr;
    size_t len = 0, cap = 0;
    bool overflow = false;

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* data, size_t n) override
    {
        if (len + n > MAX_REPLY_AUDIO_BYTES) {
            overflow = true;
            return 0;
        }
        if (len + n > cap) {
            size_t ncap = max(cap * 2, len + n + 64 * 1024);
            ncap        = min(ncap, (size_t)MAX_REPLY_AUDIO_BYTES);
            auto* nb    = (uint8_t*)heap_caps_realloc(buf, ncap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!nb) {
                overflow = true;
                return 0;
            }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + len, data, n);
        len += n;
        return n;
    }
};

String urlDecode(const String& s)
{
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.length()) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            out += (char)strtol(hex, nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

bool beginRequest(HTTPClient& http, WiFiClient*& client, const String& url)
{
    if (url.startsWith("https://")) {
        auto* sc = new WiFiClientSecure();
        if (strlen(FRECKLES_ROOT_CA) > 0) {
            sc->setCACert(FRECKLES_ROOT_CA);
        } else {
            sc->setInsecure();
            log_w("HTTPS certificate is not verified - set FRECKLES_ROOT_CA in config.h");
        }
        client = sc;
    } else {
        client = new WiFiClient();
    }
    if (!http.begin(*client, url)) return false;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setConnectTimeout(8000);
    http.setReuse(false);
    http.addHeader("X-Freckles-Device", g_deviceId);
    http.addHeader("X-Freckles-Firmware", FRECKLES_FIRMWARE_VERSION);
    if (!g_cfg.token.isEmpty()) http.addHeader("Authorization", "Bearer " + g_cfg.token);
    return true;
}

void talkTask(void*)
{
    Reply r;
    HTTPClient http;
    WiFiClient* client = nullptr;

    if (WiFi.status() != WL_CONNECTED) {
        r.status = -1;
        r.error  = "No Wi-Fi";
    } else if (!beginRequest(http, client, g_cfg.serverUrl + "/v1/talk")) {
        r.status = -2;
        r.error  = "Bad server address";
    } else {
        const char* keys[] = {"Content-Type", "X-Freckles-Text", "X-Freckles-Heard", "X-Freckles-Emotion",
                              "X-Freckles-Gesture", "X-Freckles-Continue", "X-Freckles-Provider", "X-Freckles-Error"};
        http.collectHeaders(keys, sizeof(keys) / sizeof(keys[0]));
        http.addHeader("Content-Type", "audio/wav");
        http.addHeader("Accept", "audio/wav, application/json");
        http.addHeader("X-Freckles-Mode", g_reqWake ? "wake" : "tap");

        int code = http.sendRequest("POST", const_cast<uint8_t*>(g_reqData), g_reqLen);
        r.status = code;
        if (code <= 0) {
            r.error = "Can't reach server";
        } else {
            r.text          = urlDecode(http.header("X-Freckles-Text"));
            r.heard         = urlDecode(http.header("X-Freckles-Heard"));
            r.emotion       = http.header("X-Freckles-Emotion");
            r.gesture       = http.header("X-Freckles-Gesture");
            r.provider      = http.header("X-Freckles-Provider");
            r.keepListening = http.header("X-Freckles-Continue") == "1";
            String ctype    = http.header("Content-Type");

            if (code == 200) {
                PsramSink sink;
                int n = http.writeToStream(&sink);
                if (n < 0 && sink.len == 0) {
                    r.error = "Reply cut off";
                } else if (sink.overflow) {
                    r.error = "Reply too long";
                }
                r.body    = sink.buf;
                r.bodyLen = sink.len;

                if (ctype.startsWith("application/json") && r.body) {
                    JsonDocument doc;
                    if (!deserializeJson(doc, (const char*)r.body, r.bodyLen)) {
                        if (r.text.isEmpty()) r.text = doc["text"] | "";
                        if (r.heard.isEmpty()) r.heard = doc["heard"] | "";
                        if (r.emotion.isEmpty()) r.emotion = doc["emotion"] | "";
                        if (r.gesture.isEmpty()) r.gesture = doc["gesture"] | "";
                        r.keepListening = r.keepListening || (doc["continue"] | false);
                    }
                } else if (r.body && !wav::parse(r.body, r.bodyLen, r.pcm)) {
                    r.error = "Unsupported audio";
                }
            } else if (code == 204) {
                // wake mode: nobody said "Hi Freckles", nothing to do
            } else if (code == 401 || code == 403) {
                r.error = "Server rejected device token";
            } else {
                String e = urlDecode(http.header("X-Freckles-Error"));
                r.error  = e.isEmpty() ? "Server error " + String(code) : e;
            }
        }
        http.end();
    }
    delete client;

    g_reply = std::move(r);
    g_ready = true;
    g_busy  = false;
    vTaskDelete(nullptr);
}

}  // namespace

void configure(const FrecklesSettings& s)
{
    g_cfg      = s;
    g_deviceId = settings::deviceId();
}

bool isConfigured()
{
    return g_cfg.serverUrl.startsWith("http://") || g_cfg.serverUrl.startsWith("https://");
}

bool startTalk(const uint8_t* data, size_t len, bool wakeMode)
{
    if (g_busy) return false;
    releaseReply();
    g_reqData = data;
    g_reqLen  = len;
    g_reqWake = wakeMode;
    g_ready   = false;
    g_busy    = true;
    // TLS needs a generous stack
    if (xTaskCreatePinnedToCore(talkTask, "cloud", 16384, nullptr, 3, nullptr, 0) != pdPASS) {
        g_busy = false;
        return false;
    }
    return true;
}

bool isBusy()
{
    return g_busy;
}

bool hasReply()
{
    return g_ready;
}

Reply& reply()
{
    return g_reply;
}

void releaseReply()
{
    if (g_reply.body) heap_caps_free(g_reply.body);
    g_reply = Reply();
    g_ready = false;
}

int hello(String& greeting)
{
    if (!isConfigured() || WiFi.status() != WL_CONNECTED) return -1;
    HTTPClient http;
    WiFiClient* client = nullptr;
    int code           = -2;
    if (beginRequest(http, client, g_cfg.serverUrl + "/v1/hello")) {
        http.setTimeout(6000);
        code = http.GET();
        if (code == 200) {
            JsonDocument doc;
            if (!deserializeJson(doc, http.getString())) greeting = doc["greeting"] | "";
        }
        http.end();
    }
    delete client;
    return code;
}

}  // namespace cloud
