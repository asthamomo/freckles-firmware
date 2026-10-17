#include "audio.h"
#include "config.h"
#include "wav.h"
#include <M5StackChan.h>
#include <atomic>

namespace audio {

namespace {

// ---- capture ----
int16_t g_ring[MIC_RING_CHUNKS][MIC_CHUNK_SAMPLES];
std::atomic<uint32_t> g_done{0};
uint32_t g_queued    = 0;
uint32_t g_processed = 0;

uint8_t* g_wav          = nullptr;  // header + PCM, in PSRAM
const size_t MAX_SAMPLES = (size_t)MIC_SAMPLE_RATE * MAX_UTTERANCE_SECONDS;
size_t g_samples        = 0;

bool g_listening   = false;
bool g_tapMode     = true;
bool g_capturing   = false;
uint32_t g_startMs = 0;
float g_noise      = VAD_MIN_RMS / 2.0f;
int g_loudRun      = 0;
uint32_t g_lastVoiceChunk = 0;
uint32_t g_voicedChunks   = 0;
float g_level      = 0;

// ---- playback ----
const int16_t* g_playPcm = nullptr;
size_t g_playSamples     = 0;
uint32_t g_playRate      = 16000;
uint32_t g_playStart     = 0;
uint8_t g_volume         = DEFAULT_VOLUME;

constexpr uint32_t CHUNK_MS = MIC_CHUNK_SAMPLES * 1000 / MIC_SAMPLE_RATE;

void onChunk(void*, void*, size_t)
{
    g_done.fetch_add(1, std::memory_order_release);
}

float rmsOf(const int16_t* d, size_t n)
{
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += d[i];
    int32_t mean = sum / (int64_t)n;
    uint64_t sq  = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t v = d[i] - mean;
        sq += (int64_t)v * v;
    }
    return sqrtf((float)sq / n);
}

int16_t* pcm()
{
    return reinterpret_cast<int16_t*>(g_wav + wav::HEADER_SIZE);
}

void append(const int16_t* chunk)
{
    size_t n = min((size_t)MIC_CHUNK_SAMPLES, MAX_SAMPLES - g_samples);
    memcpy(pcm() + g_samples, chunk, n * sizeof(int16_t));
    g_samples += n;
}

void resetDetector()
{
    g_capturing     = false;
    g_samples       = 0;
    g_loudRun       = 0;
    g_voicedChunks  = 0;
}

void queueChunks()
{
    while (g_queued - g_done.load(std::memory_order_acquire) < 2) {
        if (!M5.Mic.record(g_ring[g_queued % MIC_RING_CHUNKS], MIC_CHUNK_SAMPLES, MIC_SAMPLE_RATE)) break;
        g_queued++;
    }
}

}  // namespace

