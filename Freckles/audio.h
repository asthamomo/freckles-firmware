#pragma once
#include <Arduino.h>

// Microphone capture with voice-activity detection, and speaker playback with
// a loudness readout for lip sync. The CoreS3 mic and speaker share one I2S
// port, so only one of them runs at a time.
namespace audio {

enum class ListenResult : uint8_t { Running, GotSpeech, NoSpeech };

void begin(uint8_t volume);
void setVolume(uint8_t volume);

// tapMode = true: someone just asked Freckles to listen; give up after a few
// seconds of silence. tapMode = false: wait for speech indefinitely.
void startListening(bool tapMode);
ListenResult pollListening();  // call often while listening
void stopListening();
bool isCapturing();            // speech has started
float micLevel();              // 0..1

// The finished recording as a 16 kHz mono 16-bit WAV (valid after GotSpeech).
const uint8_t* wavData();
size_t wavSize();
float recordedSeconds();

// Plays 16-bit mono PCM. The buffer must stay valid until playback ends.
bool play(const int16_t* pcm, size_t samples, uint32_t sampleRate);
bool isPlaying();
float speechLevel();           // 0..1 loudness at the current playback position
void stopPlaying();

void chirpListen();
void chirpError();
void chirpHappy();

}  // namespace audio
