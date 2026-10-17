#pragma once
#include <Arduino.h>

enum class LightMode : uint8_t { Off, Idle, Listening, Thinking, Speaking, Party, Error, Setup, Sleeping };

// The 12 RGB LEDs (0-5 left, 6-11 right). Call update() often from the main loop.
namespace lights {
void setMode(LightMode m);
void setLevel(float level);  // 0..1, used by Listening / Speaking
void update();
}  // namespace lights
