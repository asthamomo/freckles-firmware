/*
 * Freckles AI - desk robot firmware
 * Hardware: M5Stack StackChan (CoreS3 + 2 serial servos + 12 RGB LEDs + head touch panel)
 *
 * Freckles records what you say, sends it to the Freckles server, and the server
 * picks an AI (ChatGPT, Gemini, Claude, ...) to answer. The server replies with
 * speech audio plus a face/gesture hint, and Freckles says it out loud.
 *
 * Talk:   pat the top of the head, or tap the screen.
 * Pet:    swipe along the top of the head.
 * Setup:  hold the screen for 3 seconds (Wi-Fi, server address, listening mode).
 *
 * Built on the open-source StackChan BSP by M5Stack (MIT licence).
 */
#include <M5StackChan.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "audio.h"
#include "body.h"
#include "cloud.h"
#include "config.h"
#include "face.h"
#include "lights.h"
#include "settings.h"

enum class State : uint8_t { Idle, Listening, Thinking, Speaking, Sleeping };

static FrecklesSettings g_settings;
static State g_state          = State::Idle;
static uint32_t g_stateSince  = 0;
static uint32_t g_lastActive  = 0;
static uint32_t g_captionUntil = 0;
static uint32_t g_moodUntil   = 0;
static uint32_t g_textOnlyUntil = 0;
static bool g_wakeRequest     = false;  // current request came from always-on listening
static uint32_t g_screenDownAt = 0;

// ---------------------------------------------------------------------------
static void enterState(State s);
static void startListening(bool tapMode);
static void goIdle();
static void showCaption(const String& text, uint32_t ms);
static void runSetupPortal(bool forced);
static void updateStatusBar();
static void handleReply();

// ---------------------------------------------------------------------------
static void showCaption(const String& text, uint32_t ms)
{
    face::setCaption(text);
    g_captionUntil = ms ? millis() + ms : 0;
}

static void enterState(State s)
{
    g_state      = s;
    g_stateSince = millis();
    g_lastActive = millis();
}

static void goIdle()
{
    enterState(State::Idle);
    face::setOverlay(Overlay::None);
    face::setMouth(0);
    if (millis() > g_moodUntil) face::setEmotion(Emotion::Neutral);
    lights::setMode(LightMode::Idle);
    body::setIdleWander(!g_settings.alwaysListening());
    if (g_settings.alwaysListening() && cloud::isConfigured()) {
        audio::startListening(false);  // quietly wait for "Hi Freckles"
    }
}

static void startListening(bool tapMode)
{
    audio::stopPlaying();
    cloud::releaseReply();
    if (tapMode) {
        audio::chirpListen();
        face::setEmotion(Emotion::Neutral);
        face::setOverlay(Overlay::Listening);
        lights::setMode(LightMode::Listening);
        showCaption("", 0);
        body::setIdleWander(false);
        body::lookForward(500);
    }
    g_wakeRequest = !tapMode;
    audio::startListening(tapMode);
    enterState(State::Listening);
}

// ---------------------------------------------------------------------------
// Wi-Fi + server setup page (captive portal from the "Freckles-Setup" network)

static WiFiManagerParameter* p_server;
static WiFiManagerParameter* p_token;
static WiFiManagerParameter* p_mode;
static WiFiManagerParameter* p_volume;
static bool g_paramsSaved = false;

static void onSaveParams()
{
    g_paramsSaved = true;
}

static void runSetupPortal(bool forced)
{
    audio::stopListening();
    audio::stopPlaying();
    lights::setMode(LightMode::Setup);
    lights::update();

    String volume = String(g_settings.volume);
    p_server = new WiFiManagerParameter("server", "Freckles server URL", g_settings.serverUrl.c_str(), 200,
                                        "placeholder='https://your-server.com'");
    p_token  = new WiFiManagerParameter("token", "Device token (optional)", g_settings.token.c_str(), 128);
    p_mode   = new WiFiManagerParameter("mode", "Listening: tap or always", g_settings.listenMode.c_str(), 8);
    p_volume = new WiFiManagerParameter("volume", "Volume (0-255)", volume.c_str(), 4);

    WiFiManager wm;
    wm.setTitle("Freckles AI");
    wm.addParameter(p_server);
    wm.addParameter(p_token);
    wm.addParameter(p_mode);
    wm.addParameter(p_volume);
    wm.setSaveParamsCallback(onSaveParams);
    wm.setSaveConfigCallback(onSaveParams);
    wm.setBreakAfterConfig(true);
    wm.setConfigPortalTimeout(FRECKLES_SETUP_TIMEOUT_S);
    wm.setConnectTimeout(15);
    wm.setAPCallback([](WiFiManager*) {
        face::setEmotion(Emotion::Happy);
        face::setOverlay(Overlay::Setup);
        face::setSetupText("Let's get connected!",
                           "On your phone, join the Wi-Fi \"" FRECKLES_SETUP_AP_NAME "\", then open 192.168.4.1");
        audio::chirpHappy();
    });

    g_paramsSaved = false;
    bool ok;
    if (forced) {
        ok = wm.startConfigPortal(FRECKLES_SETUP_AP_NAME);
    } else {
        face::setOverlay(Overlay::Setup);
        face::setSetupText("Connecting...", "Joining Wi-Fi");
        ok = wm.autoConnect(FRECKLES_SETUP_AP_NAME);
    }

    if (g_paramsSaved) {
        g_settings.serverUrl  = p_server->getValue();
        g_settings.token      = p_token->getValue();
        g_settings.listenMode = p_mode->getValue();
        g_settings.listenMode.toLowerCase();
        g_settings.volume     = constrain(atoi(p_volume->getValue()), 0, 255);
        settings::save(g_settings);
        settings::load(g_settings);  // normalises values
        audio::setVolume(g_settings.volume);
    }
    delete p_server;
    delete p_token;
    delete p_mode;
    delete p_volume;

    cloud::configure(g_settings);
    face::setOverlay(Overlay::None);
    face::setEmotion(ok ? Emotion::Happy : Emotion::Sad);
    g_moodUntil = millis() + 3000;
    showCaption(ok ? "Connected!" : "Wi-Fi not connected. Hold my screen to try again.", 5000);
}

