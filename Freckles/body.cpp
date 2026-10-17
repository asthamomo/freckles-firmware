#include "body.h"
#include "face.h"
#include <M5StackChan.h>
#include <vector>

namespace body {

namespace {

constexpr int NEUTRAL_PITCH = 120;  // a little upward, toward a person at the desk

struct Step {
    int yaw;
    int pitch;
    int speed;
    uint16_t holdMs;
};

std::vector<Step> g_steps;
size_t g_index        = 0;
uint32_t g_stepAt     = 0;
bool g_wander         = true;
uint32_t g_nextWander = 0;
bool g_torqueOff      = false;

void ensureTorque()
{
    if (g_torqueOff) {
        M5StackChan.Motion.setTorqueEnabled(true);
        g_torqueOff = false;
    }
}

void startSteps(std::vector<Step> steps)
{
    ensureTorque();
    g_steps  = std::move(steps);
    g_index  = 0;
    g_stepAt = 0;
}

void mirrorGaze(int yaw, int pitch)
{
    face::setGaze(yaw / 900.0f, -(pitch - NEUTRAL_PITCH) / 500.0f);
}

}  // namespace

void begin()
{
    M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);  // quiet + saves battery when resting
    M5StackChan.Motion.move(0, NEUTRAL_PITCH, 300);
    g_nextWander = millis() + 6000;
}

void lookForward(int speed)
{
    startSteps({{0, NEUTRAL_PITCH, speed, 0}});
}

void play(Gesture g)
{
    const int P = NEUTRAL_PITCH;
    switch (g) {
        case Gesture::Nod:
            startSteps({{0, P + 160, 700, 220}, {0, P - 20, 700, 220}, {0, P + 140, 700, 200}, {0, P, 500, 0}});
            break;
        case Gesture::Shake:
            startSteps({{-260, P, 750, 230}, {260, P, 750, 260}, {-200, P, 750, 230}, {0, P, 500, 0}});
            break;
        case Gesture::LookAround:
            startSteps({{500, P + 80, 350, 900}, {-500, P + 40, 350, 900}, {0, P, 350, 0}});
            break;
        case Gesture::Wiggle:
            startSteps({{120, P, 900, 120}, {-120, P, 900, 120}, {120, P, 900, 120}, {-120, P, 900, 120}, {0, P, 600, 0}});
            break;
        case Gesture::Dance:
            startSteps({{350, P + 150, 800, 300}, {-350, P, 800, 300}, {350, P + 150, 800, 300},
                        {-350, P, 800, 300}, {0, P + 200, 700, 250}, {0, P, 500, 0}});
            break;
        case Gesture::Curious:
            startSteps({{150, P + 120, 400, 0}});
            break;
        case Gesture::Sleep:
            startSteps({{0, 20, 150, 1500}});
            break;
        case Gesture::Wake:
            startSteps({{0, P + 200, 500, 300}, {0, P, 400, 0}});
            break;
        default:
            break;
    }
}

void setIdleWander(bool on)
{
    g_wander     = on;
    g_nextWander = millis() + 4000 + random(0, 8000);
}

bool busy()
{
    return g_index < g_steps.size();
}

void update()
{
    uint32_t now = millis();

    if (g_index < g_steps.size()) {
        if (g_stepAt == 0 || (now >= g_stepAt && !M5StackChan.Motion.isMoving())) {
            if (g_stepAt != 0) g_index++;
            if (g_index < g_steps.size()) {
                const Step& s = g_steps[g_index];
                M5StackChan.Motion.move(s.yaw, s.pitch, s.speed);
                mirrorGaze(s.yaw, s.pitch);
                g_stepAt = now + s.holdMs;
            } else {
                // finished; a sleep gesture lets the servos go limp
                if (!g_steps.empty() && g_steps.back().pitch < 60) {
                    M5StackChan.Motion.setTorqueEnabled(false);
                    g_torqueOff = true;
                }
                face::setGaze(0, 0);
            }
        }
        return;
    }

    if (g_wander && now >= g_nextWander) {
        int yaw   = random(-450, 451);
        int pitch = NEUTRAL_PITCH + random(-60, 121);
        startSteps({{yaw, pitch, 220, (uint16_t)random(1200, 2600)}, {0, NEUTRAL_PITCH, 220, 0}});
        g_nextWander = now + 9000 + random(0, 14000);
    }
}

Gesture parseGesture(const String& raw)
{
    String n = raw;
    n.toLowerCase();
    n.trim();
    if (n == "nod" || n == "yes") return Gesture::Nod;
    if (n == "shake" || n == "no") return Gesture::Shake;
    if (n == "look_around" || n == "lookaround") return Gesture::LookAround;
    if (n == "wiggle") return Gesture::Wiggle;
    if (n == "dance") return Gesture::Dance;
    if (n == "curious" || n == "tilt") return Gesture::Curious;
    return Gesture::None;
}

}  // namespace body
