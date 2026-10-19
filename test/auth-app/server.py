#!/usr/bin/env python3
"""A localhost app for testing Poca's authentication.

It answers OK when a request carries Poca's secret (the X-Poca-Secret header
equal to POCA_SECRET from its environment) and NG otherwise, including when
it was started without POCA_SECRET. The secret is never printed or logged.

  GET /            a page saying OK or NG
  GET /whoami      {"result": "OK" | "NG", "header": bool}, readable from any
                   origin (so a test can fetch it from another port)
  GET /redirect?to=<port>
                   302 to http://127.0.0.1:<port>/whoami
  OPTIONS *        a CORS preflight allowing X-Poca-Secret

Usage: server.py [--port 4300]
Standard library only.
"""

import argparse
import hmac
import json
import os
import signal
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlsplit

SECRET = os.environ.get("POCA_SECRET", "").encode()
HEADER = "X-Poca-Secret"

PAGE = """<!doctype html>
<html><head><meta charset="utf-8"><title>auth-test: {result}</title>
<style>
  body {{ margin: 0; height: 100vh; display: grid; place-items: center;
         font: 700 20vmin/1 system-ui, sans-serif;
         background: {bg}; color: #fff; }}
</style></head>
<body><div id="result">{result}</div></body></html>
"""


def verdict(headers):
    given = headers.get(HEADER)
    ok = bool(SECRET) and given is not None and hmac.compare_digest(
        given.encode(), SECRET)
    return ("OK" if ok else "NG"), given is not None


class Handler(BaseHTTPRequestHandler):
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

    # One line per request, without headers (the secret stays out of the log).
    def log_message(self, fmt, *args):
        sys.stderr.write("auth-test: %s\n" % (fmt % args))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=4300)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
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