// ---------------------------------------------------------------------------

static void updateStatusBar()
{
    static uint32_t last = 0;
    if (millis() - last < 5000 && last) return;
    last = millis();
    int level     = M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
    face::setStatus(WiFi.status() == WL_CONNECTED, level < 0 ? 100 : level, charging);
}

static void handleReply()
{
    cloud::Reply& r = cloud::reply();
    if (!r.heard.isEmpty()) Serial.printf("[heard] %s\n", r.heard.c_str());
    if (!r.text.isEmpty()) Serial.printf("[reply%s%s] %s\n", r.provider.isEmpty() ? "" : " via ",
                                         r.provider.c_str(), r.text.c_str());

    if (r.status == 204) {  // wake mode: not talking to Freckles
        cloud::releaseReply();
        goIdle();
        return;
    }

    if (!r.error.isEmpty() && r.pcm.count == 0) {
        Serial.printf("[error] %s (status %d)\n", r.error.c_str(), r.status);
        cloud::releaseReply();
        audio::chirpError();
        goIdle();
        face::setEmotion(Emotion::Sad);
        face::setOverlay(Overlay::Error);
        lights::setMode(LightMode::Error);
        showCaption(r.error, 6000);
        g_moodUntil = millis() + 4000;
        return;
    }

    face::setOverlay(Overlay::None);
    face::setEmotion(r.emotion.isEmpty() ? Emotion::Happy : face::parseEmotion(r.emotion));
    Gesture g = body::parseGesture(r.gesture);
    if (g != Gesture::None) body::play(g);
    showCaption(r.text, 0);
    lights::setMode(LightMode::Speaking);

    if (r.pcm.count > 0) {
        audio::play(r.pcm.samples, r.pcm.count, r.pcm.rate);
        g_textOnlyUntil = 0;
    } else {
        // text-only reply: show it long enough to read
        g_textOnlyUntil = millis() + 1500 + r.text.length() * 55;
    }
    enterState(State::Speaking);
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    M5StackChan.begin();
    settings::load(g_settings);

    face::begin();
    face::setBrightness(160);
    face::setEmotion(Emotion::Sleepy);
    lights::setMode(LightMode::Idle);

    audio::begin(g_settings.volume);
    body::begin();

    WiFi.mode(WIFI_STA);
    runSetupPortal(false);
    if (WiFi.status() == WL_CONNECTED && !cloud::isConfigured()) {
        // Wi-Fi works but nobody told us where the AI server is yet
        runSetupPortal(true);
    }
    cloud::configure(g_settings);

    if (!cloud::isConfigured()) {
        face::setEmotion(Emotion::Thinking);
        showCaption("I need a server address. Hold my screen for 3 seconds.", 0);
    } else if (WiFi.status() == WL_CONNECTED) {
        String greeting;
        int code = cloud::hello(greeting);
        if (code == 200) {
            face::setEmotion(Emotion::Happy);
            body::play(Gesture::Wake);
            audio::chirpHappy();
            showCaption(greeting.isEmpty() ? "Hi! Pat my head to talk." : greeting, 6000);
        } else {
            face::setEmotion(Emotion::Sad);
            showCaption(code == 401 || code == 403 ? "My server doesn't recognise me. Check the token."
                                                   : "I can't reach my server right now.",
                        8000);
        }
        g_moodUntil = millis() + 3000;
    }

    Serial.printf("Freckles %s ready. Device id: %s\n", FRECKLES_FIRMWARE_VERSION, settings::deviceId().c_str());
    goIdle();
}

