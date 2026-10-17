#include "lights.h"
#include <M5StackChan.h>

namespace lights {

namespace {
LightMode g_mode  = LightMode::Off;
float g_level     = 0;
uint32_t g_last   = 0;
uint32_t g_since  = 0;
bool g_dirtyOff   = false;

void hsv(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b)
{
    h = fmodf(h, 1.0f) * 6.0f;
    int i   = (int)h;
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    float rr, gg, bb;
    switch (i) {
        case 0: rr = v; gg = t; bb = p; break;
        case 1: rr = q; gg = v; bb = p; break;
        case 2: rr = p; gg = v; bb = t; break;
        case 3: rr = p; gg = q; bb = v; break;
        case 4: rr = t; gg = p; bb = v; break;
        default: rr = v; gg = p; bb = q; break;
    }
    r = rr * 255;
    g = gg * 255;
    b = bb * 255;
}

void all(uint8_t r, uint8_t g, uint8_t b)
{
    for (int i = 0; i < 12; i++) M5StackChan.setRgbColor(i, r, g, b);
}
}  // namespace

void setMode(LightMode m)
{
    if (m != g_mode) {
        g_mode   = m;
        g_since  = millis();
        g_dirtyOff = true;
    }
}

void setLevel(float level)
{
    g_level = constrain(level, 0.0f, 1.0f);
}

void update()
{
    uint32_t now = millis();
    if (now - g_last < 40) return;  // ~25 fps is plenty for LEDs
    g_last  = now;
    float t = (now - g_since) / 1000.0f;

    switch (g_mode) {
        case LightMode::Off:
            if (!g_dirtyOff) return;
            all(0, 0, 0);
            g_dirtyOff = false;
            break;
        case LightMode::Idle: {
            // slow warm "breathing" glow
            float v = 0.04f + 0.05f * (0.5f + 0.5f * sinf(t * 1.6f));
            all(255 * v, 150 * v, 80 * v);
            break;
        }
        case LightMode::Sleeping: {
            float v = 0.015f + 0.015f * (0.5f + 0.5f * sinf(t * 0.8f));
            all(80 * v, 90 * v, 255 * v);
            break;
        }
        case LightMode::Listening: {
            // blue, both sides fill up with the voice level
            int lit = 1 + (int)(g_level * 5.5f);
            for (int i = 0; i < 6; i++) {
                float v = i < lit ? 0.5f : 0.05f;
                M5StackChan.setRgbColor(i, 30 * v, 140 * v, 255 * v);
                M5StackChan.setRgbColor(6 + i, 30 * v, 140 * v, 255 * v);
            }
            break;
        }
        case LightMode::Thinking: {
            // a purple dot chasing around
            int head = (int)(t * 14) % 12;
            for (int i = 0; i < 12; i++) {
                int d   = (head - i + 12) % 12;
                float v = d == 0 ? 0.6f : d == 1 ? 0.25f : d == 2 ? 0.08f : 0.01f;
                M5StackChan.setRgbColor(i, 170 * v, 90 * v, 255 * v);
            }
            break;
        }
        case LightMode::Speaking: {
            float v = 0.06f + g_level * 0.5f;
            all(60 * v, 255 * v, 140 * v);
            break;
        }
        case LightMode::Party: {
            for (int i = 0; i < 12; i++) {
                uint8_t r, g, b;
                hsv(t * 0.6f + i / 12.0f, 0.8f, 0.45f, r, g, b);
                M5StackChan.setRgbColor(i, r, g, b);
            }
            break;
        }
        case LightMode::Error: {
            float v = ((int)(t * 3) % 2) ? 0.4f : 0.05f;
            all(255 * v, 30 * v, 30 * v);
            break;
        }
        case LightMode::Setup: {
            float v = 0.1f + 0.2f * (0.5f + 0.5f * sinf(t * 3));
            all(255 * v, 200 * v, 40 * v);
            break;
        }
    }
    M5StackChan.refreshRgb();
}

}  // namespace lights
