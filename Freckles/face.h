#pragma once
#include <Arduino.h>

enum class Emotion : uint8_t { Neutral, Happy, Sad, Surprised, Sleepy, Thinking, Angry, Love };

// Small animated overlay drawn on top of the face.
enum class Overlay : uint8_t { None, Listening, Thinking, Setup, Error };

namespace face {

void begin();  // starts the drawing task

void setEmotion(Emotion e);
Emotion emotion();
void setMouth(float open);              // 0..1, used for lip sync
void setGaze(float x, float y);         // -1..1 each
void setOverlay(Overlay o);
void setLevel(float level);             // 0..1 mic level for the listening ring
void setCaption(const String& text);    // subtitle at the bottom ("" hides it)
void setStatus(bool wifiOk, int batteryPercent, bool charging);
void setSetupText(const String& title, const String& body);
void setBrightness(uint8_t b);

Emotion parseEmotion(const String& name);

}  // namespace face
