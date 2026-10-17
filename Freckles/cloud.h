#pragma once
#include <Arduino.h>
#include "settings.h"
#include "wav.h"

// Talks to the Freckles server, which in turn talks to ChatGPT, Gemini, Claude
// or any other model. See SERVER_PROTOCOL.md for the exact contract.
namespace cloud {

struct Reply {
    int status = 0;        // HTTP status, or negative on network error
    String error;          // human-readable problem, if any
    String heard;          // what the server thinks the user said
    String text;           // what Freckles says back (for subtitles)
    String emotion;        // face to show while speaking
    String gesture;        // head movement to play
    String provider;       // which AI answered (for the debug log)
    bool keepListening = false;  // server wants a follow-up answer without a tap
    uint8_t* body = nullptr;     // raw response body (PSRAM)
    size_t bodyLen = 0;
    wav::Pcm pcm;                // speech audio inside body, if any
};

void configure(const FrecklesSettings& s);
bool isConfigured();

// Sends one recorded utterance in the background. Returns false if busy.
bool startTalk(const uint8_t* wav, size_t len, bool wakeMode);
bool isBusy();
bool hasReply();
Reply& reply();
void releaseReply();  // free the audio buffer once playback is done

// Quick reachability check (GET /v1/hello). Blocking, short timeout.
int hello(String& greeting);

}  // namespace cloud
