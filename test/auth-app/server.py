#!/usr/bin/env python3
"""A localhost app for testing Poca's authentication.

It answers OK when a request carries Poca's secret (the X-Poca-Secret header
equal to POCA_SECRET from its environment) and NG otherwise, including when
it was started without POCA_SECRET. The secret is never printed or logged.

WebSockets carry no header from Poca, so they go through a ticket: a page
fetches /ticket (an HTTP request, which Poca gives the header), then opens
/ws?ticket=<ticket>. A ticket is good for one handshake within --ticket-ttl
seconds; the secret itself never goes into a URL.

  GET /            a page saying OK or NG, and below it whether its
                   WebSocket opened (WS OK / WS NG)
  GET /whoami      {"result": "OK" | "NG", "header": bool}, readable from any
                   origin (so a test can fetch it from another port)
  GET /redirect?to=<port>
                   302 to http://127.0.0.1:<port>/whoami
  GET /ticket      {"ticket": "..."} with the secret, 401 without
  GET /ws?ticket=  a WebSocket that says "hello" and echoes text; 403
                   before the handshake without a good ticket
  OPTIONS *        a CORS preflight allowing X-Poca-Secret

Usage: server.py [--port 4300] [--ticket-ttl 10]
Standard library only.
"""

import argparse
import base64
import hashlib
import hmac
import json
import os
import secrets
import signal
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

