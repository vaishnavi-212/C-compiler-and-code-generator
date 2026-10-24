#!/usr/bin/env python3
"""
Mini Compiler - tiny local web server (Python standard library only).

    python server.py          ->  http://localhost:8000
    python server.py 9000     ->  another port

The page sends your C code to POST /api/compile. For every tab the server runs
the compiler (compiler/minic) once and returns its plain-text output.
Each run is a separate process with a time limit, so a bad program cannot hang the server.
"""
import json
import os
import subprocess
import sys
import tempfile
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(ROOT, "web")
EXAMPLES = os.path.join(ROOT, "examples")
BIN = os.path.join(ROOT, "compiler", "minic.exe" if os.name == "nt" else "minic")

# tab name -> compiler options
TABS = {
    "tokens": ["--lexer"],
    "tree": ["--parser"],
    "derivation": ["--derive"],
    "symbols": ["--semantic"],
    "ir": ["--ir"],
    "cfg": ["--cfg"],
    "optimised": ["--opt"],
    "run": ["--run"],
}


def compile_all(code, mode, optimise):
    if not os.path.exists(BIN):
        msg = "compiler/minic not found. Build it first: run build.sh (Linux/macOS) or build.bat (Windows)."
        return {t: msg for t in TABS}
    fd, path = tempfile.mkstemp(suffix=".c")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(code)
    out = {}
    try:
        for tab, opts in TABS.items():
            cmd = [BIN, "--mode", mode] + opts + ([] if optimise else ["-O0"]) + [path]
            try:
                r = subprocess.run(cmd, capture_output=True, timeout=10)
                out[tab] = (r.stdout + r.stderr).decode("utf-8", "replace")
            except subprocess.TimeoutExpired:
                out[tab] = "The compiler took more than 10 seconds and was stopped."
    finally:
        os.remove(path)
    return out


def examples():
    items = []
    for name in sorted(os.listdir(EXAMPLES)):
        if name.endswith(".c"):
            with open(os.path.join(EXAMPLES, name), encoding="utf-8", errors="replace") as f:
                items.append({"name": name, "code": f.read()})
    return items


class Handler(BaseHTTPRequestHandler):
    def send(self, body, ctype="application/json", status=200):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", ctype + "; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(WEB, "index.html"), encoding="utf-8") as f:
                self.send(f.read(), "text/html")
        elif self.path == "/api/examples":
            self.send(json.dumps(examples()))
        else:
            self.send("Not found", "text/plain", 404)

    def do_POST(self):
        if self.path != "/api/compile":
            return self.send("Not found", "text/plain", 404)
        try:
            n = int(self.headers.get("Content-Length", 0))
            req = json.loads(self.rfile.read(n))
            mode = "yacc" if req.get("mode") == "yacc" else "rd"
            res = compile_all(str(req.get("code", "")), mode, bool(req.get("optimise", True)))
            self.send(json.dumps(res))
        except Exception as e:  # bad request
            self.send(json.dumps({"error": str(e)}), status=400)

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    url = "http://localhost:%d" % port
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("Mini Compiler running at", url, "- press Ctrl+C to stop")
    try:
        webbrowser.open(url)
    except Exception:
        pass
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
