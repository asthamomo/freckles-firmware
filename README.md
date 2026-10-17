# Freckles AI — robot firmware

Firmware for the Freckles AI desk robot (M5Stack StackChan hardware: CoreS3,
two serial servos, 12 RGB LEDs, 3-zone head touch panel).

Freckles listens, sends what you said to the **Freckles server**, and the server
gets the answer from ChatGPT, Gemini, Claude or any other model. Freckles then
says it out loud with lip-synced mouth, an emotion on its face, matching light
colours and a head gesture. How the robot and server talk is in
[SERVER_PROTOCOL.md](SERVER_PROTOCOL.md).

## What it does

| You do | Freckles does |
|---|---|
| Pat the top of its head, or tap the screen | Chirps, lights go blue, listens until you stop talking |
| Speak | Bars on screen + LEDs follow your voice |
| Wait | Thinking face, purple light chasing around |
| — | Speaks the answer, mouth moves with the audio, subtitles, emotion + gesture |
| Tap while it's talking | Stops and listens again |
| Swipe along its head | Heart eyes, rainbow lights, happy wiggle |
| Leave it alone 5 min | Falls asleep (dim screen, droops, servos relax). Any touch wakes it |
| Hold the screen 3 s | Opens the setup page |

Idle Freckles blinks, breathes, glances around now and then, and shows its
freckles. Low battery, charging and lost Wi-Fi appear as small icons.

**Listening modes** (set on the setup page):

- `tap` (default): listens after a pat or tap.
- `always`: listens whenever someone speaks and sends it with
  `X-Freckles-Mode: wake`; the server only answers if it starts with
  "Hi Freckles". This uses more Wi-Fi/battery and sends nearby speech to your
  server, so make that clear to customers.

## Flash it (Arduino IDE)

1. Install Arduino IDE 2.x.
2. Boards Manager → install **esp32 by Espressif** (built and tested with 3.3.12).
3. Library Manager → install **M5StackChan** (StackChan-BSP) and accept its
   dependencies, then also install **WiFiManager** (by tzapu) and **ArduinoJson**.
4. Open `Freckles/Freckles.ino`. Board: **M5CoreS3** (keep PSRAM enabled,
   the default).
5. Put the robot in download mode: hold the reset button ~2 s until the green LED
   lights, release. Pick the port, click Upload.
6. Optional: set `FRECKLES_DEFAULT_SERVER` in `config.h` so robots ship already
   pointing at your server.

Library versions this was compiled against: StackChan-BSP 1.1.0, M5Unified 0.2.26,
M5GFX 0.2.33, WiFiManager 2.0.17, ArduinoJson 7.4.2.

`platformio.ini` is included for PlatformIO users (build with `pio run`).

## First boot

1. Freckles shows "Let's get connected!". On a phone, join the Wi-Fi
   **Freckles-Setup** and open `192.168.4.1` if the page doesn't pop up.
2. Pick your Wi-Fi, enter the **Freckles server URL** (and device token if your
   server uses one), choose `tap` or `always`, and save.
3. Freckles connects, says hello, and is ready.

## Try it before the AI server exists

On a computer on the same Wi-Fi:

```
python3 test-server/parrot_server.py
```

Enter `http://<computer-ip>:8080` as the server URL. Pat Freckles' head and say
something: it plays your voice back with a new face and gesture each time. This
checks the mic, Wi-Fi, speaker, face, lights and servos end to end.

Serial monitor (115200 baud) logs what was heard, the reply and any errors.

## Code map

| File | What's in it |
|---|---|
| `Freckles.ino` | Startup, setup page, and the main state machine (idle → listening → thinking → speaking → sleeping) |
| `face.*` | The animated face, drawn by its own task at ~30 fps |
| `audio.*` | Mic capture with voice detection, speaker playback, lip-sync level, chirps |
| `cloud.*` | Talks to the Freckles server in a background task |
| `body.*` | Head gestures and idle glances (StackChan Motion API) |
| `lights.*` | LED animations per state |
| `settings.*` | Saved settings (server URL, token, mode, volume) |
| `wav.h` | WAV read/write |
| `config.h` | Defaults and tuning (silence timeout, max recording, volume, …) |

## Before shipping

- **HTTPS certificate:** put your server's root CA in `FRECKLES_ROOT_CA`
  (`config.h`). Without it HTTPS works but the server's identity isn't checked.
- **Tune voice detection on real units:** `VAD_MIN_RMS` and `SILENCE_END_MS` in
  `config.h` depend on mic gain and room noise.
- **Wake word:** "Hi Freckles" is detected on the server from the transcript, not
  on the robot. A true on-device wake word needs Espressif's ESP-SR with a custom
  wake-word model.
- This was compiled for CoreS3 but not yet run on a physical robot; expect to
  adjust servo angles/speeds and volume on first test.

## Credits

Built on the open-source [StackChan BSP](https://github.com/m5stack/StackChan-BSP)
by M5Stack (MIT licence), M5Unified/M5GFX, WiFiManager and ArduinoJson.
StackChan was created by Shinya Ishikawa and the StackChan community.

## Prebuilt binary

`prebuilt/freckles-0.1.0-cores3-merged.bin` is the same firmware, ready to
flash without compiling (no server URL baked in, so the setup page will ask
for it). With the robot in download mode:

```
esptool.py --chip esp32s3 write_flash 0x0 prebuilt/freckles-0.1.0-cores3-merged.bin
```

or use M5Burner / any ESP web flasher at offset `0x0`.