SECRET = os.environ.get("POCA_SECRET", "").encode()
HEADER = "X-Poca-Secret"
WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>auth-test: {result}</title>
<style>
  body {{ margin: 0; height: 100vh; display: grid; place-content: center;
         justify-items: center; gap: 4vmin; font-family: system-ui, sans-serif;
         background: {bg}; color: #fff; }}
  #result {{ font: 700 20vmin/1 system-ui, sans-serif; }}
  #ws {{ font: 700 8vmin/1 system-ui, sans-serif; }}
</style></head>
<body><div id="result">{result}</div><div id="ws">WS …</div>
<script>
(async () => {{
  const show = (ok) => {{ document.getElementById('ws').textContent = ok ? 'WS OK' : 'WS NG'; }};
  try {{
    const r = await fetch('/ticket', {{cache: 'no-store'}});
    if (!r.ok) return show(false);
    const {{ticket}} = await r.json();
    const ws = new WebSocket('ws://' + location.host + '/ws?ticket=' + encodeURIComponent(ticket));
    ws.onmessage = (e) => {{ show(e.data === 'hello'); ws.close(); }};
    ws.onerror = () => show(false);
  }} catch (e) {{ show(false); }}
}})();
</script></body></html>
"""


class Tickets:
    """One-shot tickets that expire; kept in memory only."""

    def __init__(self, ttl):
        self.ttl = ttl
        self.lock = threading.Lock()
        self.live = {}  # ticket -> deadline

    def issue(self):
        ticket = secrets.token_urlsafe(24)
        now = time.monotonic()
        with self.lock:
            self.live = {t: d for t, d in self.live.items() if d > now}
            self.live[ticket] = now + self.ttl
        return ticket

    def take(self, ticket):
        with self.lock:
            deadline = self.live.pop(ticket, None)
        return deadline is not None and deadline > time.monotonic()


TICKETS = Tickets(10.0)


def verdict(headers):
    given = headers.get(HEADER)
    ok = bool(SECRET) and given is not None and hmac.compare_digest(
        given.encode(), SECRET)
    return ("OK" if ok else "NG"), given is not None


class Handler(BaseHTTPRequestHandler):
    # A 101 must be HTTP/1.1; every answer has a Content-Length.
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        url = urlsplit(self.path)
        result, has_header = verdict(self.headers)
        if url.path == "/":
            body = PAGE.format(
                result=result,
                bg="#2e7d32" if result == "OK" else "#c62828").encode()
            self.answer(200 if result == "OK" else 401,
                        "text/html; charset=utf-8", body)
        elif url.path == "/whoami":
            body = json.dumps({"result": result, "header": has_header})
            self.answer(200, "application/json", body.encode(),
                        {"Access-Control-Allow-Origin": "*"})
        elif url.path == "/redirect":
            port = parse_qs(url.query).get("to", [""])[0]
            if not port.isdigit():
                self.answer(400, "text/plain", b"to=<port>\n")
                return
            self.send_response(302)
            self.send_header("Location", f"http://127.0.0.1:{port}/whoami")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif url.path == "/ticket":
            # Readable from any origin so a test sees the 401; only a request
            # with the secret gets a ticket, wherever it comes from.
            cors = {"Access-Control-Allow-Origin": "*"}
            if result != "OK":
                self.answer(401, "application/json", b'{"ticket": null}', cors)
                return
            body = json.dumps({"ticket": TICKETS.issue()})
            self.answer(200, "application/json", body.encode(), cors)
        elif url.path == "/ws":
            self.websocket(parse_qs(url.query).get("ticket", [""])[0])
        else:
            self.answer(404, "text/plain", b"not found\n")

    # The preflight of a cross-origin fetch that sets X-Poca-Secret itself (a
    # test that Poca drops a forged header).
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", HEADER)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def answer(self, status, content_type, body, extra=None):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(body)

    # --- WebSocket (RFC 6455, text frames only) ---------------------------

    def websocket(self, ticket):
        key = self.headers.get("Sec-WebSocket-Key")
        if self.headers.get("Upgrade", "").lower() != "websocket" or not key:
            self.answer(400, "text/plain", b"a WebSocket handshake\n")
            return
        if not TICKETS.take(ticket):
            self.answer(403, "text/plain", b"no good ticket\n")
            return
        accept = base64.b64encode(
            hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.wfile.flush()
        self.close_connection = True
        self.send_frame(0x1, b"hello")
        while True:
            frame = self.read_frame()
            if frame is None:
                return
            opcode, payload = frame
            if opcode == 0x8:
                self.send_frame(0x8, payload[:2])
                return
            if opcode == 0x9:
                self.send_frame(0xA, payload)
            elif opcode == 0x1:
                self.send_frame(0x1, payload)
        # (a peer that vanishes ends read_frame with None)

    def read_exact(self, n):
        try:
            data = self.rfile.read(n)
        except OSError:
            return None
        return data if len(data) == n else None

    def read_frame(self):
        head = self.read_exact(2)
        if head is None:
            return None
        opcode, length = head[0] & 0x0F, head[1] & 0x7F
        if length == 126:
            ext = self.read_exact(2)
            length = ext and struct.unpack("!H", ext)[0]
        elif length == 127:
            ext = self.read_exact(8)
            length = ext and struct.unpack("!Q", ext)[0]
        if length is None or length > 1 << 20:
            return None
        mask = self.read_exact(4) if head[1] & 0x80 else b"\0\0\0\0"
        payload = self.read_exact(length) if length else b""
        if mask is None or payload is None:
            return None
        return opcode, bytes(b ^ mask[i % 4] for i, b in enumerate(payload))

    def send_frame(self, opcode, payload):
        n = len(payload)
        if n < 126:
            head = struct.pack("!BB", 0x80 | opcode, n)
        elif n < 1 << 16:
            head = struct.pack("!BBH", 0x80 | opcode, 126, n)
        else:
            head = struct.pack("!BBQ", 0x80 | opcode, 127, n)
        self.wfile.write(head + payload)
        self.wfile.flush()

    # One line per request: the path without its query (tickets) and no
    # headers (the secret stays out of the log).
    def log_request(self, code="-", size="-"):
        sys.stderr.write("auth-test: %s %s %s\n" % (
            self.command, urlsplit(self.path).path, code))

    # Errors before a request is parsed (send_error): their text may quote the
    # request line, ticket and all, so only the code goes out.
    def log_message(self, fmt, *args):
        code = next((a for a in args if isinstance(a, int)), "-")
        sys.stderr.write("auth-test: error %s\n" % code)


class Server(ThreadingHTTPServer):
    daemon_threads = True

    # A peer that hangs up (a refused WebSocket does) is not an error.
    def handle_error(self, request, client_address):
        if not isinstance(sys.exc_info()[1], (ConnectionError, TimeoutError)):
            super().handle_error(request, client_address)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=4300)
    parser.add_argument("--ticket-ttl", type=float, default=10.0)
    args = parser.parse_args()
    TICKETS.ttl = args.ticket_ttl
    server = Server(("127.0.0.1", args.port), Handler)
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
    sys.stderr.write("auth-test: 127.0.0.1:%d, secret %s\n" % (
        args.port, "given" if SECRET else "missing (always NG)"))
    sys.stderr.flush()
    try:
        server.serve_forever()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
