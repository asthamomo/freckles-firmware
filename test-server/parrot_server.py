#!/usr/bin/env python3
"""
Freckles "parrot" test server.

Lets you check a Freckles robot end to end (mic -> Wi-Fi -> server -> speaker,
face, lights, head movement) before the real AI server exists. It does not call
any AI: it plays your own recording back to you, slightly higher pitched, with a
different face and gesture each time.

Run it on a computer on the same Wi-Fi as Freckles:

    python3 parrot_server.py            # listens on port 8080
    python3 parrot_server.py --port 9000 --token mysecret

Then put  http://<this computer's IP>:8080  in Freckles' setup page.
Needs only Python 3.8+ (no packages).
"""
import argparse
import io
import itertools
import json
import struct
import urllib.parse
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

MOODS = itertools.cycle([
    ("happy", "nod"),
    ("surprised", "curious"),
    ("love", "wiggle"),
    ("thinking", "look_around"),
    ("happy", "dance"),
])

TOKEN = ""


def read_wav(data: bytes):
    with wave.open(io.BytesIO(data), "rb") as w:
        if w.getsampwidth() != 2 or w.getnchannels() != 1:
            raise ValueError("expected 16-bit mono WAV")
        return w.getframerate(), w.readframes(w.getnframes())


def make_wav(rate: int, pcm: bytes) -> bytes:
    out = io.BytesIO()
    with wave.open(out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm)
    return out.getvalue()


def boost(pcm: bytes, gain: float = 1.6) -> bytes:
    n = len(pcm) // 2
    samples = struct.unpack(f"<{n}h", pcm[: n * 2])
    return struct.pack(f"<{n}h", *(max(-32768, min(32767, int(s * gain))) for s in samples))


class Handler(BaseHTTPRequestHandler):
    server_version = "FrecklesParrot/0.1"

    def _authorised(self) -> bool:
        if not TOKEN:
            return True
        return self.headers.get("Authorization", "") == f"Bearer {TOKEN}"

    def _send(self, code, body=b"", ctype="application/json", headers=None):
        self.send_response(code)
        if body or code == 200:
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
        for k, v in (headers or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        if self.path.rstrip("/") != "/v1/hello":
            return self._send(404, b'{"error":"not found"}')
        if not self._authorised():
            return self._send(401, b'{"error":"bad token"}')
        device = self.headers.get("X-Freckles-Device", "?")
        print(f"hello from {device} (firmware {self.headers.get('X-Freckles-Firmware', '?')})")
        body = json.dumps({"greeting": "Test mode! Pat my head and I'll copy you."}).encode()
        self._send(200, body)

    def do_POST(self):
        if self.path.rstrip("/") != "/v1/talk":
            return self._send(404, b'{"error":"not found"}')
        if not self._authorised():
            return self._send(401, headers={"X-Freckles-Error": "Bad%20device%20token"})
        length = int(self.headers.get("Content-Length", "0"))
        data = self.rfile.read(length)
        try:
            rate, pcm = read_wav(data)
        except Exception as e:  # noqa: BLE001
            msg = urllib.parse.quote(f"Bad audio: {e}")
            return self._send(400, headers={"X-Freckles-Error": msg})

        seconds = len(pcm) / 2 / rate
        mode = self.headers.get("X-Freckles-Mode", "tap")
        emotion, gesture = next(MOODS)
        text = f"You said something {seconds:.1f} seconds long. Here it is back!"
        print(f"{mode}: {seconds:.1f}s of audio -> {emotion}/{gesture}")

        # Play it back ~12% faster/higher: same samples, higher declared rate.
        reply = make_wav(int(rate * 1.12), boost(pcm))
        self._send(200, reply, "audio/wav", {
            "X-Freckles-Text": urllib.parse.quote(text),
            "X-Freckles-Heard": urllib.parse.quote(f"({seconds:.1f}s of audio)"),
            "X-Freckles-Emotion": emotion,
            "X-Freckles-Gesture": gesture,
            "X-Freckles-Provider": "parrot",
        })

    def log_message(self, fmt, *args):  # quieter console
        pass


def main():
    global TOKEN
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--token", default="", help="require this device token")
    args = ap.parse_args()
    TOKEN = args.token
    srv = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print(f"Freckles parrot server on port {args.port}  (Ctrl+C to stop)")
    srv.serve_forever()


if __name__ == "__main__":
    main()
