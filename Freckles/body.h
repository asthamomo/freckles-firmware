#pragma once
#include <Arduino.h>

enum class Gesture : uint8_t { None, Nod, Shake, LookAround, Wiggle, Dance, Curious, Sleep, Wake };

// Head movement on top of StackChan's Motion API.
// Angles are in tenths of a degree: yaw -1280..1280 (+ = turn left), pitch 0..900 (+ = look up).
namespace body {
void begin();
void play(Gesture g);          // queue a gesture (replaces the current one)
void lookForward(int speed = 400);
void setIdleWander(bool on);   // random glances when nothing else is going on
bool busy();
void update();                 // call from the main loop
Gesture parseGesture(const String& name);
}  // namespace body