void loop()
{
    M5StackChan.update();
    lights::update();
    body::update();
    updateStatusBar();

    uint32_t now = millis();
    auto& head   = M5StackChan.TouchSensor;
    bool headTap = head.wasClicked();
    bool petted  = head.wasSwiped();

    // screen: tap = talk, hold 3 s = setup
    bool screenTap = false;
    auto t         = M5.Touch.getDetail();
    if (t.wasPressed()) g_screenDownAt = now;
    if (t.isPressed() && g_screenDownAt && now - g_screenDownAt > SCREEN_LONG_PRESS_MS) {
        g_screenDownAt = 0;
        runSetupPortal(true);
        goIdle();
        return;
    }
    if (t.wasReleased()) {
        if (g_screenDownAt && now - g_screenDownAt < 600) screenTap = true;
        g_screenDownAt = 0;
    }
    bool talkPressed = headTap || screenTap;
    if (talkPressed || petted) g_lastActive = now;

    if (g_captionUntil && now > g_captionUntil) {
        g_captionUntil = 0;
        face::setCaption("");
    }

    switch (g_state) {
        case State::Idle: {
            if (g_moodUntil && now > g_moodUntil) {
                g_moodUntil = 0;
                face::setEmotion(Emotion::Neutral);
                face::setOverlay(Overlay::None);
                lights::setMode(LightMode::Idle);
            }
            if (petted) {
                audio::stopListening();
                face::setEmotion(Emotion::Love);
                lights::setMode(LightMode::Party);
                body::play(Gesture::Wiggle);
                audio::chirpHappy();
                g_moodUntil = now + 2500;
                if (g_settings.alwaysListening()) audio::startListening(false);
                break;
            }
            if (talkPressed) {
                if (!cloud::isConfigured()) {
                    face::setEmotion(Emotion::Thinking);
                    showCaption("I need a server address. Hold my screen for 3 seconds.", 6000);
                } else {
                    audio::stopListening();
                    startListening(true);
                }
                break;
            }
            if (g_settings.alwaysListening() && cloud::isConfigured()) {
                // background listening for "Hi Freckles"
                lights::setLevel(audio::micLevel());
                if (audio::pollListening() == audio::ListenResult::GotSpeech) {
                    g_wakeRequest = true;
                    if (cloud::startTalk(audio::wavData(), audio::wavSize(), true)) {
                        enterState(State::Thinking);
                    } else {
                        audio::startListening(false);
                    }
                }
            }
            if (now - g_lastActive > SLEEP_AFTER_IDLE_MS && !g_settings.alwaysListening()) {
                enterState(State::Sleeping);
                face::setEmotion(Emotion::Sleepy);
                face::setBrightness(40);
                lights::setMode(LightMode::Sleeping);
                body::setIdleWander(false);
                body::play(Gesture::Sleep);
            }
            break;
        }

        case State::Listening: {
            float level = audio::micLevel();
            face::setLevel(level);
            lights::setLevel(level);
            if (talkPressed && now - g_stateSince > 400) {  // tap again to cancel
                audio::stopListening();
                showCaption("Okay, never mind!", 2500);
                goIdle();
                break;
            }
            auto res = audio::pollListening();
            if (res == audio::ListenResult::GotSpeech) {
                Serial.printf("[mic] sending %.1f s of audio\n", audio::recordedSeconds());
                if (cloud::startTalk(audio::wavData(), audio::wavSize(), g_wakeRequest)) {
                    face::setOverlay(Overlay::Thinking);
                    face::setEmotion(Emotion::Thinking);
                    lights::setMode(LightMode::Thinking);
                    enterState(State::Thinking);
                } else {
                    goIdle();
                }
            } else if (res == audio::ListenResult::NoSpeech) {
                face::setEmotion(Emotion::Thinking);
                showCaption("I didn't hear anything.", 3000);
                g_moodUntil = now + 2500;
                goIdle();
            }
            break;
        }

        case State::Thinking: {
            // in always-on mode, only look busy once we know it's for us
            if (g_wakeRequest && now - g_stateSince > 1200) {
                face::setOverlay(Overlay::Thinking);
                face::setEmotion(Emotion::Thinking);
                lights::setMode(LightMode::Thinking);
            }
            if (cloud::hasReply()) handleReply();
            break;
        }

        case State::Speaking: {
            bool playing = audio::isPlaying();
            float l      = audio::speechLevel();
            face::setMouth(l);
            lights::setLevel(l);

            if (talkPressed) {  // interrupt and listen again
                audio::stopPlaying();
                startListening(true);
                break;
            }
            bool textDone = g_textOnlyUntil && now > g_textOnlyUntil;
            if ((!playing && !g_textOnlyUntil) || textDone) {
                bool followUp = cloud::reply().keepListening;
                face::setMouth(0);
                audio::stopPlaying();
                cloud::releaseReply();
                g_textOnlyUntil = 0;
                g_captionUntil  = now + 4000;  // let the subtitle linger a moment
                g_moodUntil     = now + 2500;
                if (followUp) {
                    startListening(true);
                } else {
                    goIdle();
                }
            }
            break;
        }

        case State::Sleeping: {
            if (talkPressed || petted || t.wasPressed()) {
                face::setBrightness(160);
                face::setEmotion(Emotion::Happy);
                body::play(Gesture::Wake);
                audio::chirpHappy();
                g_moodUntil = now + 2000;
                goIdle();
            }
            break;
        }
    }

    delay(5);
}
