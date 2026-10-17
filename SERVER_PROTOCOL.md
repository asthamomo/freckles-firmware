# Freckles server protocol (v1)

Freckles never talks to ChatGPT, Gemini or Claude directly. It sends what it
heard to **your Freckles server**, and the server does the AI work:

```
Freckles robot ──(WAV over HTTPS)──> Freckles server ──> speech-to-text
                                                     ──> ChatGPT / Gemini / Claude / ...
                                                     ──> text-to-speech
Freckles robot <──(WAV + face/gesture hints)──────────┘
```

Keeping the AI on the server means API keys never sit on a robot, you can swap
or mix providers without reflashing, and you control cost and safety filters in
one place.

The base URL (e.g. `https://api.chewmaya.com`) is entered on the robot's setup
page. All paths below are relative to it.

## Headers the robot sends on every request

| Header | Example | Notes |
|---|---|---|
| `Authorization` | `Bearer 3f9c…` | Only if a device token was set. Return `401` if it's wrong. |
| `X-Freckles-Device` | `freckles-a1b2c3` | Stable per robot (from its Wi-Fi MAC). Use it to keep conversation history. |
| `X-Freckles-Firmware` | `0.1.0` | |

## `GET /v1/hello`

Called once at boot to check the server is reachable.

Response `200`, JSON:

```json
{ "greeting": "Hi! Pat my head to talk." }
```

`greeting` is optional; it's shown on screen.

## `POST /v1/talk`

One spoken turn.

Request:

- `Content-Type: audio/wav`
- `X-Freckles-Mode: tap` (someone patted/tapped Freckles, so they are talking to it)
  or `wake` (always-listening mode: only answer if the speech starts with
  "Hi Freckles" / "Hey Freckles")
- Body: WAV, 16 kHz, mono, 16-bit PCM, up to 12 seconds.

### Response A: spoken reply (normal case)

`200`, `Content-Type: audio/wav`, body = WAV, **16-bit mono PCM**, any sample
rate from 8 kHz to 48 kHz (16 kHz or 24 kHz recommended). Max 4 MB.
Chunked transfer is fine.

Optional headers (values with spaces or non-ASCII must be URL-encoded):

| Header | Values | Effect on the robot |
|---|---|---|
| `X-Freckles-Text` | URL-encoded reply text | Subtitles under the face |
| `X-Freckles-Heard` | URL-encoded transcript | Logged on the serial port |
| `X-Freckles-Emotion` | `neutral` `happy` `sad` `surprised` `sleepy` `thinking` `angry` `love` | Face while speaking |
| `X-Freckles-Gesture` | `none` `nod` `shake` `look_around` `wiggle` `dance` `curious` | Head movement |
| `X-Freckles-Continue` | `1` | Listen again right after speaking (for a follow-up question) without a tap |
| `X-Freckles-Provider` | e.g. `openai`, `gemini`, `claude` | Logged on the serial port |

### Response B: text-only reply

`200`, `Content-Type: application/json`:

```json
{ "text": "…", "emotion": "happy", "gesture": "nod", "continue": false }
```

The robot shows the text without speaking. Useful while you are building
the TTS part.

### Response C: not for me

`204 No Content` — in `wake` mode when nobody said "Hi Freckles". The robot
goes back to waiting quietly.

### Errors

- `401` / `403`: wrong device token. Robot says its server doesn't recognise it.
- Any other status: robot shows a sad face and the `X-Freckles-Error` header
  (URL-encoded) if present, otherwise "Server error <code>".

## Suggested server pipeline

1. Speech-to-text on the WAV (e.g. OpenAI transcription, Google Speech-to-Text,
   Deepgram, or local Whisper).
2. In `wake` mode, return `204` unless the transcript starts with the wake phrase;
   strip the phrase before sending it on.
3. Send the transcript plus that device's recent history to the chosen model,
   with a Freckles system prompt (short, friendly, spoken-style answers), and ask
   it to also pick an emotion and gesture (e.g. via structured output).
4. Text-to-speech the answer to 16-bit mono WAV.
5. Return it with the headers above.

Target round-trip: under 3 seconds feels conversational.

## Testing without AI

`test-server/parrot_server.py` implements this protocol without any AI: it plays
your own voice back with a new face and gesture each time.
