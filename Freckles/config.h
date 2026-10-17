// Freckles AI firmware - build-time defaults.
// Most of these can be changed on the device in the Wi-Fi setup page
// (hold the screen for 3 seconds to open it).
#pragma once

#define FRECKLES_FIRMWARE_VERSION "0.1.0"

// ---- Server ---------------------------------------------------------------
// Base URL of the Freckles server that talks to ChatGPT / Gemini / Claude etc.
// Example: "https://api.chewmaya.com" or "http://192.168.1.50:8080" for testing.
// Leave empty to force the user to enter it in the setup page.
#define FRECKLES_DEFAULT_SERVER ""

// Shared secret sent as "Authorization: Bearer <token>". Optional for testing.
#define FRECKLES_DEFAULT_TOKEN ""

// Root CA (PEM) for your HTTPS server. Strongly recommended for production.
// If left empty, HTTPS still works but the certificate is NOT verified.
#define FRECKLES_ROOT_CA ""

// ---- Setup portal ---------------------------------------------------------
#define FRECKLES_SETUP_AP_NAME "Freckles-Setup"
#define FRECKLES_SETUP_TIMEOUT_S 300

// ---- Listening ------------------------------------------------------------
// "tap"    : Freckles listens after you pat its head or tap the screen.
// "always" : Freckles listens whenever someone speaks; the server only
//            answers when the sentence starts with "Hi Freckles".
#define FRECKLES_DEFAULT_LISTEN_MODE "tap"

#define MIC_SAMPLE_RATE 16000
#define MIC_CHUNK_SAMPLES 512          // 32 ms per chunk at 16 kHz
#define MIC_RING_CHUNKS 16             // ring buffer used for pre-roll
#define MIC_PREROLL_CHUNKS 10          // ~320 ms kept before speech starts
#define MAX_UTTERANCE_SECONDS 12       // hard cap on one recording
#define SILENCE_END_MS 900             // stop after this much quiet
#define TAP_NO_SPEECH_TIMEOUT_MS 6000  // give up if nobody speaks after a tap
#define MIN_SPEECH_MS 250              // ignore shorter blips
#define VAD_MIN_RMS 350                // absolute floor for "speech"
#define VAD_NOISE_MULTIPLIER 3.0f      // speech must be this much louder than room noise

// ---- Speaking -------------------------------------------------------------
#define DEFAULT_VOLUME 170             // 0-255
#define MAX_REPLY_AUDIO_BYTES (4 * 1024 * 1024)

// ---- Behaviour ------------------------------------------------------------
#define SLEEP_AFTER_IDLE_MS (5UL * 60UL * 1000UL)
#define SCREEN_LONG_PRESS_MS 3000
#define HTTP_TIMEOUT_MS 30000
