// Freckles' animated face. Drawn off-screen into a PSRAM sprite by its own
// task (~30 fps) and pushed to the CoreS3 display in one go, so the face never
// flickers while the main loop is busy listening or talking to the server.
#include "face.h"
#include <M5StackChan.h>
#include <mutex>

namespace face {

namespace {

struct State {
    Emotion emotion  = Emotion::Neutral;
    Overlay overlay  = Overlay::None;
    float mouth      = 0;
    float gazeX      = 0;
    float gazeY      = 0;
    float level      = 0;
    String caption;
    uint32_t captionSince = 0;
    bool wifiOk      = true;
    int battery      = 100;
    bool charging    = false;
    String setupTitle;
    String setupBody;
};

std::mutex g_mutex;
State g_state;
M5Canvas* g_canvas = nullptr;

constexpr int W = 320;
constexpr int H = 240;

// Palette
uint16_t C_BG, C_FACE, C_FRECKLE, C_BLUSH, C_LISTEN, C_THINK, C_ERROR, C_CAPTION_BG, C_HEART, C_DIM;

// Blink + smoothing state (only touched by the face task)
uint32_t nextBlinkAt = 0;
uint32_t blinkStart  = 0;
float smoothMouth    = 0;
float smoothGX       = 0;
float smoothGY       = 0;

float blinkOpenness(uint32_t now)
{
    if (now >= nextBlinkAt && blinkStart == 0) {
        blinkStart = now;
    }
    if (blinkStart) {
        uint32_t t = now - blinkStart;
        if (t > 160) {
            blinkStart  = 0;
            nextBlinkAt = now + 2200 + random(0, 4200);
            return 1.0f;
        }
        float p = t / 160.0f;  // 0..1
        return fabsf(1.0f - 2.0f * p);
    }
    return 1.0f;
}

void drawFreckles(M5Canvas& c, int cx, int cy)
{
    // Freckles' signature: three little dots on each cheek.
    const int dx[3] = {-10, 0, 9};
    const int dy[3] = {2, -3, 3};
    for (int i = 0; i < 3; i++) {
        c.fillCircle(cx + dx[i], cy + dy[i], 3, C_FRECKLE);
    }
}

void drawHeart(M5Canvas& c, int cx, int cy, int r)
{
    c.fillCircle(cx - r / 2, cy - r / 4, r / 2 + 1, C_HEART);
    c.fillCircle(cx + r / 2, cy - r / 4, r / 2 + 1, C_HEART);
    c.fillTriangle(cx - r - 1, cy - r / 6, cx + r + 1, cy - r / 6, cx, cy + r, C_HEART);
}

void drawEye(M5Canvas& c, int cx, int cy, int r, float open, Emotion e, bool left)
{
    switch (e) {
        case Emotion::Happy:
            // Upside-down crescent "^"
            c.fillCircle(cx, cy, r, C_FACE);
            c.fillCircle(cx, cy + r * 0.65f, r + 2, C_BG);
            return;
        case Emotion::Love:
            drawHeart(c, cx, cy, r);
            return;
        case Emotion::Sleepy:
            c.fillRoundRect(cx - r, cy - 2, r * 2, 5, 2, C_FACE);
            return;
        default:
            break;
    }

    float rr = r;
    if (e == Emotion::Surprised) rr = r * 1.25f;
    int ry = max(2, (int)(rr * open));
    c.fillEllipse(cx, cy, (int)rr, ry, C_FACE);

    // Brows
    int bx0 = cx - r, bx1 = cx + r, by = cy - r - 12;
    if (e == Emotion::Sad) {
        // inner end raised
        if (left) c.drawWideLine(bx0, by + 6, bx1, by - 4, 3, C_FACE);
        else      c.drawWideLine(bx0, by - 4, bx1, by + 6, 3, C_FACE);
    } else if (e == Emotion::Angry) {
        // inner end lowered
        if (left) c.drawWideLine(bx0, by - 4, bx1, by + 8, 3, C_FACE);
        else      c.drawWideLine(bx0, by + 8, bx1, by - 4, 3, C_FACE);
    } else if (e == Emotion::Thinking) {
        if (left) c.drawWideLine(bx0, by, bx1, by - 4, 3, C_FACE);
    }
}

void drawMouth(M5Canvas& c, int cx, int cy, float open, Emotion e)
{
    if (open > 0.06f) {
        int w = 56 - (int)(18 * open);
        int h = 8 + (int)(38 * open);
        c.fillRoundRect(cx - w / 2, cy - h / 2, w, h, min(w, h) / 2, C_FACE);
        return;
    }
    switch (e) {
        case Emotion::Happy:
        case Emotion::Love:
            c.fillCircle(cx, cy - 10, 24, C_FACE);
            c.fillCircle(cx, cy - 18, 26, C_BG);
            break;
        case Emotion::Sad:
            c.fillCircle(cx, cy + 16, 20, C_FACE);
            c.fillCircle(cx, cy + 23, 22, C_BG);
            break;
        case Emotion::Surprised:
            c.fillEllipse(cx, cy, 11, 14, C_FACE);
            c.fillEllipse(cx, cy, 6, 9, C_BG);
            break;
        case Emotion::Thinking:
            c.fillRoundRect(cx - 6, cy - 3, 30, 6, 3, C_FACE);
            break;
        case Emotion::Angry:
            c.fillRoundRect(cx - 22, cy - 3, 44, 6, 3, C_FACE);
            break;
        case Emotion::Sleepy:
            c.fillRoundRect(cx - 10, cy - 2, 20, 5, 2, C_FACE);
            break;
        default:
            c.fillRoundRect(cx - 24, cy - 3, 48, 6, 3, C_FACE);
            break;
    }
}

void drawStatus(M5Canvas& c, const State& s)
{
    if (!s.wifiOk) {
        // little "no wifi" mark top-left
        c.drawArc(22, 26, 14, 12, 225, 315, C_ERROR);
        c.drawArc(22, 26, 8, 6, 225, 315, C_ERROR);
        c.fillCircle(22, 26, 2, C_ERROR);
        c.drawWideLine(10, 12, 34, 30, 1, C_ERROR);
    }
    if (s.charging || s.battery < 25) {
        uint16_t col = s.charging ? C_LISTEN : (s.battery < 10 ? C_ERROR : C_FRECKLE);
        c.drawRect(278, 12, 30, 14, col);
        c.fillRect(308, 16, 3, 6, col);
        int w = max(2, 26 * s.battery / 100);
        c.fillRect(280, 14, w, 10, col);
    }
}

void drawListening(M5Canvas& c, float level, uint32_t now)
{
    // five bars that bounce with the microphone level
    const int n = 5;
    for (int i = 0; i < n; i++) {
        float phase = sinf(now / 120.0f + i * 1.3f) * 0.5f + 0.5f;
        float l     = min(1.0f, level * (0.6f + 0.8f * phase) + 0.08f);
        int h       = 4 + (int)(l * 26);
        int x       = W / 2 - 40 + i * 20;
        c.fillRoundRect(x - 4, 222 - h, 8, h, 4, C_LISTEN);
    }
}

void drawThinking(M5Canvas& c, uint32_t now)
{
    // three dots in a little thought bubble, top-right
    c.fillCircle(232, 54, 4, C_THINK);
    c.fillCircle(244, 40, 6, C_THINK);
    c.fillRoundRect(252, 8, 58, 28, 14, C_THINK);
    for (int i = 0; i < 3; i++) {
        float b = sinf(now / 150.0f - i * 0.9f);
        int y   = 22 - (b > 0 ? (int)(b * 4) : 0);
        c.fillCircle(267 + i * 14, y, 4, C_BG);
    }
}

std::vector<String> wrap(M5Canvas& c, const String& text, int maxW)
{
    std::vector<String> lines;
    String line, word;
    auto flushWord = [&]() {
        if (word.isEmpty()) return;
        String trial = line.isEmpty() ? word : line + " " + word;
        if (c.textWidth(trial.c_str()) > maxW && !line.isEmpty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word = "";
    };
    for (size_t i = 0; i < text.length(); i++) {
        char ch = text[i];
        if (ch == ' ' || ch == '\n') {
            flushWord();
            if (ch == '\n' && !line.isEmpty()) {
                lines.push_back(line);
                line = "";
            }
        } else {
            word += ch;
        }
    }
    flushWord();
    if (!line.isEmpty()) lines.push_back(line);
    return lines;
}

void drawCaption(M5Canvas& c, const State& s, uint32_t now)
{
    c.setFont(&fonts::FreeSans9pt7b);
    c.setTextDatum(textdatum_t::middle_center);
    auto lines = wrap(c, s.caption, W - 36);
    if (lines.empty()) return;
    const size_t perPage = 2;
    size_t pages = (lines.size() + perPage - 1) / perPage;
    size_t page  = ((now - s.captionSince) / 2800) % pages;

    c.fillRoundRect(8, 188, W - 16, 48, 12, C_CAPTION_BG);
    for (size_t i = 0; i < perPage; i++) {
        size_t idx = page * perPage + i;
        if (idx >= lines.size()) break;
        int y = (lines.size() - page * perPage == 1) ? 212 : 201 + i * 21;
        c.setTextColor(C_FACE);
        c.drawString(lines[idx].c_str(), W / 2, y);
    }
}

void drawSetup(M5Canvas& c, const State& s)
{
    c.setTextDatum(textdatum_t::top_center);
    c.setFont(&fonts::FreeSansBold12pt7b);
    c.setTextColor(C_FACE);
    c.drawString(s.setupTitle.c_str(), W / 2, 150);
    c.setFont(&fonts::FreeSans9pt7b);
    c.setTextColor(C_DIM);
    auto lines = wrap(c, s.setupBody, W - 24);
    for (size_t i = 0; i < lines.size() && i < 3; i++) {
        c.drawString(lines[i].c_str(), W / 2, 178 + i * 19);
    }
}

void render(uint32_t now)
{
    State s;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        s = g_state;
    }
    M5Canvas& c = *g_canvas;

    smoothMouth += (s.mouth - smoothMouth) * 0.55f;
    smoothGX += (s.gazeX - smoothGX) * 0.18f;
    smoothGY += (s.gazeY - smoothGY) * 0.18f;

    float open = blinkOpenness(now);
    if (s.emotion == Emotion::Sleepy) open = 0.15f;
    if (s.emotion == Emotion::Surprised) open = max(open, 0.6f);

    // gentle breathing bob
    int bob = (int)(sinf(now / 640.0f) * 2.0f);
    int gx  = (int)(smoothGX * 10);
    int gy  = (int)(smoothGY * 8);

    bool setup   = s.overlay == Overlay::Setup;
    int faceY    = setup ? -32 : 0;
    int eyeY     = 92 + bob + gy + faceY;
    int mouthY   = 152 + bob + gy / 2 + faceY;
    int eyeL     = W / 2 - 72 + gx;
    int eyeR     = W / 2 + 72 + gx;

    c.fillScreen(C_BG);

    // blush / freckles under the eyes
    if (s.emotion == Emotion::Happy || s.emotion == Emotion::Love) {
        c.fillEllipse(eyeL - 6, eyeY + 40, 22, 11, C_BLUSH);
        c.fillEllipse(eyeR + 6, eyeY + 40, 22, 11, C_BLUSH);
    }
    drawFreckles(c, eyeL - 6, eyeY + 40);
    drawFreckles(c, eyeR + 6, eyeY + 40);

    drawEye(c, eyeL, eyeY, 22, open, s.emotion, true);
    drawEye(c, eyeR, eyeY, 22, open, s.emotion, false);
    drawMouth(c, W / 2 + gx / 2, mouthY, smoothMouth, s.emotion);

    if (s.emotion == Emotion::Sleepy) {
        c.setFont(&fonts::FreeSansBold9pt7b);
        c.setTextColor(C_DIM);
        int z = (now / 600) % 3;
        for (int i = 0; i <= z; i++) c.drawString("z", 240 + i * 14, 50 - i * 14);
    }

    switch (s.overlay) {
        case Overlay::Listening: drawListening(c, s.level, now); break;
        case Overlay::Thinking:  drawThinking(c, now); break;
        case Overlay::Setup:     drawSetup(c, s); break;
        case Overlay::Error:
            c.fillCircle(26, 26, 14, C_ERROR);
            c.fillRect(24, 16, 4, 12, C_BG);
            c.fillRect(24, 31, 4, 4, C_BG);
            break;
        default: break;
    }

    if (!setup && !s.caption.isEmpty() && s.overlay != Overlay::Listening) {
        drawCaption(c, s, now);
    }
    drawStatus(c, s);

    c.pushSprite(&M5.Display, 0, 0);
}

void task(void*)
{
    for (;;) {
        uint32_t t0 = millis();
        render(t0);
        uint32_t dt = millis() - t0;
        vTaskDelay(pdMS_TO_TICKS(dt < 33 ? 33 - dt : 1));
    }
}

}  // namespace

void begin()
{
    auto& d = M5.Display;
    d.setRotation(1);
    C_BG         = d.color565(22, 26, 48);
    C_FACE       = d.color565(255, 250, 240);
    C_FRECKLE    = d.color565(232, 140, 80);
    C_BLUSH      = d.color565(120, 60, 90);
    C_LISTEN     = d.color565(90, 190, 255);
    C_THINK      = d.color565(180, 150, 255);
    C_ERROR      = d.color565(255, 90, 90);
    C_CAPTION_BG = d.color565(44, 50, 84);
    C_HEART      = d.color565(255, 92, 140);
    C_DIM        = d.color565(170, 175, 200);

    g_canvas = new M5Canvas(&d);
    g_canvas->setPsram(true);
    g_canvas->setColorDepth(16);
    g_canvas->createSprite(W, H);

    nextBlinkAt = millis() + 1500;
    xTaskCreatePinnedToCore(task, "face", 6144, nullptr, 2, nullptr, 1);
}

void setEmotion(Emotion e)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.emotion = e;
}