void begin(uint8_t volume)
{
    g_wav = (uint8_t*)heap_caps_malloc(wav::HEADER_SIZE + MAX_SAMPLES * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    g_volume = volume;
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.setVolume(g_volume);
}

void setVolume(uint8_t volume)
{
    g_volume = volume;
    M5.Speaker.setVolume(volume);
}

void startListening(bool tapMode)
{
    stopPlaying();
    M5.Speaker.end();

    g_done      = 0;
    g_queued    = 0;
    g_processed = 0;
    g_tapMode   = tapMode;
    g_startMs   = millis();
    g_level     = 0;
    resetDetector();

    M5.Mic.setBufferReleaseCallback(nullptr, onChunk);
    M5.Mic.begin();
    g_listening = true;
    queueChunks();
}

ListenResult pollListening()
{
    if (!g_listening || !g_wav) return ListenResult::NoSpeech;

    uint32_t done = g_done.load(std::memory_order_acquire);
    while (g_processed < done) {
        const int16_t* chunk = g_ring[g_processed % MIC_RING_CHUNKS];
        float rms            = rmsOf(chunk, MIC_CHUNK_SAMPLES);
        float threshold      = max((float)VAD_MIN_RMS, g_noise * VAD_NOISE_MULTIPLIER);
        bool loud            = rms > threshold;
        g_level              = g_level * 0.6f + min(1.0f, rms / 4000.0f) * 0.4f;

        if (!g_capturing) {
            if (loud) {
                g_loudRun++;
            } else {
                g_loudRun = 0;
                if (rms > 1) g_noise = g_noise * 0.95f + rms * 0.05f;  // skip codec warm-up zeros
            }
            if (g_loudRun >= 2) {
                // speech started: keep a little audio from just before it
                g_capturing = true;
                uint32_t first = g_processed > MIC_PREROLL_CHUNKS ? g_processed - MIC_PREROLL_CHUNKS : 0;
                for (uint32_t i = first; i <= g_processed; i++) append(g_ring[i % MIC_RING_CHUNKS]);
                g_lastVoiceChunk = g_processed;
                g_voicedChunks   = 2;
            }
        } else {
            append(chunk);
            if (rms > threshold * 0.7f) {
                g_lastVoiceChunk = g_processed;
                g_voicedChunks++;
            }
            bool full    = g_samples >= MAX_SAMPLES;
            bool quiet   = (g_processed - g_lastVoiceChunk) * CHUNK_MS >= SILENCE_END_MS;
            if (full || quiet) {
                if (g_voicedChunks * CHUNK_MS >= MIN_SPEECH_MS) {
                    g_processed++;
                    stopListening();
                    wav::writeHeader(g_wav, g_samples, MIC_SAMPLE_RATE);
                    return ListenResult::GotSpeech;
                }
                resetDetector();  // just a blip (door, cough) - keep waiting
            }
        }
        g_processed++;
    }

    if (g_tapMode && !g_capturing && millis() - g_startMs > TAP_NO_SPEECH_TIMEOUT_MS) {
        stopListening();
        return ListenResult::NoSpeech;
    }

    queueChunks();
    return ListenResult::Running;
}

void stopListening()
{
    if (!g_listening) return;
    g_listening = false;
    M5.Mic.end();
    M5.Mic.setBufferReleaseCallback(nullptr, nullptr);
    g_level = 0;
}

bool isCapturing()
{
    return g_capturing;
}

float micLevel()
{
    return g_level;
}

const uint8_t* wavData()
{
    return g_wav;
}

size_t wavSize()
{
    return wav::HEADER_SIZE + g_samples * 2;
}

float recordedSeconds()
{
    return (float)g_samples / MIC_SAMPLE_RATE;
}

static void ensureSpeaker()
{
    if (g_listening) stopListening();
    if (!M5.Speaker.isRunning()) {
        M5.Speaker.begin();
        M5.Speaker.setVolume(g_volume);
    }
}

bool play(const int16_t* data, size_t samples, uint32_t rate)
{
    ensureSpeaker();
    g_playPcm     = data;
    g_playSamples = samples;
    g_playRate    = rate;
    g_playStart   = millis();
    return M5.Speaker.playRaw(data, samples, rate, false, 1, 0, true);
}

bool isPlaying()
{
    return g_playPcm && M5.Speaker.isPlaying();
}

float speechLevel()
{
    if (!isPlaying()) return 0;
    size_t pos = (size_t)((uint64_t)(millis() - g_playStart) * g_playRate / 1000);
    if (pos >= g_playSamples) return 0;
    size_t win = g_playRate / 30;  // ~33 ms window
    size_t n   = min(win, g_playSamples - pos);
    if (n < 16) return 0;
    float rms = rmsOf(g_playPcm + pos, n);
    float l   = (rms - 300.0f) / 5000.0f;
    return constrain(l, 0.0f, 1.0f);
}

void stopPlaying()
{
    if (g_playPcm) {
        M5.Speaker.stop();
        g_playPcm = nullptr;
    }
}

void chirpListen()
{
    ensureSpeaker();
    M5.Speaker.tone(1046, 70, 1, true);
    delay(80);
    M5.Speaker.tone(1568, 90, 1, true);
    delay(100);
}

void chirpError()
{
    ensureSpeaker();
    M5.Speaker.tone(330, 160, 1, true);
    delay(170);
    M5.Speaker.tone(247, 220, 1, true);
    delay(230);
}

void chirpHappy()
{
    ensureSpeaker();
    const float notes[] = {1318, 1568, 2093};
    for (float f : notes) {
        M5.Speaker.tone(f, 60, 1, true);
        delay(70);
    }
}

}  // namespace audio
