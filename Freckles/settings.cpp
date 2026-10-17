#include "settings.h"
#include "config.h"
#include <Preferences.h>
#include <WiFi.h>

namespace settings {

static const char* NS = "freckles";

void load(FrecklesSettings& s)
{
    Preferences p;
    p.begin(NS, true);
    s.serverUrl  = p.getString("server", FRECKLES_DEFAULT_SERVER);
    s.token      = p.getString("token", FRECKLES_DEFAULT_TOKEN);
    s.listenMode = p.getString("mode", FRECKLES_DEFAULT_LISTEN_MODE);
    s.volume     = p.getUChar("volume", DEFAULT_VOLUME);
    p.end();

    s.serverUrl.trim();
    while (s.serverUrl.endsWith("/")) s.serverUrl.remove(s.serverUrl.length() - 1);
    if (s.listenMode != "always") s.listenMode = "tap";
}

void save(const FrecklesSettings& s)
{
    Preferences p;
    p.begin(NS, false);
    p.putString("server", s.serverUrl);
    p.putString("token", s.token);
    p.putString("mode", s.listenMode);
    p.putUChar("volume", s.volume);
    p.end();
}

String deviceId()
{
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "freckles-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(buf);
}

}  // namespace settings