Emotion emotion()
{
    std::lock_guard<std::mutex> lk(g_mutex);
    return g_state.emotion;
}

void setMouth(float open)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.mouth = constrain(open, 0.0f, 1.0f);
}

void setGaze(float x, float y)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.gazeX = constrain(x, -1.0f, 1.0f);
    g_state.gazeY = constrain(y, -1.0f, 1.0f);
}

void setOverlay(Overlay o)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.overlay = o;
}

void setLevel(float level)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.level = constrain(level, 0.0f, 1.0f);
}

void setCaption(const String& text)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    if (g_state.caption != text) {
        g_state.caption      = text;
        g_state.captionSince = millis();
    }
}

void setStatus(bool wifiOk, int batteryPercent, bool charging)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.wifiOk   = wifiOk;
    g_state.battery  = batteryPercent;
    g_state.charging = charging;
}

void setSetupText(const String& title, const String& body)
{
    std::lock_guard<std::mutex> lk(g_mutex);
    g_state.setupTitle = title;
    g_state.setupBody  = body;
}

void setBrightness(uint8_t b)
{
    M5.Display.setBrightness(b);
}

Emotion parseEmotion(const String& raw)
{
    String n = raw;
    n.toLowerCase();
    n.trim();
    if (n == "happy" || n == "joy" || n == "excited") return Emotion::Happy;
    if (n == "sad") return Emotion::Sad;
    if (n == "surprised" || n == "wow") return Emotion::Surprised;
    if (n == "sleepy" || n == "tired") return Emotion::Sleepy;
    if (n == "thinking" || n == "curious") return Emotion::Thinking;
    if (n == "angry" || n == "grumpy") return Emotion::Angry;
    if (n == "love") return Emotion::Love;
    return Emotion::Neutral;
}

}  // namespace face
